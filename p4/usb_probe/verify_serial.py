#!/usr/bin/env python3
"""Verify the installed USB probe; never send commands to an unknown firmware.

Requires pyserial (included in the ESP-IDF Python environment).
Motor/spindle power must be off before opening the port.
"""
import argparse
import time

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("port")
args = parser.parse_args()

port = serial.Serial()
port.port = args.port
port.baudrate = 115200
port.timeout = 0.2
port.write_timeout = 2
port.dtr = False
port.rts = False


def receive_until(marker, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line:
            print(line, flush=True)
        if marker in line:
            return line
    raise RuntimeError(f"Timed out waiting for {marker!r}; no further commands sent")


try:
    port.open()
    receive_until("motion=DISABLED grblhal=NOT_STARTED")
    for _ in range(3):
        port.write(b"PING\n")
        receive_until("PONG H5_P4_USB_PROBE", seconds=3)
    port.write(b"INFO\n")
    receive_until("H5_P4_USB_PROBE version=0.1.0")
    print("PASS: probe identified; three round-trip PINGs and status received.")
finally:
    port.close()
