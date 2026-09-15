/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned mode; // 0 Gearbox, 1 Cone, 2 Async
    double pitch, ratio, x_min, x_max, z_min, z_max;
    bool aux_forward;
} h5_follow_config_t;
bool h5_follow_request(const h5_follow_config_t *);
bool h5_follow_busy(void);
bool h5_follow_manual_held(void);
bool h5_follow_selected(void);
void h5_follow_clear(void);
void h5_follow_cancel(void);
// Apply edited feed parameters after planner-controlled deceleration.
bool h5_follow_update(double pitch, double ratio, bool aux_forward);
void h5_follow_reset(void);
void h5_follow_poll(void);
void h5_follow_snapshot(h5_cycle_status_t *);
// Jog requests pause automatic feed; release resumes with retained registration.
bool h5_follow_jog(char axis, int sign, double distance, bool held);
void h5_follow_release(void);
#ifdef __cplusplus
}
#endif
