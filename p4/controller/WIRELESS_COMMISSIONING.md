# Checking the lathe without USB

USB is not required at the lathe. Firmware 0.3.2 adds a read-only diagnostics
service alongside the existing wireless firmware updater. Both use the tablet's
saved Wi-Fi network. The computer must be able to reach the tablet on that network.

## Live diagnostics

1. Tap the bottom status line (**Tap for diagnostics**).
2. The panel shows the IP address, encoder count/RPM, issued and GPIO-counted
   X/Z steps, fault status and TMC5160 register readback.
3. Open `http://<tablet-ip>:8080/diagnostics` in a browser, or record a log:

   ```sh
   python3 p4/controller/diagnostics_log.py <tablet-ip> --output lathe-check.jsonl --seconds 300
   ```

4. Press **BACK** to return to the operation controls while logging continues.
   **STOP SHARING** closes access. Sessions expire after 30 minutes and do not
   survive a restart. Opening the panel again renews the session.

The observer exposes only measurements, firmware version and fault status to
other devices on this LAN during the locally opened session. It has no remote
motion, simulator, register-write, settings or update commands, and exposes no
Wi-Fi credentials or OTA pairing key. It does not acquire the motion owner.
Outside a session it returns HTTP 403. Only `GET /diagnostics` is supported.

Snapshots are sampled by the grbl task at most four times per second; the
network task formats and transmits copies on CPU 0. `sample_age_ms` lets the
logger distinguish a current sample from a stalled foreground task. The bridge's
position/state and IRQ/PCNT counters are nearby observations, not one atomic
machine-wide instant; compare final issued/counted totals at rest.

TMC reads refresh at most every two seconds, only with the parser queue,
planner, motion timers and assisted operation idle, outside update mode.
`tmc.sampled_ms` is the last refresh attempt's uptime; registers remain cached
during motion. A missing device is reported; opening diagnostics never configures
or enables a drive. A driver connected after boot may require a restart for its
normal initialization. `simulated` must be zero during actual encoder checks.
Timing values retain the existing diagnostics semantics: pulse/deadline ticks
are 0.1 us, critical-section values cover application lock bodies, and internal
GPIO counters do not establish electrical quality at the driver input.

## Hardware sequence

- With motor enables still locked, power the tablet through its normal lathe
  wiring, join the same Wi-Fi, open diagnostics and capture a stationary baseline.
- Turn the spindle by hand and inspect count direction and the calibrated 1,200
  effective counts per spindle revolution. Existing gearing/calibration stays
  unchanged unless measurements establish a discrepancy.
- Check real TMC presence/configuration and register values after normal boot.
- Inspect STEP/DIR/enable levels and timing at the breakout/driver with a scope or
  logic analyzer. The instrument need not connect to the tablet's USB port.
  Any pulse-producing test is a separate deliberate local operation; HTTP
  diagnostics alone generates no pulses.
- Only after these checks should a separate motor-enabled firmware be prepared
  for controlled unloaded and then loaded operation checks. This version retains
  the compile-time enable lock; Wi-Fi access does not remove it.

The last steps still require an operator at the lathe. This service provides
remote observation; it does not prove wiring or loaded mechanical behavior.
The previously deferred ribbon/battery power issue remains a separate check if
normal lathe power prevents a stable boot or Wi-Fi connection.

## Firmware updates

Open **Firmware Update** on the touchscreen and use its IP and temporary pairing
key with `ota_upload.py` as described in [OTA.md](OTA.md). No USB is needed.
Update mode requires idle and holds the motion owner through upload/validation.
An update restarts the controller; live diagnostics must be reopened afterward.
Use updates only while the machine is stopped.

## Bench validation

`verify_diagnostics.py <usb-port>` automates the real local UI events and checks
HTTP session closure, fresh observations, rejected command-like requests and
unchanged step totals. It uses USB only as a test fixture, not as a requirement
for operating diagnostics. Network/motion stress requires the disconnected
bench fixture and is separate from the read-only test.
