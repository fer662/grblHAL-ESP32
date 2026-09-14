# ESP32-P4 bring-up

This fork tracks `grblHAL/ESP32`. The upstream core remains pinned at the
driver's original `main/grbl` submodule revision. H5 touchscreen/application
integration will remain outside the motion core.

## USB probe

`usb_probe` is an independent ESP-IDF 5.5.2 application for the Waveshare
ESP32-P4-WIFI6-Touch-LCD-10.1. It proves P4 compilation, USB flashing, boot and
two-way UART communication. It does **not** run grblHAL or validate motion,
spindle synchronization, the touchscreen, Wi-Fi or OTA.

The probe drives the existing H5 X/Z enable pins inactive, leaves STEP/DIR
as inputs, never emits motion pulses, and never writes NVS or storage.
Disconnect spindle/motor power before reset or flashing: application pin
configuration does not control the ROM/bootloader interval.

Build from `p4/usb_probe` after sourcing ESP-IDF 5.5.2's `export.sh`:

```sh
idf.py set-target esp32p4
idf.py build
```

Before installing, identify the chip and flash size, back up the device, and
compare the installed partition table with `partitions.csv`. For a compatible
existing H5 bootloader/table, flash **only** the probe application at `0x10000`;
do not run an unrestricted erase or replace the partition table. Keep the
backup outside this repository: it can contain credentials and calibration.
Restore the original application bytes after the test.

UART console is 115200 baud. `PING` returns `PONG H5_P4_USB_PROBE`; `INFO`
returns chip revision, flash size, reset reason and uptime. A heartbeat also
reports every two seconds. The display is not initialized by this probe.

After installing, run `python verify_serial.py /dev/cu.YOUR_PORT` from the
probe directory using the IDF Python environment. The client waits for the
probe's identification before sending three PINGs and an INFO request.

## Initial host validation (2026-09-14)

- Built successfully with the existing local ESP-IDF 5.5.2 environment
  (reports `v5.5.2-dirty`; the SDK has pre-existing local changes).
- App image: 227,552 bytes; image checksum and validation hash accepted by
  esptool 4.12.dev1.
- Generated partition table matches the existing H5 build byte for byte.
- USB inventory detects WCH `1A86:55D3`, serial `5AE7067474`.
- Device identity, backup, flash, runtime console checks and restoration are
  pending confirmation that spindle/motor power is off. Enumeration identifies
  the USB bridge, not the ESP chip behind it.

Next gates: real grblHAL P4 HAL, measured STEP timing, spindle feedback,
H5 application integration, and dual-slot OTA/rollback provisioning.
