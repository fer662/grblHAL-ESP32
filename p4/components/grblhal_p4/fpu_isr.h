/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
void p4_fpu_call(void (*callback)(void));
bool p4_fpu_test(void);
