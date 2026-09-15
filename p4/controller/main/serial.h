/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
bool h5_serial_init(void);
void h5_serial_poll(void);
unsigned h5_serial_overflows(void);

bool h5_serial_pending(void); // grbl task only, includes incomplete USB line
