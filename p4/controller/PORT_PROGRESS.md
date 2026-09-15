# Port execution status

Updated 2026-09-15. Original committed H5 baseline: `981851b2`.
The original H5 checkout and its unrelated dirty experiments remain unchanged.
This is a disconnected Waveshare P4 bench build with X/Z enables hardlocked off.

## Implemented

- [x] Native grblHAL motion, accelerated jog, hold/resume, cancellation and reset.
- [x] X/Z path-specific spindle rate and acceleration limits.
- [x] Turn/Thread with depths, starts, spindle registration, preview and cancellation.
- [x] Face and progressive Cut profiles.
- [x] Ellipse geometry with native lookahead and spindle-progress feed.
- [x] Gearbox/Cone stop, reversal, reengagement and manual override.
- [x] Async manual override/resume; serialized edits to armed feed parameters.
- [x] Finite touch-jog repeat while held during all three assisted-feed modes.
- [x] Deliberate depth advance at a completed-depth boundary, retaining final depth.
- [x] TMC5160 SPI configuration and missing-device/readback diagnostics.
- [x] Separate persistent grbl/UI/Wi-Fi settings and asynchronous speaker service.
- [x] Hosted Wi-Fi, authenticated dual-slot OTA, boot confirmation and rollback.
- [x] Full flash backup and migration preserving original H5 NVS/storage addresses.

See [OPERATIONS.md](OPERATIONS.md), [SPINDLE_TRACKING.md](SPINDLE_TRACKING.md)
and [OTA.md](OTA.md) for behavior, configuration, limits and recovery.
Gearbox/Cone have a 30 RPM engagement floor in the current supported bench
range; lower-speed/hand-spindle behavior is not yet implemented or validated.
No operation is removed, but this limit must not be mistaken for full original
behavior at arbitrary hand-rotation speed.

## Regression evidence

Device logs and private backups are outside Git. Results during implementation:

- Final motion suite: exact 4,632 X / 47,745 Z pulses, including cancellation/restart and
  40,000-pulse counter rollover; no fault, overlap, deadline or receive errors.
- Spindle tracking: all 13 ramp/phase cases, exact 104,000 Z pulses, peak
  cutting-window phase error 0.0075 mm; no timing faults and continuing UI updates.
- UI: actual LVGL operation START/RUN/STOP, manual press/release and held finite
  increments, feed edits,
  depth skip and update-panel motion ownership passed.
- Face/Cut/Ellipse: all six directional recipes and queued-chord cancellation passed.
- Turn/Thread: repeated depths/starts, signs, phase, cancellations and RPM ceiling passed.
- Gearbox/Cone/Async: bounds, stop/reverse, retained phase and manual override passed.
- OTA: successful alternate-slot boot and confirmation in both slots; deliberate
  new-image rejection booted the previous image. Incorrect authentication,
  incomplete images and incorrect digests were rejected without selecting them.
- Host geometry tests and Python syntax checks passed.

- X/taper: six paths passed; exact 26,400 X / 11,000 Z pulses and peak phase
  error 0.002842 mm in their cutting windows.
- Startup: both G-code and jog were rejected before peripheral initialization
  completed; the same session moved normally afterward. Five consecutive cold
  reversal tests then passed with no incidental deadline miss.
- Fault tests: spindle stall/reversal and deliberately masked interrupts stopped
  STEP output and latched the expected fault. Deadline checks were not relaxed.
- Persistence: a temporary axis-rate setting and UI pitch/pass settings survived
  reboot. The original rate and normal bench UI defaults were restored and
  verified through another reboot.
- OTA ownership: rejected authentication, truncated and corrupt images retained
  the selected app. Closing during authentication prevented later erase; closing
  after upload acceptance kept motion locked until the transfer ended.
- USB/UI: three reconnects, two screen captures and a continuous connection passed.
- Combined services load: exact 19,200 X / 51,200 Z pulses with repeated sound,
  continuing UI updates and 90 Wi-Fi connections; zero timing/receive faults.
- Final image: deliberate rejected boot returned to `ota_1`; the same image then
  installed into `ota_0`, booted, passed readiness checks and confirmed. `ota_0`
  remains selected with no pending validation.

Final normal motion callback maximum was 54 us; the combined-load run measured
73 us. The final 13-case spindle run measured a 257 us maximum across the entire
callback, with zero deadline faults; its pulse-high service range was 15.3–71.4 us.
These aggregate maxima include different motion phases and rates. They are not
worst-case timing guarantees, and no claim is made that every pulse is 10 us.

Application version: `0.3.0`. Built image SHA-256:
`346fc8c39bfa5aa6cb8935233cfd91417ac5628593090b9f65fadabb1ba8ff34`.
Core pin: `f799fd0f284c25d592821f800452cba6fc1ea78a`.

## Hardware acceptance still required

- [ ] Inspect external STEP/DIR pulse widths, jitter, skew and setup/hold at the connector.
- [ ] Verify the actual geared spindle encoder, directions, pulse counts and phase.
- [ ] Connect the TMC5160 with outputs disabled and verify actual register/current settings.
- [ ] Confirm physical speaker output and touch behavior with an operator.
- [ ] Establish machine origin, travel, clearance and stop behavior with the actual drives.
- [ ] Validate loaded acceleration, reversal, thread entry/exit and all eight operations.
- [ ] Investigate the breakout/tablet battery-dependent power behavior after the refactor.

No software test can mark these hardware checks complete. Internal pulse counters
observe MCU GPIO only; the current TMC5160 report correctly says the device is
missing. The internal pulse-service measurements above are not constant
10 us pulses or an externally measured jitter bound. Motor enables remain
locked; the compile-time gate cannot be bypassed with a command or setting.
