/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <math.h>
#include <stdbool.h>
// Both idle jogging and assisted manual override use this policy (mm/min).
static inline double h5_jog_feed(char axis, double maximum, bool rapid)
{
    return rapid ? maximum : fmin(maximum, axis == 'X' ? 60.0 : 960.0);
}
