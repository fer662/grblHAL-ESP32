#!/usr/bin/env python3
"""Check USB reopen, UI progress, and screenshot lock release on the idle bench app.

No motion, settings or flash writes. The USB bridge may reset on port open.
"""
import argparse
import re
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('port')
parser.add_argument('--seconds', type=float, default=60)
args = parser.parse_args()


def connect():
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, .1
    port.dtr = port.rts = False
    port.open()
    return port


def response(port, command, timeout=6):
    port.write((command + '\n').encode())
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        line = port.readline().decode('ascii', 'replace').strip()
        if not line:
            continue
        lines.append(line)
        if line.startswith('error:') or 'panic' in line.lower() or 'Guru Meditation' in line:
            raise RuntimeError(line)
        if line == 'ok':
            return lines
    raise TimeoutError((command, lines[-8:]))


def ui(port):
    lines = response(port, '$P4UI')
    line = next(s for s in lines if s.startswith('[P4UI:READY:1|'))
    return dict(part.split(':', 1) for part in line[1:-1].split('|')[1:])


start = time.monotonic()
previous = None
reopens = captures = 0
while time.monotonic() - start < args.seconds or reopens < 3:
    with connect() as port:
        time.sleep(2.5)  # This bridge can reset the controller on every open.
        port.reset_input_buffer()
        info = response(port, '$I')
        assert any('H5_P4_BENCH_MOTOR_ENABLES_LOCKED' in s for s in info)
        current = ui(port)
        if previous:
            if int(current['UPTIME']) <= int(previous['UPTIME']):
                print('NOTE: USB reopen reset the controller; waiting for boot is required.', flush=True)
        if reopens in (0, 2):
            screen = response(port, '$P4SCREEN', timeout=60)
            match = re.fullmatch(r'\[P4SCREEN:(\d+),(\d+)\|RGB565_RLE\]', screen[0])
            assert match, screen[:2]
            assert screen[-2:] == ['[P4SCREEN:END]', 'ok']
            count = 0
            for line in screen[1:-2]:
                assert re.fullmatch(r'(?:[0-9a-f]{8})+', line)
                count += sum(int(line[i:i + 4], 16) for i in range(0, len(line), 8))
            assert count == int(match[1]) * int(match[2])
            captures += 1
        # A successful snapshot must release the display lock.
        time.sleep(.2)
        after = ui(port)
        assert int(after['UI_UPDATES']) > int(current['UI_UPDATES']), 'UI stopped after snapshot'
        diagnostics = response(port, '$P4')
        data = next(s for s in diagnostics if s.startswith('[P4:'))
        for field in ('FAULT', 'OVERLAP', 'LATE', 'RX_OVF'):
            assert f'|{field}:0|' in data, data
        previous = after
        reopens += 1
        print(f'PASS: connection {reopens}, UI updates={after["UI_UPDATES"]}, uptime={after["UPTIME"]} ms', flush=True)
    time.sleep(2)
print(f'PASS: {reopens} USB connections, {captures} screen captures, UI progress on each connection over {time.monotonic() - start:.1f}s.')
with connect() as port:
    time.sleep(2.5)
    port.reset_input_buffer()
    previous = ui(port)
    hold_start = time.monotonic()
    polls = 0
    while time.monotonic() - hold_start < args.seconds:
        time.sleep(1)
        current = ui(port)
        assert int(current['UPTIME']) > int(previous['UPTIME']), 'Reboot while connected'
        assert int(current['UI_UPDATES']) > int(previous['UI_UPDATES']), 'UI stopped while connected'
        previous = current
        polls += 1
        if polls % 10 == 0:
            print(f'PASS: continuous connection {polls}s; UI updates={current["UI_UPDATES"]}', flush=True)
    print(f'PASS: persistent link and UI progress for {time.monotonic() - hold_start:.1f}s without reset.')
