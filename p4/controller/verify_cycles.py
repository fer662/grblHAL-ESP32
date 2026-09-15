#!/usr/bin/env python3
"""Exercise Turn/Thread service and actual LVGL START/STOP on the disconnected P4.

Motor enables must be locked. Encoder transitions are synthesized on GPIO.
No inference about loaded motor or physical thread accuracy is made.
"""
import argparse
import math
from pathlib import Path
import re
import struct
import time
import zlib
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('port')
parser.add_argument('--limit-only', action='store_true', help='Run just the RPM-ceiling cancellation regression')
parser.add_argument('--screen', type=Path, help='Save the rendered cycle preview')
args = parser.parse_args()
port = serial.Serial()
port.port, port.baudrate, port.timeout = args.port, 115200, .05
port.dtr = port.rts = False
history = []


def receive(predicate, timeout=15, allow_busy=False):
    deadline = time.monotonic()+timeout
    lines = []
    while time.monotonic() < deadline:
        line = port.readline().decode(errors='replace').strip()
        if not line:
            continue
        history.append(line)
        print(line, flush=True)
        lines.append(line)
        assert 'panic' not in line.lower() and 'Guru Meditation' not in line, line
        assert not line.startswith('ALARM:'), line
        assert not line.startswith('error:') or (allow_busy and line == 'error:8'), line
        if predicate(line):
            return lines
    raise TimeoutError(lines[-8:])


def command(text):
    print('>>> '+text, flush=True)
    port.write((text+'\n').encode())
    return receive(lambda line: line == 'ok', timeout=60)


def fields(line):
    return dict(item.split(':', 1) for item in line.strip('[]').split('|'))


def diag():
    data = fields(next(line for line in command('$P4') if line.startswith('[P4:')))
    assert data['EN'] == 'LOCKED' and data['ENABLE_PINS'] == '1,0'
    assert all(data[k] == '0' for k in ('FAULT', 'OVERLAP', 'LATE', 'RX_OVF')), data
    for axis in ('X', 'Z'):
        issued, _, counted = map(int, data[axis].split(','))
        assert issued == counted, data
    return data


def idle():
    deadline = time.monotonic()+10
    while time.monotonic() < deadline:
        port.write(b'?')
        if receive(lambda line: line.startswith('<'))[-1].startswith('<Idle|'):
            return
        time.sleep(.05)
    raise TimeoutError('Idle')


def screenshot():
    data = command('$P4SCREEN')
    header = next(line for line in data if line.startswith('[P4SCREEN:'))
    width, height = map(int, re.search(r'(\d+),(\d+)', header).groups())
    rgb = bytearray()
    for line in data[data.index(header)+1:data.index('[P4SCREEN:END]')]:
        assert re.fullmatch(r'(?:[0-9a-f]{8})+', line)
        for i in range(0, len(line), 8):
            count, color = int(line[i:i+4], 16), int(line[i+4:i+8], 16)
            rgb.extend(bytes((((color >> 11) & 31)*255//31, ((color >> 5) & 63)*255//63, (color & 31)*255//31))*count)
    assert len(rgb) == width*height*3
    if args.screen:
        def chunk(kind, payload):
            return struct.pack('>I', len(payload))+kind+payload+struct.pack('>I', zlib.crc32(kind+payload))
        raw = b''.join(b'\0'+rgb[y*width*3:(y+1)*width*3] for y in range(height))
        args.screen.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))+chunk(b'IDAT', zlib.compress(raw))+chunk(b'IEND', b''))


def validate_cycle(lines, before, passes, starts, pitch, rpm, forward, length):
    plan = fields(next(line for line in lines if line.startswith('[H5PLAN:')))
    lead, lead_in = float(plan['H5PLAN'].split(':', 1)[1]), float(plan['LEAD_IN'])
    assert abs(lead-abs(pitch)*starts) < 1e-6
    direction = (1 if rpm*pitch > 0 else -1)
    z_start, z_end = (0, length) if direction > 0 else (length, 0)
    x_start, x_end = (0, .1) if forward else (.1, 0)
    assert abs(float(plan['APPROACH'])-(z_start-direction*lead_in)) < 1e-6
    assert (float(plan['FINISH'])-z_end)*direction > 0
    done = [fields(line) for line in lines if line.startswith('[H5DONE:')]
    expected = ['Setup', 'Retract', 'Approach', 'Take up'] + ['Infeed', 'Register', 'Spindle', 'Cut', 'Retract', 'Return', 'Take up']*(passes*starts) + ['Return to start', 'Finish infeed', 'Restore phase', 'Finish']
    assert [item['STAGE'] for item in done] == expected, [item['STAGE'] for item in done]
    cuts = [item for item in done if item['STAGE'] == 'Cut']
    for i, cut in enumerate(cuts):
        assert int(cut['H5DONE'].split(':')[-1]) == i//starts+1
        assert int(cut['START']) == i % starts+1
        assert abs(float(cut['X'])-(x_start+(x_end-x_start)*(i//starts+1)/passes)) <= .5/1200+1e-6
        assert abs(float(cut['Z'])-float(plan['FINISH'])) <= .002501
    syncs = [i for i, line in enumerate(lines) if line.startswith('[P4SYNC:')]
    assert len(syncs) == passes*starts
    peaks = []
    for i, index in enumerate(syncs):
        sync = fields(lines[index])
        points = []
        for line in lines[index+1:]:
            match = re.fullmatch(r'\[P4SYNCPOINT:(\d+),(-?\d+),(\d+)\]', line)
            if not match:
                break
            points.append(tuple(map(int, match.groups())))
        assert points and len(points) < 512
        expected_pulses = round(abs(float(plan['FINISH'])-float(plan['APPROACH']))*200)
        assert int(sync['PULSES']) == expected_pulses
        assert sync['FAULT'] == 'NONE'
        reference = (i % starts)*1200/starts
        errors = [(encoder-reference)*lead/1200-(step/200-lead_in)
                  for step, encoder, _ in points if lead_in <= step/200 <= lead_in+length]
        assert len(errors) >= 20
        origin = round(errors[0]/lead)*lead
        peak = max(abs(error-origin) for error in errors)
        assert peak <= .015+2*lead/1200, (i, peak)
        peaks.append(peak)
    after = diag()
    for axis, scale in (('X', 1200), ('Z', 200)):
        position = int(before[axis].split(',')[1])
        travel = 0
        for item in done:
            target = round(float(item[axis])*scale)
            travel += abs(target-position)
            position = target
        assert int(after[axis].split(',')[0])-int(before[axis].split(',')[0]) == travel, (axis, travel, before, after)
    assert abs(float(done[-1]['X'])-x_start) < .001
    assert abs(float(done[-1]['Z'])-z_start) < .005
    print(f'PASS CYCLE: {passes} passes x {starts} starts, pitch={pitch}, RPM={rpm}, aux={forward}, peak phase={max(peaks):.6f}mm; exact stage order and pulse totals', flush=True)


def cycle(passes, starts, pitch, rpm=300, forward=True, length=6, ui=False, collision=False, threading=True):
    command('$P4SIM='+str(rpm))
    time.sleep(.4)
    before = diag()
    command('$P4CYCLETRACE=1')
    if ui:
        command('$P4UITEST=6')
        time.sleep(.3)
        screenshot()
        assert diag()['Z'] == before['Z'], 'Preview caused motion'
    beginning = len(history)
    if ui:
        command('$P4UITEST=7')
    else:
        command(f'$P4CYCLE={int(threading)},{pitch},{passes},{starts},{int(forward)},0,0.1,0,{length},375')
    if collision:
        receive(lambda line: '|STAGE:Cut]' in line, 15)
        time.sleep(.3)
        port.write(b'G0Z99\n$110=1\n')
    receive(lambda line: '|STAGE:Cycle complete]' in line, 65, allow_busy=collision)
    lines = history[beginning:]
    if collision:
        assert lines.count('error:8') == 2
        assert any(line.startswith('$110=60') for line in command('$$'))
    validate_cycle(lines, before, passes, starts if threading else 1, pitch, rpm, forward, length)


def cancelled(stage, via_ui):
    command('$P4SIM=300'); time.sleep(.3)
    command('$P4CYCLETRACE=0')
    command('$P4CYCLE=1,0.5,2,2,1,0,0.1,0,6,375')
    receive(lambda line: f'|STAGE:{"Cut" if stage == "Index" else stage}]' in line, 15)
    time.sleep(.8 if stage == 'Cut' else .05)
    if via_ui:
        command('$P4UITEST=8')
    else:
        port.write(b'\x85')
    receive(lambda line: 'GrblHAL' in line, 10)
    time.sleep(.3)
    idle()
    before = diag()
    time.sleep(.5)
    after = diag()
    assert before['X'] == after['X'] and before['Z'] == after['Z']
    assert any('ACTIVE:0' in line for line in command('$P4CYCLE'))
    if stage == 'Index':
        assert abs(int(after['Z'].split(',')[1])/200+3.135) < .005, 'Moved while waiting for index'
    if stage == 'Cut':
        assert int(after['Z'].split(',')[1])/200 < 7.96, 'STOP allowed the cut to finish'
    print(f'PASS CANCEL: {stage}, via '+('LVGL STOP' if via_ui else 'realtime cancel')+'; decelerated, reset, no queued return', flush=True)


def rpm_limit():
    command('$P4SIM=300'); time.sleep(.3)
    command('G21G8G90G53G0X0Z0'); idle()
    command('$P4SIMRAMP=450,100,2500')
    command('$P4CYCLE=1,0.5,1,2,1,0,0.1,0,6,375')
    lines = receive(lambda line: 'GrblHAL' in line, 15)
    assert any('Spindle outside RPM range' in line for line in lines)
    time.sleep(.3); idle()
    data = diag()
    assert int(data['Z'].split(',')[1])/200 < 7.96, 'RPM limit allowed the cut to finish'
    assert any('ACTIVE:0' in line and 'Spindle outside RPM range' in line for line in command('$P4CYCLE'))
    command('$P4SIM=OFF')
    print('PASS: RPM ceiling cancels during cutting and retains the reason for the operator.', flush=True)


try:
    port.open(); time.sleep(3); port.reset_input_buffer()
    assert any('H5_P4_BENCH_MOTOR_ENABLES_LOCKED' in line for line in command('$I'))
    ready_deadline=time.monotonic()+30
    while not any('P4UI:READY:1' in s for s in command('$P4UI')):
        assert time.monotonic()<ready_deadline,'Peripheral initialization did not finish'
        time.sleep(.1)
    diag()
    if args.limit_only:
        rpm_limit()
        raise SystemExit(0)
    cycle(2, 2, .5, ui=True, collision=True)
    command('G92X10Z20')
    command('G20G7')
    cycle(2, 1, .5, rpm=-300, forward=False)
    command('G92.1')
    cycle(1, 2, -.5)
    cycle(2, 1, .1, length=3, threading=False)
    cancelled('Approach', False)
    cancelled('Index', False)
    cancelled('Cut', True)
    # A rejected cycle must not move either axis.
    before = diag()
    command('$P4CYCLE=1,0.5,2,2,1,0,0.1,0,310,375')
    receive(lambda line: 'exceeds travel span' in line)
    after = diag()
    assert before['X'] == after['X'] and before['Z'] == after['Z']
    command('G51X2')
    command('$P4CYCLE=1,0.5,1,1,1,0,0.1,0,6,375')
    receive(lambda line: 'Cancel coordinate scaling/rotation' in line)
    after = diag()
    assert before['X'] == after['X'] and before['Z'] == after['Z']
    command('G50')
    rpm_limit()
    command('$P4SIM=OFF')
    assert any('P4FPUTEST:PASS' in line for line in command('$P4FPUTEST'))
    assert any('P4UI:READY:1' in line for line in command('$P4UI'))
    print('PASS: assisted cycles, multistart phase, parser ownership, cancellation, invalid geometry, FPU and UI.', flush=True)
finally:
    if port.is_open:
        port.write(b'\x18'); port.close()
