# Port execution status

Updated 2026-09-16. Original committed H5 baseline: `981851b2`.
The original H5 checkout and its unrelated dirty experiments remain unchanged.
Earlier acceptance used a disconnected Waveshare P4 with enables locked. The
0.3.3 main build enables normal axis controls for the operator-requested first
unloaded Z test; installation status and remaining checks are recorded below.

## 0.3.21 Thread transition timing (built; not installed)

Replaced quintic easing with per-pass acceleration/cruise/deceleration. The
21-block path preserves phase and bounds, checks rounded X chord rates and
sizes entry/withdrawal for each actual depth. Preview explicitly reports final-
pass stations. Same 10 mm example: 6.785 mm full depth, versus 4.355 mm in 0.3.20.
Documented why old H5 nominally used the entire 10 mm: X entered before Z started
and withdrew only after Z stopped. That sequence has different endpoint behavior.

Validation: real native planner/step ISR simulation, all five depths at 50–500 RPM,
451 RPM screenshot setup, old/new accelerations, direction, multiple starts,
slow speed changes and cancellation; phase checked through entry/withdrawal too.
Geometry sweeps cover triangular/trapezoidal profiles, rates, acceleration,
low-RPM rounding, tiny depths and all pass directions. Command-emitter and
G54/units preview regressions passed; ESP32-P4 build and LVGL renders completed.
No OTA or attached-machine motion commands sent. Includes the uninstalled Rapids
and X acceleration changes from 0.3.20.

## 0.3.20 Rapids and X acceleration (built; not installed)

Added persistent fourth STEP choice Rapids, always hold-to-run using the native
axis maximum. The center mode toggle visibly locks to Hold while selected, then
restores its prior choice. Release/slide-out cancels and Jog Limits applies.
Native X maximum remains 300 mm/min; normal X acceleration increases from 25 to
500 mm/s². Motion revision 3 upgrades the former acceleration once and preserves
custom settings and subsequent tuning. Bench defaults remain unchanged.

Validation: production jog routing (both axes/directions, both jog modes, live
rate, limits, release, unit cycling, invalid rates and assisted-mode rejection),
actual LVGL pointer/render checks, saved-preference validation and migration
failure/retry tests, native core threading simulation with the new acceleration,
geometry/preview, cancellation/enable regressions and ESP32-P4 build passed.
No on-machine motion commands, OTA or physical acceleration validation performed.

## 0.3.19 continuous clear-entry Thread (OTA uploaded)

Checked committed H5 981851b: X infeed preceded phase wait there too. Implemented
one continuous native-planner pass with clear Z run-up, moving X infeed/withdrawal,
and clear braking, within the Z bounds. Added one opt-in core patch for phase
continuity across blocks. Real entry/withdrawal stations replace full-span in-cut
acceleration. X maximum rate increases from the inherited manual-jog 60 to
300 mm/min, with a one-time revision-2 native settings upgrade; jogging remains
60 and X acceleration remains 25 mm/s². Custom rate settings are preserved.

Validation: production batch handler + actual core planner, segment generation
and step ISR under a virtual clock/AddressSanitizer; 50–500 RPM, direction/depth,
internal and multistart cases, slow RPM ramps, cancellation, X speed, phase,
pulse totals and endpoints. Existing host regressions and firmware build passed;
LVGL preview rendered in both units. No attached-machine motion/OTA or loaded
cut validation. Details in [CONTINUOUS_THREADING.md](CONTINUOUS_THREADING.md).

OTA uploaded on 2026-09-16 at the operator’s request. The updater reported
`Firmware verified; device restarting.` Independent post-boot confirmation
and loaded-machine testing remain outstanding. No motion commands were sent.
Uploaded application SHA-256: `8d8c469e17c409e6d30b97e60bd9b67decf47044e50423c506f8d5ea1d682d51`.

## 0.3.18 actual cut preview and acceleration (OTA uploaded)

Thread preview now reports actual X infeed/retract Z stations, cutting travel,
first/final X depths and X clearance. Removed estimated thread-length fields
and the arbitrary settling heuristic. The replacement short-span gate checks
nominal acceleration/braking feasibility at the RPM ceiling. Existing machine
endpoints, G54 conversion, bounded travel and multiple-start registration remain.
Z acceleration is 100 mm/s² in normal builds, with a boot-only native/NVS upgrade
from the former saved 50 default. Other tuning is preserved, including subsequent
rollback; benchmark defaults/migration are kept at the former baseline.

Validation: host geometry/preview and actual emitter checks, native-setting
migration gate/persistence-failure stubs, G54/jog/enable/cancellation regressions,
ESP32-P4 build, and rendered production preview in both units. Bench script syntax
checked only; no on-lathe motion or OTA performed. No loaded-machine validation
of 100 mm/s², full-pitch end-to-end thread claim, or synchronized X pullout.

## 0.3.17 work-coordinate integration (OTA uploaded)

Removed the UI-local origin. X0/Z0 submit a core-thread idle-guarded native
G54/G10 L20 request. Core WCO and active WCS now drive all coordinate displays
and limit editing. Machine endpoints and assisted motion generation are unchanged.
The editor rejects stale coordinate frames; preview preparation waits for commands.

Validation: ESP32-P4 application build; host zero-gate and UI request/ACK tests;
metric/inch conversion and preview regressions; assisted geometry/emitter,
cancellation and enable-output tests; production LVGL touch/editor and layout
checks. Native G10 semantics are supplied by the pinned core; the new host gate
test uses a parser spy, not a complete grblHAL emulator. No on-lathe commands or
OTA installation were performed during implementation. Subsequently uploaded
over Wi-Fi at the operator's request: firmware verified and device restart
acknowledged; no remote motion was requested.

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

## 0.3.6 larger touchscreen controls

Replaced the original small triangular jog pad with four separated 160 x 112
pixel buttons and a dedicated row of limit buttons. Retained physical direction
mapping and all eight operations. Expanded position rows, primary controls,
cycle settings, mode menu and number pad for the 1280 x 800 display. Disabled
axes are visually greyed out. Sliding off a jog cancels it and requires lifting
before another direction can start, including LVGL's same-tick target transfer.

Built and visually inspected the production LVGL widgets in a desktop renderer
with simulated backend data. Actual LVGL pointer-input assertions passed for
direction/release, drag cancellation, disabled axes, separate limit controls,
all eight modes, button bounds/overlap, SHIFT distance entry and numeric limit
entry. This does not establish real touch calibration or motion timing under
display load. No remote motion command was issued.

The IDF 5.5.2 normal firmware build passed with 23% of the OTA application
partition free. Application SHA-256:
`bd1fcb4352046f087bb08b9b80cd4a018a9a67b40bb973224a2935021b5ed7aa`.
OTA installation and operator acceptance remain pending; the tablet was not
reachable during this UI work. See [UI_ROADMAP.md](UI_ROADMAP.md) for previews
and proposed controls using existing grblHAL features.

## 0.3.7 outer-compass jog and limit layout

Applied the operator's selected outer-compass arrangement only to the jog/limit
panel. X+ and X- limits sit above/below their arrows; Z+ and Z- limits sit to
their left/right. Jog targets stay 160 x 112 pixels, with a 16-pixel separation
from each associated limit. The panel grows downward to 600 x 628 pixels.
Other controls, mode-specific settings and input semantics are unchanged.

The existing production-LVGL desktop pointer/layout regression passed, including
all eight modes, jog release/slide cancellation, disabled axes, limit separation
and SHIFT numeric entry. Gearbox and Thread preview pixels outside the jog/limit
panel match 0.3.6 exactly. The IDF 5.5.2 normal firmware build passed with 23%
OTA partition space free. Application SHA-256:
`9ee9209cc20a47311215869677afed3af951f0ba265850966d3a1b9e7b101c1e`.
The operator opened FW Update and the tablet verified and accepted this image
over LAN OTA, then restarted. Post-boot version/rollback validation and operator
acceptance remain pending; HTTP diagnostics requires opening the local Diagnostics
screen. No remote motion command was issued. Current previews are linked from
[UI_ROADMAP.md](UI_ROADMAP.md).

## 0.3.8 explicit Hold / Single-step jog mode

Added the center jog-mode toggle and moved STEP into the right pane. The jog
cross now uses 128 x 128 square buttons, retaining the outer directional limits.
Other operation controls retain their positions. Mode and distance are independent:
Hold stops on release; Single step submits one bounded increment, completes after
release and does not auto-repeat. Existing SHIFT and unit-selection actions remain.
The mode persists in a formerly reserved ui_v1 preference byte; old blobs default
to Hold without changing pitch, step, calibration or operation settings.

Ordinary jogs reject overlapping taps until a coherent bridge motion snapshot
has observed the prior command's acknowledgment and motion has finished. Stop,
axis-disable and reset paths release the single-step reservation. During active
assisted feed, Single sends the existing one-shot override request; synchronized
Z still rounds to whole pitches. A Hold release now also discards a still-pending
override, preventing a quick released press from starting late.

The production-function host tests passed for mode/distance independence, both
unit systems, release behavior, duplicate/pending/active presses, bound clipping,
cancellation, disabled axes, assisted-feed routing, release-before-start and
preference-layout compatibility. The existing core cancellation/completion test
passed. Actual LVGL pointer tests passed, including slide-out into the new center
button without changing modes, relocated STEP and all eight mode layouts. Desktop
Gearbox, Thread and Single-step previews were visually checked.

The IDF 5.5.2 normal build passed, with 23% OTA application space free. SHA-256:
`5329b5459066476600205f03502fd87cd19e6d6c1957d8621ae6b77e4ffcdac1`.
OTA installation and real-machine acceptance are pending; no remote motion or
connected bench test was run. Further corner controls are proposals in
[UI_ROADMAP.md](UI_ROADMAP.md).

## 0.3.9 jog-panel alignment and corner sizing

Removed the standalone JOG heading. Z limits now share their jog buttons'
128-pixel height and vertical alignment; X limits share their 128-pixel width and
horizontal alignment. STEP fills the upper-left 220 x 208 corner with centered
text. The mode toggle and motion behavior are unchanged. Other corner actions
remain proposals, with matching space reserved in the layout documentation.

The existing production-LVGL desktop input/layout checks passed, including all
eight modes, both jog-mode labels and slide-out protection. Gearbox, Thread and
Single-step previews were regenerated and Thread was visually inspected. The
IDF 5.5.2 normal build passed. Application SHA-256:
`38b9a433e12edcd4a599dff4a0c79242a1c37e12ee83e2c6d05300a41717f903`.
OTA installation remains pending. No remote motor commands were issued.

## 0.3.10 jog-limit bypass and endpoint editor

Added matching corner controls for JOG LIMITS ON/OFF and EDIT LIMITS. Manual
jog bypass retains endpoint values, is visibly indicated and resets to ON at
power-on. Assisted operations retain their existing bounds. The editor supports
signed display coordinates in mm/in, current-position capture, per-endpoint
clearing, X/Z spans, draft cancellation and atomic Apply. Changing bypass,
capturing a position or applying edits requires stopped motion, no pending UI
move, and no assisted operation or update in progress. No jog-speed control added.

Production-function host checks passed for bypass/re-enable, pending/moving/
assisted/update guards, endpoint ordering, atomic updates, unit/origin conversion
and out-of-range rejection. Existing Hold/Single and assisted-feed routing checks
also passed. The actual LVGL pointer regression passed for all eight modes and
new toggle/editor interactions, including signed input, Cancel, Clear, Use current
and refused Apply while busy. Main, bypassed, editor and signed-keypad framebuffer
previews were visually inspected.

The normal IDF 5.5.2 build passed; 22% of the OTA application partition remains free.
Application SHA-256:
`7d56d9555102f066ccb056bace9783efe570cd30c5f55a5e998ab2aee7cb92f1`.
OTA installation is pending. No connected hardware motion tests were run.

## 0.3.11 larger jog targets

Expanded the right pane upward to y=16 and widened it to 640 pixels. All four
jog targets are now 152 x 180 (67% more touch area than 128 x 128). Removed the
hint below the panel, aligned X/Z limit dimensions to their jogs, and resized
corner controls to 232 x 260. The 128 x 128 center mode control is recentered.
All operation controls and motion behavior are unchanged.

The production LVGL pointer/layout regression passed, including all eight modes,
direction/release, slide-out cancellation, disabled-axis controls and the limit
editor. The Thread framebuffer render was visually inspected. The normal
IDF 5.5.2 firmware build passed. Application SHA-256:
`78f7248d26aedd017f9f93e2e1a9db9be7a6bb8b424d5d041016f5be80e4c3e3`.
OTA installation is pending; no connected motion tests were run.

## 0.3.12 bottom-aligned action row

Moved START and SHIFT to y=652 so their 88-pixel-tall buttons end at y=740,
aligned with the right-pane controls. The existing LVGL pointer/layout regression
passed for all eight modes; the Thread render was visually inspected. The normal
IDF 5.5.2 firmware build passed. Application SHA-256:
`c7a8d5adfc60cf409a788c958980366e0bc6f48597f102e414417d439699ec55`.
OTA installation is pending. No connected motion tests were run.

## 0.3.13 consistent left-pane rows

Standardized left-pane header, body and action buttons to 80-pixel heights.
Upper rows start at y=16/112/208/304 with 16-pixel gaps; PITCH moves from y=318
to y=304 to align with the third settings row. START/SHIFT remain bottom-aligned
at y=740. Existing LVGL pointer/layout checks passed for all eight modes, and the
Thread render was visually inspected. The normal IDF 5.5.2 build passed.
Application SHA-256:
`732de0f03451e16d23685268957decbdbcba003bac6f4e7817561ee371579150`.
OTA installation is pending; no connected motion tests were run.

## 0.3.14 bounded non-thread cutting moves

Operator reported Turn going beyond Z=10 by about 0.26 mm at 0.10 mm/rev.
The existing Turn recipe shared Thread's indexed G33 lead-in/run-out. A host
reproduction at 400 RPM (500 RPM preview ceiling) produced Z=10.250 approach,
10.255 takeup and -0.230 finish for a 10-to-0 cut.

Turn now uses the existing G95 feed-per-revolution path, with acceleration and
deceleration inside its entered endpoints. Indexed geometry remains exclusive
to Thread. The shared extra one-step approach was also removed from Face, Cut
and Ellipse; the runtime emitter uses the same rule as the geometry preview.
Depth/pass progression, pitch, auxiliary direction, 0.5 mm tool-clearance retract,
fixed Z for Cut, and Thread lead-in/run-out/phase are preserved. The preview now
explains that non-thread cutting-axis moves stay within bounds while the
clearance retract may extend beyond the depth-axis bounds.

Host geometry and production command-emitter tests passed for all non-thread
profiles, both spindle and pitch signs, both auxiliary directions and multiple
passes. Tests cover short Turn spans, ellipse points and generated approach,
cut and return commands; Thread's original G33/takeup/phase reference commands
still pass. The existing core cancellation/completion test passed. The offline
bench verifier was updated for bounded G95 Turn and syntax-checked; it was not
run against the connected lathe. No core or driver changes were needed.

The normal IDF 5.5.2 build passed (22% OTA slot free). Application SHA-256:
`82e7e7fa93f114a82411931293cc54fe96616246c07cd7f3090d63feca5c97d4`.
OTA and loaded-machine acceptance are pending. G95 ramps feed inside the cut
and does not preserve thread phase, so endpoint surface finish needs operator
assessment. No remote motion commands were issued.

## 0.3.15 threading synchronization inside the entered Z span

Operator identified that retaining Thread's external lead-in/run-out could exceed
the length cleared by a preceding Turn cycle. The committed H5 baseline clamps
its cutting pass through `posFromSpindle(..., true)`, although its return included
one-step overshoot. The larger external synchronization margins were introduced
by this port and were not compatible with the intended limit semantics.

Thread now returns to the start bound, takes up one step inward, and emits G33
to the opposite bound. Existing synchronization and braking margins consume
space inside the span. The estimated steady-pitch region is exposed in the
preview and H5PLAN diagnostics; fewer than one usable Z step rejects the cycle
before movement. Phase registration remains referenced to the entered start
bound using the actual approach offset, independent of the selected RPM ceiling.
Multi-start lead and phase spacing are retained. Depth-axis tool-clearance
retracts remain separate and unchanged.

Host geometry and production-emitter checks passed for both directions, multiple
passes, multi-start phase arithmetic, phase invariance under changed margins,
short-span rejection and the other profile operations. Existing core cancellation
checks passed. The actual production preview construction/text was extracted into
a temporary LVGL host harness; its framebuffer was inspected and text-versus-RUN
button overlap asserted. The offline device verifier now expects in-bound Thread
positions and measures phase only in the estimated steady region; syntax checked,
not run on the connected machine. Core and pulse-driver code are unchanged.

The normal IDF 5.5.2 build passed (22% OTA slot free). Application SHA-256:
`ce2c696391fed1dbefcbf4e428f2feccc66c3e8820734315e7cf51b4f91bb109`.
No remote motion commands or OTA upload were issued. Loaded thread phase/finish
acceptance remains pending. Run-in/run-out occur at cutting depth and are not
promised as usable thread; the preview's usable region is an estimate.

## 0.3.16 preview uses displayed zero and units

Operator screenshots showed Z limits 0..10 but preview -1.5..8.5. Limit/DRO
labels applied the UI origin offset while the preview printed raw machine mm.
All preview positions now apply the corresponding X/Z display origin and selected
units. Lead, run-in/out and usable length convert units without origin offsets.
The preview explicitly identifies main-screen zero/units and radial slide travel.
The planner, emitted movement, grblHAL G54 offsets and X0/Z0 behavior are unchanged.

A host test executing the production formatter passed the screenshot case,
independent X/Z origins, facing's axis swap, mm/in values, and thread-region
positions versus lengths. It also verifies the plan remains byte-for-byte
unchanged. Actual LVGL Turn/mm and Thread/inch renders were visually inspected;
the latter's text clears the RUN button. The normal IDF 5.5.2 build passed.
Application SHA-256:
`ee1083c4cf9109c57d75e622f1bd467135e0ba6c260e00d2fba88b191edfed4b`.
OTA installation is pending. No connected motion tests were run.

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
