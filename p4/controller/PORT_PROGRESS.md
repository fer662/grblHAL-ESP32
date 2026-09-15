# Port execution status

Updated 2026-09-15. Original committed H5 baseline: `981851b2`.
The original H5 checkout and its unrelated dirty experiments remain unchanged.
Earlier acceptance used a disconnected Waveshare P4 with enables locked. The
0.3.3 main build enables normal axis controls for the operator-requested first
unloaded Z test; installation status and remaining checks are recorded below.

## Implemented

- [x] Native grblHAL motion, accelerated jog, hold/resume, cancellation and reset.
- [x] X/Z path-specific spindle rate and acceleration limits.
- [x] Turn/Thread with depths, starts, spindle registration, preview and cancellation.
- [x] Face and progressive Cut profiles.
- [x] Ellipse geometry with native lookahead and spindle-progress feed.
- [x] Gearbox/Cone stop, reversal, reengagement and manual override.
- [x] Low-speed/hand-spindle position following and transition to powered G33.
- [x] Core cancellation/completion race fixed with a reproducing host regression.
- [x] Async manual override/resume; serialized edits to armed feed parameters.
- [x] Finite touch-jog repeat while held during all three assisted-feed modes.
- [x] Deliberate depth advance at a completed-depth boundary, retaining final depth.
- [x] TMC5160 SPI configuration and missing-device/readback diagnostics.
- [x] Separate persistent grbl/UI/Wi-Fi settings and asynchronous speaker service.
- [x] Hosted Wi-Fi, authenticated dual-slot OTA, boot confirmation and rollback.
- [x] Locally opened, read-only Wi-Fi diagnostics and computer-side log capture.
- [x] Full flash backup and migration preserving original H5 NVS/storage addresses.

See [OPERATIONS.md](OPERATIONS.md), [SPINDLE_TRACKING.md](SPINDLE_TRACKING.md)
and [OTA.md](OTA.md) for behavior, configuration, limits and recovery.
Gearbox/Cone now use native-planner encoder-position targets at low speed and
G33 for powered tracking, with 30/35 RPM handover hysteresis. The position follower
has no RPM engagement floor; the bench simulator tests down to 1 RPM. See
[HAND_FOLLOW.md](HAND_FOLLOW.md) for phase registration, latency and coverage.

## Previous 0.3.0 regression evidence

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

Previous application version: `0.3.0`. Built image SHA-256:
`346fc8c39bfa5aa6cb8935233cfd91417ac5628593090b9f65fadabb1ba8ff34`.
Core pin: `f799fd0f284c25d592821f800452cba6fc1ea78a`.

## Previous 0.3.1 software acceptance

**All 20 disconnected-device suites passed on the final image.** This includes
native spindle/X/Z/ramp motion, concurrent services, hand following, powered
follow, held fine jogs, operation UI, startup gating, ordinary motion, profiles,
Turn/Thread, three intentional fault stops, persistence, failed OTA transfers,
USB/display continuity, actual rollback and actual boot confirmation.

The core is pinned to `09df51bf9f527795920661b47db791181d4f5b2a`.
Its fifth isolated patch handles simultaneous cycle completion and cancellation.
The reproducing host test fails against the previous core and passes on this pin;
host profile geometry and Python syntax checks also passed.

| Final image check | Result |
| --- | --- |
| Hand spindle | 1, 5, 15 and 29 RPM, both directions, signed pitch, Cone, 8 mm/rev lead, bounds, stationary settling and manual/G33 handoff passed. Twelve repeated handoff/index-wait cancellations completed without sticking. |
| Spindle synchronization | Exact 36,000 Z pulses; repeated/multiple-start phase passed. |
| X/Z synchronized paths | Exact 26,400 X and 11,000 Z pulses; six directional/ramped paths passed. |
| Spindle ramps | All 13 cases, exact 104,000 Z pulses; peak cutting-window phase error 0.0075 mm. |
| Concurrent services | Exact 19,200 X and 51,200 Z pulses with audio, UI updates and 129 Wi-Fi connections. |
| Ordinary motion | Exact GPIO/counter agreement, including a 40,000-pulse counter rollover; cancellation and restart passed. |
| OTA | Deliberate rejected boot returned to ota_1; the normal update booted and confirmed ota_0. No pending validation remains. |

Normal suites reported no unexpected fault, overlap, missed-deadline or receive
overflow. The intentional stall/reversal/deadline tests latched the expected
fault and stopped STEP output. No fault thresholds were relaxed.

Earlier 0.3.1 candidates exposed an intermittent timing fault, including an 84 us
interrupt-entry delay. Application status formatting now happens outside
interrupt-masked cross-core locks. The final load test's last move reported lock
bodies of 6 us / 13 us on CPU 0 / CPU 1. `$P4CRITICAL` and fault-preserving host
diagnostics support further investigation; see [SPINDLE_TRACKING.md](SPINDLE_TRACKING.md)
for measurement scope and the preserved failure history.

The final normal-motion callback maximum was 47 us; the load run measured 64 us,
and the X/Z synchronization run measured 149 us. Observed pulse-high service
values across these normal suites reached 120.5 us. These are aggregate internal
measurements across different rates and motion stages, **not a worst-case timing
guarantee or external waveform acceptance**.

Application version: `0.3.1`. Built and OTA-tested image SHA-256:
`ecc4e9dfb1686635c90004b94afea01576753f06c193a090d56fdaf28a0df4f4`.
That image was confirmed in `ota_0` (`0x210000`) and left idle with motor enables
locked and the synthetic encoder stopped.
Logs and the final screen capture remain in the private external backup directory.

## 0.3.2 wireless diagnostics

USB is not required for the hardware checks or future OTA installation. Tap the
bottom status line to open Diagnostics, then use its IP from a computer on the
same network. BACK returns to operation controls while the 30-minute read-only
session continues. STOP SHARING closes access. See
[WIRELESS_COMMISSIONING.md](WIRELESS_COMMISSIONING.md).

Encoder, timing and fault observations are copied by the grbl task at up to 4 Hz;
HTTP formatting/transmission stays on CPU 0. TMC register refresh is idle-only at
most every two seconds. Both on-screen and remote samples report their age.
The endpoint accepts no motion, simulation, configuration or firmware commands.
No motor-enable gate or core planner code changed.

The final 0.3.2 image built and installed over authenticated OTA. The automated
read-only device test passed: actual local UI opening/back/closure events, fresh
HTTP snapshots, rejected POST/command-like/oversized requests, fragmented
requests, closure during a request, and unchanged zero step counts. Driver
fault/overlap/deadline/receive counters remained zero. Python syntax and diff
checks passed. This run did not repeat synthetic encoder or pulse-producing
stress tests: the tablet's current attachment state was not reconfirmed.
`verify_services_load.py` now includes HTTP observations alongside its existing
motion/audio/UI load and remains to be run on the disconnected fixture.

Application version: `0.3.2`. OTA image SHA-256:
`642f5d8a4b593102a69c9e97ff88ee42335e75e03ae95fa2571876428a577752`.
The selected partition is `ota_0`; the USB status check reported no pending boot
validation. A 30-second Wi-Fi-only capture recorded 103 valid snapshots, zero
connection errors and a maximum sample age of 247 ms, with no pulses or faults.
The 1280x800 panel capture was visually checked; its initial Wi-Fi-disconnected
message was captured before association, and the subsequent network log
confirmed connectivity. The tablet was returned to the normal operation screen.
Private logs: `diagnostics-032-final.log`, `ota-diagnostics-032-final.log`,
`diagnostics-032-wireless.jsonl`, and `diagnostics-032-screen.png`.
The earlier full 20-suite acceptance applies to 0.3.1, not automatically to this
new diagnostic sampling/network workload. Hardware acceptance remains below.

## First connected encoder and TMC checks, 2026-09-15

The operator confirmed that one complete hand-turned spindle revolution in each
direction produces the expected encoder count change. This checks the real
encoder's count scale and reversal, not powered-speed accuracy or thread phase
under load. The earlier 30-second wireless baseline contained 103 stationary
samples at count 2,128, no connection errors and no controller motion faults.

Before a tablet restart, the TMC5160 responded with IOIN `30000040` but the boot
initialization flag was false and CHOPCONF was `10410150`. After the operator
restarted the tablet, six fresh observations (uptime approximately 20–30 seconds)
reported transport OK, device present, configured true and CHOPCONF `17008425`.
That readback matches initialization, including two microsteps and interpolation.
Enable GPIOs remained X=1/Z=0; issued/counted step totals were zero after reboot
and controller fault counters stayed clear. No remote motion or register-write
command was sent. Log: `tmc-restart-20260915-142454.jsonl` in the private backup
folder.

This is consistent with the driver becoming available only after the original
boot; the exact cause of the earlier initialization failure is not established.
The current diagnostic's 1,700 mA and 75 mOhm fields are requested configuration,
not measured current or independent current-register readback. Full current,
electrical enable/STEP/DIR, powered spindle and loaded-drive checks remain open.

## 0.3.3 main axis-control build

The operator explicitly requested the main firmware's existing axis controls,
without a separate commissioning/arming UI. The first physical movement is Z
only with the carriage disengaged, before scope/logic-analyzer measurement.

The driver now honors native grblHAL enable masks, configured polarity and
idle-hold behavior. Startup/update readiness and a latched driver fault inhibit
enables. Motion settings, pitch/gear calibration, touchscreen operations and
planner code are unchanged. Normal firmware excludes synthetic encoder output,
the GPIO encoder self-test, deliberate IRQ-stall injection and motion-producing
UI test fixtures. `H5_BENCH_ONLY=ON` remains an explicit CMake option for the
original disconnected bench suites.

The 0.3.3 application builds successfully. The host test of the actual enable
callback passes all axis/polarity masks, hold requests, startup inhibition,
fault shutdown and the bench gate. ELF inspection confirms the simulator and
encoder-output self-test are absent and enable/fault routines remain in IRAM.
Image SHA-256:
`60586cc83c5279a049a3794962ffd20a29c1b098a65f57c0253a9f5b1df10245`.
That image was superseded before installation by 0.3.4 below. No motor
movement or connected motion regression has been performed by the agent.

## 0.3.4 selectable OTA pairing

The operator requested a conditional pairing requirement for the trusted LAN.
`H5_OTA_REQUIRE_PAIRING` is now a CMake option, default OFF for this project;
ON retains the original temporary-key/HMAC protocol. The LAN protocol explicitly
announces `H5OTA0` and accepts only the size/digest header. Both modes retain
local update-mode entry, idle/ownership checks, expiry/session-generation checks,
full image validation and boot rollback. UI/USB/HTTP status reports the selected
policy without exposing pairing keys over HTTP. The uploader supports both
protocols, including old firmware that always requires pairing.

Six local socket tests passed: paired upload, missing-key rejection, bad-HMAC
rejection, keyless upload, and digest-error handling in both modes. The actual
driver enable callback's host tests still pass. The 0.3.4 main image built with
axis controls enabled and pairing disabled, SHA-256:
`9510b28762df9899fe72ccaa5bb84733838b0fd7d1a69975e421c73a7d8669cf`.
The operator's photo supplied the current paired-only firmware's temporary key;
the real tablet authenticated, verified and accepted the update, then restarted.
The key and device logs remain outside Git. Subsequent HTTP diagnostics confirmed
0.3.4 on ota_1 with boot validation complete and pairing disabled. Following the
operator's movement tests, issued/counted totals matched at rest (X 99818,
Z 257924), with no controller motion fault, late or overlap counts. No remote
motion command has been issued.

## 0.3.5 physical axis-disable fix

The X/Z disable buttons previously changed only UI flags and cancelled jogs.
They never updated the driver enable mask, so native indefinite idle-hold kept
the motors energized. The UI now requests a per-axis hardware disable. The grbl
task issues native STOP to cancel assisted operations and flush motion queues,
retains torque during deceleration and the final pulse, then applies the mask.
Later wake/hold callbacks respect disabled axes. Requests survive core resets;
power-on starts with axes available as before. Rapid toggles still complete a
stop before applying the latest request. New submissions are rejected while
transitioning; disabled-axis G-code is rejected and a pulse-level guard prevents
silent stepping with a disabled output. UI and read-only diagnostics expose the
pending/applied disable state (mask X=1, Z=4).

Current, sense-resistor assumptions, hold percentage, microsteps, calibration and
grblHAL core are unchanged. The operator explicitly asked to preserve the
existing current and defer further current investigation.

Host tests of the actual enable/request/validation functions passed for both
normal and bench builds, including X/Z polarity, stop-before-release, final
pulse and planner drain, rapid toggles, reset persistence, disabled-axis moves,
full-circle arcs and G28/G30 rejection. The core cancellation/completion race
test also passed. The IDF 5.5.2 normal build passed; enable, pulse-start and fault
routines remain in IRAM. Existing LVGL enum and unused-variable warnings remain.
Application SHA-256:
`81ef39bcd5eb91980dc190605316263061faf2185ff2d102fb322ebcb4595c75`.
The tablet verified and accepted this image through LAN OTA and restarted.
Post-boot diagnostics and physical release confirmation are pending. No remote
motion command or connected motion test was run by the agent.

## Hardware acceptance still required

- [ ] Inspect external STEP/DIR pulse widths, jitter, skew and setup/hold at the connector.
- [x] Operator verified real spindle counts per hand-turned revolution in both directions.
- [ ] Verify powered spindle measurement and actual thread phase.
- [x] TMC5160 detected after restart; initialization and CHOPCONF readback passed with enables locked.
- [ ] Complete TMC current-register and physical drive-setting validation.
- [ ] Confirm physical speaker output and touch behavior with an operator.
- [ ] Establish machine origin, travel, clearance and stop behavior with the actual drives.
- [ ] Validate loaded acceleration, reversal, thread entry/exit and all eight operations.
- [ ] Investigate the breakout/tablet battery-dependent power behavior after the refactor.

No software test can mark these hardware checks complete. Internal pulse counters
observe MCU GPIO only; the real TMC5160 now responds and passes initialization,
but loaded behavior and actual current have not been measured. The internal pulse-service measurements above are not constant
10 us pulses or an externally measured jitter bound. Motor enables remain
locked in the historical bench builds. The normal 0.3.3 build permits axis
control by operator request; remaining physical measurements are not claimed.
