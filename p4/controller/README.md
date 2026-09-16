# H5 grblHAL ESP32-P4 port

This is a separate ESP-IDF application using a pinned `main/grbl` core submodule.
The core fork keeps five small changes separate: spindle timing/acceleration,
configurable AMASS cutoff and a cancellation/completion race fix. See
[spindle tracking](SPINDLE_TRACKING.md), [hand following](HAND_FOLLOW.md) and the
upstream-update table below. The original ESP32/S3 driver source
remains untouched; its build has not been revalidated against these core changes.
The P4 HAL uses ESP-IDF GPTimer, GPIO, UART and PCNT APIs.

## Current scope

**0.3.14 keeps ordinary turning within its entered Z endpoints.** Turn now uses
G95 feed per revolution with acceleration/deceleration inside the cut; Thread's
G33 lead-in/run-out is unchanged. Non-thread profiles also omit the shared one-step
approach outside the cutting-axis bounds. Existing tool-clearance retracts remain.
See [operation semantics](OPERATIONS.md) and [cycle validation](ASSISTED_CYCLES.md).

**0.3.13 aligns both left-pane columns on 80-pixel rows with 16-pixel gaps.**
PITCH aligns with the third settings row; START and SHIFT retain their bottom
alignment with the right pane. See [current touchscreen previews](UI_ROADMAP.md).

**0.3.12 bottom-aligns START and SHIFT with the right pane** at y=740.
See [current touchscreen previews](UI_ROADMAP.md).

**0.3.11 enlarges all four jog buttons to 152 x 180 pixels** (67% more touch
area). The right pane uses the space up to the top edge; its bottom hint is
removed. Limits remain directionally aligned and corner controls fill the new
spaces. See [current touchscreen previews](UI_ROADMAP.md).

**0.3.10 adds JOG LIMITS ON/OFF and EDIT LIMITS in the right pane.** Manual
jogging can bypass the saved endpoints without clearing them; assisted operations
retain their bounds. The editor supports signed display coordinates, Use current,
Clear and X/Z spans, with Apply/Cancel. Bypass resets to ON at power-on.
See [controls and previews](UI_ROADMAP.md).

**0.3.9 aligns limit controls with their jog buttons and fills the STEP corner.**
The redundant JOG heading is removed; the center toggle is preserved. See
[current touchscreen previews](UI_ROADMAP.md).

**0.3.8 separates Hold and Single-step jogging.** A center toggle selects the
behavior; STEP has moved into the right pane and controls distance independently.
Hold cancels on release, while Single step completes one bounded increment without
auto-repeat. The selection persists. Active spindle-follow Z overrides retain
their whole-pitch rounding. See [jog controls and previews](UI_ROADMAP.md).

**0.3.7 places machining limits outside their corresponding jog arrows.**
The rest of the 0.3.6 layout is retained. See the updated
[Gearbox and Thread previews](UI_ROADMAP.md).

**0.3.6 enlarges the touchscreen layout for the 10.1-inch tablet.** Jog targets
are separated from each other and from limit controls; sliding off a jog requires
lifting before starting another direction. Position rows, cycle controls, menus
and the number pad are larger. All eight operations retain their existing
behavior. See [UI previews and proposed additions](UI_ROADMAP.md) and the
[desktop pointer regression](tests/ui_preview/README.md).

**0.3.4 adds `H5_OTA_REQUIRE_PAIRING` (default OFF for this LAN).** Set it ON at
build time to restore temporary-key authentication. Update mode, digest/image
validation and rollback are retained in either mode; see [OTA.md](OTA.md).

**0.3.3 enables the existing X/Z axis controls in the normal application.**
There is no separate arming or motor-test UI. The operator selected a first
Z-only movement test with the carriage disengaged from the leadscrew, ahead of
external waveform measurement. This does not establish loaded-machine acceptance.

Live diagnostics and OTA are available over Wi-Fi; USB is not required at the
lathe. See [wireless commissioning](WIRELESS_COMMISSIONING.md).

- Real grblHAL G-code parser, planner, Bresenham/AMASS step generation,
  acceleration profiles, feed hold/resume, jog cancellation and reset handling.
- X/Z STEP/DIR on the existing H5 pins; calibration 1200 / 200 steps per mm,
  max rates 60 / 960 mm/min, accelerations 25 / 50 mm/s², X radial coordinates.
- grblHAL retains XYZ storage; Y commands and arcs outside G18 are rejected.
- 10 MHz GPTimer scheduler and separate pulse timer. Minimum requested STEP
  width 10 us by default, direction setup at least 5 us. GPIO writes span two
  banks and are sequential; external edge skew has not been measured.
- The motion linker fragment places the stepper/PID/driver/spindle code and
  constants in internal RAM. Startup blocks motion until audio and hosted Wi-Fi
  initialization finishes; optional failures remain reported as unavailable.
  Application shared locks use short copies; status formatting happens outside
  interrupt-masked sections. `$P4CRITICAL` exposes their measured body durations.
  Timer callbacks and core step ISR are in IRAM. Cache-disabled operation is
  **not claimed** for the complete call chain: settings writes and OTA require
  standstill and block competing operation starts.
- Two hardware PCNT units independently count GPIO STEP edges via the internal
  input path. This checks MCU outputs, not ribbon wiring or motor motion.
- A third PCNT unit reads spindle A/B using the existing x2 decoding. Accumulator
  watchpoints replace task-level read/clear. Effective calibration stays at
  1200 counts/revolution. No physical index signal is assumed.
- Bench-only X, Z and tapered `G33` synchronization, encoder-derived revolution phase,
  repeated/multi-start phase diagnostics, and spindle stall/reversal fault stops.
  A P4 assembly boundary preserves the interrupted task's floating-point state;
  a 1,000-interrupt canary runs at boot and is available as `$P4FPUTEST`.
  Path-projected rate and acceleration limits cover both physical axes. G76
  remains rejected; Thread supplies explicit passes and multiple starts.
- UART0 via the tablet's USB bridge. A single grbl task owns command input;
  realtime commands are handled while the planner is busy. UART and software
  buffer errors cancel the command stream rather than executing truncated input.
- Normal grblHAL motor-enable control, with the existing inversion settings
  (X active low, Z active high) and idle-hold behavior. Startup, update mode and
  latched driver faults inhibit enables. The X/Z label buttons stop pending
  motion before releasing the selected motor. Disabled axes stay released across
  subsequent idle-hold/wake requests and core resets, until enabled on screen
  (or the tablet restarts). The normal build excludes the encoder
  simulator, GPIO encoder self-test and deliberate interrupt-stall test.
  An explicit `-DH5_BENCH_ONLY=ON` CMake build retains the disconnected fixture;
  all pulse-producing bench scripts require that build and a disconnected tablet.
  See [wireless commissioning](WIRELESS_COMMISSIONING.md).
- Core settings and UI preferences persist in a separate `h5_settings` NVS
  partition. Existing H5 NVS/storage is untouched. Positions, zeros and machining
  stops must be reestablished after reboot.
- H5 touchscreen widgets imported from the committed H5 baseline, LVGL 8.3.11,
  Waveshare MIPI display and GT911 touch. UI and LVGL run on core 0; grblHAL and
  its timer interrupts run on core 1.
- Queued UI commands enter the normal grbl parser at USB line boundaries.
  Status is copied into a coherent snapshot; callbacks never run motion code.
  Jog cancellation bypasses the queue and invalidates pending jog requests.
  Stream reset also discards pending UI requests and clears held gestures.
- X/Z readouts, zero, numeric/continuous/fine jog, machining stops, units,
  original pitch picker, eight operation tabs and cycle parameter controls.
  Gearbox, Cone and Async use a serialized assisted-feed service with manual
  override and resume. Gearbox/Cone include low-speed encoder-position following
  and phase-preserving handover to powered G33. Turn, Thread, Face, Cut and Ellipse use the native planner
  through a cycle service with touchscreen preview and controlled cancellation.
  See [assisted cycles](ASSISTED_CYCLES.md) for geometry and operating limits.
- X TMC5160 SPI setup uses the upstream Trinamic library, with readback and an
  explicit missing-device report on this disconnected tablet. Audio uses the
  ES8311 codec through a separate worker, so touch callbacks never wait on sound.
- Hosted Wi-Fi and authenticated dual-slot OTA, touchscreen update mode, boot
  confirmation and rollback. See [OTA and recovery](OTA.md) before flashing.
- SDK application logs are suppressed on UART0 to protect protocol responses.
  Core diagnostics remain available through the commands below.

**Not ready to run the lathe yet:** synthetic encoder and internal GPIO tests
cannot validate ribbon wiring, actual motor movement, physical clearance or
cutting behavior. The normal build now permits motor operation. All eight operations
are implemented; the remaining regression and hardware gates are tracked in
[port progress](PORT_PROGRESS.md). The power/battery investigation is deferred.

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

The tablet has migrated to factory + two OTA slots, all below 16 MiB.
Follow [OTA.md](OTA.md) for the exact layout, authenticated uploads and rollback.
Do not run unrestricted `idf.py flash`: it also overwrites the bootloader,
partition table and OTA selection data. App-only factory writes do not update
an already selected OTA slot. Retain the full 32 MiB original backup outside Git;
restoring original H5 now requires that complete backup. Backups contain private
settings and may contain credentials.

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

### Historical validation, 2026-09-14

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
The earlier pre-services build passed that expanded suite: 4,680 X pulses and 47,324 Z
pulses matched hardware counts; fault/overlap/late/RX-overflow counters were zero.
Internal pulse service spanned 15.3–21.4 us and the longest core callback was 17 us.

Run `python verify_connection.py PORT --seconds 60` for reopen/snapshot checks
followed by a 60-second continuous-connection test. These are bounded bench
checks, not a long-duration or machine-load stability certification.

The spindle-enabled build subsequently passed the motion suite again, the
11-cut spindle suite, and independent stall, reversal and missed-deadline fault
tests. That historical stage is recorded in
[SPINDLE_VALIDATION.md](SPINDLE_VALIDATION.md). The earlier spindle-tracking build also
passed 13 ramp/phase cases, the motion suite, the nine-cut steady spindle suite,
all three fault tests and USB/UI regressions. Peak encoder-equivalent phase
error was 0.0075 mm in the evaluated cutting windows; all 104,000 Z pulses in
the tracking suite matched PCNT counts. See [SPINDLE_TRACKING.md](SPINDLE_TRACKING.md)
for the acceleration measurements, assumptions and remaining limits. Those
historical runs used locked motor enables.

## Remaining port sequence

See [PORT_PROGRESS.md](PORT_PROGRESS.md) for current implementation, final
regression status and the hardware checks that require the actual lathe.
Historical measurements below and in linked validation documents are identified
by their firmware stage; they do not certify later builds.

## Build selection

Normal application (axis controls enabled):

```sh
idf.py -C p4/controller -DH5_BENCH_ONLY=OFF build
```

Disconnected bench fixture only (enables locked, synthetic encoder available):

```sh
idf.py -C p4/controller -B build-bench -DH5_BENCH_ONLY=ON -DH5_OTA_REQUIRE_PAIRING=ON build
```

Check the selected build and device `$I` identity before any pulse-producing
bench script. The `verify_*.py` motion suites are not connected-lathe procedures.
The host-only `tests/enable_outputs_test.py` checks the actual driver's enable
callback and disable request handling for axis masks, inversion, idle-hold,
stop-before-release, final pulse/planner drain, rapid toggles, reset persistence,
disabled-axis G-code rejection, startup/fault inhibition and the bench compile
gate without accessing hardware.

## Upstream updates

The upstream remote is `https://github.com/grblHAL/ESP32.git`; keep fork commits
on `codex/p4-motion-driver`. Merge upstream driver changes, inspect any updated
core submodule revision, then rerun this build and device suite. Review HAL
version, settings version, core source inventory and ISR dependencies on every
core upgrade. Do not advance the core automatically from a moving branch.

The core submodule points to `https://github.com/fer662/grblHAL-core`, branch
`codex/spindle-tracking`, commit `09df51bf9f527795920661b47db791181d4f5b2a`.
It has five isolated commits over upstream `516e5ad80757bd2eba86bff18feb613ca121dc16`:

| Commit | Purpose |
| --- | --- |
| `44aad88` | Exclude carried fractional-step time from spindle phase targets. |
| `17c1303` | Opt-in RPM feed-forward and acceleration phase tracking. |
| `28dabc2` | Opt-in rate and acceleration limits for the actual X/Z path. |
| `f799fd0` | Configurable AMASS cutoff; the upstream default remains 8000 Hz. |
| `09df51b` | Avoid waiting for a second completion when cancellation and completion coincide. |

The original `codex/spindle-segment-time` branch retains only the first fix.
Preserve this separation when merging/rebasing upstream; drop a local patch only
after equivalent upstream behavior passes the regressions. Configuration,
services, touchscreen, settings and hardware adaptation stay outside the core.
For future upgrades, build the pinned application, run geometry tests and all
`verify_*.py` suites on the disconnected tablet, then repeat relevant external
waveform/encoder checks. A changed timing implementation invalidates old physical
acceptance measurements even if the software pulse totals still match.

ESP-IDF reference: [P4 GPTimer API](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/peripherals/gptimer.html).
