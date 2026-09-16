# Continuous clear-entry threading — introduced in 0.3.19

## Acceleration update in 0.3.20

Normal X acceleration increases to 500 mm/s² for the requested machine trial;
X maximum stays 5 mm/s. The entry curve and fixed stations across depth passes
are unchanged. For the example below, full-depth Z becomes 2.825..7.180
(length 4.355 mm). This modest length increase reflects the curve's peak-speed
constraint at 5 mm/s; it does not mean the acceleration setting was ignored.
Rapids provides a direct native-planner move to assess acceleration separately.
Version 0.3.19 settings and its example below describe the prior release.

## What the old firmware did

Read from committed H5 `981851b` (`main/modes/ModeTurn.cpp`), without using its
uncommitted experiments. `opSubIndex == 0` feeds the auxiliary/X axis to depth,
then assigns `spindlePosSync`. Only after that wait does sub-index 1 register the
origin and sub-index 2 start spindle-following Z. It retracts X after Z reaches
the end stop. Thus it also waited for synchronization with X already engaged.

## Current pass

1. Retract X and position Z one step inside the starting bound, taking up direction.
2. Keep X at clearance while waiting for spindle phase and accelerating Z.
3. Feed X in along a programmed curve while Z continues spindle-synchronous travel.
4. Traverse the full-depth region at the selected lead.
5. Withdraw X while Z is still moving; X reaches clearance before final Z braking.
6. Stop at the opposite Z bound, return at clearance, repeat starts/passes.

Both Z travel limits remain hard endpoints of this assisted recipe; no run-up or
pullout is placed beyond them. The original 0.5 mm radial clearance, linear depth
passes, internal/external direction and multi-start phase registration remain.
STOP cancels with native deceleration; it is not an automatic clearance retract.

## Programmed geometry, not an estimated quality window

The preview reports run-up, entry, full-depth, withdrawal and stopping stations
in the active core work coordinates. Full-depth length is the distance between
actual programmed end-of-infeed and beginning-of-withdrawal targets. Those
stations are identical across passes; X depth changes. There is no settling-time
subtraction or estimated usable-region field. This describes commanded motion,
not stock detection or a measurement of finished mechanical thread accuracy.

The entry/exit shape is `s(t) = 10t³ - 15t⁴ + 6t⁵`, approximated by 12 straight
native planner blocks per ramp. Points are rounded to real X/Z steps before
computing each block's path lead. Each Z chord contains an integral, equal number
of Z steps. The native planner handles junction speeds and acceleration.

At ceiling Z speed `v = lead × RPM / 60`, clear run-up/braking length is
`ceil(v²/(2aZ) × stepsZ) / stepsZ + one Z step`. For worst-pass radial stroke D,
entry time must be at least `max(1.875 D/vX, sqrt((10/sqrt(3)) D/aX))`, based on
the shape's peak slope and curvature. A further `12/(stepsX × vX)` accounts for
one X step of point-rounding error per chord. Entry length is `v × time`, rounded
up to 12 Z steps. The same interval is reserved for withdrawal. At least one
full-depth Z step must fit between them; otherwise the cycle is rejected.

Normal-build X maximum rate is now 300 mm/min (5 mm/s), up from 60 mm/min.
The former value came from H5's manual-jog speed, not a measured motor limit.
Manual X jogging still requests 60 mm/min; X acceleration stays 25 mm/s².
This also raises the available X rate for other planned moves, including rapids
and X cutting feeds; their requested feed and acceleration limits still apply.
Motor current, steps/mm, gearing and pulse timing are unchanged.

A boot-only native settings upgrade (`motion_rev=2`) changes saved X max rate
from the old 60 default to 300. Custom rates survive. Revision 1's Z migration is
not repeated, and explicit tuning after revision 2 survives future boots.
Disconnected bench builds retain X 60 and Z acceleration 50 without migration.

For X=0..1 plus 0.5 mm clearance, Z=0..10, lead 0.5 and a 564 RPM ceiling,
the new defaults produce full-depth Z=2.945..7.060 (4.115 mm). X begins entering
at Z=0.125 and reaches clearance again at Z=9.880; Z stops at 10. Entry duration
is now constrained by X acceleration rather than its former 1 mm/s rate cap.
Reducing spindle RPM leaves more full-depth length inside the same bounds.
The 5 mm/s maximum has passed host motion tests but still needs loaded-machine
validation; this change does not claim a measured maximum for the installed motor.

## Integration with grblHAL

The assisted service submits `$P4THREADPASS` only at its owned cut stage. The
core-thread handler in `main/thread_path.c` verifies idle motion, axis availability,
RPM/direction, start position and planner capacity, then soft-checks every target.
It fills all 27 native planner blocks without yielding before starting motion.
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
both Z directions, shallow/deep passes, internal direction, three-start lead,
slow +/-5 RPM/s changes, cancellation before start and during travel, exact
pulse/end positions, X speed, no Z stop inside entry/withdrawal, and spindle phase
through connected blocks. The full-depth phase error is below 0.02 mm in the
assertions (observed around 0.003 mm or less for these cases).

Geometry, emitted-command, preview/units/G54, jog, enable, cancellation and
settings-migration regressions also run. The disconnected hardware bench script
was updated and syntax checked, not executed on the attached machine.

These host checks do not establish P4 ISR wall-clock deadlines, loaded motor
tracking, backlash, actual stock clearance or finished-thread accuracy. No motion
was sent to the lathe and no OTA was performed while implementing this change.
