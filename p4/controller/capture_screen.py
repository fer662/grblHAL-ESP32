#!/usr/bin/env python3
"""Capture the idle tablet's rendered LVGL screen over USB as PNG.

Resets the grbl session before capture. No motion or flash commands are sent.
Uses only pyserial and the Python standard library.
"""
import argparse
import re
import struct
import time
import zlib
from pathlib import Path
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('port')
parser.add_argument('output', type=Path)
args = parser.parse_args()
port = serial.Serial()
port.port, port.baudrate, port.timeout = args.port, 115200, .1
port.dtr = port.rts = False


def lines_until(predicate, timeout=15):
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        line = port.readline().decode('ascii', 'replace').strip()
        if not line:
            continue
        if 'panic' in line.lower() or 'Guru Meditation' in line or line.startswith('error:'):
            raise RuntimeError(line)
        lines.append(line)
        if predicate(line):
            return lines
    raise TimeoutError('No complete response from tablet')


def png_chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


try:
    port.open()
    time.sleep(2.5)  # Allow a possible USB-bridge hardware reset to finish.
    port.reset_input_buffer()
    port.write(b'\x18')
    lines_until(lambda s: 'GrblHAL' in s)
    deadline = time.monotonic() + 10
    while True:
        port.write(b'$P4UI\n')
        if any('P4UI:READY:1' in s for s in lines_until(lambda s: s == 'ok')):
            break
        if time.monotonic() > deadline:
            raise TimeoutError('UI did not initialize')
        time.sleep(.1)
    port.write(b'$P4SCREEN\n')
    header = lines_until(lambda s: s.startswith('[P4SCREEN:'))[-1]
    match = re.fullmatch(r'\[P4SCREEN:(\d+),(\d+)\|RGB565_RLE\]', header)
    assert match, header
    width, height = map(int, match.groups())
    assert 0 < width <= 2048 and 0 < height <= 2048
    encoded = lines_until(lambda s: s == '[P4SCREEN:END]', timeout=60)
    lines_until(lambda s: s == 'ok')
    rgb = bytearray()
    for line in encoded[:-1]:
        assert re.fullmatch(r'(?:[0-9a-f]{8})+', line), line
        for offset in range(0, len(line), 8):
            count, color = int(line[offset:offset + 4], 16), int(line[offset + 4:offset + 8], 16)
            assert count and len(rgb) + count * 3 <= width * height * 3
            rgb.extend(bytes((((color >> 11) & 31) * 255 // 31,
                              ((color >> 5) & 63) * 255 // 63,
                              (color & 31) * 255 // 31)) * count)
    assert len(rgb) == width * height * 3
    scanlines = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    png = b'\x89PNG\r\n\x1a\n'
    png += png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += png_chunk(b'IDAT', zlib.compress(scanlines)) + png_chunk(b'IEND', b'')
    args.output.write_bytes(png)
    print(f'{width}x{height} screen saved to {args.output}')
finally:
    port.close()
