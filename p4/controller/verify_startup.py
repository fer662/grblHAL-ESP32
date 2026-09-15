#!/usr/bin/env python3
"""Prove startup rejects motion until optional peripheral initialization finishes."""
import serial,sys,time
from bench_client import Bench
class StartingBench(Bench):
    def __init__(self,path):
        self.history=[];self.port=serial.Serial();self.port.port=path
        self.port.baudrate=115200;self.port.timeout=.05;self.port.write_timeout=2
        self.port.dtr=self.port.rts=False;self.port.open()
        time.sleep(2.7);self.port.reset_input_buffer()
        assert any('H5_P4_BENCH_MOTOR_ENABLES_LOCKED' in s for s in self.command('$I'))
b=StartingBench(sys.argv[1])
try:
    ui=b.fields('$P4UI','[P4UI:')
    assert ui['P4UI']=='READY:0','Missed the startup window; test did not exercise the gate'
    b.command('G91G94G1X1F60',error=8);b.command('')
    b.command('$J=G91X1F60',error=8);b.command('')
    d=b.diagnostics();assert d['X']=='0,0,0' and d['Z']=='0,0,0',d
    end=time.monotonic()+30
    while b.fields('$P4UI','[P4UI:')['P4UI']!='READY:1':
        assert time.monotonic()<end
        time.sleep(.1)
    b.command('G91G94G1X0.01F60');b.idle()
    d=b.diagnostics();assert d['X']=='12,12,12',d
    print('PASS: G-code and jog rejected during initialization; same session moves normally after readiness',flush=True)
finally:b.close()
