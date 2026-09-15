/* SPDX-License-Identifier: GPL-3.0-or-later
 * P4-specific HAL. Upstream grblHAL owns planning, interpolation and state.
 * Bench build: actual STEP/DIR signals, but motor enables always inactive.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_ldo_regulator.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "hal/gpio_ll.h"
#include "grbl/hal.h"
#include "grbl/protocol.h"
#include "grbl/state_machine.h"
#include "grbl/task.h"
#include "serial.h"
#include "feedback.h"
#include "bridge.h"

#if !H5_BENCH_ONLY
#error "Machine output enable requires the remaining hardware acceptance gates."
#endif

static gptimer_handle_t step_timer, pulse_timer;
static esp_ldo_channel_handle_t io_ldo;
static portMUX_TYPE core_lock = portMUX_INITIALIZER_UNLOCKED;
static on_execute_realtime_ptr previous_realtime;
static on_settings_changed_ptr previous_settings;
static on_unknown_sys_command_ptr previous_command;
static delay_callback_ptr delayed_callback;
static volatile bool running, fault;
static volatile unsigned pulse_phase; // 0 idle, 1 direction setup, 2 pulse high, 3 hold
static bool reset_after_pulse;
static uint32_t pulse_ticks = 100, direction_ticks = 50, tick_period = 10000;
static axes_signals_t step_invert, direction_invert, pending_steps;
static uint8_t direction;
static volatile struct {
    uint32_t x_pulses, z_pulses, interrupts, overlaps, late, min_period, max_period;
    uint32_t pulse_min, pulse_max, max_isr_us;
    int32_t x_position, z_position;
} diag = {.min_period = UINT32_MAX, .pulse_min = UINT32_MAX};
static uint64_t pulse_started;
typedef struct {
    uint64_t previous;
    uint32_t count, minimum, first[16], last[16];
} pulse_trace_t;
static pulse_trace_t trace_x, trace_z;
static void IRAM_ATTR trace_edge(pulse_trace_t *trace, uint64_t now)
{
    if (trace->previous) {
        uint32_t interval = now - trace->previous;
        if (!trace->minimum || interval < trace->minimum) trace->minimum = interval;
        if (trace->count < 16) trace->first[trace->count] = interval;
        trace->last[trace->count % 16] = interval;
        trace->count++;
    }
    trace->previous = now;
}


static void IRAM_ATTR irq_disable(void) { portENTER_CRITICAL(&core_lock); }
static void IRAM_ATTR irq_enable(void) { portEXIT_CRITICAL(&core_lock); }
static void IRAM_ATTR set_bits(volatile uint_fast16_t *v, uint_fast16_t b)
{ irq_disable(); *v |= b; irq_enable(); }
static uint_fast16_t IRAM_ATTR clear_bits(volatile uint_fast16_t *v, uint_fast16_t b)
{ irq_disable(); uint_fast16_t old = *v; *v &= ~b; irq_enable(); return old; }
static uint_fast16_t IRAM_ATTR set_value(volatile uint_fast16_t *v, uint_fast16_t b)
{ irq_disable(); uint_fast16_t old = *v; *v = b; irq_enable(); return old; }
static void IRAM_ATTR steps_write(uint8_t mask)
{
    mask ^= step_invert.mask;
    gpio_ll_set_level(&GPIO, H5_X_STEP, !!(mask & X_AXIS_BIT));
    gpio_ll_set_level(&GPIO, H5_Z_STEP, !!(mask & Z_AXIS_BIT));
}
static void IRAM_ATTR enable(axes_signals_t axes, bool hold)
{
    // Cannot be bypassed by $X, settings, G-code, or a software reset.
    gpio_ll_set_level(&GPIO, H5_X_ENABLE, 1);
    gpio_ll_set_level(&GPIO, H5_Z_ENABLE, 0);
}
static void IRAM_ATTR reset_direction(void)
{
    direction = 0;
    gpio_ll_set_level(&GPIO, H5_X_DIR, direction_invert.x);
    gpio_ll_set_level(&GPIO, H5_Z_DIR, direction_invert.z);
}
static void IRAM_ATTR idle(bool clear)
{
    irq_disable();
    if (running) {
        running = false;
        gptimer_stop(step_timer);
    }
    if (clear && pulse_timer) {
        // Finish an already asserted pulse even on reset. Changing DIR before
        // its trailing edge would corrupt the final step. The next wake has a
        // 1 ms settle interval, so completion happens before new pulse service.
        if (pulse_phase == 2 || pulse_phase == 3) reset_after_pulse = true;
        else {
            gptimer_set_alarm_action(pulse_timer, NULL);
            pulse_phase = 0;
            steps_write(0);
            // Core st_wake_up clears its direction cache; reset ours too.
            reset_direction();
        }
    }
    irq_enable();
}
void IRAM_ATTR h5_motion_fault(void)
{
    fault = true;
    idle(true);
    enable((axes_signals_t){0}, false);
    system_set_exec_alarm(Alarm_MotorFault);
}
static void IRAM_ATTR schedule_pulse(uint32_t ticks)
{
    uint64_t now;
    gptimer_get_raw_count(pulse_timer, &now);
    gptimer_alarm_config_t alarm = {.alarm_count = now + ticks};
    if (gptimer_set_alarm_action(pulse_timer, &alarm) != ESP_OK) h5_motion_fault();
}
static void IRAM_ATTR assert_pulse(void)
{
    steps_write(pending_steps.mask);
    gptimer_get_raw_count(pulse_timer, &pulse_started);
    if (pending_steps.x) {
        trace_edge(&trace_x, pulse_started);
        diag.x_pulses++;
        diag.x_position += (gpio_ll_get_level(&GPIO, H5_X_DIR) ^ direction_invert.x) ? -1 : 1;
    }
    if (pending_steps.z) {
        trace_edge(&trace_z, pulse_started);
        diag.z_pulses++;
        diag.z_position += (gpio_ll_get_level(&GPIO, H5_Z_DIR) ^ direction_invert.z) ? -1 : 1;
    }
    pulse_phase = 2;
    schedule_pulse(pulse_ticks);
}
static bool IRAM_ATTR pulse_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *ctx)
{
    if (pulse_phase == 1 && !fault) assert_pulse();
    else if (pulse_phase == 2) {
        steps_write(0);
        uint64_t now;
        gptimer_get_raw_count(pulse_timer, &now);
        uint32_t duration = now - pulse_started;
        if (duration < diag.pulse_min) diag.pulse_min = duration;
        if (duration > diag.pulse_max) diag.pulse_max = duration;
        if (reset_after_pulse) {
            pulse_phase = 3;
            schedule_pulse(direction_ticks);
        } else pulse_phase = 0;
    } else if (pulse_phase == 3) {
        reset_direction();
        reset_after_pulse = false;
        pulse_phase = 0;
    }
    return false;
}
static void IRAM_ATTR pulse_start(stepper_t *stepper)
{
    if (fault) return;
    if (stepper->step_out.y) { h5_motion_fault(); return; }
    if (pulse_phase && (stepper->step_out.mask || stepper->dir_changed.mask)) {
        diag.overlaps++;
        h5_motion_fault();
        return;
    }
    if (stepper->dir_changed.mask) {
        direction = stepper->dir_out.mask;
        uint8_t output = direction ^ direction_invert.mask;
        gpio_ll_set_level(&GPIO, H5_X_DIR, !!(output & X_AXIS_BIT));
        gpio_ll_set_level(&GPIO, H5_Z_DIR, !!(output & Z_AXIS_BIT));
    }
    if (stepper->step_out.mask) {
        pending_steps = stepper->step_out;
        if (stepper->dir_changed.mask && direction_ticks) {
            pulse_phase = 1;
            schedule_pulse(direction_ticks);
        } else assert_pulse();
    }
}
static void IRAM_ATTR cycles(uint32_t ticks)
{
    // Never silently clamp an impossible step rate: stop with a visible alarm.
    if (ticks < 250) { h5_motion_fault(); return; } // 40 kHz ISR ceiling
    if (ticks > H5_STEP_HZ * 2) ticks = H5_STEP_HZ * 2;
    uint64_t now;
    gptimer_get_raw_count(step_timer, &now);
    if (running && now + 20 >= ticks) {
        diag.late++;
        h5_motion_fault();
        return;
    }
    tick_period = ticks;
    if (ticks < diag.min_period) diag.min_period = ticks;
    if (ticks > diag.max_period) diag.max_period = ticks;
    gptimer_alarm_config_t alarm = {
        .alarm_count = ticks, .reload_count = 0, .flags.auto_reload_on_alarm = true,
    };
    if (gptimer_set_alarm_action(step_timer, &alarm) != ESP_OK) h5_motion_fault();
}
static bool IRAM_ATTR step_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *ctx)
{
    if (!running || fault) return false;
    uint64_t start = esp_timer_get_time();
    if (event->count_value > tick_period / 2) {
        diag.late++;
        h5_motion_fault();
        return false;
    }
    diag.interrupts++;
    hal.stepper.interrupt_callback();
    uint32_t duration = esp_timer_get_time() - start;
    if (duration > diag.max_isr_us) diag.max_isr_us = duration;
    return false;
}
static void wake(void)
{
    if (fault || running) return;
    enable((axes_signals_t){AXES_BITMASK}, false);
    ESP_ERROR_CHECK(gptimer_set_raw_count(step_timer, 0));
    cycles(10000); // 1 ms driver settle before first planner tick
    running = true;
    ESP_ERROR_CHECK(gptimer_start(step_timer));
}
static limit_signals_t limits(void) { return (limit_signals_t){0}; }
static void limits_enable(bool on, axes_signals_t axes) { }
static control_signals_t controls(void) { return (control_signals_t){.motor_fault = fault}; }
static coolant_state_t coolant_get(void) { return (coolant_state_t){0}; }
static void coolant_set(coolant_state_t state) { }
static uint32_t ticks_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static uint64_t micros(void) { return esp_timer_get_time(); }
static void delayed(void *arg)
{
    delay_callback_ptr callback = delayed_callback;
    delayed_callback = NULL;
    if (callback) callback();
}
static void delay_ms(uint32_t ms, delay_callback_ptr callback)
{
    task_delete(delayed, NULL);
    delayed_callback = callback;
    if (callback) {
        if (!ms) delayed(NULL);
        else if (!task_add_delayed(delayed, NULL, ms)) h5_motion_fault();
    } else if (ms) vTaskDelay(pdMS_TO_TICKS(ms));
}
static void realtime(sys_state_t state)
{
    previous_realtime(state);
    h5_serial_poll();
    h5_bridge_poll();
    // Let the idle task and UART worker run while the core waits for input.
    // Pulse timing belongs solely to the hardware timers, never this delay.
    static uint32_t yielded;
    uint32_t now = ticks_ms();
    if (now != yielded) { yielded = now; vTaskDelay(1); }
}
static void settings_changed(settings_t *s, settings_changed_flags_t changed)
{
    if (previous_settings) previous_settings(s, changed);
    pulse_ticks = (uint32_t)ceilf(fmaxf(2.0f, s->steppers.pulse_microseconds) * 10.0f);
    direction_ticks = (uint32_t)ceilf(fmaxf(5.0f, s->steppers.pulse_delay_microseconds) * 10.0f);
    step_invert = s->steppers.step_invert;
    direction_invert = s->steppers.dir_invert;
    if (pulse_timer && !running) steps_write(0);
}
static status_code_t command(sys_state_t state, char *line)
{
    if (strcmp(line, "P4UI") == 0) {
        h5_status_t snapshot;
        h5_bridge_snapshot(&snapshot);
        char text[160];
        snprintf(text, sizeof(text), "[P4UI:READY:%u|COMMAND:%lu|COMPLETED:%lu|STATUS:%d|GENERATION:%lu|UI_UPDATES:%lu|UPTIME:%lu]\r\n",
            h5_ui_ready(), (unsigned long)snapshot.command_id, (unsigned long)snapshot.completed_id,
            snapshot.command_status, (unsigned long)snapshot.stream_generation,
            (unsigned long)h5_ui_updates(), (unsigned long)hal.get_elapsed_ticks());
        hal.stream.write(text);
        return Status_OK;
    }
    if (strncmp(line, "P4UITEST=", 9) == 0) {
        if (strlen(line) != 10) return Status_InvalidStatement;
        return h5_ui_test_action(line[9]) ? Status_OK : Status_InvalidStatement;
    }
    if (strcmp(line, "P4SCREEN") == 0) {
        if (state != STATE_IDLE || running || pulse_phase) return Status_IdleError;
        return h5_ui_screenshot(hal.stream.write) ? Status_OK : Status_SelfTestFailed;
    }
    if (strcmp(line, "P4ENCODERTEST") == 0) {
        if (state != STATE_IDLE || running || pulse_phase) return Status_IdleError;
        bool ok = h5_feedback_selftest();
        hal.stream.write(ok ? "[P4ENCODERTEST:PASS|COUNTS:32000,-32000,0]\r\n" : "[P4ENCODERTEST:FAIL]\r\n");
        return ok ? Status_OK : Status_SelfTestFailed;
    }
    if (strcmp(line, "P4TRACE=RESET") == 0) {
        if (state != STATE_IDLE || running || pulse_phase) return Status_IdleError;
        memset(&trace_x, 0, sizeof(trace_x));
        memset(&trace_z, 0, sizeof(trace_z));
        return Status_OK;
    }
    if (strcmp(line, "P4TRACE") == 0) {
        if (state != STATE_IDLE || running || pulse_phase) return Status_IdleError;
        pulse_trace_t *traces[] = {&trace_x, &trace_z};
        for (unsigned axis = 0; axis < 2; axis++) {
            pulse_trace_t *t = traces[axis];
            char text[480];
            int len = snprintf(text, sizeof(text), "[P4TRACE:%c|COUNT:%lu|MIN:%lu|FIRST:",
                axis ? 'Z' : 'X', (unsigned long)t->count, (unsigned long)t->minimum);
            unsigned n = t->count < 16 ? t->count : 16;
            for (unsigned i = 0; i < n; i++)
                len += snprintf(text + len, sizeof(text) - len, "%s%lu", i ? "," : "", (unsigned long)t->first[i]);
            len += snprintf(text + len, sizeof(text) - len, "|LAST:");
            for (unsigned i = 0; i < n; i++)
                len += snprintf(text + len, sizeof(text) - len, "%s%lu", i ? "," : "", (unsigned long)t->last[(t->count - n + i) % 16]);
            snprintf(text + len, sizeof(text) - len, "]\r\n");
            hal.stream.write(text);
        }
        return Status_OK;
    }
    if (strcmp(line, "P4") != 0) return previous_command ? previous_command(state, line) : Status_Unhandled;
    int x, z, encoder;
    h5_feedback_read(&x, &z, &encoder);
    irq_disable();
    uint32_t xp = diag.x_pulses, zp = diag.z_pulses, n = diag.interrupts;
    uint32_t overlap = diag.overlaps, late = diag.late, min = diag.min_period, max = diag.max_period;
    uint32_t pmin = diag.pulse_min, pmax = diag.pulse_max, cost = diag.max_isr_us;
    int32_t xpos = diag.x_position, zpos = diag.z_position;
    irq_enable();
    char text[480];
    snprintf(text, sizeof(text), "[P4:BENCH|EN:LOCKED|NVS:RAM|SYNC:OFF|X:%lu,%ld,%d|Z:%lu,%ld,%d|ENC:%d|ISR:%lu|OVERLAP:%lu|LATE:%lu|PERIOD:%lu,%lu|PULSE:%lu,%lu|ISR_US:%lu|RX_OVF:%u|FAULT:%u|ENABLE_PINS:%u,%u]\r\n",
        (unsigned long)xp, (long)xpos, x, (unsigned long)zp, (long)zpos, z, encoder,
        (unsigned long)n, (unsigned long)overlap, (unsigned long)late, (unsigned long)min, (unsigned long)max,
        (unsigned long)pmin, (unsigned long)pmax, (unsigned long)cost, h5_serial_overflows(), fault, gpio_get_level(H5_X_ENABLE), gpio_get_level(H5_Z_ENABLE));
    hal.stream.write(text);
    return Status_OK;
}
static status_code_t validate(modal_groups_t *commands, parser_state_t *state, parser_block_t *block, spindle_t *spindle)
{
    // Core uses three-axis storage; an absent Y must not silently move virtually.
    if (block->words.y || block->words.v) {
        hal.stream.write("[MSG:P4 has no Y axis]\r\n");
        return Status_GcodeUnsupportedCommand;
    }
    if ((block->modal.motion == MotionMode_CwArc || block->modal.motion == MotionMode_CcwArc)
        && block->modal.plane_select != PlaneSelect_ZX) {
        hal.stream.write("[MSG:P4 arcs require G18]\r\n");
        return Status_GcodeUnsupportedCommand;
    }
    return Status_Unhandled;
}
static bool setup(settings_t *s)
{
    ESP_ERROR_CHECK(gpio_set_level(H5_X_ENABLE, 1));
    ESP_ERROR_CHECK(gpio_set_level(H5_Z_ENABLE, 0));
    gpio_config_t outputs = {.pin_bit_mask = (1ULL << H5_X_ENABLE) | (1ULL << H5_Z_ENABLE), .mode = GPIO_MODE_INPUT_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&outputs));
    esp_ldo_channel_config_t ldo = {.chan_id = 4, .voltage_mv = 3300};
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo, &io_ldo));
    outputs.pin_bit_mask = (1ULL << H5_X_STEP) | (1ULL << H5_Z_STEP) | (1ULL << H5_X_DIR) | (1ULL << H5_Z_DIR);
    outputs.mode = GPIO_MODE_INPUT_OUTPUT;
    steps_write(0);
    gpio_set_level(H5_X_DIR, direction_invert.x);
    gpio_set_level(H5_Z_DIR, direction_invert.z);
    ESP_ERROR_CHECK(gpio_config(&outputs));
    h5_feedback_init();
    gptimer_config_t timer = {.clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP,
        .resolution_hz = H5_STEP_HZ, .intr_priority = 3};
    ESP_ERROR_CHECK(gptimer_new_timer(&timer, &step_timer));
    ESP_ERROR_CHECK(gptimer_new_timer(&timer, &pulse_timer));
    gptimer_event_callbacks_t step_cb = {.on_alarm = step_alarm}, pulse_cb = {.on_alarm = pulse_alarm};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(step_timer, &step_cb, NULL));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(pulse_timer, &pulse_cb, NULL));
    ESP_ERROR_CHECK(gptimer_enable(step_timer));
    ESP_ERROR_CHECK(gptimer_enable(pulse_timer));
    ESP_ERROR_CHECK(gptimer_start(pulse_timer));
    return s->version.id == SETTINGS_VERSION;
}
bool driver_init(void)
{
    hal.info = "ESP32-P4";
    hal.driver_version = "260914";
    hal.driver_options = "BENCH_ONLY,GPTIMER,PCNT";
    hal.board = "Waveshare P4 10.1 H5";
    hal.driver_url = "https://github.com/fer662/grblHAL-ESP32";
    hal.f_mcu = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    hal.f_step_timer = H5_STEP_HZ;
    hal.step_us_min = 2;
    hal.driver_setup = setup;
    hal.delay_ms = delay_ms;
    hal.get_elapsed_ticks = ticks_ms;
    hal.get_micros = micros;
    hal.irq_disable = irq_disable;
    hal.irq_enable = irq_enable;
    hal.set_bits_atomic = set_bits;
    hal.clear_bits_atomic = clear_bits;
    hal.set_value_atomic = set_value;
    hal.stepper.wake_up = wake;
    hal.stepper.go_idle = idle;
    hal.stepper.enable = enable;
    hal.stepper.cycles_per_tick = cycles;
    hal.stepper.pulse_start = pulse_start;
    hal.limits.get_state = limits;
    hal.limits.enable = limits_enable;
    hal.control.get_state = controls;
    hal.coolant.get_state = coolant_get;
    hal.coolant.set_state = coolant_set;
    hal.driver_cap.amass_level = 3;
    hal.driver_cap.step_pulse_delay = 1;
    hal.nvs.type = NVS_None; // Core's RAM buffer; never touch H5 settings.
    previous_realtime = grbl.on_execute_realtime;
    grbl.on_execute_realtime = realtime;
    previous_settings = grbl.on_settings_changed;
    grbl.on_settings_changed = settings_changed;
    previous_command = grbl.on_unknown_sys_command;
    grbl.on_unknown_sys_command = command;
    grbl.on_pre_gcode_execute = validate;
    h5_bridge_init();
    return h5_serial_init() && hal.version == 10;
}
