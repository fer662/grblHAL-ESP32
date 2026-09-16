/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
bool p4_serial_init(void);
void p4_serial_poll(void);
unsigned p4_serial_overflows(void);
