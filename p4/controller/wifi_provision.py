#!/usr/bin/env python3
"""Provision Wi-Fi over local USB without printing credentials.
JSON input must contain SSID and PASSWORD. Keep it outside version control.
"""
import argparse,json,time,serial
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('port');p.add_argument('config',type=Path);a=p.parse_args()
c=json.loads(a.config.read_text());ssid=c['SSID'].encode();password=c['PASSWORD'].encode()
assert 1<=len(ssid)<=32 and len(password)<=63
port=serial.Serial();port.port=a.port;port.baudrate=115200;port.timeout=.1;port.dtr=port.rts=False;port.open();time.sleep(4);port.reset_input_buffer()
try:
 port.write(('$P4WIFI='+ssid.hex()+','+password.hex()+'\n').encode())
 deadline=time.monotonic()+15
 while time.monotonic()<deadline:
  line=port.readline().decode(errors='replace').strip()
  if line=='ok':print('Wi-Fi configuration saved.');break
  if line.startswith(('error:','ALARM:')):raise RuntimeError(line)
 else:raise TimeoutError('Provisioning response')
 for _ in range(90):
  port.write(b'$P4OTA\n');deadline=time.monotonic()+2;connected=False
  while time.monotonic()<deadline:
   line=port.readline().decode(errors='replace').strip()
   if line.startswith('[P4OTA:'):
    d=dict(v.split(':',1) for v in line[1:-1].split('|'));connected=d['WIFI']=='1'
    if connected:print('Tablet connected at '+d['IP'])
   if line=='ok':break
  if connected:break
  time.sleep(1)
 else:raise TimeoutError('Wi-Fi connection; verify network credentials and signal')
finally:port.close()
