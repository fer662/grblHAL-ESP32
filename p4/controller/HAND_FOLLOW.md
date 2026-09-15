# Slow spindle and hand following

Gearbox and Cone retain the calibrated encoder-position relationship below the
G33 acquisition range. The touchscreen and USB commands use the same service.
There is no minimum RPM in the position-following algorithm: each measured count
can change the requested position, subject to the physical step resolution.
The disconnected simulator can generate 1–600 RPM in either direction.

## Motion path

The original relationship is `Z = reference_Z + encoder_turns × signed_pitch`.
Cone adds `X = reference_X + (Z - reference_Z) × slope`, with the existing ratio
and auxiliary direction. Both axes' bounds clip the resulting line.

The service sends absolute machine-coordinate G1 endpoints at most every 20 ms,
with at most two blocks retained in the native planner, plus its fixed step
segment buffer. It never extrapolates a future spindle angle. grblHAL supplies
acceleration, axis rate limits, interpolation and reversal braking. A stopped
spindle leaves a finite target; the axes finish that target and remain still.
This is a position follower with quantization and dynamic lag, not zero-latency
gearing. Large leads and fast changes can create acceleration-limited lag.

Powered operation retains G33. Position following hands over at 35 RPM; G33
hands back below 30 RPM. The transition decelerates first. Manual override uses
the same cancellation and reset path, and still retains whole-lead Z increments.
After braking/manual movement the axis waits for its registered spindle phase,
in either direction, before position following resumes. Only whole revolutions
are discarded during registration. A newly armed operation establishes its
reference immediately and does not wait for a full revolution.

At a bound, complete excess revolutions are discarded only after the axis reaches
the stop. The remaining partial revolution is retained, matching original H5.
Reversal therefore unwinds that partial turn instead of all turns spent at the
stop. Absolute targets avoid accumulating per-command rounding errors.

## Cancellation race

Short moves exposed a core event-ordering bug: `EXEC_CYCLE_COMPLETE` and
`EXEC_MOTION_CANCEL` could arrive together. The old cycle handler entered Idle,
then set `execute_hold` and waited for another completion. A finished cycle could
never produce that event. The isolated core patch skips starting cancellation
when the same event batch already completes the cycle. Ordinary cancellation
still requests native deceleration.

`tests/cancel_completion_test.py` compiles the actual core cycle handler with host
stubs, tests normal/fast cancellation and coincident completion, and checks both
ordinary and tool-change completion. It fails against the prior core and passes
against the patched core. `verify_hand_follow.py` exercises the full controller,
including repeated mode/index-wait cancellation transitions.

## Bench checks

Run on the disconnected, enable-locked tablet:

```sh
python tests/cancel_completion_test.py
python verify_hand_follow.py /dev/cu.usbmodem5AE70674741
python verify_follow.py /dev/cu.usbmodem5AE70674741
python verify_fine_override.py /dev/cu.usbmodem5AE70674741
```

The hand-follow suite tests 1, 5, 15 and 29 RPM in both directions, positive and
negative pitch, Cone geometry, an 8 mm/rev lead, stationary-spindle settling,
complete turns at a stop, manual pause/release and powered/slow transitions.
Final settled Z must agree with encoder position within 0.0031 mm (half a Z step
plus reporting tolerance). Phase after reengagement is compared modulo one lead.
Internal pulse counters must agree exactly with commanded GPIO steps and report
no overlap/deadline faults. This does not establish loaded-machine accuracy or
external STEP/DIR waveforms. See [PORT_PROGRESS.md](PORT_PROGRESS.md) for results.
