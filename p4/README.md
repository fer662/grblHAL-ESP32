# Experimental ESP32-P4 driver

This branch isolates ESP32-P4 hardware support from the touchscreen lathe application.
It is based on `grblHAL/ESP32` at `1cea7438a4a0a9eda35106e53a0944ae4f821d35`.
The existing ESP32/S3 driver files and `main/grbl` submodule remain unchanged.
The core stays at upstream `516e5ad80757bd2eba86bff18feb613ca121dc16`.

## Layout

- `components/grblhal_p4`: GPTimer step/pulse service, GPIO, UART stream, PCNT
  pulse/encoder capture, NVS settings, and the P4 RV32F interrupt boundary.
- `boards/waveshare_p4_xz_map.h`: example external X/Z breakout pin mapping,
  enable polarity, encoder calibration and I/O LDO configuration.
- `example`: minimal standalone ESP-IDF application, with no display, Wi-Fi,
  OTA, audio, Trinamic setup, or assisted-cycle dependencies.

The grbl task and its peripheral interrupts are initialized on CPU 1. An embedding
application owns scheduling of its other tasks; this component does not globally
wrap FreeRTOS task creation or impose an application-specific UI readiness check.

## Build

Use ESP-IDF **5.5.2** and its ESP32-P4 tools, after sourcing that installation's
`export.sh`. From the repository root:

```sh
git submodule update --init main/grbl
idf.py -C p4/example build
```

The default example locks both motor enables inactive (`P4_BENCH_ONLY=ON`). It can
still generate STEP/DIR pulses when commanded; it is intended for a disconnected
bench. To compile the normal enable-control path explicitly:

```sh
idf.py -C p4/example -B build-axis -DP4_BENCH_ONLY=OFF build
```

Example settings are not machine calibration. Review the board map, driver enable
polarity, electrical interface and motion parameters before using outputs. The
example uses a standard single-application IDF partition layout and has no OTA;
its partition table is not a drop-in replacement for an existing lathe controller installation.
This extraction has not been flashed or commissioned on a machine.

UART0 is the grbl stream at 115200 8N1. `$P4` reports pulse counts, timing faults,
receive overflow and storage status. `$P4TRACE`, `$P4TRACE=RESET`, `$P4DEADLINE`,
and `$P4FPUTEST` expose driver diagnostics. Fault injection `$P4IRQTEST` is compiled
only in the enable-locked build. Startup also verifies FP register preservation.
NVS initialization errors preserve flash and fall back to RAM-only settings.

## Embed in an application

Add `p4/components/grblhal_p4` to `EXTRA_COMPONENT_DIRS` before including IDF's
`project.cmake`. Set `GRBLHAL_P4_CONFIG_HEADER` to an absolute path to your
application's machine configuration header and choose `P4_BENCH_ONLY` explicitly.
The example demonstrates startup and the required board/configuration macros.
Call `p4_storage_init()` before `grbl_enter()`; start the latter on a dedicated
CPU 1 task. Keep all access to core state and command processing on that task.

To supply your own serial/storage/spindle and application policy, set
`GRBLHAL_P4_CUSTOM_SERVICES=ON`, then register a `p4_driver_hooks_t` table with
`p4_driver_configure()` before `grbl_enter()`. The table is copied into internal
RAM. `initialize` is required; the other callbacks are optional. ISR callbacks
(`on_idle`, `on_block`, `on_step`) must be IRAM-safe and nonblocking. Foreground
callbacks execute on the grbl task. The application must arrange its own linker
placement for interrupt callbacks and their callees/data.

`motion_allowed` gates motor readiness and wakeup; the application owns startup
and update policy. `p4_set_disabled_axes()` accepts changes only after the pulse
service is idle; the caller must cancel/drain the planner first. The driver then
masks both enable signals and pulses for disabled axes. Diagnostics are available
through `p4_driver_snapshot()` without exposing application-specific state.

Set `GRBLHAL_CORE_ROOT` to the parent directory containing a separate `grbl`
checkout to pin core independently. The default remains this repo's `main/grbl`.
The configuration header is a public C compile option so embedding code sees the
same core types and feature switches as the driver.

[esp32-p4-lathe-controller](https://github.com/fer662/esp32-p4-lathe-controller)
consumes this component through these hooks. Historical integration branches
remain available for existing pins; they are not ancestors of this clean branch.

## Scope and limits

This initial board configuration has X/Z outputs and retains XYZ core indexing.
Y/V commands and non-ZX arcs are rejected. G76 is not enabled by this driver.
There are no physical limit, homing or control inputs, probe input, coolant outputs,
or spindle relay/PWM outputs. M3/M4 only declare the expected external-spindle
state and direction. The board example does not configure external smart drivers.

Encoder feedback uses x2 A/B counting. Its revolution boundary is derived from
counts, not a physical index; phase does not survive a reboot or lost counts.
Synchronization uses the unmodified upstream algorithm, so it does not include
the separate core fork's fractional-step correction, RPM feed-forward,
acceleration phase compensation, path-limit extensions, or configurable AMASS.
Those general proposals live in the [core fork](https://github.com/fer662/grblHAL-core/tree/codex/core-improvements).

The 40 kHz interrupt ceiling is a fault threshold, not a certified operating rate.
The timer service detects overlaps and missed deadlines and latches a motor fault.
Software PCNT observations cannot establish connector pulse width, jitter, drive
response or machining accuracy. The inherited timer/FPU approach has prior P4
bench evidence; this independently extracted configuration needs fresh hardware
validation. Do not apply the integrated application's historical measurements to it.

## Provenance and review

The driver code was extracted from the GPL-3.0-or-later P4 implementation in the
H5 integration branch at `96a7b91`. Application coordination, touchscreen hooks,
networking, OTA, UI settings migrations, and synthetic spindle commands were
removed. Board wiring and example defaults are separated from the driver.
Core changes are deliberately excluded from the port diff.

Compare this branch to `upstream/master` to review only the P4 files, its CI job,
and this README entry. See [VALIDATION.md](VALIDATION.md) for extraction checks.
