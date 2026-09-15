/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t position[3];
    float steps_per_mm[3];
    float rpm;
    uint32_t command_id, completed_id, stream_generation;
    int command_status, alarm;
    bool ready, moving, held;
    char state[24];
} h5_status_t;

void h5_bridge_init(void);
void h5_bridge_flush(void); // grbl task only; discard requests on stream reset
void h5_bridge_poll(void); // grbl task only
int32_t h5_bridge_read(void); // grbl stream only, at a USB line boundary
bool h5_bridge_active(void);
uint32_t h5_bridge_submit(const char *line); // thread safe, nonblocking
void h5_bridge_cancel(void); // cancels queued and active jogs, bypasses queue
void h5_bridge_hold(void);
void h5_bridge_resume(void);
void h5_bridge_snapshot(h5_status_t *status);
void h5_ui_start(void);
bool h5_ui_ready(void);
bool h5_ui_screenshot(void (*write)(const char *));
bool h5_ui_test_action(char action); // isolated bench diagnostic only
#ifdef __cplusplus
}
#endif
