#!/usr/bin/env python3
"""Spindle synchronization checks on the disconnected, enable-locked P4 only.

Uses real GPIO A/B transitions and hardware encoder/STEP counters. No motors,
physical index, external wiring or cutting accuracy are tested. Fault cases
latch until a hardware reset; run each as a separate fresh boot.
"""
import argparse
import re
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('port')
parser.add_argument('--fault', choices=['stall', 'reverse', 'deadline'])
parser.add_argument('--abrupt', action='store_true', help='Diagnostic speed jumps outside the assumed spindle-slew envelope')
args = parser.parse_args()
port = serial.Serial()
port.port, port.baudrate, port.timeout = args.port, 115200, .05
port.write_timeout = 2
port.dtr = port.rts = False


def receive(predicate, timeout=20):
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        line = port.readline().decode(errors='replace').strip()
        if not line:
            continue
        print(line, flush=True)
        lines.append(line)
        assert 'Guru Meditation' not in line and 'panic' not in line.lower(), line
        if predicate(line):
            return lines
    raise TimeoutError(lines[-5:])


def command(text, error=None):
    print('>>> ' + text, flush=True)
    port.write((text + '\n').encode())
    lines = receive(lambda x: x == 'ok' or x.startswith(('error:', 'ALARM:')))
    assert lines[-1] == ('ok' if error is None else f'error:{error}'), (text, lines)
    if error is not None:
        command('')  # acknowledge the deliberately rejected command
    return lines


def fields(command_text, prefix):
    line = next(x for x in command(command_text) if x.startswith(prefix))
    return dict(x.split(':', 1) for x in line.strip('[]').split('|'))


def diagnostics(fault=False):
    d = fields('$P4', '[P4:')
    assert d['P4'] == 'BENCH' and d['EN'] == 'LOCKED', d
    assert d['ENABLE_PINS'] == '1,0' and d['SYNC'] == 'BENCH', d
    assert d['FAULT'] == str(int(fault)), d
    for key in ('OVERLAP', 'RX_OVF'):
        assert d[key] == '0', d
    assert int(d['LATE']) == (1 if fault and args.fault == 'deadline' else 0), d
    for axis in ('X', 'Z'):
        issued, _, counted = map(int, d[axis].split(','))
        assert issued == counted, d
    return d


def idle():
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        port.write(b'?')
        s = receive(lambda x: x.startswith('<'), 2)[-1]
        assert not s.startswith('<Alarm'), s
        if s.startswith('<Idle|'):
            return
        time.sleep(.05)
    raise TimeoutError('Idle')


def cut(phase=0, pitch=1, direction=1, speed=300, change=None):
    command('$P4SIM=' + str(speed))
    command('M4 S300' if speed < 0 else 'M3 S300')
    time.sleep(.3)
    command('$P4PHASE=' + str(phase))
    if change is not None:
        command('$P4SIMCHANGE=' + str(change) + ',1500')
    before = diagnostics()
    command(f'G33 Z{direction * 10} K{pitch}')
    after = diagnostics()
    assert int(after['Z'].split(',')[0]) - int(before['Z'].split(',')[0]) == 2000
    d = fields('$P4SYNC', '[P4SYNC:')
    assert d['FAULT'] == 'NONE' and d['PULSES'] == '2000', d
    trace = '\n'.join(command('$P4SYNCTRACE'))
    points = [tuple(map(int, match))
              for match in re.findall(r'P4SYNCPOINT:(\d+),(-?\d+)', trace)]
    assert len(points) == 126, len(points)
    errors = [(encoder - points[0][1]) * pitch / 1200 - (step - 1) / 200
              for step, encoder in points if 400 <= step <= 1600]
    spread = max(errors) - min(errors)
    # Allow two full Z steps plus two encoder counts. The speed-step test
    # intentionally measures a transient and uses a separate, stated bound.
    bound = .15 if change is not None else .01 + 2 * pitch / 1200
    assert spread <= bound, (phase, pitch, speed, change, spread, bound)
    phase_at_4mm = next(encoder for step, encoder in points if step == 800) % 1200
    print(f'PASS: phase={phase}, pitch={pitch}, RPM={speed}->{change or speed}, '
          f'cruise phase-error span={spread:.6f} mm; absolute lead-in offset={errors[0]:.6f} mm.', flush=True)
    command('G0 Z0')
    idle()
    return phase_at_4mm


try:
    port.open()
    time.sleep(3)
    port.reset_input_buffer()
    assert any('H5_P4_BENCH_MOTOR_ENABLES_LOCKED' in x for x in command('$I'))
    diagnostics()
    assert any('P4FPUTEST:PASS' in x for x in command('$P4FPUTEST'))
    command('$90=0.25')
    command('$91=0')
    command('G21 G18 G8 G90 G94')
    if args.fault:
        if args.fault == 'deadline':
            command('G1 Z20 F960')
            time.sleep(.3)
            port.write(b'$P4IRQTEST\n')
        else:
            command('$P4SIM=300')
            command('M3 S300')
            time.sleep(.3)
            command('$P4SIMCHANGE=' + ('0' if args.fault == 'stall' else '-300') + ',1500')
            port.write(b'G33 Z20 K1\n')
        receive(lambda x: x.startswith('ALARM:'), 6)
        before = diagnostics(fault=True)
        time.sleep(.3)
        after = diagnostics(fault=True)
        assert before['Z'] == after['Z'], (before, after)
        if args.fault != 'deadline':
            d = fields('$P4SYNC', '[P4SYNC:')
            assert d['FAULT'] == ('STALL' if args.fault == 'stall' else 'REVERSED'), d
        print('PASS: ' + args.fault + ' latches motor fault and stops STEP output; hardware reboot required.', flush=True)
    else:
        ui_before = fields('$P4UI', '[P4UI:')
        command('$P4SIM=300')
        command('M3 S300')
        time.sleep(.3)
        command('$P4ENCODERTEST', error=8)
        command('G33 X1 Z10 K1', error=20)
        command('$P4SIM=OFF')
        phases = [cut(phase=p) for p in (0, 0, 0, 400, 800)]
        registration = [((value - offset - phases[0] + 600) % 1200) - 600
                        for value, offset in zip(phases, (0, 0, 0, 400, 800))]
        assert max(registration) - min(registration) <= 8, registration
        print(f'PASS: repeated/multi-start registration spread {max(registration) - min(registration)} encoder counts.', flush=True)
        cut(pitch=.5)
        cut(pitch=2)
        cut(direction=-1)
        cut(speed=-300)
        if args.abrupt:
            cut(change=240)
            cut(change=360)
        command('$P4SIM=OFF')
        assert any('P4ENCODERTEST:PASS' in x for x in command('$P4ENCODERTEST'))
        assert any('P4FPUTEST:PASS' in x for x in command('$P4FPUTEST'))
        diagnostics()
        ui_after = fields('$P4UI', '[P4UI:')
        assert int(ui_after['UI_UPDATES']) > int(ui_before['UI_UPDATES']) + 100
        print('PASS: spindle bench suite; UI remained active. Speed transients, lead-in and physical phase still require machine validation.', flush=True)
finally:
    if port.is_open:
        port.write(b'\x18')
        port.close()
