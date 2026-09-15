# Spindle synchronization bench record

## Architecture and scope

The P4 runs native grblHAL `G33` through the existing planner and step ISR.
The HAL reports spindle revolutions from the calibrated A/B encoder: x2 decode,
1200 effective counts/revolution. M3/M4 only select the expected direction;
this external spindle implementation does not switch a relay or regulate RPM.
Revolution boundaries are derived from continuous encoder count, with a phase
offset for multi-start tests. They are not a physical index or a phase reference
that survives lost counts or a reboot.

All tests use the isolated USB-powered tablet. X enable stays HIGH, Z enable LOW.
The synthetic spindle drives actual A/B GPIO transitions into PCNT; STEP edges
are independently counted by PCNT. This validates MCU logic and timing, not
external wiring, encoder signals, mechanical backlash, drive response or a cut.

## Core correction

Upstream base: `grblHAL/core` `516e5ad80757bd2eba86bff18feb613ca121dc16`.
Fix: `fer662/grblHAL-core` `44aad88e60ccebd47729252e401ff98245c5bf39`.

Segment preparation added `dt * target_feed` to the spindle position target.
But `dt` includes a fractional step carried from the previous segment and time
for a new fraction which is not executed yet. Repeatedly adding that latter
fraction advances the phase target too far. The correction loop then slows the
axis to follow this erroneous target, producing cumulative pitch error.

The fix uses `inv_rate * n_step * target_feed`, before AMASS scales the step
count. This represents only the whole steps executed by the segment. The next
fraction is carried exactly once by the existing segment preparation logic.
No H5-specific logic or configuration was added to the core.

Measured at nominal 300 RPM, K1, Z200 steps/mm, P=1/I=0, over steps 400–1600
(a 6 mm interval inside a 10 mm move):

| Build | Span of encoder/axis phase error |
| --- | ---: |
| Original core | 0.2917 mm |
| Corrected core | at most 0.00083 mm in the initial gain-comparison run |

P=0 removed most original drift, while increasing P made the original drift
worse. With the fix, the same constant-speed gain sweep (0.1, 0.5, 1, 2)
maintained pitch to within an encoder count in that run. Absolute phase error
still includes acceleration at the start; it is not zero just because its
variation during cruise is small.

## P4 floating-point interrupt boundary

ESP-IDF 5.5.2's P4 FreeRTOS port uses lazy task FPU allocation and aborts if
allocation is first requested inside an interrupt. Native spindle correction
uses floating point inside the step ISR. `main/fpu_isr.S` explicitly saves all
32 physical FP registers, FCSR and the previous mstatus.FS bits, enables the
FPU for the core callback, then restores the interrupted context. It does not
change other mstatus bits or enable interrupts. The ESP-IDF SDK is unchanged.

`$P4FPUTEST` keeps distinct values in all 32 task FP registers and a nondefault
rounding mode while 1,000 timer interrupts deliberately overwrite every FP
register and FCSR. It passed on the P4. The same test runs before driver setup
succeeds; failure latches the motion fault. This port-owned assembly must be
reviewed on SDK/toolchain/CPU changes; it is not a claim that arbitrary ESP-IDF
interrupt handlers may use floating point.

## Diagnostics and reproduction

Use the IDF Python environment with `pyserial`:

```sh
python verify_spindle.py PORT
python verify_spindle.py PORT --fault stall
# Hardware reset before the next fault test.
python verify_spindle.py PORT --fault reverse
# Hardware reset before deliberately missing timer deadlines.
python verify_spindle.py PORT --fault deadline
```

The normal suite covers 11 cuts: repeated zero-phase passes, 400/800-count
offsets (three-start phase registration), K0.5/K1/K2, both Z directions,
reverse spindle rotation, and abrupt 300→240/360 RPM changes. It checks exact
STEP counts, cruise phase-error span, continued UI updates, FPU preservation,
and encoder rollover. The gain is explicitly P=1, I=0; RAM defaults are zero,
so do not assume an unconfigured G33 has active position correction.

- `$P4SYNC`: RPM, accumulated count, phase, tracking/index-wait state, fault,
  simulator rate, block STEP count, first/last edge encoder counts and pitch.
- `$P4SYNCTRACE`: encoder count at the first Z pulse and each 16th pulse,
  up to 128 samples, available while idle.
- `$P4PHASE=0..1199`: encoder-derived phase offset, idle only.
- `$P4SIM=30..600` or negative equivalent: synthetic quadrature RPM.
  Timer quantization makes requested 300 RPM approximately 301.2 RPM.
- `$P4SIM=0`: halt transitions to simulate a stalled spindle.
- `$P4SIM=OFF`: delete simulator and release A/B to inputs. The older
  `$P4ENCODERTEST` is refused while the simulator owns the pins.
- `$P4SIMCHANGE=rpm,delay_ms`: schedule a speed change while G33 is executing;
  this bypasses no motion protection and is isolated to the bench build.
- `$P4IRQTEST`: while a normal move runs, deliberately mask interrupts for
  2 ms. The independent deadline check must latch a timing fault. Never use
  this diagnostic with a machine attached.

Spindle tracking samples accumulated PCNT position every 2 ms. It folds the
temporary ±30000-count jump visible if the step interrupt preempts the PCNT
rollover ISR. Tracking must sample more often than 15000 encoder counts; these
rates are comfortably inside that constraint. More than 100 ms without counts
or a retreat of more than three counts latches the motor fault during a cut.
An aborted cut is not resumed automatically.

The index wait avoids its usual 1 ms task sleep within approximately 2 ms of
either side of a revolution boundary. It yields normally elsewhere, including
when encoder counts stop, so it does not busy-wait indefinitely. A regression
run originally failed the eight-count registration bound with a 12-count span;
the first full run with this scheduling change reduced it to three counts.

Step deadlines are checked against both the auto-reloading step timer's phase
and the separate free-running pulse timer. The latter detects whole missed
periods that a modulo timer reading alone can hide. A greater-than-1.5-period
gap faults; this is a detection threshold, not a promise of acceptable jitter
up to that value. The deadline fault test intentionally blocks interrupts and
checks that the fault latches and STEP counts remain stationary afterward.

### Final installed-build checks, 2026-09-14

- `verify_motion.py`: passed; 4,596 X and 47,116 Z pulses matched PCNT.
  No unexpected faults, overlaps, late alarms or UART overflows. Internally
  measured pulse service was 15.3–23.4 us; longest step callback was 27 us.
- `verify_spindle.py`: all 11 cuts passed, 44,000 Z pulses matched PCNT,
  repeated/multi-start registration span two counts. K1 constant-speed cruise
  phase-error span was at most one encoder count (0.00083 mm); K0.5 and K2
  measured 0.00292 and 0.005 mm respectively. This excludes lead-in/deceleration.
  Pulse service was 15.3–27.2 us; longest callback 26 us. UI updates continued.
- Separate stall, reversal and deliberate missed-deadline tests passed. Each
  latched the fault and stopped pulse counts; the deadline case recorded exactly
  one late alarm. Hardware resets separated the fault tests.
- Final USB/UI check: three reopens, two complete screen captures with the UI
  still updating afterward, then a 15-second continuous connection without
  reboot or UI stall. Opening the bridge can reset the P4; clients wait for boot.

These are bounded runs on the tablet, not a worst-case execution-time proof or
long-duration stress qualification. Test logs and original flash backups are
kept privately outside Git. No real motor or physical encoder was attached.

## Remaining acceptance work

The automated speed-change bound is deliberately 0.15 mm, a regression bound
for this early implementation, **not a thread tolerance or machining approval**.
P=1 produces about 0.10 mm temporary phase error on the abrupt 20% speed change.
P=8 reduces that to roughly 0.013 mm at K1 but showed approximately 0.022 mm
oscillation at K2, so it is not adopted as a universal setting. Integral gain
and feed-forward/correction constraints need systematic evaluation across
pitch, RPM, AMASS level and available axis acceleration.

At K1/300 RPM, cruise also trails the first-pulse reference by about 0.225 mm
because of acceleration. Repeated synthetic passes register consistently, but
changes in starting RPM alter lead-in and phase lag. The assisted threading
service must account for this before the tool enters the cut. Check physical
encoder gearing/phase under load and loss-of-count detection before enabling
threading on the UI. Existing user calibration must remain unchanged.

Only straight Z G33 is exposed in this bench build. The core correction speed
limit currently uses Z settings; X/tapered G33 and G76 need per-axis constraints
and separate tests. All original assisted operations remain port requirements.
