/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grbl/hal.h"
#include <stdio.h>
// ESP-Hosted and some UI workers request no affinity. Reserve core 1 for the
// explicitly pinned grbl task and hardware interrupts. Keep this policy in the
// application instead of patching managed SDK components. Explicit SDK core
// assignments (including the per-core idle and IPC tasks) remain unchanged.
BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t,
                                          TaskHandle_t *, BaseType_t);
BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t size, void *arg,
                                          UBaseType_t priority, TaskHandle_t *handle, BaseType_t core)
{
    return __real_xTaskCreatePinnedToCore(task, name, size, arg, priority, handle,
                                          core == tskNO_AFFINITY ? 0 : core);
}
TaskHandle_t __real_xTaskCreateStaticPinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t,
                                                  StackType_t *, StaticTask_t *, BaseType_t);
TaskHandle_t __wrap_xTaskCreateStaticPinnedToCore(TaskFunction_t task, const char *name, uint32_t size,
                                                  void *arg, UBaseType_t priority, StackType_t *stack,
                                                  StaticTask_t *buffer, BaseType_t core)
{
    return __real_xTaskCreateStaticPinnedToCore(task, name, size, arg, priority, stack, buffer,
                                                core == tskNO_AFFINITY ? 0 : core);
}
void h5_task_report(void)
{
    const char *names[] = {"grblHAL",     "H5_UI",     "H5_audio",        "H5_network", "lvgl",   "lvgl_task",
                           "sdio_rx_buf", "sdio_read", "sdio_process_rx", "sdio_write", "rpc_rx", "rpc_tx",
                           "rpc_supp_cb", "tiT",       "Tmr Svc"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++) {
        TaskHandle_t task = xTaskGetHandle(names[i]);
        if (!task)
            continue;
        char line[130];
        snprintf(line, sizeof(line), "[P4TASK:%s|CORE:%ld|PRIORITY:%lu|STACK_FREE:%lu]\r\n", names[i],
                 (long)xTaskGetCoreID(task), (unsigned long)uxTaskPriorityGet(task),
                 (unsigned long)uxTaskGetStackHighWaterMark(task));
        hal.stream.write(line);
    }
}
