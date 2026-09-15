#!/usr/bin/env python3
"""Install a bench app over Wi-Fi and verify boot confirmation or deliberate rollback."""
import argparse,time
from bench_client import Bench
from ota_upload import upload
p=argparse.ArgumentParser(description=__doc__);p.add_argument('port');p.add_argument('image');p.add_argument('--rollback',action='store_true');a=p.parse_args()
b=Bench(a.port)
try:
    before=b.fields('$P4OTA','[P4OTA:')['PARTITION']
    b.command('$P4OTATEST=REJECT' if a.rollback else '$P4OTATEST=CLEAR')
    b.command('$P4OTA=1')
    for _ in range(60):
        d=b.fields('$P4OTA','[P4OTA:')
        if d['P4OTA']=='ACTIVE:1' and d['WIFI']=='1':break
        time.sleep(.5)
    else:raise TimeoutError('Update/Wi-Fi readiness')
    b.history=[]
    upload(d['IP'],bytes.fromhex(d['KEY']),a.image)
    b.receive(lambda s:s.startswith('GrblHAL '),30)
    if a.rollback:b.receive(lambda s:s.startswith('GrblHAL '),30)
    end=time.monotonic()+30
    while time.monotonic()<end:
        d=b.fields('$P4OTA','[P4OTA:')
        if d['PENDING_VERIFY']=='0' and d['P4OTA']=='ACTIVE:0':break
        time.sleep(.5)
    else:raise TimeoutError('Boot confirmation')
    assert (d['PARTITION']==before)==a.rollback,(before,d)
    boots=[s for s in b.history if 'Loaded app from partition at offset' in s]
    assert len(boots)==(2 if a.rollback else 1),boots
    ready_deadline=time.monotonic()+30
    while not any('P4UI:READY:1' in s for s in b.command('$P4UI')):
        assert time.monotonic()<ready_deadline,'Peripheral initialization did not finish after reboot'
        time.sleep(.1)
    b.diagnostics()
    print(('PASS: deliberately rejected OTA boot returned to '+before) if a.rollback else ('PASS: alternate app '+d['PARTITION']+' booted and confirmed'),flush=True)
finally:b.close()
