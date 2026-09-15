/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool threading, aux_forward;
    unsigned passes, starts;
    double pitch, x_min, x_max, z_min, z_max, rpm_limit;
} h5_cycle_config_t;
typedef struct {
    double x, z, rpm, z_acceleration, z_max_rate, z_steps_mm;
} h5_cycle_machine_t;
typedef struct {
    h5_cycle_config_t config;
    int direction, spindle_direction;
    double lead, lead_in, run_out, z_start, z_end, approach, finish;
    double x_start, x_end, clearance, takeup;
    unsigned starts;
} h5_cycle_plan_t;

// Pure geometry: millimeters are machine coordinates, X is radial. The stop
// rectangle is the cutting area, not a homed hard-travel envelope.
bool h5_cycle_plan(const h5_cycle_config_t *, const h5_cycle_machine_t *, h5_cycle_plan_t *, char *error, size_t size);
double h5_cycle_depth(const h5_cycle_plan_t *, unsigned pass);
unsigned h5_cycle_phase(const h5_cycle_plan_t *, unsigned start);
#ifdef __cplusplus
}
#endif
