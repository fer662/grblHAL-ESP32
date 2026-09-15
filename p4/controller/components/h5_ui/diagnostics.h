/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t sampled_ms, tmc_sampled_ms;
    int64_t encoder;
    float rpm;
    uint32_t x_pulses, z_pulses, isr_us, pulse_min, pulse_max, late, overlap;
    int32_t x_counted, z_counted;
    uint32_t deadline_kind, deadline_elapsed, deadline_period, deadline_counter;
    uint32_t critical[2];
    uint32_t tmc_ioin, tmc_chopconf, tmc_status;
    bool fault, tracking, waiting, simulated, tmc_transport, tmc_present, tmc_configured;
    uint8_t enable_x, enable_z;
    char sync_fault[16];
} h5_diagnostics_t;
void h5_diagnostics_start(void); // Local UI/USB only; expires after 30 minutes.
void h5_diagnostics_stop(void);
bool h5_diagnostics_active(void);
void h5_diagnostics_snapshot(h5_diagnostics_t *snapshot);
#ifdef __cplusplus
}
#endif
