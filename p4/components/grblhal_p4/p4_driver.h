/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "grbl/hal.h"
#ifdef __cplusplus
extern "C" {
#endif
// Register once, before grbl_enter(), on the grbl task. The driver copies the
// table into internal RAM. ISR callbacks must use IRAM code and internal data;
// on_idle/on_block/on_step must not block, allocate, or touch flash.
typedef struct {
    bool (*initialize)(void);
    bool (*setup)(settings_t *settings);
    void (*feedback_ready)(void);
    bool (*motion_allowed)(void);
    void (*realtime)(sys_state_t state);
    bool (*busy_wait)(void);
    void (*on_wake)(void);
    void (*on_idle)(void);
    void (*on_block)(stepper_t *stepper);
    void (*on_step)(axes_signals_t steps);
    status_code_t (*command)(sys_state_t state, char *line);
    status_code_t (*validate)(modal_groups_t *, parser_state_t *, parser_block_t *, spindle_t *);
    unsigned (*rx_overflows)(void);
    bool (*storage_ready)(void);
} p4_driver_hooks_t;
typedef struct {
    uint32_t x_pulses, z_pulses, isr_us, pulse_min, pulse_max, late, overlap;
    int32_t x_counted, z_counted;
    uint32_t deadline_kind, deadline_elapsed, deadline_period, deadline_counter;
    bool fault;
    uint8_t enable_x, enable_z;
} p4_driver_diagnostics_t;
bool p4_driver_configure(const p4_driver_hooks_t *hooks);
bool p4_motion_idle(void);
void p4_motion_fault(void);
bool p4_motor_controls_enabled(void);
// Grbl task only; accepts updates only after the timer and pulse service drain.
// Policy (cancel/decelerate first, persistence) belongs to the application.
bool p4_set_disabled_axes(uint8_t mask);
void p4_driver_snapshot(p4_driver_diagnostics_t *snapshot);
#ifdef __cplusplus
}
#endif
