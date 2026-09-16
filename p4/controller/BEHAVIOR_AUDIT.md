# Assisted-operation behavior audit

Compared on 2026-09-16:

- Old committed H5 firmware: `981851b` in `projects/H5`. Uncommitted motion-planner experiments excluded.
- Current port: `ec7cee8`, firmware 0.3.23. This build restores plunge / Z pass / retract threading.
- Last confirmed tablet installation in this task: 0.3.19. Subsequent builds, including 0.3.23, have not been installed.

This is a source comparison, not a loaded-machine test or proof of complete behavioral equivalence. No firmware changes or machine commands were made for this audit. Paths below are relative to this controller directory unless prefixed with `H5:`; those references mean the committed old source above.

## Follow-up in 0.3.24

The user requested removal of profile RPM-window policies, correction of manual
override speed, and restoration of saved positions/limits/disable selections.
Those changes are implemented in 0.3.24; Turn/Face G95 behavior was accepted.
Spindle stops now retain the active pass; reversal retraces after controlled
braking. Explicit STOP still cancels. See `OPERATIONS.md` for current behavior.
The findings below remain a historical comparison against 0.3.23, not a claim
that all listed differences remain in 0.3.24. Ellipse returns, implicit unset
bounds and live-edit policies have not been changed by this follow-up.

## Material changes beyond the moving-X threading path

### 1. Profile cycles require a running spindle and impose an RPM window

Turn, Thread, Face, Cut and Ellipse require at least 30 RPM. The preview chooses an upper bound from the smaller of the measured RPM times 1.25 (rounded up) and 88% of the cutting-axis rate divided by lead. Planning additionally rejects feeds above 89% of the axis maximum. Ellipse has additional per-axis checks.

During a cycle, falling below 30 RPM, reversing the spindle, exceeding the chosen ceiling or entering feed hold cancels the operation. It does not retain a paused pass for later resumption. A Thread pass must also have enough distance to accelerate and decelerate at the ceiling feed, with at least one step at feed.

The old operations progressed from encoder counts and had no equivalent 30 RPM / 125% window. They could be armed with the spindle stopped and progressed slowly. Their reversal behavior was encoder-driven rather than this cancellation policy.

The numerical window and cancellation policy are port choices, not requirements imposed by grblHAL.

Sources: `components/h5_ui/ui.cpp::preview_cycle`, `components/h5_ui/cycle_plan.c::h5_cycle_plan`, `main/cycle.c::poll_cycle`; H5: `main/NormalOperationMode.cpp::update`, `main/modes/ModeTurn.cpp::modeTurn`, `main/main.cpp::modeCut/modeEllipse`.

### 2. Turn and Face no longer register every pass to spindle angle

Old Turn, Face and Thread all called `modeTurn`, which waited for spindle phase before advancing the cutting axis. The port uses indexed G33 only for Thread. Turn and Face use G95 feed per revolution. Their nominal feed per revolution is retained, but their passes do not have the same angular registration guarantee.

The port also chooses the starting end using both pitch sign and measured spindle direction. The old profile start/end selection used the captured pitch sign alone. Reversed-spindle startup therefore is not behaviorally equivalent.

Sources: `components/h5_ui/cycle_plan.c::h5_cycle_plan`, `main/cycle.c::emit`; H5: `main/NormalOperationMode.cpp::update`, `main/modes/ModeTurn.cpp::modeTurn`.

### 3. Unset limits are replaced with implicit bounds in continuous assisted modes

Gearbox, Cone and Async construct finite bounds when a stop is unset. With both stops unset, these are X +/-50 mm and Z +/-150 mm around the position when START is pressed. With only one stop set, the fallback is asymmetric: a missing minimum uses current position minus half the travel span; a missing maximum uses minimum plus the full span.

These fallback bounds are not entered into the limit buttons. Motion can therefore stop at a bound the screen still shows as unset. The old automatic Gearbox/Cone paths clamped against explicitly set stops and did not substitute this centered envelope for unset stops.

Sources: `components/h5_ui/ui.cpp::buttonOnOffPress`, `main/follow.c`; H5: `main/modes/GearBoxMode.cpp`, `main/main.cpp::posFromSpindle/modeCone`.

### 4. Ellipse has a changed return path

Old Ellipse returned X to its starting bound between passes. After the last pass it returned X only, leaving Z at the cutting end.

The generic port cycle retracts X an additional 0.5 mm beyond that starting bound and returns Z to the start, including at completion. This is a material path change, not merely smoother acceleration, and should be treated as an unrequested discrepancy.

The nominal scaled quarter-ellipse shape and orientation were retained. Its execution changed from continuously computed encoder targets to G95 line segments, using a nominal 0.002 mm chord tolerance with 8–256 segments.

Sources: `components/h5_ui/cycle_plan.c::h5_cycle_plan/h5_cycle_point`, `main/cycle.c::emit` stages 1/8, 2/9 and 11/12; H5: `main/main.cpp::modeEllipse`.

### 5. Running edits and pass advancement behave differently

Changing profile pitch, direction/infeed selection, starts or passes now cancels the active cycle. Stop-setting callbacks also cancel motion; the limit editor and work-zero actions require idle conditions. The old firmware applied some changes during operation: same-sign pitch changes could continue Turn/Face, while pass-count changes were rejected while running. Old behavior depended on the particular parameter and mode.

The new pass-advance request waits until all starts at the current depth complete before skipping a later depth. Old Turn/Face/Thread could advance the depth index during an ongoing cutting pass.

Gearbox/Cone/Async parameter changes use a controlled stop and restart/reanchor sequence, rather than the old immediate target/origin updates. START also opens a preview before running a profile, instead of immediately arming it.

Sources: `components/h5_ui/ui.cpp::apply_feed_edit/setTurnPasses/setStarts/setLeftStop/setRightStop`, `main/cycle.c::poll_cycle`, `main/follow.c`; H5: `main/main.cpp::applyDupr/applyStarts/setTurnPasses/buttonOnOffPress`, `main/modes/ModeTurn.cpp::modeTurn`.

### 6. Manual override and return speeds have changed

Idle normal X jogging requests 60 mm/min (1 mm/s). Manual X override while Gearbox/Cone/Async is armed instead requests the configured axis maximum: 300 mm/min (5 mm/s) with the new settings, even when Rapids is not selected. The UI rejects selecting Rapids during an armed feed, making this particularly inconsistent.

The old manual-move code capped speed at the manual speed setting, including during overrides. Using the axis maximum here is an unintended consequence of increasing that maximum.

Profile positioning and return moves also use G0, so raising the axis maximum changes these moves as well as Rapids jogging. Old profile positioning used the manual speed setting. The port initially clears the depth axis before positioning the cutting axis; old Turn/Face/Thread could move both axes toward their initial positions together.

Sources: `components/h5_ui/ui.cpp::h5_ui_jog`, `main/follow.c` manual-jog branch, `main/cycle.c::emit`; H5: `main/main.cpp::getStepMaxSpeed/taskMoveX`, `main/modes/ModeTurn.cpp::modeTurn`.

### 7. Power-cycle restoration is different

Old H5 stored and restored axis positions, display origins, machining stops and each axis's disabled selection. The port does not restore machine positions, machining stops or disabled selections. Neither firmware restores an armed operation.

Native G54 offsets **do** persist in the port's grblHAL settings. Because the machine position reference is not restored, persistence of the offset alone does not preserve its relationship to the physical workpiece after power-up.

The persistence paragraph in `OPERATIONS.md` is stale where it says readout zeros are not restored. `ASSISTED_CYCLES.md` correctly describes native G54 offset persistence and the need to re-establish the physical reference.

Sources: `components/h5_ui/preferences.h`, `components/h5_ui/ui.cpp` axis initialization, `main/driver.c` disabled-state initialization and work-zero command, `main/storage.c::read_core/write_core`; H5: `main/main.cpp::saveIfChanged/setup`.

## Requested or explicitly accepted changes

- X0/Z0 now set native G54 rather than private display offsets.
- Cutting-axis movement must stay within the entered bounds; the old one-step outside-start takeup was removed. Thread starts one Z step inside the bound.
- Separate Hold / Single Step, Jog Limits toggle/editor, Rapids selection and the larger touchscreen layout.
- Higher motion settings discussed with the user: X maximum 5 mm/s, X acceleration 500 mm/s² and Z acceleration 100 mm/s² in the latest build. These are configured motion settings, not loaded-machine performance measurements.
- Replacing the old pulse-generation logic with grblHAL planning means enforcing rate and acceleration limits, including braking. The old encoder-following code did not provide equivalent planned endpoint deceleration; Async used a fixed-period interrupt. This general change was part of the requested refactor, but does not justify unrelated changes to operation sequences.

## Smaller numerical differences

- Pass-depth division now uses a fractional calculation before motor-step quantization. Old Turn/Cut divided integer step spans first, biasing some intermediate depths when the span did not divide evenly by the pass count. Final requested depth is unchanged.
- Multi-start phase calculation rounds each start separately rather than multiplying a rounded phase increment, avoiding cumulative rounding.
- The new planner rejects excessively large coordinate values and cycles whose total span, including clearance and initial position, exceeds 100 mm X or 300 mm Z.

## What the threading restoration does and does not establish

Version 0.3.23 removes the moving-X entry/withdrawal sequence. It plunges X, executes the Z-only synchronized pass, then withdraws X after Z stops. It does not revert the other policies listed above.

For a 10 mm span, 9.995 mm is the programmed Z travel with X at cutting depth, given the one-step inward start. This is not a measured guarantee of perfect thread pitch across all 9.995 mm: acceleration and synchronization still affect the ends. Source inspection and geometric tests cannot establish loaded thread quality.

## Recommended review order

1. Restore the old Ellipse return path and make normal manual-override speed consistent with normal jogging.
2. Explicitly decide the stopped-spindle, reversal, RPM-window and unset-limit behaviors rather than treating them as inherent planner requirements.
3. Agree which settings and physical reference information should survive reboot, and how live edits/pass advancement should work.
4. Verify each operation's path and controls against the committed baseline before calling it behaviorally equivalent.
