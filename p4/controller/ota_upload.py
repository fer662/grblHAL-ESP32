#!/usr/bin/env python3
"""Authenticated OTA upload while the H5 touchscreen is in update mode.

Use --usb PORT to enter update mode and obtain pairing information locally,
or --host IP --key-file FILE for a wireless upload (key shown on touchscreen).
"""
import argparse,hashlib,hmac,json,socket,struct,time
from pathlib import Path

def pair_usb(port):
    import serial
    p=serial.Serial();p.port=port;p.baudrate=115200;p.timeout=.1;p.dtr=p.rts=False;p.open();time.sleep(3);p.reset_input_buffer()
    def command(text):
        p.write((text+'\n').encode());lines=[];deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            line=p.readline().decode(errors='replace').strip()
            if line=='ok':return lines
            if line.startswith(('error:','ALARM:')):raise RuntimeError(line)
            if line:lines.append(line)
        raise TimeoutError('USB response')
    try:
        command('$P4OTA=1')
        for _ in range(60):
            lines=command('$P4OTA')
            s=next(s for s in lines if s.startswith('[P4OTA:'))
            d=dict(field.split(':',1) for field in s[1:-1].split('|'))
            if d['P4OTA']=='ACTIVE:1' and d['WIFI']=='1' and len(d['KEY'])==32:return d['IP'],bytes.fromhex(d['KEY'])
            time.sleep(.5)
        raise RuntimeError('Wi-Fi or update mode unavailable')
    finally:p.close()

def upload(host,key,image,*,wrong_auth=False,truncate=False,bad_digest=False):
    data=Path(image).read_bytes();digest=hashlib.sha256(data).digest()
    if bad_digest:digest=b'\0'*32
    with socket.create_connection((host,3232),10) as s:
        s.settimeout(60)
        f=s.makefile('rb',buffering=0)
        greeting=f.readline().decode().strip()
        if not greeting.startswith('H5OTA1 '):raise RuntimeError(greeting)
        nonce=bytes.fromhex(greeting.split()[1]);header=struct.pack('!I',len(data))+digest
        mac=hmac.digest(key,nonce+header,'sha256')
        if wrong_auth:mac=b'\0'*32
        s.sendall(header+mac)
        response=f.readline().decode().strip()
        if wrong_auth:
            assert response=='AUTH',response;print('PASS: invalid authentication rejected before image erase');return
        if response!='READY':raise RuntimeError(response)
        if truncate:
            s.sendall(data[:8192]);print('Sent incomplete image; active slot must remain unchanged');return
        s.sendall(data)
        response=f.readline().decode().strip()
        if bad_digest:
            assert response.startswith('INVALID'),response;print('PASS: incorrect image digest rejected');return
        if response!='OK':raise RuntimeError(response)
        print('Firmware authenticated and verified; device restarting.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('image',type=Path);p.add_argument('--usb');p.add_argument('--host');p.add_argument('--key-file',type=Path)
    p.add_argument('--test',choices=['wrong-auth','truncate','bad-digest'])
    a=p.parse_args()
    if a.usb:host,key=pair_usb(a.usb)
    elif a.host and a.key_file:host,key=a.host,bytes.fromhex(a.key_file.read_text().strip())
    else:p.error('use --usb or --host and --key-file')
    upload(host,key,a.image,wrong_auth=a.test=='wrong-auth',truncate=a.test=='truncate',bad_digest=a.test=='bad-digest')
