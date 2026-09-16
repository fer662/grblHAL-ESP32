# Assisted cycle service

All eight operations now have implementations. This document describes the
Turn/Thread geometry and records its earlier validation. See [OPERATIONS.md](OPERATIONS.md)
for Face, Cut, Ellipse, Gearbox, Cone, Async, parameter edits and the current
operating limits. See [PORT_PROGRESS.md](PORT_PROGRESS.md) for final regression status.

## Change in 0.3.22: air clearance with stationary Z

Before phase wait, X pre-positions one step outside the configured starting-X
surface while Z stays at the approach point. Synchronized entry/withdrawal use
this near-surface X position. The full clearance retract happens after Z stops.
The starting-X limit must describe the stock surface; there is no contact sensing.
All Z endpoints and thread registration remain unchanged. The preview explicitly
shows X pre-position and post-stop retract as well as final-pass cutting stations.

## Change in 0.3.21: remove extra Thread easing

Entry/withdrawal now use a trapezoidal-velocity X profile, sized separately for
each depth pass. Points are step-quantized and checked against X maximum speed.
The final-pass full-depth section is reported explicitly; the synchronization
origin stays fixed across passes. See [geometry and legacy comparison](CONTINUOUS_THREADING.md).

## Change in 0.3.19: synchronize and accelerate at clearance

Thread now executes one continuous indexed path: clear Z run-up, moving X
entry, full-depth cut, moving X withdrawal, clear Z braking. The preview shows
actual programmed stations and full-depth length. Entry/exit lengths respect
X speed and acceleration; short spans are rejected rather than exceeded.
Normal X maximum rate is now 300 mm/min (native saved-setting upgrade from the
former 60 default), while manual jogging stays 60 and X acceleration stays 25 mm/s².
The old committed H5 implementation also fed X in before its spindle phase wait.
See [continuous threading](CONTINUOUS_THREADING.md) for source findings, geometry,
core integration, test coverage and machine limitations. Historical sections
below describe earlier behavior and are superseded by this change.

## Change in 0.3.18: actual motion endpoints; Z acceleration

Thread previews now show the emitter's actual commanded positions: X infeeds
at `approach`, G33 runs from `approach` to `finish` with X at cutting depth,
and X retracts at `finish` after Z stops. The reported cutting travel is
`abs(finish - approach)`. First/final X depth and the X clearance target are
also explicit. Inch position displays use five decimals so a 0.005 mm approach
step is visible. The RUN button now says RUN CYCLE.

For the reported 0..10 mm Z span, 0.5 mm lead and five passes, this means
X infeed at Z=0.005, X retract at Z=10.000, and 9.995 mm of Z travel at depth.
With X depth bounds 0..1 and forward infeed, X depths are 0.200 through 1.000,
and clearance is X=-0.500. G54 and reversed-direction conversion still apply.
The previous 6.515 mm number did not control either contact station.

There is no speculative usable-thread/steady-pitch region in the planner or
preview. A cutting travel is not a claim of full-pitch finished thread over that
whole length: the existing cycle accelerates and brakes with X at depth, waits
for Z to stop, then retracts X. This update does not add a synchronized X pullout
or a stock/contact model. Entry/retract stations describe commanded geometry.

Z acceleration increases from 50 to 100 mm/s² in normal builds; X stays at 25.
At the example ceiling of 564 RPM and 0.5 mm/rev, nominal acceleration/braking
travel at constant spindle speed is `v²/(2a)` = 0.11045 mm per ramp at 100,
compared with 0.2209 mm at 50. This does not move either endpoint.
The earlier H5 baseline requested 50 mm/s² Z too, but initialized its step rate
at 400 steps/s = 2 mm/s instead of starting from rest.

The boot-only migration uses native `$122` storage semantics, forces the core
NVS buffer to flash before recording `motion_rev=1`, and only upgrades a saved
value exactly equal to the former 50 mm/s² default. Other custom values and later
manual tuning are retained. Disconnected bench builds keep Z=50 and skip this
migration. Increasing acceleration has not been tested on the loaded lathe.

Short Thread passes now use a necessary speed-feasibility check: available G33
travel must fit two `v²/(2a)` ramps rounded up to Z steps plus one cruise step,
using the configured RPM ceiling. This replaces the old two-lead/250 ms settling
heuristic; it does not certify pitch quality. All targets remain inside Z bounds.
H5PLAN diagnostics now report `APPROACH`, `FINISH`, `CUT_LENGTH` and `ACCEL`;
legacy estimated-region fields have been removed. The disconnected bench trace
analyzer retains a conservative sampling window solely for its phase assertions.

Rendered production UI: [Thread in mm](docs/thread-cycle-0318.png),
[Thread in inches](docs/thread-inch-0318.png).

## Change in 0.3.16: consistent preview coordinates

Cycle previews now use the main-screen zero and selected mm/in units for every
position, including approach, retract and the estimated thread region. Lengths
and lead only change units. For example, machine Z=-1.5..8.5 with a +1.5 mm
readout offset appears as Z=0..10 in both the limit buttons and preview.
The underlying plan and emitted machine-coordinate moves are unchanged.

Actual LVGL previews: [Turn in mm](docs/turn-cycle-0316.png),
[Thread in inches](docs/thread-inch-0316.png).

## Change in 0.3.17: native G54 touchscreen zero

X0/Z0 now select G54 and set the selected axis's current work position to zero
with native `G54 G10 L20 P1 X0` or `Z0`. The UI sends `$P4ZERO=X/Z`; this P4
system command rechecks idle state, pulse/stepper/planner activity, cycle ownership,
OTA and axis changes on the core task before calling the native parser. It does
not move an axis. The other axis's stored G54 offset is retained. If a sender had
selected another work system, switching back to G54 can change both readouts.

The bridge publishes `gc_get_offset(axis, true)` and the active coordinate-system
ID. The DRO, saved-limit labels, numeric editor and cycle preview all subtract
that same core offset, including G92 and tool-length offsets. G54 is the default;
a sender selecting G55/etc. is reflected in the touchscreen and its coordinate
label. X remains radial slide travel, including when a sender selects G7.
There is no separate touchscreen origin. Zeroing waits for a complete post-ACK
snapshot; an editor opened before a work-zero/unit change must be reopened.

Saved limits stay in machine steps. Changing work zero changes their displayed
numbers, never their physical positions. Assisted cycles still use G53 for
absolute positioning and G91 for relative cuts; their paths are unchanged.
G53 applies machine coordinates to one motion block; G54 selects a work system.

Native G54 offsets persist through grblHAL's settings storage. Without homing,
the machine reference is not repeatable across power cycles: establish the work
zero again after power-up. The old volatile touchscreen origins are not migrated.

Actual 0.3.17 LVGL previews: [Turn in mm](docs/turn-cycle-0317.png),
[Thread in inches](docs/thread-inch-0317.png).

## Change in 0.3.15: bounded threading

The original committed H5 cut used `posFromSpindle(..., true)` to clamp to its
stops, although it requested a one-step overshoot on return. The P4 port added
longer Thread/Turn lead-in/run-out outside the entered span. That was incompatible
with clearing a length by turning and then threading that same length.

Thread now treats the entered Z endpoints as its travel bounds. The one-step
approach and the synchronization/braking allowances fit inside them. This reduces
the estimated usable thread length instead of extending travel. A setup with less
than one Z step left between the allowances is rejected before motion. The depth-
axis tool-clearance retract is a separate, unchanged movement.

[Actual LVGL preview with simulated readings](docs/thread-cycle-0315.png).
The steady-pitch region is an estimate from the existing margins, not a measured
thread-quality guarantee. The tool is at cutting depth during run-in/run-out, so
those portions are not promised as usable thread. There is no automatic pull-out
chamfer. Loaded phase/finish acceptance remains pending.

## Change in 0.3.14

Turn now uses G95 feed per revolution and stops at the entered Z endpoints.
Its acceleration/deceleration occur inside that span, without Thread's lead-in,
run-out or index registration. Face and Cut already used G95. All non-thread
profiles, including Ellipse, now omit the extra one-step approach beyond the
cutting-axis bounds. The 0.5 mm depth-axis tool-clearance retract is preserved;
Cut still keeps Z fixed. Thread was unchanged in that version; 0.3.15 supersedes
its external lead-in/run-out behavior as described above.
The preview distinguishes these behaviors. Earlier Thread/Turn bench records
below describe the pre-0.3.14 implementation where both used G33.

## Operator workflow

1. Select Turn or Thread, set signed feed/pitch, depth passes, X and Z machining
   bounds, and the auxiliary direction. Thread also uses the starts setting.
2. With both axes available and the spindle encoder running, press START.
3. Review the cycle preview. It shows radial X coordinates, cutting Z bounds,
   clear run-up, moving X infeed/withdrawal, full-depth start/end and length,
   first/final X depths, X clearance and maximum RPM. All
   coordinates in this preview use the **main-screen zero and selected units**.
   The motion plan and diagnostic commands continue using machine millimeters.
4. RUN CYCLE copies the configuration to the grbl task. The status line
   shows stage, pass and start. STOP cancels the cycle with controlled braking;
   a partially cut thread cannot be resumed with cycle-start.

The preview is a real LVGL panel; its RUN button requests a copied configuration.
A second validation on the grbl task checks current position, controller state,
RPM, and motion settings before moving. Active coordinate scaling or rotation
is rejected; ordinary work offsets, metric/imperial and diameter modes cannot
change the generated machine-coordinate path. Recipes use a copied configuration; editing their motion parameters cancels
the active recipe so the displayed settings cannot silently disagree with the cut. Changing machining stops, disabling an axis, switching modes,
requesting firmware update or attempting to jog cancels the active cycle.

## Preserved and explicit geometry

The committed H5 baseline's `main/modes/ModeTurn.cpp` supplies the pass order,
linear radial depth progression, auxiliary-direction choice, 0.5 mm clearance,
one-step Z approach, return-to-start behavior and multiple-start lead. The
0.3.15 approach step is inside the entered span rather than outside it.
The new service uses grblHAL for every move; no old step/task synchronization
logic or uncommitted H5 motion experiment is used.

For Thread, the generated sequence is:

1. Set metric/radial coordinates and the XZ plane.
2. Retract X to its clearance position before moving Z to the approach.
3. Move to the Z start bound, then take up one step inward in the cutting direction.
4. Keep X at clearance, register spindle phase and select spindle direction.
5. Queue the complete continuous path before the single index wait.
6. Accelerate Z clear, infeed X while moving, cut the full-depth section,
   withdraw X while moving, then brake Z clear at the opposite bound.
7. Return Z at clearance and take up one step inward.
8. Repeat all starts at the same depth before increasing depth.
9. After the final pass, return Z to the cutting start and X to its initial bound.
10. Restore zero diagnostic phase, absolute distance mode and feed-per-minute mode.

M3/M4/M5 only describe the external spindle to the controller; they do not switch
or regulate the physical lathe spindle. Feed direction is signed pitch multiplied
by observed spindle direction. Thread lead is `abs(pitch) * starts`; Turn uses
one start. X remains radial, including depth and clearance.

### In-bound travel, phase and speed feasibility

The default RPM ceiling remains 125% of current measured speed, capped at
88% of the Z maximum-feed/lead ratio. Starting above that ceiling or below
30 RPM is rejected. Leaving that range or reversing the spindle cancels a
running cycle.

Thread takes up one Z step inward: `approach = start_bound + direction / steps`.
The continuous clear-entry sequence and its actual full-depth stations are
specified in [CONTINUOUS_THREADING.md](CONTINUOUS_THREADING.md). RPM and X limits
now determine entry/withdrawal lengths, but never extend the entered bounds.

Each start's requested spindle phase includes that one-step displacement divided
by lead. The prepared core profile still supplies acceleration phase compensation.
Changing acceleration does not change the thread's reference to the entered start
bound. Multiple starts retain individually rounded fractions of one revolution.
See [spindle tracking](SPINDLE_TRACKING.md) for backend behavior and earlier bench
measurements; those measurements do not validate the new loaded acceleration.

All profile cutting-axis approach, cutting and return targets remain within their
entered bounds. The depth-axis tool-clearance retract can extend beyond that
axis's depth bounds. These are not universal machine travel limits.
The preview exposes those extensions. Preflight limits the full requested span,
including the current position, to the inherited 100 mm X and 300 mm Z values.
These span checks are not a homed machine envelope or proof of chuck, shoulder
or workpiece clearance. Physical travel constraints and clearance validation
remain required before connecting the machine or removing the enable lock.

## Execution and cancellation

`cycle_plan.c` is pure geometry shared by preview and execution. `main/cycle.c`
runs transitions only on the grbl task. It submits one line through the normal
bridge/parser, waits for that exact command's acknowledgment, and then requires
an empty planner and actual idle state before advancing. Ellipse cutting chords
are the exception: acknowledgments pipeline them into native lookahead, and
only the last chord requires standstill. Neither G33 nor rapid acknowledgment
alone is considered motion completion.

While a cycle owns the command stream, ordinary UI moves and competing USB
G-code/settings are rejected. They are not saved for execution after the cycle.
Realtime status and reset remain available. Feed hold and jog-cancel request
cycle cancellation; cycle-start cannot resume a cancelled pass. `$P4CYCLE` and
the bench UI STOP event are the only ordinary USB commands serviced during a
cycle. A pending request does not claim USB input until startup has verified
there is no competing queued or partial line.

STOP invalidates pending cycle commands, requests grblHAL motion cancellation with normal deceleration, and waits
for stepping and the core cycle state to stop. G33 disables ordinary feed hold,
so cancellation uses the core motion-cancel path. Cancellation during index
waiting uses the core's explicit stop-before-wakeup path. It then resets parser/planner state together, retaining
the stopped machine position. No automatic retract or return is attempted after
STOP. The operator must decide the recovery move. Fault/reset also invalidates
the recipe and the UI gesture state.

The service guards against recursive entry: UART writes may invoke the realtime
hook while waiting for transmit FIFO space. A diagnostic report must never
advance the same stage twice. The initial bench run exposed this defect; exact
stage-order and pulse-total regressions cover the corrected behavior.

## Bench diagnostics and tests

The USB start command uses machine-mm coordinates:

```
$P4CYCLE=operation,pitch,passes,starts,aux_forward,x_min,x_max,z_min,z_max,rpm_limit
```

`operation` is 0 Turn, 1 Thread, 2 Face, 3 Cut or 4 Ellipse; `aux_forward` is 0/1. For example, on the disconnected simulator:

```
$P4SIM=300
$P4CYCLETRACE=1
$P4CYCLE=1,0.5,2,2,1,0,0.1,0,6,375
```

Wait for measured RPM to settle before requesting the cycle. `$P4CYCLE` reports
active state, pass, start and stage. `$P4CYCLETRACE=1` adds completed-stage
positions and per-cut spindle traces for the bench suite; it defaults off.
All of this remains behind the existing enable-locked P4 application gate.

Additional `$P4UITEST` actions:

- `6`: load the explicit two-pass/two-start bench fixture, select Thread, and
  send CLICKED to the actual START button, opening the preview without moving.
- `7`: send CLICKED to the preview's RUN button.
- `8`: send CLICKED to the actual START/STOP button; used as STOP while running.

Host geometry regression:

```sh
cc -std=c11 -Wall -Wextra -Werror -Icomponents/h5_ui \
  components/h5_ui/cycle_plan.c tests/cycle_plan_test.c -lm -o /tmp/h5-cycle-plan-test
/tmp/h5-cycle-plan-test
python3 tests/cycle_commands_test.py
python3 tests/cycle_preview_test.py
```

The host command test executes the production command emitter with a simulated
bridge and checks Turn/Face/Cut/Ellipse approach, cut and return targets for both
spindle/pitch signs, both auxiliary directions and multiple passes. It also checks
bounded Thread G33 commands, inward takeup and multi-start phase registration. The preview formatter test checks nonzero origins, both axes, metric/inch
positions and lengths, and verifies that formatting does not change the plan.
These checks do not measure loaded motion.

Device regression (disconnected, enable-locked bench only):

```sh
python verify_cycles.py PORT --screen /private/path/cycle-preview.png
python verify_motion.py PORT
python verify_spindle_tracking.py PORT
```

The cycle suite covers touchscreen preview/RUN, multiple depths and starts,
reverse spindle, negative pitch, both auxiliary directions, Turn feed,
competing USB motion/settings, STOP during approach and cutting, and rejected
travel geometry. It compares continuous spindle phase over the cutting area,
exact stage order, infeed/end positions, total commanded travel and independent
GPIO PCNT pulse counts. These are internal disconnected-bench measurements,
not loaded machining or external waveform measurements.

## Historical Turn/Thread device results, 2026-09-14

These results describe the earlier app-only installation, before the OTA
partition migration and additional services. They are not the current build.

The app was built with the existing ESP-IDF 5.5.2 environment and installed
app-only at `0x10000`, preserving the original bootloader, partition table and
storage. Esptool verified the written hash. The application remains installed
on the USB-powered tablet, disconnected from the lathe, with motor enables locked.
The core pin remains `17c13030ab8a7317943bf54ae1a55d3c57f139bd`; this cycle service
adds no core patches.

- Four complete recipes / ten cutting passes passed: two depths x two starts,
  reverse spindle with opposite auxiliary direction, negative pitch with two
  starts, and two-depth Turn feed. Maximum cutting-window phase error was
  **0.003334 mm** (encoder-equivalent, rounded upward).
- Every recipe passed exact stage-order, radial depth, endpoint and total-travel
  checks. **22,920 X and 44,628 Z pulses** across the final recipe/cancellation suite,
  including RPM-ceiling cancellation,
  matched GPIO PCNT counts. Fault, overlap, late and RX-overflow counters stayed
  zero. Maximum measured core callback time was 44 us; internal pulse service
  spanned 15.3–31.2 us.
- Actual LVGL START, preview RUN and STOP event callbacks were exercised. The
  preview caused no motion; its rendered 1280x800 image was captured and visually
  checked for readable text, coordinates and controls. This does not simulate
  the physical GT911 touch sensor or a human tap.
- Competing USB movement and settings commands were rejected during cutting.
  Existing work offsets and inch/diameter mode did not alter the path. Invalid
  travel geometry and active scaling were rejected without emitting pulses.
- Cancellation passed during Z approach, before any synchronized STEP output
  while waiting for index, and during cutting via the actual LVGL STOP callback.
  Each returned to idle through reset without an in-motion reset alarm, retained
  the stopped position, and emitted no queued retract or return afterward.
- The existing motion suite passed: 4,596 X / 47,220 Z pulses matched PCNT counts,
  including its 40,000-pulse rollover move. The 13-case spindle ramp regression
  also passed with all 104,000 Z pulses counted, zero fault/overlap/late/overflow,
  and continuing UI progress.
- USB/UI regression passed three reconnects, two captures and a 15-second
  persistent connection. The final status-only fix preserves the stop reason
  through reset; its RPM-ceiling test passed with a 300-to-450 RPM ramp, a
  375 RPM ceiling, controlled stop before the cut endpoint, and the correct
  reason retained on screen. The complete cycle suite also passed on this final build.

Installed application SHA-256:
`dc20998b0efcb1083cc8473de507fbc43d23686a43a4a54fba6ed7d898befa64`.


## Remaining work

Current coverage and outstanding checks are maintained in
[PORT_PROGRESS.md](PORT_PROGRESS.md). Physical encoder, wiring, drive and cutting
validation remain mandatory before the enable lock can be removed.
