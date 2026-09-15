"""Bounded serial helpers for the disconnected, enable-locked P4 bench."""
import re
import time
import serial

class Bench:
    def __init__(self, path):
        self.history=[]
        self.port=serial.Serial(); self.port.port=path; self.port.baudrate=115200
        self.port.timeout=.05; self.port.write_timeout=2; self.port.dtr=self.port.rts=False
        self.port.open(); time.sleep(3); self.port.reset_input_buffer()
        assert any('H5_P4_BENCH_MOTOR_ENABLES_LOCKED' in s for s in self.command('$I'))
        end=time.monotonic()+30
        while not any('P4UI:READY:1' in s for s in self.command('$P4UI')):
            assert time.monotonic()<end,'Peripheral initialization did not finish'
            time.sleep(.1)
    def receive(self, predicate, timeout=30, error=None):
        end=time.monotonic()+timeout; lines=[]
        while time.monotonic()<end:
            s=self.port.readline().decode(errors='replace').strip()
            if not s: continue
            print(re.sub(r'KEY:[^|]*', 'KEY:<withheld>', s),flush=True); lines.append(s);self.history.append(s)
            assert 'panic' not in s.lower() and 'Guru Meditation' not in s and not s.startswith('ALARM:'),s
            assert not s.startswith('error:') or s==f'error:{error}',s
            if predicate(s):return lines
        raise TimeoutError(lines[-5:])
    def command(self, text, error=None, timeout=60):
        print('>>> '+text,flush=True); self.port.write((text+'\n').encode())
        lines=self.receive(lambda s:s=='ok' or s.startswith('error:'),timeout,error)
        assert lines[-1]==('ok' if error is None else f'error:{error}'),lines
        return lines
    def fields(self, command, prefix):
        line=next(s for s in self.command(command) if s.startswith(prefix))
        return dict(v.split(':',1) for v in line[1:-1].split('|'))
    def diagnostics(self):
        d=self.fields('$P4','[P4:')
        assert d['EN']=='LOCKED' and d['ENABLE_PINS']=='1,0',d
        assert all(d[k]=='0' for k in ('FAULT','OVERLAP','LATE','RX_OVF')),d
        for axis in ('X','Z'):
            a,_,b=map(int,d[axis].split(','));assert a==b,d
        return d
    def idle(self):
        end=time.monotonic()+20
        while time.monotonic()<end:
            self.port.write(b'?')
            if self.receive(lambda s:s.startswith('<'),2)[-1].startswith('<Idle|'):return
            time.sleep(.05)
        raise TimeoutError('Idle')
    def close(self):
        self.port.write(b'\x18');self.port.close()
