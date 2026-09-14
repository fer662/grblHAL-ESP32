#!/usr/bin/env python3
"""Exercise real grblHAL on an isolated P4 bench build with enables locked.

Run with the IDF Python environment. Never run with the lathe attached.
No flash operations or permanent settings writes are performed by this client.
"""
import argparse
import re
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('port')
args = parser.parse_args()
port = serial.Serial()
port.port = args.port
port.baudrate = 115200
port.timeout = 0.05
port.write_timeout = 2
port.dtr = port.rts = False


def receive(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        line = port.readline().decode('utf-8', 'replace').strip()
        if not line:
            continue
        print(line, flush=True)
        lines.append(line)
        if 'Guru Meditation' in line or 'panic' in line.lower():
            raise RuntimeError('Firmware crashed')
        if predicate(line):
            return lines
    raise RuntimeError('Response deadline exceeded')


def command(text, error=None):
    print('>>> ' + text, flush=True)
    port.write((text + '\n').encode())
    lines = receive(lambda s: s == 'ok' or s.startswith('error:'))
    expected = 'ok' if error is None else f'error:{error}'
    assert lines[-1] == expected, (text, lines)
    if error is not None:
        # grblHAL deliberately blocks subsequent G-code after an error.
        # Acknowledge it before the next independent test command.
        command('')
    return lines


def status():
    port.write(b'?')
    return receive(lambda s: s.startswith('<') and s.endswith('>'))[-1]


def wait_state(state, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = status()
        if line.startswith('<' + state + '|'):
            return line
        if line.startswith('<Alarm') and state != 'Alarm':
            raise RuntimeError(line)
        time.sleep(.05)
    raise RuntimeError('State deadline: ' + state)


def diagnostics():
    lines = command('$P4')
    line = next(s for s in lines if s.startswith('[P4:'))
    assert '|EN:LOCKED|NVS:RAM|SYNC:OFF|' in line, line
    fields = dict(part.split(':', 1) for part in line.strip('[]').split('|'))
    assert fields['ENABLE_PINS'] == '1,0', fields
    for key in ('FAULT', 'OVERLAP', 'LATE', 'RX_OVF'):
        assert fields[key] == '0', (key, fields[key])
    for axis in ('X', 'Z'):
        pulses, position, observed = map(int, fields[axis].split(','))
        assert pulses == observed, (axis, pulses, observed)
        fields[axis] = pulses, position, observed
    return fields


def idle():
    wait_state('Idle', timeout=25)
    # Allow the last pulse-clear interrupt to complete before reading counters.
    time.sleep(.03)
    return diagnostics()


def exact_move(gcode, delta_x, delta_z):
    before = diagnostics()
    command(gcode)
    after = idle()
    for axis, delta in (('X', delta_x), ('Z', delta_z)):
        assert after[axis][0] - before[axis][0] == abs(delta), (gcode, axis, before, after)
        assert after[axis][1] - before[axis][1] == delta, (gcode, axis, before, after)
    return after


try:
    port.open()
    # Positive identification before sending any commands that could move axes.
    receive(lambda s: 'H5_P4_BENCH_MOTOR_ENABLES_LOCKED' in s or 'GrblHAL' in s, timeout=12)
    info = command('$I')
    assert any('H5_P4_BENCH_MOTOR_ENABLES_LOCKED' in s for s in info), info
    diagnostics()
    if status().startswith('<Alarm'):
        command('$X')
    assert any('P4ENCODERTEST:PASS' in s for s in command('$P4ENCODERTEST'))
    command('G21 G18 G8 G91 G94')
    exact_move('G1 X1 Z2 F60', 1200, 400)
    exact_move('G1 X-1 Z-2 F60', -1200, -400)
    print('PASS: coordinated X/Z move and reverse; GPIO pulse counts and net positions exact.', flush=True)

    command('$P4TRACE=RESET')
    exact_move('G1 X1 F60', 1200, 0)
    traces = command('$P4TRACE')
    trace = next(s for s in traces if s.startswith('[P4TRACE:X|'))
    fields = dict(part.split(':', 1) for part in trace.strip('[]').split('|'))
    first = list(map(int, fields['FIRST'].split(',')))
    last = list(map(int, fields['LAST'].split(',')))
    fastest = int(fields['MIN'])
    assert first[0] > first[-1] and last[-1] > last[0], trace
    assert first[0] > fastest * 1.3 and last[-1] > fastest * 1.3, trace
    print('PASS: X pulse intervals show acceleration, cruise, and deceleration.', flush=True)

    command('G1 Y1 F60', error=20)
    command('G33 Z1 K1', error=20)
    print('PASS: absent Y and unsupported spindle-sync commands rejected.', flush=True)

    command('G1 Z20 F960')
    time.sleep(.2)
    port.write(b'!')
    wait_state('Hold:0')
    held = status().split('|MPos:')[1].split('|')[0]
    time.sleep(.2)
    assert status().split('|MPos:')[1].split('|')[0] == held, 'Motion continued during feed hold'
    port.write(b'~')
    idle()
    print('PASS: feed hold reaches standstill; resume finishes.', flush=True)

    for i in range(12):
        before = diagnostics()
        command('$J=G91 Z10 F600')
        time.sleep(.06)
        port.write(b'\x85')
        after = idle()
        delta = after['Z'][0] - before['Z'][0]
        assert 0 < delta < 2000
        assert after['Z'][1] - before['Z'][1] == delta
        exact_move('$J=G91 Z-0.1 F60', 0, -20)
    print('PASS: 12 jog/cancel/reverse/restart cycles without a stuck axis.', flush=True)

    command('G1 Z50 F960')
    time.sleep(.1)
    port.write(b'\x18')
    receive(lambda s: 'GrblHAL' in s, timeout=5)
    if status().startswith('<Alarm'):
        command('$X')
    command('G21 G18 G8 G91 G94')
    exact_move('G1 X0.1 Z-0.1 F60', 120, -20)
    print('PASS: reset during motion followed by a fresh coordinated move.', flush=True)

    before = diagnostics()
    command('G1 Z200 F960')
    command('$I+')  # Exercise report output while step segments still need preparation.
    after = idle()
    assert after['Z'][0] - before['Z'][0] == 40000
    assert after['Z'][1] - before['Z'][1] == 40000
    print('PASS: 40,000-pulse move crosses PCNT hardware rollover without lost counts.', flush=True)
    final = diagnostics()
    minimum, maximum = map(int, final['PULSE'].split(','))
    assert minimum >= 100, (minimum, maximum)  # 10 MHz clock => 10 us requested width
    print('PASS: P4 grblHAL bench suite complete. Physical load, encoder phase, and external waveform measurements still required.', flush=True)
finally:
    port.close()
