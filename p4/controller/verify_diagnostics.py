#!/usr/bin/env python3
"""Device test of read-only HTTP diagnostics and its real touchscreen controls.

USB is used to automate local UI gestures for this bench test only. No simulator,
motion, register-write or fault-injection commands are sent.
"""
import json
import socket
import sys
import time
import urllib.error
from bench_client import Bench
from diagnostics_log import snapshot

b = Bench(sys.argv[1])
try:
    for _ in range(60):
        net = b.fields('$P4OTA', '[P4OTA:')
        if net['WIFI'] == '1':
            break
        time.sleep(.5)
    assert net['WIFI'] == '1', net
    host = net['IP']

    def unavailable():
        try:
            snapshot(host)
            raise AssertionError('Diagnostics exposed outside local session')
        except urllib.error.HTTPError as error:
            assert error.code == 403, error

    b.command('$P4DIAG=0')
    unavailable()
    b.command('$P4UITEST=V')
    time.sleep(2.5)
    first = snapshot(host)
    assert first['motor_enables_locked'] and first['enable_pins'] == [1, 0], first
    assert first['sample_age_ms'] < 1000 and first['ready'], first
    assert first['tmc']['sampled_ms'] > 0, first
    assert all(key not in json.dumps(first).lower() for key in ('password', 'pairing', 'ssid')), first
    assert first['simulated'] == 0, first

    def request(raw, status):
        with socket.create_connection((host, 8080), 3) as client:
            client.settimeout(3)
            client.sendall(raw)
            data = b''
            while True:
                try:
                    part = client.recv(4096)
                except ConnectionResetError:
                    break
                if not part:
                    break
                data += part
            assert data.startswith(f'HTTP/1.1 {status} '.encode()), data

    request(b'POST /diagnostics HTTP/1.1\r\nContent-Length: 8\r\n\r\nG1X10000', 405)
    request(b'GET /$P4SIM=60 HTTP/1.1\r\n\r\n', 404)
    request(b'GET /diagnostics?command=G1X1 HTTP/1.1\r\n\r\n', 404)
    request(b'GET /diagnostics HTTP/1.1\r\nX:' + b'a' * 600, 400)
    with socket.create_connection((host, 8080), 3) as client:
        client.settimeout(3)
        client.sendall(b'GET /diagnostics HTTP/1.1\r\n')
        time.sleep(.1)
        client.sendall(b'Host: tablet\r\n\r\n')
        assert client.recv(4096).startswith(b'HTTP/1.1 200 ')
    b.command('$P4UITEST=B')
    time.sleep(.5)
    after = snapshot(host)
    assert after['issued_steps_xz'] == first['issued_steps_xz'], (first, after)
    assert after['simulated'] == 0 and after['sample_age_ms'] < 1000, after
    b.command('$P4UITEST=V')
    b.command('$P4UITEST=W')
    time.sleep(.5)
    unavailable()
    b.command('$P4DIAG=1')
    time.sleep(.5)
    assert snapshot(host)['sample_age_ms'] < 1000
    with socket.create_connection((host, 8080), 3) as client:
        client.settimeout(3)
        client.sendall(b'GET /diagnostics HTTP/1.1\r\n')
        b.command('$P4DIAG=0')
        client.sendall(b'\r\n')
        assert client.recv(4096).startswith(b'HTTP/1.1 403 ')
    unavailable()
    b.diagnostics()
    print('PASS: local session controls, live cached HTTP diagnostics, read-only request rejection, no pulses generated')
finally:
    b.close()
