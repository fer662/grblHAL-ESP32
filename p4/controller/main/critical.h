/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "freertos/FreeRTOS.h"
// Task-only application locks. Tags: 1xxx bridge, 2xxx cycle, 3xxx follow,
// 4xxx network, 5xxx storage; suffix is the source line of the acquisition.
void h5_critical_enter(portMUX_TYPE *lock, unsigned tag);
void h5_critical_exit(portMUX_TYPE *lock);
void h5_critical_reset(void);
void h5_critical_report(void);
