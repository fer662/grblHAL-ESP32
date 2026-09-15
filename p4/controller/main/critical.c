/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "critical.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "grbl/hal.h"
#include <stdatomic.h>
#include <stdio.h>

static uint64_t entered[2];
static unsigned site[2];
// Publish site and duration together. Each core is the sole recording writer
// for its slot; the other core can inspect it without another spinlock.
static atomic_uint maximum[2];
void IRAM_ATTR h5_critical_enter(portMUX_TYPE *lock, unsigned tag)
{
    portENTER_CRITICAL(lock);
    unsigned core = xPortGetCoreID();
    entered[core] = esp_timer_get_time();
    site[core] = tag;
}
void IRAM_ATTR h5_critical_exit(portMUX_TYPE *lock)
{
    unsigned core = xPortGetCoreID();
    uint64_t duration = esp_timer_get_time() - entered[core];
    unsigned us = duration > 65535 ? 65535 : duration;
    if (us > (atomic_load_explicit(&maximum[core], memory_order_relaxed) >> 16))
        atomic_store_explicit(&maximum[core], (us << 16) | site[core], memory_order_relaxed);
    portEXIT_CRITICAL(lock);
}
void IRAM_ATTR h5_critical_reset(void)
{
    atomic_store_explicit(&maximum[0], 0, memory_order_relaxed);
    atomic_store_explicit(&maximum[1], 0, memory_order_relaxed);
}
void h5_critical_report(void)
{
    char line[110];
    for (unsigned core = 0; core < 2; core++) {
        unsigned value = atomic_load_explicit(&maximum[core], memory_order_relaxed);
        snprintf(line, sizeof(line), "[P4CRITICAL:CORE:%u|BODY_US:%u|SITE:%u]\r\n",
                 core, value >> 16, value & 65535);
        hal.stream.write(line);
    }
}
