# Assisted Turn and Thread cycles

This is the first assisted-cycle service on top of the validated straight-Z
spindle backend. It implements **Turn** and **Thread**, including repeated
radial infeed and multiple starts, in the disconnected, enable-locked bench
application. Async feed and the existing jog controls remain available.
Gearbox, Cone, Face, Cut and Ellipse remain required migration work; their START
buttons remain unavailable until their motion semantics have been implemented
and tested. This is not a completed migration of all eight operations.

## Operator workflow

1. Select Turn or Thread, set signed feed/pitch, depth passes, X and Z machining
   bounds, and the auxiliary direction. Thread also uses the starts setting.
2. With both axes available and the spindle encoder running, press START.
3. Review the cycle preview. It shows radial X coordinates, cutting Z bounds,
   approach position, run-out end, retracted X position and maximum RPM. All
   coordinates in this preview are explicitly **machine coordinates in mm**,
   independent of the readout's display origin or selected display units.
4. RUN BENCH CYCLE copies the configuration to the grbl task. The status line
   shows stage, pass and start. STOP cancels the cycle with controlled braking;
   a partially cut thread cannot be resumed with cycle-start.

The preview is a real LVGL panel; its RUN button requests a copied configuration.
A second validation on the grbl task checks current position, controller state,
RPM, and motion settings before moving. Active coordinate scaling or rotation
is rejected; ordinary work offsets, metric/imperial and diameter modes cannot
change the generated machine-coordinate path. Parameter changes cannot mutate a
running recipe. Changing machining stops, disabling an axis, switching modes,
requesting firmware update or attempting to jog cancels the active cycle.

## Preserved and explicit geometry

The committed H5 baseline's `main/modes/ModeTurn.cpp` supplies the pass order,
linear radial depth progression, auxiliary-direction choice, 0.5 mm clearance,
one-step Z backlash approach, return-to-start behavior and multiple-start lead.
The new service uses grblHAL for every move; no old step/task synchronization
logic or uncommitted H5 motion experiment is used.

The generated sequence is:

1. Set metric/radial coordinates and the XZ plane.
2. Retract X to its clearance position before moving Z to the approach.
3. Approach Z from the cutting direction, including the original one-step takeup.
4. Move X to the current depth.
5. Register the spindle phase for this start and select expected spindle direction.
6. Execute one straight-Z G33 move through lead-in, cutting area and run-out.
7. Retract X, return Z at clearance, and take up Z in the cutting direction.
8. Repeat all starts at the same depth before increasing depth.
9. After the final pass, return Z to the cutting start and X to its initial bound.
10. Restore zero diagnostic phase, absolute distance mode and feed-per-minute mode.

M3/M4/M5 only describe the external spindle to the controller; they do not switch
or regulate the physical lathe spindle. Feed direction is signed pitch multiplied
by observed spindle direction. Thread lead is `abs(pitch) * starts`; Turn uses
one start. X remains radial, including depth and clearance.

### Lead-in, phase and run-out

The default preview RPM ceiling is 125% of current measured speed, capped at
98% of the Z maximum-feed/pitch ratio. It never silently scales thread pitch.
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
the actual prepared profile. The cycle's requested start phase also subtracts
`lead-in / lead` revolutions, so the thread reference is at the **cutting start**,
not the approach position. Each multiple start uses its own rounded fraction
of a revolution, avoiding accumulated rounding error for counts such as seven
starts. See [spindle tracking](SPINDLE_TRACKING.md) for the slew assumption and
scope of the backend measurements.

Machining bounds describe the cutting area. They are **not universal travel
limits**: clearance, takeup, lead-in and run-out deliberately extend beyond them.
The preview exposes those extensions. Preflight limits the full requested span,
including the current position, to the inherited 100 mm X and 300 mm Z values.
These span checks are not a homed machine envelope or proof of chuck, shoulder
or workpiece clearance. Physical travel constraints and clearance validation
remain required before connecting the machine or removing the enable lock.

## Execution and cancellation

`cycle_plan.c` is pure geometry shared by preview and execution. `main/cycle.c`
runs transitions only on the grbl task. It submits one line through the normal
bridge/parser, waits for that exact command's acknowledgment, and then requires
an empty planner and actual idle state before advancing. G33 acknowledgment
already waits for the synchronized move to finish; acknowledgment of a rapid
alone is not considered motion completion.

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
$P4CYCLE=thread,pitch,passes,starts,aux_forward,x_min,x_max,z_min,z_max,rpm_limit
```

`thread` and `aux_forward` are 0/1. For example, on the disconnected simulator:

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
```

Device regression:

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

## Device results, 2026-09-14

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

- Preserve Gearbox engagement/reversal/stop semantics and Cone coupling.
- Validate synchronized X/tapered paths before enabling Face, Cut or Cone.
- Implement Ellipse with its original spindle-progress and pass-scaling semantics.
- Expose deliberate pass advance at the cycle API boundary; no mid-cut skip is
  currently offered by this first service.
- Complete Async manual override/resume semantics, persistent preferences,
  TMC5160 SPI, sound, Wi-Fi and OTA migration.
- Establish the physical machine envelope, inspect actual encoder/pulse signals,
  and test loaded motion and thread entry/exit before enabling motor outputs.
