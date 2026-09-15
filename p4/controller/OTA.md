# Wireless updates and settings

## Flash layout

This port preserves the original H5 NVS (`0x9000`), PHY (`0xf000`) and 7 MiB
storage partition (`0x810000`). Its former 8 MiB factory allocation is divided
into a 2 MiB recovery/factory app at `0x10000` and two 3 MiB OTA apps at
`0x210000` and `0x510000`. OTA metadata is at `0xf10000`; separate grblHAL/UI/Wi-Fi
settings occupy 64 KiB at `0xf12000`. Nothing auto-starts motion after boot.

All boot-critical partitions are below 16 MiB. During implementation, an image
written across that boundary matched USB flash readback exactly but failed the
SDK's mapped checksum verification. Moving the executable slots alone was not
enough: the bootloader also failed to select OTA metadata above the boundary.
See the corresponding [Espressif issue](https://github.com/espressif/esp-idf/issues/18051).
This layout avoids enabling experimental 32-bit cache access.

## First installation

Use ESP-IDF 5.5.2 and its Python environment. Save a full 32 MiB backup and
`SHA256SUMS` outside Git. `migrate_partitions.py PORT --backup DIRECTORY` verifies
that backup and the original layout, then installs the bootloader, partition
table and factory application. The migration intentionally replaces the old H5
application; it preserves the original data partitions. Do not run it again on
an already migrated device. Recovery to original H5 requires restoring the full
backup, including the original table/bootloader, rather than just the app window.

## Wi-Fi

`wifi_provision.py PORT PRIVATE_JSON` reads `SSID` and `PASSWORD` from a local
JSON file and sends them over USB without logging them. Keep that file out of
Git. Credentials are stored in the separate P4 settings partition. Hosted Wi-Fi
uses the tablet's ESP32-C6 over SDIO with the original Waveshare pins. The
application pins `esp_hosted` 2.11.6 and `esp_wifi_remote` 1.3.2. The existing C6
firmware was retained and successfully connected; this OTA protocol updates the
P4 application, not the C6 coprocessor. Check host/slave compatibility before
upgrading those dependencies.

## Routine updates

Open Firmware Update on the touchscreen while the controller is stopped. It
shows the IP address and a temporary pairing key. Save that key in a local file:

```sh
python ota_upload.py build/h5_grblhal_p4.bin --host DEVICE_IP --key-file PRIVATE_KEY_FILE
```

While USB is attached, the helper can obtain the pairing information locally:

```sh
python ota_upload.py build/h5_grblhal_p4.bin --usb USB_PORT
```

The device accepts a connection on TCP port 3232 only in update mode. A fresh
challenge and HMAC-SHA256 authenticate the image size and SHA256 digest before
flash erase. The complete received image must match that digest and pass IDF
image validation before changing the boot slot. This authenticates the firmware;
it does not encrypt the public firmware payload or provide secure boot against
physical flash access. Pairing keys expire when update mode closes or after five
minutes without an active upload. USB is a trusted local provisioning interface.

Motion commands and new operations are refused during updates. Entry waits for
an empty planner and completed STEP pulse. UI settings and grbl NVS writes also
wait for idle; they do not share an active motion/OTA flash-writing window.
Interrupted uploads leave the current boot slot selected. Failed validation
leaves it selected too. Closing the panel during an authenticated upload does
not interrupt the upload or release its motion lock. Authentication captures the
update session; accepting it and retaining motion ownership are serialized with
panel close and expiry. A handshake from a closed session cannot start erase.

A new OTA app starts with motion locked until driver initialization, the FPU ISR
canary, settings and touchscreen readiness pass. It confirms the app after five
seconds of successful readiness. Failure to become ready within 30 seconds
requests rollback. The bootloader also rolls back an unconfirmed app on reset.
`$P4OTATEST=REJECT` is an enable-locked bench test that deliberately rejects the
next OTA boot. `$P4OTATEST=CLEAR` cancels that test before uploading.

## Persistent state

The core's settings/coordinate blobs use its own CRC/versioned storage format.
UI preferences have a separate version: operation, units/pitch display, pitch,
step increment, passes, starts, cone ratio, infeed direction and sound. They
save after two seconds without another preference change and only while idle.
Current position, zero offsets, machining stops and an armed operation are not
restored automatically after power loss: this machine has no absolute axis
position feedback. Establish the position and bounds again before machining.

`$P4STORE`, `$P4OTA`, `$P4AUDIO` and `$P4TMC` provide diagnostics. `$P4OTA` reveals
a pairing key only through the local USB interface while update mode is active;
do not publish those responses or private settings partition dumps.


## Reproducible bench checks

- `verify_persistence.py PORT`: temporary setting/UI change, reboot, verification
  and restoration of the original axis rate and normal bench UI defaults.
- `verify_ota.py PORT IMAGE`: authentication/digest/truncation failures, close
  during authentication, and motion ownership during an accepted upload.
- `verify_ota_boot.py PORT IMAGE --rollback`: deliberate new-image rejection and
  actual return to the previous boot slot.
- `verify_ota_boot.py PORT IMAGE`: alternate-slot installation and boot confirmation.

Run only on the disconnected, enable-locked tablet. Boot checks keep USB open
across reboot so they verify which partitions actually execute, then wait for
peripheral readiness; upload acceptance alone is not treated as successful boot.

## Working at the lathe without USB

The touchscreen supplies the update IP and temporary pairing key. Live encoder,
timing and TMC diagnostics use a separate read-only service so observation does
not acquire update mode or stop operation. See
[wireless commissioning](WIRELESS_COMMISSIONING.md).
