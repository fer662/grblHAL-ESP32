# H5 grblHAL ESP32-P4 port

This is a separate ESP-IDF application using a pinned `main/grbl` core submodule.
The core fork contains one isolated spindle-segment timing fix, described in
[spindle validation](SPINDLE_VALIDATION.md). The original ESP32/S3 driver source
remains untouched; its build has not been revalidated against the core fix.
The P4 HAL uses ESP-IDF GPTimer, GPIO, UART and PCNT APIs.

## Current scope

- Real grblHAL G-code parser, planner, Bresenham/AMASS step generation,
  acceleration profiles, feed hold/resume, jog cancellation and reset handling.
- X/Z STEP/DIR on the existing H5 pins; calibration 1200 / 200 steps per mm,
  max rates 60 / 960 mm/min, accelerations 25 / 50 mm/s², X radial coordinates.
- grblHAL retains XYZ storage; Y commands and arcs outside G18 are rejected.
- 10 MHz GPTimer scheduler and separate pulse timer. Minimum requested STEP
  width 10 us by default, direction setup at least 5 us. GPIO writes span two
  banks and are sequential; external edge skew has not been measured.
- Timer callbacks and core step ISR are in IRAM. Cache-disabled operation is
  **not claimed**: flash writing and OTA are absent from the bench app.
- Two hardware PCNT units independently count GPIO STEP edges via the internal
  input path. This checks MCU outputs, not ribbon wiring or motor motion.
- A third PCNT unit reads spindle A/B using the existing x2 decoding. Accumulator
  watchpoints replace task-level read/clear. Effective calibration stays at
  1200 counts/revolution. No physical index signal is assumed.
- Bench-only straight Z `G33` synchronization, encoder-derived revolution phase,
  repeated/multi-start phase diagnostics, and spindle stall/reversal fault stops.
  A P4 assembly boundary preserves the interrupted task's floating-point state;
  a 1,000-interrupt canary runs at boot and is available as `$P4FPUTEST`.
  G76 and X/tapered synchronized moves remain rejected pending validation.
- UART0 via the tablet's USB bridge. A single grbl task owns command input;
  realtime commands are handled while the planner is busy. UART and software
  buffer errors cancel the command stream rather than executing truncated input.
- Bench-only compile gate forces X enable HIGH and Z enable LOW. No command
  or setting can energize drivers. Only use with the tablet disconnected.
- Settings live in the core's RAM buffer. Existing H5 NVS/storage is untouched.
- H5 touchscreen widgets imported from the committed H5 baseline, LVGL 8.3.11,
  Waveshare MIPI display and GT911 touch. UI and LVGL run on core 0; grblHAL and
  its timer interrupts run on core 1.
- Queued UI commands enter the normal grbl parser at USB line boundaries.
  Status is copied into a coherent snapshot; callbacks never run motion code.
  Jog cancellation bypasses the queue and invalidates pending jog requests.
  Stream reset also discards pending UI requests and clears held gestures.
- X/Z readouts, zero, numeric/continuous/fine jog, machining stops, units,
  original pitch picker, eight operation tabs and cycle parameter controls.
  Async Z feed uses grbl's accelerated jog path. Other START buttons explicitly
  report that spindle-synchronized operations are pending.
- SDK application logs are suppressed on UART0 to protect protocol responses.
  Core diagnostics remain available through the commands below.

**Not ready to run the lathe yet:** synchronization has synthetic bench coverage,
but speed-change tuning, phase/lead-in compensation and threading recipes are
unfinished. TMC5160 SPI initialization, assisted cutting recipes, sound,
persistent settings, Wi-Fi and dual-slot OTA/rollback are not enabled.
All eight H5 operations remain migration requirements; none is being removed.
The disabled enables are intentional even though STEP/DIR are real outputs.

## Build

Initialize the pinned core with `git submodule update --init main/grbl` and use
ESP-IDF 5.5.2 with an ESP32-P4 toolchain:

```sh
cd p4/controller
idf.py set-target esp32p4
idf.py build
```

Core configuration belongs to `main/machine.h`; P4 hardware code belongs here.
Do not edit the core's configuration headers. The core C file list is explicit
in `main/CMakeLists.txt`, based on the upstream ESP32 driver's list.

## Bench installation and recovery

First save a full 32 MB device backup outside Git, hash it, and compare the
actual partition table against this app's generated table. Keep the existing
bootloader and table. Only write `build/h5_grblhal_p4.bin` at `0x10000`.
The factory partition is 8 MB. Do not use unrestricted `idf.py flash` on H5:
that command also writes the bootloader/table.

Save the original bytes for the app's entire sector-rounded overwrite window,
including the tail sector. Keep the new application installed while continuing
the port. Recovery is optional: restore that window and verify all 32 MB against
the pre-test backup with `esptool verify_flash` to return to old H5. If successive
test images differ in size, the recovery window must cover the largest erase.
Backups may contain credentials and must never be committed.

Run `python verify_motion.py /dev/cu.YOUR_PORT` with the IDF Python environment.
The script identifies the bench image before sending motion commands. All
waits have deadlines. A failed test does not restore firmware automatically;
the installing operator must run the recovery procedure above.

## Diagnostics

`$P4` returns:

- `X` / `Z`: issued pulse count, signed issued position, PCNT-observed edge count.
- `ENC`: signed spindle PCNT count; no reset is performed by a read.
- `ISR`, `OVERLAP`, `LATE`, `FAULT`: timing and fault diagnostics.
- `PERIOD`: minimum/maximum requested interrupt period in 0.1 us ticks.
- `PULSE`: minimum/maximum internally observed pulse service duration in 0.1 us
  ticks; these timestamps are not a substitute for a scope/logic analyzer.
- `ISR_US`: longest measured core step callback in microseconds.
- `RX_OVF`: UART/ring errors (must stay zero for a valid run).

`$P4TRACE=RESET` clears timing traces only while idle. `$P4TRACE` reports the
first and last 16 edge intervals plus the minimum for each axis, in 0.1 us
ticks. Pulse counters are not cleared, so the hardware comparison remains
valid across multiple moves and counter rollover.

A timing fault stops timer service, cancels pending pulses, keeps enables off,
and raises a motor fault alarm. A hardware reboot is required to clear this
bench fault; `$X` cannot bypass it. A normal feed hold/jog cancel uses grblHAL's
controlled stop, not this fault path.

### Touchscreen diagnostics

`$P4UI` reports display readiness, UI command/completion IDs, last command status
and stream generation, plus `UI_UPDATES` and `UPTIME` for liveness checks.
The isolated bench diagnostic `$P4UITEST=n` sends events to the actual LVGL jog
buttons: 1=X+, 2=X-, 3=Z+, 4=Z-, 0=release, 5=press lost (finger dragged off).
It exercises widget callbacks and the command queue, but does not simulate the
GT911 sensor or a physical tap.

`$P4SCREEN` captures the rendered LVGL screen while idle, using RGB565 run-length
encoding. `python capture_screen.py PORT screen.png` decodes it without Pillow.
The capture client resets the grbl session. A rendered screenshot verifies layout,
not physical panel color, touch alignment or an attached motor.

### Validation, 2026-09-14

The app was built with ESP-IDF 5.5.2, flashed via USB at 0x10000, and left installed
on the disconnected Waveshare tablet. The full `verify_motion.py` suite passed
with the display rendering throughout: touchscreen-handler jog/cancel in all four
directions, coordinated moves/reversal, X acceleration/deceleration trace, hold/
resume, 12 jog/cancel/reverse cycles, reset recovery, synthetic spindle counter
rollover and a 40,000-pulse Z move while serving a detailed USB report.
Issued and hardware-counted pulses matched: X 4,140; Z 46,602. All timing fault,
overlap, late-alarm and RX overflow counters stayed zero. Internal pulse service
was 15.3–19.9 us; longest core step callback was 16 us. These are software/PCNT
observations, not scope measurements or proof of machining accuracy.

After the initial successful suite, USB stopped returning data on subsequent connections,
including a ROM bootloader probe and explicit hardware-reset attempt. Screen
capture could not be verified at that point: the capture command was not reached.
The owner subsequently confirmed that the display and touch were responsive and
power-cycled the tablet, restoring USB access.

The reconnect investigation reproduced a client timing issue: opening the USB
port can reset the P4. Commands sent before its roughly two-second startup were
lost. Waiting 2.5 seconds before querying the application passed ten reconnects
and two full screen captures. A separate connection held open for 60 seconds
showed advancing UI update counts and controller uptime, with no reboot or stall.
This explains the reproduced command timeout; it does not conclusively explain
the earlier ROM bootloader connection failure.

Screen capture is now verified. Its initial lock check confused `ESP_OK` (zero)
with a Boolean failure and returned while holding the display lock. It now tests
the actual error code and releases the lock after taking a snapshot. A regression
check confirms UI updates continue after every capture. Jog buttons also cancel
on LVGL `PRESS_LOST`, so dragging out of a held button follows the release path.
The motion suite now sends both `RELEASED` and `PRESS_LOST` through the actual
LVGL button event dispatch, verifying cancellation in all four directions before
running the coordinated-motion, acceleration, hold/reset and rollover checks.
The final installed build passed that expanded suite: 4,680 X pulses and 47,324 Z
pulses matched hardware counts; fault/overlap/late/RX-overflow counters were zero.
Internal pulse service spanned 15.3–21.4 us and the longest core callback was 17 us.

Run `python verify_connection.py PORT --seconds 60` for reopen/snapshot checks
followed by a 60-second continuous-connection test. These are bounded bench
checks, not a long-duration or machine-load stability certification.

The spindle-enabled build subsequently passed the motion suite again, the
11-cut spindle suite, and independent stall, reversal and missed-deadline fault
tests. Detailed measurements and unresolved tuning/lead-in issues are recorded
in [SPINDLE_VALIDATION.md](SPINDLE_VALIDATION.md). Motor enables remain locked.

## Remaining port sequence

1. Finish synchronization acceptance: tune speed-change response across pitches,
   account for acceleration/phase lead-in, constrain correction acceleration,
   validate X/tapered synchronization, and measure the real geared encoder.
   Synthetic Z phase tracking and explicit P4 FPU context handling now work;
   see [measurements and remaining limits](SPINDLE_VALIDATION.md).
2. Implement all eight assisted-operation semantics in an application cycle
   service over grbl motion, preserving signed pitch, starts, pass progression,
   cone/ellipse geometry, machining stops and deliberate clearance moves.
3. Add verified TMC5160 SPI configuration, settings/preferences storage and sound.
4. Add hosted Wi-Fi and dual-slot OTA with rollback; prepare and test a backup-
   preserving partition migration. Flash writes must require motion stopped.
5. Verify external waveforms, encoder phase and actual drives before removing the
   bench enable lock; then validate the preserved operations on the machine.

## Upstream updates

The upstream remote is `https://github.com/grblHAL/ESP32.git`; keep fork commits
on `codex/p4-motion-driver`. Merge upstream driver changes, inspect any updated
core submodule revision, then rerun this build and device suite. Review HAL
version, settings version, core source inventory and ISR dependencies on every
core upgrade. Do not advance the core automatically from a moving branch.

The core submodule now points to `https://github.com/fer662/grblHAL-core`, branch
`codex/spindle-segment-time`, commit `44aad88e60ccebd47729252e401ff98245c5bf39`.
Its parent is upstream `516e5ad80757bd2eba86bff18feb613ca121dc16`. Keep the
single timing correction as a separate commit when merging/rebasing upstream.
If upstream incorporates the fix, drop the local commit after rerunning the
spindle regression. Configuration and H5 application code stay outside the core.

ESP-IDF reference: [P4 GPTimer API](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/peripherals/gptimer.html).
