#!/usr/bin/env python3
"""Disconnected Face/Cut/Ellipse endpoint, pass ordering and stop regression."""
import sys,time,re
from bench_client import Bench
b=Bench(sys.argv[1])
try:
    b.command('G21G18G8G90G94'); b.command('$P4CYCLETRACE=1')
    for op in (2,3,4):
        for reverse in (False,True):
            b.command('G90G94G53G0X0Z0');b.idle()
            rpm=-300 if reverse else 300
            b.command(f'$P4SIM={rpm}');time.sleep(.3)
            before=b.diagnostics()
            b.command(f'$P4CYCLE={op},0.05,2,1,{int(not reverse)},0,0.1,0,1,360')
            lines=b.receive(lambda s:'STAGE:Cycle complete' in s,90)
            cuts=[s for s in lines if s.startswith('[H5DONE:') and '|STAGE:Cut|' in s]
            assert len(cuts)==2,cuts
            for n,s in enumerate(cuts,1):
                d=dict(v.split(':',1) for v in s[1:-1].split('|'));x,z=float(d['X']),float(d['Z'])
                if op==2: ex,ez=(0 if reverse else .1),(1-n/2 if reverse else n/2)
                elif op==3: ex,ez=(.1-.1*n/2 if reverse else .1*n/2),0
                else: ex,ez=.1*n/2,(0 if reverse else 1)
                assert abs(x-ex)<=1/1200+.000001 and abs(z-ez)<=1/200+.000001,(s,ex,ez)
            after=b.diagnostics();assert int(after['X'].split(',')[0])>int(before['X'].split(',')[0])
            print(f'PASS PROFILE: operation {op}, spindle {rpm}, two depths at expected endpoints',flush=True)
    # STOP during an ellipse must discard queued chords, decelerate and release
    # ownership. A subsequent cycle must still be usable.
    b.command('$P4SIM=300');time.sleep(.3)
    b.command('$P4CYCLE=4,0.02,2,1,1,0,0.2,0,3,360')
    b.receive(lambda s:'STAGE:Cut]' in s,30)
    time.sleep(.3);b.port.write(b'!')
    b.receive(lambda s:'GrblHAL' in s or 'grblHAL' in s,10)
    time.sleep(.3);b.idle();b.diagnostics()
    assert b.fields('$P4CYCLE','[H5CYCLE:')['H5CYCLE']=='ACTIVE:0'
    b.command('$P4SIM=OFF')
    print('PASS: profile cancellation discards queued chords and returns to Idle',flush=True)
finally:b.close()
