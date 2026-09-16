# Desktop UI preview and touch regression

Builds the production LVGL widgets with the vendored LVGL version and a simulated
backend. It has no network, serial, GPIO or motion-driver connection. The host
configuration and memory allocation are separate from the firmware configuration.

```sh
cmake -S p4/controller/tests/ui_preview -B /tmp/h5-ui-preview-build
cmake --build /tmp/h5-ui-preview-build --target ui_preview -j 8
cd /tmp/h5-ui-preview-build
./ui_preview
```

The executable writes `gearbox.ppm`, `thread.ppm`, `disabled.ppm`, `modes.ppm`
plus `rapids.ppm`, `single-step.ppm`, `keypad.ppm`, `settings.ppm`, `pitches.ppm`,
`limits-off.ppm`, `limit-editor.ppm` and `limit-keypad.ppm`. The widgets are the real application code; positions, RPM,
backend callbacks and the status footer use fixture data. PPM images can be opened
or converted to PNG with a normal image tool.

The regression drives LVGL's pointer input, rather than directly calling button
callbacks. It covers all four directions and their release, sliding between
buttons without lifting, disabled-axis input, separating limit buttons from jog,
all eight mode selectors, main-screen button bounds/overlap, SHIFT distance entry
without a jog, and entering a numeric limit through the number pad. It also covers
the center Hold/Single toggle, the relocated STEP control and slide-out into the
center without accidentally changing modes. Rapids renders the amber control and
locks the center to Hold, retaining and restoring a prior Single preference. Limit-editor tests cover signed
metric/inch coordinates with nonzero display origins, Use current, spans, draft
cancellation, per-endpoint clearing, invalid ordering and refused Apply while busy.
The toggle test verifies that bypass retains endpoint values.

Run `python3 p4/controller/tests/jog_modes_test.py` for the production jog-routing
host test: Hold versus Single, release, metric/inch distances, pending/active tap
rejection, bound clipping, cancellation, assisted-feed routing and quick Hold
release before the service polls. It also checks preference-layout compatibility,
manual limit bypass/re-enable, idle/pending guards, atomic endpoint validation,
coordinate conversion and out-of-range rejection.

This verifies layout and GUI event routing, not electrical outputs, real touch
calibration, motor stopping distance or concurrent motion/display timing.
