/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "grbl/hal.h"
void h5_spindle_init(void);
void h5_spindle_ready(void);
void h5_spindle_poll(void);
void h5_spindle_idle(void);
void h5_spindle_block(stepper_t *stepper);
void h5_spindle_edge(bool z_step);
bool h5_spindle_simulator_active(void);
float h5_spindle_rpm(void); // grbl task only; same measurement used by the HAL
bool h5_spindle_near_index(void); // short foreground polling window, no task delay
status_code_t h5_spindle_command(sys_state_t state, char *line);
