/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fpu_isr.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
extern void p4_fpu_clobber(void);
extern bool p4_fpu_canary(volatile uint32_t *count);
static volatile uint32_t count;
static bool IRAM_ATTR probe_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *ctx)
{
    p4_fpu_call(p4_fpu_clobber);
    count++;
    return false;
}
bool p4_fpu_test(void)
{
    gptimer_handle_t timer;
    gptimer_config_t config = {.clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000, .intr_priority = 3};
    if (gptimer_new_timer(&config, &timer) != ESP_OK) return false;
    gptimer_event_callbacks_t cb = {.on_alarm = probe_alarm};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cb, NULL));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    gptimer_alarm_config_t alarm = {.alarm_count = 100, .reload_count = 0, .flags.auto_reload_on_alarm = true};
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));
    count = 0;
    ESP_ERROR_CHECK(gptimer_start(timer));
    bool ok = p4_fpu_canary(&count);
    ESP_ERROR_CHECK(gptimer_stop(timer));
    ESP_ERROR_CHECK(gptimer_disable(timer));
    ESP_ERROR_CHECK(gptimer_del_timer(timer));
    return ok;
}
