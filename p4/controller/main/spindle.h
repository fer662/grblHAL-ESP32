/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "grbl/hal.h"
void h5_spindle_profile(bool enabled);
float h5_spindle_profile_rpm(void);
void h5_spindle_init(void);
void h5_spindle_ready(void);
void h5_spindle_poll(void);
void h5_spindle_idle(void);
void h5_spindle_block(stepper_t *stepper);
void h5_spindle_edge(axes_signals_t steps);
bool h5_spindle_simulator_active(void);
float h5_spindle_rpm(void); // grbl task only; same measurement used by the HAL
bool h5_spindle_waiting_index(void); // grbl task only; no pulse output started yet
bool h5_spindle_near_index(void); // short foreground polling window, no task delay
status_code_t h5_spindle_command(sys_state_t state, char *line);

int64_t h5_spindle_position(void);
void h5_spindle_follow(bool enabled);
void h5_spindle_follow_braking(void);

void h5_spindle_entry_arm(double seconds, double lead, double acceleration);
bool h5_spindle_entry_cutting(void); // Latched until the next entry is armed.
void h5_spindle_entry_prepare(void); // Clear the previous pass latch before submitting another entry.
