#!/usr/bin/env python3
"""Coordinated GPIO motion while touchscreen, audio and hosted Wi-Fi are busy."""
import socket,sys,threading,time
from bench_client import Bench
from diagnostics_log import snapshot
b=Bench(sys.argv[1]);done=threading.Event();connections=[];failures=[];worker=None
try:
    for _ in range(60):
        net=b.fields('$P4OTA','[P4OTA:')
        if net['WIFI']=='1':break
        time.sleep(.5)
    assert net['WIFI']=='1' and net['P4OTA']=='ACTIVE:0',net
    b.command('$P4DIAG=1')
    time.sleep(.5)
    def network_load():
        while not done.is_set():
            try:
                with socket.create_connection((net['IP'],3232),2) as s:
                    s.settimeout(2)
                    response=s.recv(32)
                    if response!=b'DISABLED\n':raise RuntimeError(response)
                    observation=snapshot(net['IP'],timeout=2)
                    assert observation['motor_enables_locked'] and observation['sample_age_ms']<1000,observation
                    assert observation['fault']==0 and observation['late']==0 and observation['overlap']==0,observation
                    connections.append(1)
            except Exception as error:failures.append(str(error))
            done.wait(.05)
    worker=threading.Thread(target=network_load);worker.start()
    before=b.diagnostics();ui=b.fields('$P4UI','[P4UI:')
    for i in range(8):
        sign=1 if i%2==0 else -1
        b.command(f'G21G18G8G91G94G1X{2*sign}Z{32*sign}F960')
        end=time.monotonic()+10
        while time.monotonic()<end:
            assert any('P4AUDIO:READY' in s for s in b.command('$P4AUDIO'))
            time.sleep(.12);b.port.write(b'?')
            if b.receive(lambda s:s.startswith('<'),2)[-1].startswith('<Idle|'):break
        else:raise TimeoutError('loaded move')
    after=b.diagnostics();ui_after=b.fields('$P4UI','[P4UI:')
    for axis,total in [('X',19200),('Z',51200)]:
        a=tuple(map(int,before[axis].split(',')));z=tuple(map(int,after[axis].split(',')))
        assert z[0]-a[0]==total and z[1]==a[1],(axis,a,z)
    assert int(ui_after['UI_UPDATES'])-int(ui['UI_UPDATES'])>100
    assert len(connections)>50 and not failures,(len(connections),failures)
    print(f'PASS: 19,200 X / 51,200 Z pulses exact with audio, UI updates and {len(connections)} Wi-Fi connections plus HTTP diagnostics',flush=True)
except BaseException:
    b.port.write(b'$P4\n$P4DEADLINE\n');end=time.monotonic()+2
    while time.monotonic()<end:
        line=b.port.readline().decode(errors='replace').strip()
        if line:print(line,flush=True)
    raise
finally:
    done.set()
    if worker:worker.join(timeout=3)
    b.close()
