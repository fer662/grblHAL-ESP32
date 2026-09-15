# ESP32-P4 bring-up

This fork tracks `grblHAL/ESP32`. See [the controller](controller/README.md) for
the current P4 motion/touchscreen application. The core is pinned to our
`fer662/grblHAL-core` fork with an isolated spindle-segment timing fix and
opt-in RPM feed-forward/acceleration phase compensation.
H5 touchscreen/application integration remains outside the motion core.

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

## Host and device validation (2026-09-14)

- Built successfully with the existing local ESP-IDF 5.5.2 environment
  (reports `v5.5.2-dirty`; the SDK has pre-existing local changes).
- App image: 230,160 bytes; image checksum and validation hash accepted by
  esptool 4.12.dev1.
- Tablet physically disconnected from the lathe, powered only by USB-C.
- WCH `1A86:55D3` USB/UART bridge; esptool identifies ESP32-P4 revision 1.3,
  40 MHz crystal, and 32 MB flash.
- Full 32 MB flash backup saved privately with a SHA-256 manifest before writing.
- Generated partition table matches both the existing H5 build and the actual
  tablet partition table byte for byte.
- Flashed only the probe app at `0x10000`; esptool verified the written hash.
  Existing bootloader successfully loaded and started the probe.
- Three PING/PONG round trips and an INFO response passed at 115200 baud.
  Status reports two cores, revision 103, 33,554,432 flash bytes, motion disabled,
  and grblHAL not started.
- Restored the original bytes for all erased sectors (`0x10000` through
  `0x48fff`, 233,472 bytes). A full 32 MB `esptool verify_flash` digest comparison
  against the pre-test backup passed before restarting H5. This also checks the
  preserved bootloader, partition table, settings and storage.
- The backed-up application identifies itself as `lvgl_demo_v9`, version
  `54db9b2-dirty`, built February 24, 2026. Restoration used the actual device
  backup, not a rebuild of the current H5 checkout.
- After restoration, a 20-second serial capture confirmed that H5 booted,
  initialized the LCD and GT911 touch controller, and started the LVGL task.
  No crash or reboot was observed in that interval. Three GT911 I2C read errors
  appeared around 11 seconds; their cause is uninvestigated. Display and touch
  initialization in the log is not a visual or touch-interaction test.

Two bring-up fixes are included: only request valid P4 LDO channel 4 (the legacy
H5 channel-5 request is invalid), and explicitly configure UART0 as 115200 8N1
with flow control disabled, then flush its receive buffer. The initial probe
booted and transmitted status but failed command reception before the UART fix.

Next gates: real grblHAL P4 HAL, measured STEP timing, spindle feedback,
H5 application integration, and dual-slot OTA/rollback provisioning.
