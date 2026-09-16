/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { H5_TURN, H5_THREAD, H5_FACE, H5_CUT, H5_ELLIPSE } h5_cycle_operation_t;

typedef struct {
    bool threading, aux_forward;
    unsigned passes, starts;
    double pitch, x_min, x_max, z_min, z_max, rpm_limit;
    h5_cycle_operation_t operation;
} h5_cycle_config_t;
typedef struct {
    double x, z, rpm, z_acceleration, z_max_rate, z_steps_mm;
    double x_acceleration, x_max_rate, x_steps_mm;
} h5_cycle_machine_t;
typedef struct {
    h5_cycle_config_t config;
    int direction, spindle_direction;
    double lead, cut_start, cut_end, approach, finish;
    double entry_begin, full_begin, full_end, exit_end, x_steps_mm, z_steps_mm;
    double thread_x_rate, thread_x_acceleration, thread_clearance;
    double cut_acceleration; // Configured mm/s^2, also used for thread speed feasibility.
    double depth_start, depth_end, clearance, takeup;
    unsigned starts, segments;
    char cut_axis, depth_axis;
    bool indexed;
} h5_cycle_plan_t;

// Pure geometry: millimeters are machine coordinates, X is radial. Cutting-axis
// targets stay inside the entered bounds, including Thread synchronization.
// Depth-axis clearance retracts are separate; this is not a homed travel envelope.
bool h5_cycle_plan(const h5_cycle_config_t *, const h5_cycle_machine_t *, h5_cycle_plan_t *, char *error, size_t size);
double h5_cycle_depth(const h5_cycle_plan_t *, unsigned pass);
void h5_cycle_point(const h5_cycle_plan_t *, unsigned pass, unsigned segment, double *x, double *z, double *feed);
const char *h5_cycle_name(h5_cycle_operation_t);
unsigned h5_cycle_phase(const h5_cycle_plan_t *, unsigned start);
enum { H5_THREAD_ACCEL_SEGMENTS = 4, H5_THREAD_RAMP_SEGMENTS = 2 * H5_THREAD_ACCEL_SEGMENTS + 1, H5_THREAD_BLOCKS = 2 * H5_THREAD_RAMP_SEGMENTS + 3 };
void h5_thread_stations(const h5_cycle_plan_t *, unsigned pass, double *full_begin, double *full_end);
// Point 0 is the clear approach, point H5_THREAD_BLOCKS is the clear finish.
void h5_thread_point(const h5_cycle_plan_t *, unsigned pass, unsigned point, double *x, double *z);
#ifdef __cplusplus
}
#endif
