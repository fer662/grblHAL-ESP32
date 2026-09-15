# Spindle tracking and acceleration phase compensation

This extends the earlier [spindle bench record](SPINDLE_VALIDATION.md). The
controller remains an isolated, motor-enable-locked P4 bench application.

## Operating assumption

The owner confirmed that spindle speed changes can be assumed slow enough for
the axes to follow. For straight Z threading:

`required acceleration [mm/s²] = pitch [mm/rev] × spindle slew [RPM/s] / 60`

At 2 mm pitch, 100 RPM/s requires 3.33 mm/s², against the unchanged Z setting of
50 mm/s². Smooth ramps within that envelope are acceptance tests. Instantaneous
speed jumps remain optional diagnostics, while stall/reversal tests check fault
handling. These tests do not establish the real spindle's maximum slew or the
motor's loaded capability. Required feed must also stay within the axis speed
limit; the acceleration assumption does not remove that limit.

## Changes

grblHAL still owns path planning, acceleration profiles, interpolation, step
counts, cancellation and the motion state machine. The P4 opts into extensions
in its core fork using `SPINDLE_SYNC_FEED_FORWARD` and
`SPINDLE_SYNC_INDEX_ORIGIN`. Other drivers retain their existing behavior when
those options are disabled.

1. **Measured RPM sets the base feed.** The position loop corrects residual
   phase error instead of having to create a position error to change speed.
   Corrections are converted from millimeters to AMASS timer events, preserving
   fractional events. This makes gain independent of the number of internal
   timer ticks per motor step. P defaults to 0.25; I/D remain zero.
2. **Velocity profiles follow RPM.** The existing grblHAL G95 profile-update
   mechanism also runs for synchronized moves. This lets the planner start
   deceleration at the appropriate distance for the current speed. Once braking
   begins, RPM updates stop replanning it: its target is now standstill. The initial
   feed-forward prototype tracked the cut but retained the old stopping profile;
   timestamps exposed a roughly 170 mm/s² sampled slowdown at the end.
3. **The phase reference stays fixed.** Segment targets use completed path
   distance plus a fixed initial acceleration deficit. Updating the velocity
   profile therefore does not move the thread's phase reference.
4. **Acceleration phase is compensated.** For a start from rest, the spindle
   travels ahead of the axis by `L = v²/(2a)` during acceleration. The P4 advances
   the encoder-derived start phase by `L/pitch` revolutions, using the offset
   calculated from the actual prepared profile. The core references the integer
   revolution boundary, removing variable startup scheduling delay from that
   reference. Acceleration-aware phase registration is also described in
   [LinuxCNC's G33 documentation](https://www.linuxcnc.org/docs/2.8/html/gcode/g-code.html#_g33_spindle_synchronized_motion).
5. **Correction blends in after acceleration.** Phase correction increases from
   zero to full strength over the first 100 ms of cruise. This avoids adding a
   correction speed jump at the transition out of full acceleration. Corrected
   segment velocity is also limited using the previous segment's velocity,
   duration, and the configured Z acceleration. The spindle-slew assumption
   does not by itself constrain a software-generated correction.

At 300 RPM, the uncompensated deficit is 0.25 mm at K1 and 1 mm at K2. Changing
the encoder-derived start phase compensates this deficit; it does not change
steps/mm, spindle gearing or pitch calibration.

## Lead-in and run-out

`$P4SYNC` now includes:

- `INDEX_PHASE`: the acceleration-compensated encoder phase used for the pass.
- `COMP_MM`: the prepared profile's acceleration deficit.
- `LEAD_MM`: a conservative bench settling distance,
  `max(2 × pitch, 4 × COMP_MM + 0.25 × v)`, with v in mm/s.

At 300 RPM this gives 1 mm for K0.5, 2.25 mm for K1, and 6.5 mm for K2. These
distances are checked in the ramp tests, not presented as universal machine
clearances. A cutting pass needs space for both lead-in and deceleration after
the thread ends. The test moves are 20 mm long and evaluate cutting from the
reported lead distance through 16 mm, leaving 4 mm for run-out.

The assisted-operation service must still place these sections in actual machine
coordinates, respect machining stops and available clearance, and preserve a
common thread reference when choosing approach positions for repeated passes.
No touchscreen threading cycle is enabled yet. Physical encoder and loaded
motor validation are still required before machine use.

## Tests

```sh
python verify_spindle_tracking.py PORT
python verify_spindle.py PORT
python verify_motion.py PORT
```

The tracking test runs 13 passes, covering K0.5/K1/K2, 150/300/450 starting RPM,
30/100 RPM/s rising and falling ramps, reverse spindle/Z direction, multi-start
phase offsets, and ramps beginning during index waiting or near deceleration.
It verifies the default gain, actual GPIO pulse counts, phase relative to the
requested spindle reference, and continuing UI updates. It checks the reported
lead-in distance rather than assuming every part of the move is a valid cut.

`$P4SIMRAMP=target_rpm,slew_rpm_per_second,delay_ms` schedules a same-direction
ramp on the disconnected bench. The simulator uses a 10 MHz timer and applies
period changes at quadrature edges without resetting elapsed phase. Requested
RPM is still quantized by timer resolution; the controller reads the resulting
GPIO encoder counts. The model is not a simulation of spindle mechanics.

`$P4SYNCTRACE` now records `step,encoder_count,timestamp_us` for the first pulse
and every 16th pulse, up to 512 samples. Host tests estimate acceleration from
these pulse intervals. During the evaluated cut, sampled acceleration must
remain below the 50 mm/s² Z setting. Whole-move acceleration uses 64-step windows:
at higher feeds, 16 steps take less than one planner segment and can overstate
the normal staircase changes. The whole-move regression bound is 60 mm/s²,
allowing sampling margin around the nominal 50 mm/s² profile. The earlier
end-of-cut defect exceeds this bound even with the same 64-step estimator.
That margin is not an increased axis acceleration setting or a proof of
instantaneous mechanical acceleration; external waveform/load checks remain.

The older suite's `--abrupt` flag runs speed jumps separately. It is not part
of acceptance under the owner's spindle-slew assumption. Stall, reversal and
missed-deadline fault checks remain mandatory bench regressions.

## Installed build results, 2026-09-14

Built with the existing ESP-IDF 5.5.2 environment and flashed app-only through
USB at `0x10000`; esptool verified the image hash. The new application remains
installed. Core pin: `17c13030ab8a7317943bf54ae1a55d3c57f139bd` on
`fer662/grblHAL-core:codex/spindle-tracking`, above the separately preserved
fractional-time fix `44aad88`. See [upstream updates](README.md#upstream-updates)
for the update workflow.

- All 13 tracking cases passed. Peak encoder-equivalent phase error across
  evaluated cutting windows was **0.0075 mm**; maximum phase-error span was
  **0.0100 mm**. Each pass selects its arbitrary whole-revolution origin once,
  preserving continuous phase so a later whole-turn slip cannot be hidden.
  This strengthened continuous-phase assertion was also checked offline against
  all 13 complete traces captured in the final hardware run.
- Maximum sampled cutting-window acceleration was **27.260 mm/s²**. Maximum
  whole-move acceleration with the documented 64-step estimator was
  **51.630 mm/s²**, below the 60 mm/s² regression bound. The configured Z
  acceleration remains **50 mm/s²**.
- All **104,000 Z pulses** in the tracking suite, including return moves,
  matched GPIO PCNT counts. Fault, overlap, late and RX-overflow counters stayed
  zero. Internal pulse service spanned 15.3–28.8 us; the longest measured core
  callback was 40 us. UI updates continued throughout the roughly 94-second run.
- The motion regression passed with X **4,596** and Z **47,324** issued pulses
  matching PCNT counts. The nine-cut steady spindle regression also passed.
- Separate stall, reversal and deliberately missed-deadline tests each stopped
  STEP output and latched the expected fault. Hardware resets cleared those
  test faults; the final firmware session is not left faulted.
- Three USB connection checks, two screen captures, and a subsequent 15-second
  persistent connection passed with continuing UI updates. The 1,000-interrupt
  floating-point preservation test passed as well.

These are MCU timestamp and GPIO-count measurements on a disconnected tablet.
They do not measure loaded motor acceleration, actual thread accuracy, external
pulse shape, or the physical spindle encoder. The next application step is to
use this backend in assisted cycles with explicit approach, lead-in, cut,
run-out and return geometry while preserving the original operation semantics.
