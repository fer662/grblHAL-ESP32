/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cycle_plan.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t position[3];
    float steps_per_mm[3];
    float max_rate[3], acceleration[3];
    float rpm;
    uint32_t command_id, completed_id, stream_generation;
    int command_status, alarm;
    bool ready, moving, held;
    char state[24];
} h5_status_t;

typedef struct {
    bool active;
    unsigned pass, start;
    char message[96];
} h5_cycle_status_t;
bool h5_cycle_request(const h5_cycle_config_t *config); // thread safe, copied on submit
bool h5_cycle_busy(void);
bool h5_cycle_owns_stream(void);
void h5_cycle_cancel(void);
bool h5_cycle_advance(void);
void h5_cycle_snapshot(h5_cycle_status_t *status);
uint32_t h5_bridge_cycle_submit(const char *line); // cycle service only
bool h5_bridge_empty(void); // grbl task only
void h5_bridge_discard_cycle_commands(void); // grbl task only
enum { H5_OWNER_PROFILE=1, H5_OWNER_FOLLOW=2, H5_OWNER_UPDATE=3 };
bool h5_operation_claim(unsigned owner);
void h5_operation_release(unsigned owner);
bool h5_motion_idle(void);
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
uint32_t h5_ui_updates(void);
bool h5_ui_screenshot(void (*write)(const char *));
bool h5_ui_test_action(char action); // isolated bench diagnostic only
#ifdef __cplusplus
}
#endif
