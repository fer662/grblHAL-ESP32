#!/usr/bin/env python3
"""Finite touchscreen jogging repeats while held and pauses assisted feed."""
import re,sys,time
from bench_client import Bench
b=Bench(sys.argv[1])
def position():
    b.port.write(b'?');s=b.receive(lambda s:s.startswith('<'),2)[-1]
    return list(map(float,re.search(r'MPos:([^|]+)',s).group(1).split(',')))
try:
    b.command('$P4SIM=300');time.sleep(.3)
    for action in 'GKA':
        b.command('G21G18G8G90G94G53G0X0Z0');b.idle()
        b.command('$P4UITEST='+action);b.receive(lambda s:'STAGE:Following' in s,10)
        time.sleep(.3);b.command('$P4UITEST=N');b.command('$P4UITEST=1')
        b.receive(lambda s:'STAGE:Manual override' in s,10)
        time.sleep(.2);first=position();time.sleep(.65);last=position()
        assert last[0]-first[0]>=.02,(action,first,last)
        assert abs(last[2]-first[2])<.001,(action,first,last)
        b.command('$P4UITEST=0');b.receive(lambda s:'STAGE:Following' in s,10)
        time.sleep(.3);b.command('$P4UITEST=8')
        b.receive(lambda s:s.startswith('GrblHAL '),10);time.sleep(.2);b.idle();b.diagnostics()
        print('PASS: '+action+' finite jog repeats while held; automatic feed pauses until release',flush=True)
    b.command('$P4SIM=OFF');b.command('$P4UITEST=R');time.sleep(3)
finally:b.close()
