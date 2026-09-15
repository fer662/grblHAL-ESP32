/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "diagnostics.h"
void h5_diagnostics_poll(bool idle); // Sole grbl task; no network or formatting.
void h5_diagnostics_serve(int fd); // Network task; cached observations only.
void h5_driver_snapshot(h5_diagnostics_t *s);
void h5_spindle_snapshot(h5_diagnostics_t *s);
void h5_tmc_snapshot(h5_diagnostics_t *s, bool refresh);
