# Assisted cycle service

All eight operations now have implementations. This document describes the
Turn/Thread geometry and records its earlier validation. See [OPERATIONS.md](OPERATIONS.md)
for Face, Cut, Ellipse, Gearbox, Cone, Async, parameter edits and the current
operating limits. See [PORT_PROGRESS.md](PORT_PROGRESS.md) for final regression status.

## Change in 0.3.16: consistent preview coordinates

Cycle previews now use the main-screen zero and selected mm/in units for every
position, including approach, retract and the estimated thread region. Lengths
and lead only change units. For example, machine Z=-1.5..8.5 with a +1.5 mm
readout offset appears as Z=0..10 in both the limit buttons and preview.
The underlying plan and emitted machine-coordinate moves are unchanged.

Actual LVGL previews: [Turn in mm](docs/turn-cycle-0316.png),
[Thread in inches](docs/thread-inch-0316.png).

### G53, G54 and the touchscreen zero

The grblHAL core supports G53 (machine coordinates for one motion block) and
G54/G55/etc. (persistent work-coordinate offsets). The touchscreen X0/Z0 actions
currently set UI-local `originPos` values, separate from those core offsets.
Assisted cycles position with G53 and cut with relative G91 moves, so their
physical targets do not depend on the selected G54/G55 offset. This update only
changes how their preview is presented; it does not make X0/Z0 program G54.
Without homing, the current internal machine reference is not a repeatable
physical machine datum across power cycles.

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
   approach position, stopping endpoint, estimated usable thread region, retracted
   X position and maximum RPM. All
   coordinates in this preview use the **main-screen zero and selected units**.
   The motion plan and diagnostic commands continue using machine millimeters.
4. RUN BENCH CYCLE copies the configuration to the grbl task. The status line
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
4. Move X to the current depth.
5. Register the spindle phase for this start and select expected spindle direction.
6. Execute one straight-Z G33 move to the opposite Z bound. Run-in and braking
   consume part of this span; the estimated steady-pitch region is shorter.
7. Retract X, return Z to the start bound at clearance, and take up one step inward.
8. Repeat all starts at the same depth before increasing depth.
9. After the final pass, return Z to the cutting start and X to its initial bound.
10. Restore zero diagnostic phase, absolute distance mode and feed-per-minute mode.

M3/M4/M5 only describe the external spindle to the controller; they do not switch
or regulate the physical lathe spindle. Feed direction is signed pitch multiplied
by observed spindle direction. Thread lead is `abs(pitch) * starts`; Turn uses
one start. X remains radial, including depth and clearance.

### In-bound run-in, phase and run-out

The default preview RPM ceiling is 125% of current measured speed, capped at
88% of the Z maximum-feed/pitch ratio. It never silently scales thread pitch.
The cycle refuses to start if current RPM exceeds that ceiling or is below the
bench encoder minimum of 30 RPM. During execution, leaving that RPM range or
changing spindle direction cancels the cycle. Existing encoder stall/reversal
fault handling still protects an active synchronized cut.

At the ceiling RPM, with velocity `v = lead * RPM / 60` and Z acceleration `a`:

- Acceleration deficit `L = v² / (2a)`.
- Lead-in `max(2 * lead, 4 * L + 0.25 * v) + 0.01 mm`.
- Run-out `L + 0.25 * v + 0.01 mm`.

Distances are rounded upward to Z steps. They remain fixed for every pass in
the cycle. The earlier backend performs acceleration phase compensation using
the actual prepared profile. The cycle's requested start phase adds the inward approach offset divided by
lead, so registration remains referenced to the **entered start bound**. Changing
the RPM ceiling changes the estimated usable region, not that phase reference. Each multiple start uses its own rounded fraction
of a revolution, avoiding accumulated rounding error for counts such as seven
starts. See [spindle tracking](SPINDLE_TRACKING.md) for the slew assumption and
scope of the backend measurements.

For Thread, `approach = start_bound + direction / steps_per_mm`, `takeup =
start_bound`, and `finish = end_bound`. The estimated steady region starts at
`approach + direction * run_in` and ends at `finish - direction * run_out`.
These margins reduce usable length; they never enlarge the Z target span.

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
