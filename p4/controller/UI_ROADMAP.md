# Touchscreen improvements

## Implemented in 0.3.8

The right pane now contains a separate central **JOG MODE: HOLD / SINGLE STEP**
toggle and the relocated **STEP** button in its upper-left corner. The square
128 x 128 jog targets form a balanced cross around the mode control. All eight
operation-specific control sets retain their existing positions.

- Hold moves while the direction is pressed and stops on release or slide-out.
- Single step starts one selected increment per press; it completes after release
  and does not repeat when held. Ordinary jogs reject taps while a previous step
  is pending or moving. Machining limits still clip requested distances.
- The step size no longer selects the jog mode: 1 mm is now a real single step
  when Single step is selected. The mode is saved independently, reusing a
  previously reserved preference byte; older settings load with Hold selected.
- STEP still cycles the existing metric/inch increments; its long press still
  switches units. SHIFT retains one-off numeric movement and limit entry.
- During active spindle-synchronized feed, the existing Z override rule rounds
  displacement up to whole pitches to retain phase. Single-step routing retains
  that rule, then allows assisted feed to resume; it is not a fixed absolute Z
  displacement in that context.

Desktop previews: [Thread](docs/ui-038-thread.png),
[Single step](docs/ui-038-single-step.png), [Gearbox](docs/ui-038-gearbox.png).

Suggested remaining corner controls, not implemented:

1. **Jog speed**: explicit slow/normal choices within each axis's configured rate.
2. **Limits**: edit the endpoints together, see their span, and clear one axis's
   limits deliberately while stopped. Keep the directional endpoint shortcuts.
3. **Cancel movement**: a readily accessible way to interrupt a single move.

## Implemented in 0.3.7

The operator selected the outer-compass jog/limit layout while retaining the
rest of the 0.3.6 UI. Each limit now sits beyond its corresponding jog arrow,
with a 16-pixel gap. The four jog buttons remain 160 x 112 pixels. The panel
extends downward to 600 x 628; other controls retain their positions and behavior.
Direction mapping, SHIFT entry, limit setting/clearing and slide-out cancellation
are unchanged.

Current desktop renders, with simulated readings:

- [Gearbox](docs/ui-037-gearbox.png)
- [Thread, with all mode-specific controls](docs/ui-037-thread.png)

## Implemented in 0.3.6

The original 218 x 218 pixel jog pad used adjacent triangular targets. The new
600 x 580 panel has four 160 x 112 rectangular targets, separated by empty space,
with explicit X/Z signs. H5's physical mapping is retained: top X+, bottom X-,
left Z+, right Z-. Machining-limit buttons occupy a separate labeled row.
Configured limits have an amber outline; disabled-axis jogs are greyed out.

Sliding off a jog cancels it and requires lifting before another direction can
start. LVGL 8 delivers the next target's press during the same input tick unless
its input is reset; the implementation resets and waits for release. The desktop
pointer regression reproduces that behavior and verifies the fix.

Position rows are 304 x 80; primary controls, RPM/mode selection, cycle settings,
mode-menu choices and the numeric keypad use the larger display. Settings remain
in stable columns and all eight existing operations remain available. The
standalone FPS label is hidden, leaving the status area for controller information.

Desktop renders, with simulated readings:

- [Gearbox](docs/ui-036-gearbox.png)
- [Thread](docs/ui-036-thread.png)
- [Distance keypad](docs/ui-036-keypad.png)

## Suggested additions using the existing core

These are proposals, not enabled controls in 0.3.8. They require no new motion
planner. Source references describe this checkout, not all upstream configurations.

| Priority | UI addition | Existing support and integration required |
| --- | --- | --- |
| 1 | Jog speed selector and direct increment choices | `$J` already accepts a feed and relative distance; replace the current fixed X/Z feed choices and cycling step selector with explicit choices. Clamp to configured axis limits. Native jogs deliberately ignore feed override, so this should set jog feed directly. |
| 2 | Clearly labeled X radius/diameter and machine/work position views | The core supports G7/G8 and machine/work coordinates. The current assisted DRO uses physical slide travel plus a local origin; keep this distinct from G-code diameter mode and implement explicit conversion/reporting. |
| 3 | Work setup: measured diameter, face coordinate, G54–G59 selection | G10 and work coordinate systems already exist. Replace the current display-only zero operation with an explicit work-setup workflow where appropriate; preserve assisted-mode zero behavior until migrated deliberately. Position still needs re-establishing after unobserved manual movement. |
| 4 | G-code program controls: pause/resume, single block, optional stop | The core supports feed hold/cycle start, single block and optional stop. Add a program owner alongside assisted services. Current assisted-mode hold requests cancel their operation, and G33 disables ordinary feed hold; a generic pause button must reflect these distinctions. |
| 5 | Feed and rapid override controls | Native realtime overrides exist. Expose current percentages and reset-to-100% for ordinary program moves; G33 disables feed override to preserve synchronization. These controls are not jog-speed or thread-pitch controls. |
| 6 | Tool library with measured X/Z offsets | Core tool-offset support exists, but its persistent tool table is currently disabled (`N_TOOLS=0`). Enable/configure table capacity and persistence, then add manual tool-change and measurement workflows. Tool nose geometry/CAM post compatibility needs its own validation. |

Relevant sources: `main/grbl/protocol.c` (realtime overrides and single block),
`main/grbl/gcode.c` (G7/G8, G10, coordinates, tool offsets and G33 restrictions),
`main/grbl/motion_control.c` (`mc_jog_execute`), `main/grbl/config.h` (`N_TOOLS`),
`p4/controller/main/bridge.c` (assisted operation handling), and
`p4/controller/components/h5_ui/ui.cpp` (current DRO origin and jog feed).

A visible “Next pass” action would also improve discovery of the current long
press on PASSES. That is an existing H5 cycle-service action, not a new native
G-code feature. Homing/probing hardware work remains deferred by the operator.
