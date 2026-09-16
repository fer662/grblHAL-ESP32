/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stdbool.h>
void p4_feedback_init(void);
void p4_feedback_read(int *x_pulses, int *z_pulses, int *encoder);

int32_t p4_encoder_count(void);
