/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "p4_driver.h"
#include "serial.h"
#include "spindle.h"
#include "storage.h"
static bool initialize(void)
{
    p4_storage_hal();
    p4_spindle_init();
    return p4_serial_init();
}
static void realtime(sys_state_t state)
{
    p4_serial_poll();
    p4_spindle_poll();
}
const p4_driver_hooks_t *p4_default_driver_hooks(void)
{
    static const p4_driver_hooks_t hooks = {
        .initialize = initialize,
        .feedback_ready = p4_spindle_ready,
        .realtime = realtime,
        .busy_wait = p4_spindle_near_index,
        .on_idle = p4_spindle_idle,
        .on_block = p4_spindle_block,
        .rx_overflows = p4_serial_overflows,
        .storage_ready = p4_storage_ready,
    };
    return &hooks;
}
