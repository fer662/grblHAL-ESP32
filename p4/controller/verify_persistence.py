#!/usr/bin/env python3
"""Verify isolated settings across USB-triggered reboot; restore bench defaults."""
import sys,time
from bench_client import Bench
b=Bench(sys.argv[1]);original=None
try:
    original=float(next(s.split('=',1)[1] for s in b.command('$$') if s.startswith('$110=')))
    temporary=original-1 if original>1 else original+.1
    b.command(f'$110={temporary:.3f}')
    b.command('$P4UITEST=F');time.sleep(3)
    saved=b.fields('$P4STORE','[P4STORE:')
    assert saved['DIRTY']=='0' and saved['FAILURES']=='0' and saved['PITCH']=='500' and saved['PASSES']=='2',saved
    b.close();b=Bench(sys.argv[1])
    read=float(next(s.split('=',1)[1] for s in b.command('$$') if s.startswith('$110=')))
    assert abs(read-temporary)<.0001,(read,temporary)
    restored=b.fields('$P4STORE','[P4STORE:')
    assert restored['PITCH']=='500' and restored['PASSES']=='2',restored
    print('PASS: core setting and UI pitch/pass count survived hardware reboot',flush=True)
finally:
    if original is not None:
        b.command(f'$110={original:.3f}')
        b.command('$P4UITEST=R');time.sleep(3)
    b.close()
b=Bench(sys.argv[1])
try:
    read=float(next(s.split('=',1)[1] for s in b.command('$$') if s.startswith('$110=')))
    assert abs(read-original)<.0001
    restored=b.fields('$P4STORE','[P4STORE:')
    assert restored['PITCH']=='1000' and restored['PASSES']=='1' and restored['FAILURES']=='0',restored
    b.diagnostics()
    print('PASS: original axis rate and normal bench UI defaults restored and persisted',flush=True)
finally:b.close()
