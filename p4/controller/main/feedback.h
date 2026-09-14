/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stdbool.h>
void h5_feedback_init(void);
void h5_feedback_read(int *x_pulses, int *z_pulses, int *encoder);
// Bench only, idle only: drives A/B internally; never call with encoder attached.
bool h5_feedback_selftest(void);
