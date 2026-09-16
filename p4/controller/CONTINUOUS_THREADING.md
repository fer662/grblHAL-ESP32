# Continuous clear-entry threading — introduced in 0.3.19

**Superseded by 0.3.23:** the operator chose the former plunge / Z pass / retract
sequence. See [current Thread behavior](THREADING.md). The implementation below
is retained as historical documentation and is no longer compiled into the port.

## Historical profile — 0.3.22

Thread now approaches X to one native step outside the configured starting-X
surface while Z is stationary. Phase wait and Z run-up happen at that near-surface
position. The synchronized entry/withdrawal covers only the depth stroke plus
that one-step stand-off. After Z stops, X completes the existing 0.5 mm clearance
retract before returning Z. This removes air clearance from the Z transition
budget without waiting for synchronization with X already at cutting depth.

The configured depth-start X limit is treated as the stock surface. There is no
contact sensor or independent stock measurement. The operator must establish
that bound accordingly; the one-step stand-off is commanded geometry, not a
measured clearance allowance for runout or tool/setup error.



The quintic easing curve from 0.3.19/0.3.20 is removed. Each pass now uses its
own acceleration / constant-speed / deceleration transition, sized from its
actual X stroke and the native X speed and acceleration settings. X maximum
remains 5 mm/s; the 0.3.20 trial acceleration remains 500 mm/s².

For Z 0..10 mm, X depth 0..1 mm, 0.5 mm clearance, lead 0.5 mm/rev and a 564 RPM
ceiling, the final full-depth section is Z **1.145..8.860**, length **7.715 mm**.
The first of five passes reaches its depth at Z 0.390 and withdraws at 9.615.
The preview labels entry and withdrawal as final-pass positions and shows the
near-surface pre-position before Z starts and the full retract after Z stops. Version 0.3.21 included the full 0.5 mm air stroke in each synchronized
transition, giving 6.785 mm at full depth. The prior
quintic profile at the same settings reported 4.355 mm (0.3.20); at X acceleration
25 it reported 4.115 mm (0.3.19).

## What the old firmware did

Read from committed H5 `981851b` (`main/modes/ModeTurn.cpp`), without using its
uncommitted experiments. `opSubIndex == 0` feeds the auxiliary/X axis to depth,
then assigns `spindlePosSync`. Only after that wait does sub-index 1 register the
origin and sub-index 2 start spindle-following Z. It retracts X after Z reaches
the end stop. Thus it also waited for synchronization with X already engaged.
`NormalOperationMode.cpp` dispatches Thread to `modeTurn(&z,&x)`.
`posFromSpindle(...,true)` in `main.cpp` clamps the target to the entered stops;
`Axis.cpp` stops issuing pulses when pending position reaches zero. Continuous
cutting does not plan a braking ramp before the final bound. Z starts at 2 mm/s
and then uses the configured acceleration (50 mm/s²); X starts at 1 mm/s.

For the same 0..10 bounds, that older sequence commands all **10 mm at depth**.
It spends no Z distance on X entry or withdrawal because Z is stationary during
both. This is nominal full-depth travel, not proof of correct thread pitch during
start/stop transients. Stationary Z with the spindle turning can cut an annular
mark/groove while X withdraws. The new shorter full-depth region is a consequence
of choosing moving entry/withdrawal inside the same bounds, not a grblHAL limit.
The current 2.285 mm difference consists of two 1.020 mm transitions, 0.120 mm
clear Z run-up, 0.120 mm braking, and the initial 0.005 mm take-up offset.

## Current pass

1. Retract X and position Z one step inside the starting bound, taking up direction.
2. With Z stationary, approach X to one step outside the starting-X surface.
3. Keep X there while waiting for spindle phase and accelerating Z.
4. Feed X in with an acceleration / cruise / deceleration transition while Z continues spindle-synchronous travel.
5. Traverse the full-depth region at the selected lead.
6. Withdraw X while Z is still moving; reach the near-surface position before Z braking.
7. Stop at the opposite Z bound, finish the full X clearance retract, return Z and repeat.

Both Z travel limits remain hard endpoints of this assisted recipe; no run-up or
pullout is placed beyond them. The original 0.5 mm radial clearance, linear depth
passes, internal/external direction and multi-start phase registration remain.
STOP cancels with native deceleration; it is not an automatic clearance retract.

## Programmed geometry, not an estimated quality window

The preview reports run-up, entry, full-depth, withdrawal and stopping stations
in the active core work coordinates. Full-depth length is the distance between
actual programmed end-of-infeed and beginning-of-withdrawal targets. Those
stations vary with pass depth; the preview reports the final pass. There is no settling-time
subtraction or estimated usable-region field. This describes commanded motion,
not stock detection or a measurement of finished mechanical thread accuracy.

For radial stroke D from the quantized near-surface position to the pass depth, X maximum V and X acceleration A, peak speed is
`min(V, sqrt(D*A))`. Acceleration and braking each take `peak/A`; cruise takes
`D/peak - peak/A`. Two X steps are added to sizing D to allow point rounding.
At ceiling Z speed `v = lead * RPM / 60`, these durations become Z distances.
Each acceleration/deceleration portion has four native straight-line chords;
cruise has one. Acceleration intervals round upward to four Z steps, cruise to
one Z step, with a minimum interval of one such group. The actual rounded X
chords are checked in both entry and withdrawal directions. If a chord would
exceed X maximum at the RPM ceiling, only its acceleration or cruise interval
is lengthened, then rechecked. No settling-time or jerk-easing allowance is added.
This remains a step-quantized polygonal approximation executed by the native
planner, including its junction and path acceleration limits.

Z clear run-up/braking uses `ceil(v²/(2aZ) * stepsZ) / stepsZ + one Z step`.
All passes are checked to fit within the entered Z bounds with at least one
full-depth Z step; otherwise the cycle is rejected. The index phase and initial
Z approach are independent of pass depth, preserving thread registration.

Current normal settings are X 300 mm/min and 500 mm/s², Z 960 mm/min and
100 mm/s². Revision-3 boot migration from 0.3.20 preserves custom tuning and
subsequent changes. Disconnected bench profiles retain their former settings.
Motor current, steps/mm, gearing and pulse timing are unchanged. These are
commanded profiles, not a measurement of the installed motor's torque margin.

## Integration with grblHAL

The assisted service submits `$P4THREADPASS` only at its owned cut stage. The
core-thread handler in `main/thread_path.c` verifies idle motion, axis availability,
RPM/direction, start position and planner capacity, then soft-checks every target.
It fills all 21 native planner blocks without yielding before starting motion.
No queue streaming can starve the pass; no second index wait occurs mid-path.
Normal G-code G33 behavior is unchanged. G76 remains disabled.

Each chord uses path lead `Z_lead × chord_length / abs(delta_Z)`. This retains
Z distance per spindle revolution when X moves simultaneously. One small opt-in
core extension, `SPINDLE_SYNC_CONTINUOUS`, carries:

- a continuation flag and accumulated spindle revolutions through planner/ISR blocks;
- the first block's spindle origin through all subsequent blocks;
- the first acceleration phase deficit in revolutions, converted to each block's path lead;
- the correction blend weight, without restarting the settling blend at each chord.

It requires the existing feed-forward, index-origin and path-limit options.
The driver keeps one cumulative Z/encoder trace for the whole pass. All pulse
output, interpolation, acceleration, cancellation and soft-limit checking still
use grblHAL. No LVGL callback or second task generates motor steps.

## Verification and limits

`python3 p4/controller/tests/thread_native_test.py` compiles the production batch
handler with the actual pinned core planner, segment generator, spindle feedback
and step ISR on a host virtual clock, with AddressSanitizer. Hardware/index wait,
GPIO, spindle encoder and task callbacks are simulated. Tests cover 50–500 RPM,
both Z directions, all five pass depths, internal direction, three-start lead,
slow +/-5 RPM/s changes, cancellation before start and during travel, exact
pulse/end positions, X speed, no Z stop inside entry/withdrawal, and spindle phase
through connected blocks. Phase-error assertions now include moving entry and
withdrawal as well as full depth, and remain below 0.02 mm. Geometry tests cover
triangular/trapezoidal profiles, low/high acceleration, rate and RPM, tiny depths,
internal/external cuts, all pass depths, both Z directions and rounded chord rates.

Geometry, emitted-command, preview/units/G54, jog, enable, cancellation and
settings-migration regressions also run. The disconnected hardware bench script
was updated and syntax checked, not executed on the attached machine.

These host checks do not establish P4 ISR wall-clock deadlines, loaded motor
tracking, backlash, actual stock clearance or finished-thread accuracy. No motion
was sent to the lathe and no OTA was performed while implementing this change.
