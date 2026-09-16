# Extraction validation

## Source boundaries

The clean branch starts at upstream ESP32 `1cea7438`, with the unchanged upstream
core pin `516e5ad8`. No H5 application history is an ancestor of this branch.
The original application checkout and its uncommitted threading work are retained.

## Reproduction

The standalone example requires only ESP-IDF 5.5.2 and the `main/grbl` submodule.
Local clean output-directory builds passed for both enable policies.
The bench image is 480,416 bytes; the motor-enabled image is 480,704 bytes.
CI builds both `P4_BENCH_ONLY=ON` and `OFF` from a fresh checkout. Local builds
use the installed IDF 5.5.2 SDK; that SDK has unrelated pre-existing modifications,
so the CI build is the pristine-SDK reproducibility check.

The host test `python3 p4/tests/pulse_service_test.py` executes the extracted
production enable and pulse callbacks with GPIO/timer stubs. Both enable policies,
direction setup/hold, reset during an asserted/pending pulse, overlap faults and
timer deadline faults pass. It does not measure physical timing.

Hardware tests are not part of this extraction. No firmware has been flashed and
no motion commands have been sent. External pulse timing, encoder phase, reset
behavior and loaded motion require commissioning of this exact configuration.

## Publication audit

The review covers the new changes and the existing fork-only commit history.
Gitleaks scans for credential patterns are supplemented with checks for local
user paths, private network addresses, hardware addresses and sensitive assigned
values. The existing integration branch's 31 screenshot blobs were also OCR
reviewed for private text. Screenshot metadata contained only color-space and image-size fields.
No credentials or private device/network details were
found in those checks. Source files, documentation and CI are the only new
published artifacts; build products and local audit outputs are excluded.

New commits use the GitHub no-reply author address. Existing public historical
branches retain their original author attribution. Automated scanning cannot
prove the absence of every possible secret; the audit describes what was checked.
