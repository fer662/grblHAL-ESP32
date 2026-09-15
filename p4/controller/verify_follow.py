#!/usr/bin/env python3
"""Disconnected assisted-feed bounds, reversal, manual override and STOP."""
import sys,time,re
from bench_client import Bench
b=Bench(sys.argv[1])
def status():
    b.port.write(b'?')
    s=b.receive(lambda s:s.startswith('<'),3)[-1]
    xyz=list(map(float,re.search(r'MPos:([^|]+)',s).group(1).split(',')))
    return s,xyz

def wait_bound(x,z,timeout=30):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        s,p=status()
        if s.startswith('<Idle|') and abs(p[0]-x)<.002 and abs(p[2]-z)<.007:return
        time.sleep(.15)
    raise AssertionError(('bound',x,z,p))
def phase_check(z_start,direction):
    ref=next(s for s in b.history if s.startswith('[H5FOLLOWREF:'))
    r=dict(v.split(':',1) for v in ref[1:-1].split('|'))
    anchor_z=float(r['H5FOLLOWREF'].split(':')[1]);anchor_counts=float(r['COUNTS']);pitch=float(r['PITCH'])
    sync=b.fields('$P4SYNC','[P4SYNC:');density=float(sync['STEPS_MM']);lead=float(sync['LEAD_MM'])
    # During reversal, low-speed following can issue a few steps before RPM
    # crosses the powered threshold. The trace covers only the subsequent G33.
    # Its endpoint was checked by wait_bound; derive its actual start from the
    # independently counted Z steps, instead of assuming it starts at the bound.
    z_steps=int(sync['AXIS_STEPS'].split(',')[1])
    z_start=direction*2-direction*z_steps/200
    trace=b.command('$P4SYNCTRACE');errors=[]
    for s in trace:
        if not s.startswith('[P4SYNCPOINT:'):continue
        steps,counts,us=map(int,s[len('[P4SYNCPOINT:'):-1].split(','))
        distance=steps/density
        if distance<lead+.05 or distance>abs(2*direction-z_start)-.2:continue
        # These fixtures make Z the dominant trace axis, including the cone.
        z=z_start+direction*steps/200
        delta=(direction*counts-anchor_counts)/1200*pitch-(z-anchor_z)
        errors.append(abs(delta-round(delta/pitch)*pitch))
    assert errors and max(errors)<.012,(sync,errors)
    print(f'PASS FOLLOW PHASE: direction {direction}, peak {max(errors):.6f} mm',flush=True)

def stop():
    b.port.write(b'!');b.receive(lambda s:s.startswith('GrblHAL '),10);time.sleep(.2);b.idle();b.diagnostics()
try:
    for mode in (0,1,2):
        b.command('G21G18G8G90G94G53G0X0Z0');b.idle()
        b.command('$P4SIM=300' if mode!=2 else '$P4SIM=OFF');time.sleep(.3)
        b.history=[]
        b.command(f'$P4FOLLOW={mode},0.1,0.2,0,-1,1,-2,2')
        wait_bound(.2 if mode==1 else 0,2)
        if mode!=2:
            phase_check(0,1)
            b.command('$P4SIM=0');time.sleep(.2)
            b.command('$P4SIM=-300')
            wait_bound(-.2 if mode==1 else 0,-2)
            phase_check(2,-1)
            b.command('$P4SIM=300')
            time.sleep(.7)
            b.command('$P4SIM=0');time.sleep(.3)
            s,p=status();assert s.startswith('<Idle|'),s
            time.sleep(.2);assert status()[1]==p
            b.command('$P4SIM=300');b.receive(lambda s:'STAGE:Following' in s,10)
            time.sleep(.4)
        # A held X jog pauses automatic movement until release.
        b.command('$P4MANUAL=X,1,0.1,1')
        b.receive(lambda s:'STAGE:Manual override' in s,10)
        time.sleep(.5);_,p=status();time.sleep(.3);_,q=status();assert p==q,(p,q)
        b.command('$P4RELEASE')
        if mode!=2: b.receive(lambda s:'STAGE:Following' in s,10)
        stop()
        print(f'PASS FOLLOW: mode {mode}, bounds, manual pause/release, STOP'+(', spindle stop/reversal' if mode!=2 else ''),flush=True)
    b.command('$P4SIM=OFF')
except BaseException:
    b.fault_diagnostics()
    raise
finally:b.close()
