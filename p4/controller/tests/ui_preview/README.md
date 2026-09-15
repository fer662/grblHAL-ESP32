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
plus `keypad.ppm`, `settings.ppm` and `pitches.ppm`. The widgets are the real application code; positions, RPM,
backend callbacks and the status footer use fixture data. PPM images can be opened
or converted to PNG with a normal image tool.

The regression drives LVGL's pointer input, rather than directly calling button
callbacks. It covers all four directions and their release, sliding between
buttons without lifting, disabled-axis input, separating limit buttons from jog,
all eight mode selectors, main-screen button bounds/overlap, SHIFT distance entry
without a jog, and entering a numeric limit through the number pad.

This verifies layout and GUI event routing, not electrical outputs, real touch
calibration, motor stopping distance or concurrent motion/display timing.
