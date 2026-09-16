/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "grbl/hal.h"
void p4_spindle_init(void);
void p4_spindle_ready(void);
void p4_spindle_poll(void);
void p4_spindle_idle(void);
void p4_spindle_block(stepper_t *stepper);
bool p4_spindle_near_index(void);
float p4_spindle_rpm(void);
int64_t p4_spindle_position(void);
