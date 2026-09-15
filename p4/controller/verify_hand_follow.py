#!/usr/bin/env python3
"""Disconnected encoder-position feed: low speed, reversal, bounds and handoff.

Uses actual encoder/STEP GPIO loopback and the native planner, with enables locked.
No assertion here certifies external electrical waveforms or loaded machine motion.
"""
import re
import sys
import time
from bench_client import Bench

b = Bench(sys.argv[1])

def status():
    b.port.write(b'?')
    line = b.receive(lambda s: s.startswith('<'), 3)[-1]
    return line, list(map(float, re.search(r'MPos:([^|]+)', line).group(1).split(',')))

def counts():
    return int(b.fields('$P4SYNC', '[P4SYNC:')['RAW'])

def stop():
    b.port.write(b'!')
    b.receive(lambda s: s.startswith('GrblHAL '), 10)
    time.sleep(.2)
    b.idle()
    b.diagnostics()

def arm(mode, pitch, rpm, bounds=(-10, 10)):
    b.command('G21G18G8G90G94G53G0X0Z0')
    b.idle()
    b.command(f'$P4SIM={rpm}')
    time.sleep(.2)
    b.history = []
    b.command(f'$P4FOLLOW={mode},{pitch},0.2,0,-2,2,{bounds[0]},{bounds[1]}')
    if not any('STAGE:Following spindle position' in s for s in b.history):
        b.receive(lambda s: 'STAGE:Following spindle position' in s, 5)
    ref = next(s for s in b.history if s.startswith('[H5FOLLOWREF:'))
    fields = dict(v.split(':', 1) for v in ref[1:-1].split('|'))
    return float(fields['H5FOLLOWREF'].split(':')[1]), int(fields['COUNTS'])

def freeze_and_check(ref, pitch, mode, phase_only=False):
    b.command('$P4SIM=0')
    time.sleep(.5)
    b.idle()
    raw = counts()
    _, p = status()
    expected = ref[0] + (raw - ref[1]) / 1200 * pitch
    error = p[2] - expected
    if phase_only:
        error -= round(error / pitch) * pitch
    assert abs(error) <= .0031, (p, expected, error)
    if mode == 1 and not phase_only:
        assert abs(p[0] - expected * .1) <= .0011, (p, expected)
    time.sleep(.4)
    assert status()[1] == p, 'Creep with stationary spindle'
    return p

try:
    # Positive/negative pitches and both spindle directions, including a large
    # lead that needs continuous acceleration through queued planner endpoints.
    for mode, pitch, rpm in [(0, .6, 1), (0, .1, 5), (0, -.1, 15),
                             (0, .2, 29), (1, .4, 15), (0, 8, 29)]:
        ref = arm(mode, pitch, rpm)
        time.sleep(1.2)
        a = freeze_and_check(ref, pitch, mode)
        assert abs(a[2]) >= .005, a
        b.command(f'$P4SIM={-rpm}')
        time.sleep(1.8)
        c = freeze_and_check(ref, pitch, mode)
        assert (c[2] - a[2]) * pitch < 0, (a, c)
        stop()
        print(f'PASS HAND FOLLOW: mode={mode} pitch={pitch} rpm=+/-{rpm}', flush=True)

    # Complete turns at a bound must be forgotten, while the partial turn and
    # original mechanical phase remain. Five extra turns must not delay reversal.
    ref = arm(1, .6, 29, (-.1, .1))
    time.sleep(11)
    s, p = status()
    assert s.startswith('<Idle|') and abs(p[2] - .1) < .003, (s, p)
    b.command('$P4SIM=-29')
    end = time.monotonic() + 2.6
    while time.monotonic() < end:
        _, q = status()
        if q[2] < .075:
            break
        time.sleep(.1)
    else:
        raise AssertionError(('Excess turns retained at bound', q))
    time.sleep(1)
    s, q = status()
    assert s.startswith('<Idle|') and abs(q[2] + .1) < .003 and abs(q[0] + .01) < .001, q
    stop()
    print('PASS HAND BOUNDS: excess revolutions discarded; reverse reaches opposite stop', flush=True)

    # Low -> powered G33 -> low, then manual pause/release. The final position
    # must still have the starting mechanical phase, modulo complete leads.
    ref = arm(0, .1, 15)
    time.sleep(1)
    b.command('$P4SIM=60')
    b.receive(lambda s: s == '[H5FOLLOW:ACTIVE:1|STAGE:Following]', 8)
    time.sleep(3)
    b.command('$P4SIM=15')
    b.receive(lambda s: 'STAGE:Following spindle position' in s, 10)
    time.sleep(5)
    freeze_and_check(ref, .1, 0, phase_only=True)
    b.command('$P4MANUAL=X,1,0.1,1')
    b.receive(lambda s: 'STAGE:Manual override' in s, 8)
    time.sleep(.6)
    _, p = status()
    time.sleep(.3)
    assert status()[1] == p
    b.command('$P4RELEASE')
    b.command('$P4SIM=-15')
    time.sleep(5)
    freeze_and_check(ref, .1, 0, phase_only=True)
    stop()
    print('PASS HAND HANDOFF: G33 transitions and manual release retain spindle phase', flush=True)
    # Repeated transitions deliberately interrupt tiny completed moves and G33's
    # index wait. They must not strand the core in a cancellation hold.
    ref = arm(0, .1, 15)
    for i in range(12):
        time.sleep(.15 + i * .013)
        b.command('$P4SIM=60')
        b.receive(lambda s: s == '[H5FOLLOW:ACTIVE:1|STAGE:Following]', 5)
        time.sleep(.11 + i * .007)
        b.command('$P4SIM=15')
        b.receive(lambda s: 'STAGE:Following spindle position' in s, 5)
    stop()
    print('PASS HAND CANCELLATION: 12 repeated low/G33/index-wait transitions', flush=True)
    b.command('$P4SIM=OFF')
finally:
    b.close()
