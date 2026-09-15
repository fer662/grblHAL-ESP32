#!/usr/bin/env python3
"""Bench OTA failure handling and motion ownership; does not select a new app."""
import hashlib,hmac,socket,struct,sys,time
from pathlib import Path
from bench_client import Bench
from ota_upload import upload
b=Bench(sys.argv[1]);image=Path(sys.argv[2]);data=image.read_bytes()
def status():return b.fields('$P4OTA','[P4OTA:')
def enable():
    b.command('$P4OTA=1')
    for _ in range(60):
        d=status()
        if d['P4OTA']=='ACTIVE:1' and d['WIFI']=='1':return d['IP'],bytes.fromhex(d['KEY'])
        time.sleep(.5)
    raise TimeoutError('Wi-Fi/update readiness')
def handshake(host,key):
    s=socket.create_connection((host,3232),10);s.settimeout(10)
    f=s.makefile('rb',buffering=0);greeting=f.readline().decode().strip()
    assert greeting.startswith('H5OTA1 '),greeting
    header=struct.pack('!I',len(data))+hashlib.sha256(data).digest()
    return s,f,header+hmac.digest(key,bytes.fromhex(greeting.split()[1])+header,'sha256')
try:
    original=status()['PARTITION']
    host,key=enable()
    for case in ('wrong_auth','truncate','bad_digest'):
        upload(host,key,image,**{case:True});time.sleep(1)
        d=status();assert d['PARTITION']==original and d['PENDING_VERIFY']=='0',d
    # Header receipt was already waiting when the UI closed. Authentication
    # alone must not permit a later erase after the motion owner was released.
    s,f,header=handshake(host,key)
    b.command('$P4OTA=0');assert status()['P4OTA']=='ACTIVE:0'
    s.sendall(header);reply=f.readline().decode().strip()
    assert reply in ('DISABLED',''),reply
    f.close();s.close()
    print('PASS: closing during authentication prevents subsequent flash erase',flush=True)
    host,key=enable();s,f,header=handshake(host,key)
    s.sendall(header);assert f.readline()==b'READY\n'
    b.command('$P4OTA=0');assert status()['P4OTA']=='ACTIVE:1'
    b.command('G1X1F60',error=8)
    s.sendall(data[:8192]);f.close();s.close();time.sleep(1)
    b.command('$P4OTA=0');assert status()['P4OTA']=='ACTIVE:0'
    b.command('G91G94G1X0.01F60');b.idle();b.diagnostics()
    assert status()['PARTITION']==original
    print('PASS: accepted upload retains motion lock until transfer terminates; original app retained',flush=True)
finally:
    b.command('$P4OTA=0');b.close()
