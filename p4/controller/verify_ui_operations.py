#!/usr/bin/env python3
"""Exercise operation START/RUN/STOP, manual override and depth skip via LVGL."""
import sys,time
from bench_client import Bench
b=Bench(sys.argv[1])
try:
    b.command('G21G18G8G90G94G53G0X0Z0');b.idle();b.command('$P4SIM=300');time.sleep(.4)
    for action in 'FCE':
        b.command('G90G94G53G0X0Z0');b.idle()
        b.command('$P4UITEST='+action);time.sleep(.15)
        b.command('$P4UITEST=7')
        b.receive(lambda s:'STAGE:Cycle complete' in s,60)
        b.diagnostics();print('PASS UI CYCLE: '+action,flush=True)
    # Long-pressing PASSES skips a depth only after the current depth completes.
    b.command('$P4CYCLETRACE=1')
    b.command('$P4CYCLE=2,0.05,4,1,1,0,0.1,0,1,360')
    b.receive(lambda s:'STAGE:Cut]' in s,30)
    b.command('$P4UITEST=D')
    lines=b.receive(lambda s:'STAGE:Cycle complete' in s,60)
    cuts=[s for s in lines if s.startswith('[H5DONE:') and '|STAGE:Cut|' in s]
    assert len(cuts)==3 and [int(s.split(':')[2].split('|')[0]) for s in cuts]==[1,3,4],cuts
    print('PASS UI: depth skip occurs at a completed depth boundary and retains final depth',flush=True)
    for action in 'GKA':
        b.command('G90G94G53G0X0Z0');b.idle()
        b.command('$P4UITEST='+action)
        b.receive(lambda s:'STAGE:Following' in s,20)
        time.sleep(.4);b.command('$P4UITEST=1')
        b.receive(lambda s:'STAGE:Manual override' in s,10)
        time.sleep(.2);b.command('$P4UITEST=0')
        b.receive(lambda s:'STAGE:Following' in s,10)
        reference=next(s for s in reversed(b.history) if s.startswith('[H5FOLLOWREF:'))
        expected=2*float(reference.split('|PITCH:')[1].rstrip(']'))
        time.sleep(.3);b.command('$P4UITEST=+')
        b.receive(lambda s:s.startswith('[H5FOLLOWREF:') and f'|PITCH:{expected:.6f}' in s,10)
        b.receive(lambda s:'STAGE:Following' in s,10)
        time.sleep(.3);b.command('$P4UITEST=8')
        b.receive(lambda s:s.startswith('GrblHAL '),10);time.sleep(.2);b.idle();b.diagnostics()
        print('PASS UI FOLLOW: '+action+', manual press/release and STOP',flush=True)
    b.command('$P4SIM=OFF')
    # Update-panel close releases the motion lock without arming an operation.
    b.command('$P4UITEST=U');time.sleep(.3)
    d=b.fields('$P4OTA','[P4OTA:');assert d['P4OTA']=='ACTIVE:1'
    b.command('G1X1F60',error=8)
    b.command('$P4UITEST=Q');time.sleep(.3)
    assert b.fields('$P4OTA','[P4OTA:')['P4OTA']=='ACTIVE:0'
    b.command('G91G94G1X0.01F60');b.idle();b.diagnostics()
    print('PASS UI: update panel locks motion and CLOSE releases it',flush=True)
finally:b.close()
