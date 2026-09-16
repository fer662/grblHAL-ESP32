/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t version;
    int32_t mode, measure, pitch_type, pitch, move_step, passes, starts;
    float cone_ratio;
    // Uses a formerly zeroed reserved byte; old ui_v1 blobs default to Hold.
    uint8_t aux_forward, sound, jog_mode, reserved; // jog_mode: 0 Hold, 1 Single step
} h5_preferences_t;
bool h5_preferences_get(h5_preferences_t *);
void h5_preferences_set(const h5_preferences_t *);
#ifdef __cplusplus
}
#endif
