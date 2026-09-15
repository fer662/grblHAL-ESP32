/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
void h5_fpu_call(void (*callback)(void));
bool h5_fpu_test(void);
