# H5 operations on grblHAL

The touchscreen keeps the original eight operation tabs and calibrated pitch
picker. Every move enters the same grblHAL parser, planner and X/Z pulse driver.
The normal application permits operator axis control; hardware bench suites require
a separate disconnected, enable-locked build.

| Operation | Implemented motion |
| --- | --- |
| Gearbox | Z follows signed spindle pitch between configured bounds. Stops and reversals decelerate and reengage with retained spindle/Z registration. |
| Cone | Gearbox motion with X/Z slope `-ratio/2 × auxiliary-direction sign`, clipped to both axes' bounds. X is radial. |
| Async | Z advances at signed configured mm/s using normal acceleration, with manual override and resume. |
| Turn | Repeated G95 feed-per-revolution Z cuts with linear X depth progression and clearance returns. Acceleration and deceleration stay within the entered Z endpoints. |
| Thread | Phase-gated entry and indexed Z cuts with lead = pitch × starts and phase registration. X reaches depth before the Z-only pass and retracts after Z stops. Preview reports Z endpoints and travel at cutting depth. |
| Face | Repeated X cuts with Z depth progression and clearance; native G95 feed accelerates and decelerates at the specified X endpoints. |
| Cut | Progressively deeper X plunges, returning to the X start each pass; Z remains fixed. |
| Ellipse | Scaled quarter-ellipse X/Z paths per depth, retaining the original spindle-progress parameterization and auxiliary direction. Chords feed native lookahead. |

## Interaction and cancellation

Set machining bounds, pitch/feed, auxiliary direction and applicable depth/start
counts. Profile operations open a geometry preview before RUN. Gearbox, Cone and
Async arm from START and remain armed at a bound. STOP decelerates, discards
queued commands and resets parser state while retaining the stopped position.
There is no automatic recovery retract after cancellation.

Profile cycles may be started with the spindle stopped. Thread positions Z and
waits with X clear before its phase-timed plunge; other profiles position and
infeed, then wait armed for rotation. There is no minimum RPM,
preview-derived RPM ceiling or five-second Thread index timeout. Stopping the
spindle during cutting decelerates and retains the pass/depth. Restart in the
same direction resumes the remaining cut; restart in reverse retraces the cut
and waits at its starting station, without advancing the depth. Thread reacquires
spindle phase at the stopped position. STOP is an explicit cancellation and does
not auto-resume. Interrupted Thread acquisition/plunge retracts and re-arms the
same entry; a stopped Z cut resumes without a new plunge. Turn/Face/Cut/Ellipse
still use G95; Thread uses the native synchronized planner/stepper.

When started at zero RPM, pitch sign selects the initial travel direction as
with the old firmware. A reverse-running spindle at preview/start selects the
opposite travel direction. Actual axis rate/acceleration settings are retained:
Thread waits armed if lead times RPM exceeds its axis maximum, instead of issuing
an impossible G33 or cancelling the whole cycle. A later RPM increase is handled
by native planning; removing policy windows does not make unlimited feed achievable.

During an armed feed, a manual arrow pauses automatic motion, performs the manual
move and resumes after release. Single Step completes one increment; Hold stops on release. Normal manual
override and idle jogging share rates: X 60 and Z 960 mm/min, capped by native
axis settings. Rapids remains a separate idle-jog selection. Z increments in spindle modes round to whole
leads, unless clipped by a bound. The spindle/Z registration survives ordinary
stop/reverse and manual override. Whole revolutions while waiting at a bound do
not accumulate a later catch-up move.

Editing feed, cone ratio or auxiliary direction during an armed feed requests
controlled deceleration, adopts the latest parameters and establishes a new
registration at the stopped position before reengaging. Editing an active
profile's motion parameters cancels it; review the new preview before restarting.
Changing stops, switching tabs or disabling an axis cancels active motion.

Long-press PASSES requests a depth skip after the current depth's starts finish.
The last depth is retained. It never jumps out of a cut halfway through.

## Geometry and operating limits

- The exact calibration remains X 1200 and Z 200 steps/mm, spindle 1200 effective
  counts/revolution; normal-build limits are X 300 and Z 960 mm/min, acceleration 25/100 mm/s².
  Ordinary X jogging stays 60 mm/min; the fourth STEP choice, Rapids, uses each
  axis maximum while held. Version 0.3.25 restores X acceleration to 25 mm/s²
  after the 500 mm/s² trial caused reported stalls; custom settings are preserved.
- X is radial slide travel. X0/Z0 select and set native G54. DRO, limit editor and
  previews use the active core work coordinates and selected mm/in display units.
  Saved endpoints and generated moves retain machine coordinates when zero changes.
  The last saved machine position and native G54 offset are restored at startup.
  This assumes the axes did not move while unpowered; restoration is not homing.
  G18 is the supported arc plane; Y and G76 are rejected.
- Thread waits for phase with X clear, then executes a queued X-only plunge
  and synchronized Z-only pass, without another index wait at depth. X retracts after Z stops. Preview reports commanded
  infeed/retract Z positions and travel at depth; a 10 mm span has 9.995 mm travel
  after the one-step approach. Native acceleration and axis maximum rates remain.
  See [threading](THREADING.md). Clearance is separate from cutting-axis bounds.
- 0.3.18 upgrades saved default Z acceleration from 50 to 100 mm/s² once, through
  native grbl settings; custom tuning and X settings are retained. Bench builds
  keep Z=50. The higher normal-build acceleration needs loaded-machine validation.
- Turn/Face/Cut use feed-per-revolution profiles with acceleration at their endpoints.
  They are not indexed threading cuts. As of 0.3.14, all non-thread profiles also
  omit the one-step approach beyond the cutting-axis bound. The existing 0.5 mm
  tool-clearance retract on the depth axis is retained; Cut keeps Z fixed.
- Ellipse uses a 0.002 mm geometric chord tolerance, 8–256 segments per quarter arc.
  Segment feed represents spindle progress rather than constant path feed.
- Rate checks project the path onto both axes; spindle-synchronization corrections
  use the actual path acceleration and rate limits.
- Gearbox/Cone follow encoder positions below 30 RPM, including hand turning and
  reversal. Targets enter the native accelerated planner without predicting future
  spindle rotation. Above 35 RPM they transition to G33; the 30/35 RPM hysteresis
  prevents repeated switching near the threshold. See [HAND_FOLLOW.md](HAND_FOLLOW.md).
- After manual movement or braking, low-speed feed may show **Waiting for spindle
  phase** until the spindle reaches the retained axis registration (at most one
  revolution). A newly armed feed follows immediately, subject to step resolution.
  Low-speed position following is assisted feed; indexed Thread recipes keep
  G33 phase registration, with all cutting-axis targets inside the Z bounds.
  Profile cycles no longer impose a minimum RPM.
- The supported tracking assumption is physically gradual spindle speed change.
  Sudden synthetic stop/reverse tests exercise cancellation; they do not establish
  loaded tracking capability for arbitrary spindle acceleration.
- Persisted settings include UI pitch, units, operation, depth/start counts, cone
  ratio and sound. From 0.3.24, settled machine positions, machining stops and
  axis-disable selections also persist. Native G54 offsets already persist in
  grblHAL settings; no private display offset is reintroduced. Machine state is
  committed after 500 ms without a state change while the planner, pulse output
  and command queues are idle (including an armed cycle waiting at rest).
  Power loss during motion or before that commit restores the last saved state.
  Armed operations do not restart at power-on. There is no import of stale
  positions from the separate original H5 firmware's NVS partition.

## Bench coverage

`verify_cycles.py` checks bounded Turn moves, Thread phase, geometry and cancellation;
`verify_profiles.py` checks Face/Cut/Ellipse endpoints and queued cancellation;
`verify_follow.py` checks assisted-feed bounds, phase, stop/reverse and override;
`verify_hand_follow.py` checks low-speed positions, reversals, bounds and G33 handoff;
`verify_ui_operations.py` exercises operation buttons, manual gestures, parameter
edits, pass advance and the OTA panel. Read [PORT_PROGRESS.md](PORT_PROGRESS.md)
for which suites passed on the final build. Internal PCNT counts and synthetic
encoder phase cannot substitute for external waveform or loaded-machine checks.
