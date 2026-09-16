/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
// Called from the grbl task. Timing faults latch until hardware restart.
bool p4_motion_idle(void);
void p4_motion_fault(void);
bool p4_motor_controls_enabled(void);
