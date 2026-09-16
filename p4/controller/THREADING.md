# Thread cycle — 0.3.24

Restores the operator-requested H5 order using native grblHAL G33:

1. Retract X to the configured 0.5 mm clearance.
2. Return Z to the starting bound, then take up one Z step inside it.
3. Move X to this pass's cutting depth with Z stationary.
4. Register the spindle phase, wait for synchronization, and execute one Z-only
   G33 move to the opposite Z bound. X stays at cutting depth throughout.
5. After Z stops, retract X to clearance and return Z for the next pass/start.

For entered Z 0..10 mm, the default 200 Z steps/mm gives approach Z 0.005 and
finish Z 10: **9.995 mm of Z travel at cutting depth**. Reverse travel runs from
9.995 to 0. The preview displays these commanded endpoints and length, first/final
X depth, and retract target in native work coordinates and the selected units.
There is no moving-entry deduction, estimated usable-thread region, or inferred
stock-contact surface.

The native planner retains speed and acceleration limits. Z accelerates and
brakes with X engaged. There is no preview RPM window or acceleration-distance
acceptance test. Geometry must fit at least the takeup step and one cutting step.
The cut waits armed if its requested lead times RPM exceeds the actual axis maximum;
there is no additional 10% rate headroom. The sequence
matches old H5, while pulse timing and acceleration remain grblHAL-controlled.
Multi-start lead/phase, depth progression, internal/external direction, bounds,
G54, cancellation, Rapids, and the 5 mm/s / 500 mm/s² X trial settings are retained.

The custom `$P4THREADPASS` command, batch executor and moving-entry geometry have
been removed. The pinned core still contains its optional continuous-block patch,
but this port no longer enables it. Thread uses the standard G33 execution path.

## Stopped spindle and reversal

RUN may position and plunge X with the spindle off; the Z cut waits for rotation.
Index acquisition has no five-second timeout in the P4 build. Spindle stop during
a cut retains depth/pass/start after native deceleration. Restart reacquires
phase from the stopped Z coordinate without replaying X infeed. Reverse rotation
retraces toward the original approach; it does not complete or advance the pass.
At the approach it waits for forward rotation. Explicit STOP cancels and cannot
automatically resume. The external switch still physically controls the spindle.

## Verification

Host command-emitter checks confirm X reaches depth at stationary Z, the cut is
Z-only, and X retract follows the cut without changing Z. Geometry and preview
tests cover direction, starts, bounded endpoints, minimum step geometry,
work coordinates and units. Rapids/jog, cancellation and axis-disable regressions
remain passing. The actual core planner/segment/step ISR simulation runs one
synchronized Z block at each depth, including RPM ramps and cancellation; it
asserts no X pulses, exact final Z and pulse count, and steady-region phase error.
The parser, physical spindle/index, task timing and loaded mechanics are outside
that virtual-clock test. Firmware is built; no OTA or on-machine motion was sent.

Previous moving-entry work is recorded in [CONTINUOUS_THREADING.md](CONTINUOUS_THREADING.md)
and git history; it is superseded by this sequence.
