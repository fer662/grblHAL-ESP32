#!/usr/bin/env python3
"""Exercise both OTA wire protocols against a local receiver; no device or flash access."""
import hashlib
import hmac
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from ota_upload import upload

class PairingTest(unittest.TestCase):
    def exchange(self,paired,*,missing=False,bad_auth=False,bad_digest=False):
        payload=bytes(range(256))*8
        key=bytes(range(16));nonce=bytes(range(32))
        failures=[];seen=[]
        with tempfile.TemporaryDirectory() as directory:
            image=Path(directory)/'firmware.bin';image.write_bytes(payload)
            with socket.socket() as listener:
                listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(3)
                def server():
                    try:
                        with listener.accept()[0] as peer:
                            peer.settimeout(3)
                            peer.sendall(b'H5OTA1 '+nonce.hex().encode()+b'\n' if paired else b'H5OTA0\n')
                            if missing:
                                self.assertEqual(peer.recv(1),b'');return
                            def receive(n):
                                result=b''
                                while len(result)<n:
                                    part=peer.recv(n-len(result))
                                    if not part:raise EOFError('truncated protocol')
                                    result+=part
                                return result
                            header=receive(68 if paired else 36)
                            self.assertEqual(struct.unpack('!I',header[:4])[0],len(payload))
                            if paired:
                                expected=hmac.digest(key,nonce+header[:36],'sha256')
                                if bad_auth:
                                    self.assertNotEqual(header[36:],expected);peer.sendall(b'AUTH\n');return
                                self.assertEqual(header[36:],expected)
                            peer.sendall(b'READY\n')
                            body=receive(len(payload));self.assertEqual(body,payload)
                            if bad_digest:
                                self.assertNotEqual(hashlib.sha256(body).digest(),header[4:36])
                                peer.sendall(b'INVALID:ESP_ERR_INVALID_CRC\n')
                            else:
                                self.assertEqual(hashlib.sha256(body).digest(),header[4:36]);peer.sendall(b'OK\n')
                            seen.append(body)
                    except BaseException as error:failures.append(error)
                worker=threading.Thread(target=server);worker.start()
                try:
                    if missing:
                        with self.assertRaisesRegex(RuntimeError,'requires a 32-character pairing key'):
                            upload('127.0.0.1',None,image,port=listener.getsockname()[1])
                    else:
                        upload('127.0.0.1',key if paired else None,image,port=listener.getsockname()[1],
                               wrong_auth=bad_auth,bad_digest=bad_digest)
                finally:
                    worker.join(4)
                self.assertFalse(worker.is_alive())
                if failures:raise failures[0]
                self.assertEqual(len(seen),0 if missing or bad_auth else 1)
    def test_existing_paired_protocol(self):self.exchange(True)
    def test_paired_requires_key_before_sending_image(self):self.exchange(True,missing=True)
    def test_paired_bad_auth(self):self.exchange(True,bad_auth=True)
    def test_lan_without_key(self):self.exchange(False)
    def test_lan_still_checks_digest(self):self.exchange(False,bad_digest=True)
    def test_paired_still_checks_digest(self):self.exchange(True,bad_digest=True)

if __name__=='__main__':unittest.main()
