#!/usr/bin/env python3
"""Measure ramp tracking and compensated phase on the disconnected P4 bench.

Spindle slew is deliberately below available Z acceleration. These GPIO/PCNT
measurements are not external waveform or loaded motor measurements.
"""
import argparse
import re
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('port')
parser.add_argument('--case', type=int, choices=range(13), help='Run one case while diagnosing a failure')
args = parser.parse_args()
port = serial.Serial()
port.port, port.baudrate, port.timeout = args.port, 115200, .05
port.write_timeout = 2
port.dtr = port.rts = False


def receive(predicate, timeout=30):
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        line = port.readline().decode(errors='replace').strip()
        if not line:
            continue
        print(line, flush=True)
        lines.append(line)
        assert not line.startswith(('error:', 'ALARM:')), line
        assert 'Guru Meditation' not in line and 'panic' not in line.lower(), line
        if predicate(line):
            return lines
    raise TimeoutError(lines[-4:])


def command(text):
    print('>>> ' + text, flush=True)
    port.write((text + '\n').encode())
    return receive(lambda line: line == 'ok')


def fields(text, prefix):
    line = next(line for line in command(text) if line.startswith(prefix))
    return dict(field.split(':', 1) for field in line.strip('[]').split('|'))


def diagnostics():
    data = fields('$P4', '[P4:')
    assert data['EN'] == 'LOCKED' and data['ENABLE_PINS'] == '1,0', data
    for key in ('FAULT', 'OVERLAP', 'LATE', 'RX_OVF'):
        assert data[key] == '0', data
    for axis in ('X', 'Z'):
        issued, _, counted = map(int, data[axis].split(','))
        assert issued == counted, data
    return data


def idle():
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        port.write(b'?')
        if receive(lambda line: line.startswith('<'), 2)[-1].startswith('<Idle|'):
            return
        time.sleep(.05)
    raise TimeoutError('Idle')


def cut(pitch, rpm, target, rate, phase=0, direction=1, delay=900):
    command('$P4SIM=' + str(rpm))
    command('M4 S300' if rpm < 0 else 'M3 S300')
    command('$P4PHASE=' + str(phase))
    time.sleep(.3)
    before = diagnostics()
    if target != rpm:
        assert pitch * rate / 60 < 50, 'Spindle ramp exceeds configured Z acceleration'
        command(f'$P4SIMRAMP={target},{rate},{delay}')
    command(f'G33 Z{20 * direction} K{pitch}')
    data = fields('$P4SYNC', '[P4SYNC:')
    assert data['FAULT'] == 'NONE' and data['PULSES'] == '4000', data
    assert float(data['STEPS_MM']) == 200, data
    lead = float(data['LEAD_MM'])
    assert lead < 12, ('Insufficient cutting distance', lead)
    trace = '\n'.join(command('$P4SYNCTRACE'))
    points = [tuple(map(int, match)) for match in re.findall(r'P4SYNCPOINT:(\d+),(-?\d+),(\d+)', trace)]
    assert len(points) == 251 and points[-1][0] == 4000, len(points)
    # Report phase against the requested spindle reference, not the first
    # output pulse: that would hide the acceleration-dependent phase offset.
    errors = [(encoder - phase) * pitch / 1200 - step / 200
              for step, encoder, _ in points if lead <= step / 200 <= 16]
    # Choose the arbitrary whole-revolution origin once, not at each sample:
    # wrapping every point separately could conceal a whole-turn phase slip.
    origin = round(errors[0] / pitch) * pitch
    errors = [error - origin for error in errors]
    assert len(errors) >= 40, len(errors)
    peak, span = max(map(abs, errors)), max(errors) - min(errors)
    assert peak <= .01 + 2 * pitch / 1200, (pitch, rpm, target, phase, peak)
    velocities = []
    for a, b in zip(points, points[1:]):
        if lead <= a[0] / 200 and b[0] / 200 <= 16:
            velocities.append(((a[2] + b[2]) / 2e6, (b[0] - a[0]) / 200 * 1e6 / (b[2] - a[2])))
    acceleration = max(abs((b[1] - a[1]) / (b[0] - a[0])) for a, b in zip(velocities, velocities[1:]))
    assert acceleration <= 50, ('Sampled acceleration exceeds Z setting', acceleration, pitch, rpm, target)
    coarse = points[::4]
    if coarse[-1] != points[-1]:
        coarse.append(points[-1])
    all_velocities = [((a[2] + b[2]) / 2e6, (b[0] - a[0]) / 200 * 1e6 / (b[2] - a[2]))
                      for a, b in zip(coarse, coarse[1:])]
    whole_acceleration = max(abs((b[1] - a[1]) / (b[0] - a[0])) for a, b in zip(all_velocities, all_velocities[1:]))
    # At high feeds 16 steps take less than one planner segment, overstating
    # normal staircase changes. Use 64-step windows for whole-move acceleration.
    # The earlier end-of-cut defect still exceeds 60 with this same estimator.
    assert whole_acceleration <= 60, ('Abrupt acceleration/deceleration', whole_acceleration, pitch, rpm, target)
    after = diagnostics()
    assert int(after['Z'].split(',')[0]) - int(before['Z'].split(',')[0]) == 4000
    print(f'PASS TRACKING: K={pitch}, RPM={rpm}->{target}, slew={rate}, phase={phase}, '
          f'lead={lead:.3f}mm, peak phase error={peak:.6f}mm, span={span:.6f}mm, '
          f'peak sampled acceleration={acceleration:.3f}mm/s^2, whole move={whole_acceleration:.3f}mm/s^2', flush=True)
    command('$P4SIM=0')
    command('G0 Z0')
    idle()


try:
    port.open()
    time.sleep(3)
    port.reset_input_buffer()
    assert any('H5_P4_BENCH_MOTOR_ENABLES_LOCKED' in line for line in command('$I'))
    diagnostics()
    before = fields('$P4UI', '[P4UI:')
    settings = command('$$')
    assert any(line.startswith('$90=') and float(line.split('=')[1]) == .25 for line in settings), 'Unexpected default sync P gain'
    assert any(line.startswith('$91=') and float(line.split('=')[1]) == 0 for line in settings), 'Unexpected default sync I gain'
    command('G21 G18 G8 G90 G94')
    cases = [(.5, 300, 360, 100), (1, 300, 360, 30), (1, 300, 240, 100),
                 (2, 300, 360, 100), (2, 300, 240, 30), (1, 150, 210, 100),
                 (1, 450, 390, 100), (1, -300, -360, 100, 400, -1),
                 (1, 150, 150, 0, 0), (1, 300, 300, 0, 400), (1, 450, 450, 0, 800),
                 (2, 300, 360, 100, 0, 1, 100), (2, 300, 360, 30, 0, 1, 1900)]
    for case in cases if args.case is None else [cases[args.case]]:
        cut(*case)
    command('$P4SIM=OFF')
    assert any('P4FPUTEST:PASS' in line for line in command('$P4FPUTEST'))
    diagnostics()
    after = fields('$P4UI', '[P4UI:')
    assert int(after['UI_UPDATES']) > int(before['UI_UPDATES']) + 100
    print('PASS: ramp tracking, phase compensation, pulse counts and UI liveness.', flush=True)
finally:
    if port.is_open:
        port.write(b'\x18')
        port.close()
