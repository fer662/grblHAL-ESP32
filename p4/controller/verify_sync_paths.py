#!/usr/bin/env python3
"""Verify spindle-synchronized X and X/Z paths on the disconnected P4."""
import math
import re
import sys
import time
from bench_client import Bench
b=Bench(sys.argv[1])
try:
    b.diagnostics(); b.command('G21G18G8G90G94')
    # Both dominant pulse axes, mixed directions, reverse spindle, RPM ramps.
    for x,z,pitch,rpm,target in [(3,0,.1,300,360),(-3,0,.1,300,240),
        (1,10,.5,300,360),(2,5,.2,300,360),(-1,10,.5,-300,-360),(-2,-5,.2,300,240)]:
        b.command('G53G0X0Z0');b.idle();b.command(f'$P4SIM={rpm}');time.sleep(.3)
        b.command('M3S300' if rpm>0 else 'M4S300');b.command('$P4PHASE=0')
        before=b.diagnostics();b.command(f'$P4SIMRAMP={target},50,900')
        b.command(f'G33X{x}Z{z}K{pitch}')
        b.idle()  # Parser acknowledgment alone does not prove motion completion.
        d=b.fields('$P4SYNC','[P4SYNC:');assert d['FAULT']=='NONE',d
        expected=(round(abs(x)*1200),round(abs(z)*200))
        assert tuple(map(int,d['AXIS_STEPS'].split(',')))==expected,d
        assert int(d['PULSES'])==max(expected)
        length=math.hypot(x,z); density=max(expected)/length
        assert abs(float(d['STEPS_MM'])-density)<.001
        points=[tuple(map(int,m)) for m in re.findall(r'P4SYNCPOINT:(\d+),(-?\d+),(\d+)', '\n'.join(b.command('$P4SYNCTRACE')))]
        errors=[enc*pitch/1200-step/density for step,enc,_ in points if float(d['LEAD_MM'])+.05<=step/density<=length-.5]
        assert len(errors)>=20,(d,len(errors))
        origin=round(errors[0]/pitch)*pitch
        peak=max(abs(e-origin) for e in errors)
        assert peak<=.01+2/density,(x,z,peak)
        after=b.diagnostics()
        for axis,pulses in zip(('X','Z'),expected):
            assert int(after[axis].split(',')[0])-int(before[axis].split(',')[0])==pulses
        print(f'PASS SYNC PATH: X{x} Z{z}, K{pitch}, RPM{rpm}->{target}, dominant {d["TRACE_AXIS"]}, peak phase {peak:.6f}mm',flush=True)
    b.command('$P4SIM=300');time.sleep(.3);b.command('M3S300')
    b.command('G33X20K1',error=43) # X speed limit, even though Z could do it
    b.command('')
    b.command('$P4SIM=OFF');b.diagnostics()
except BaseException:
    b.fault_diagnostics()
    raise
finally:b.close()
