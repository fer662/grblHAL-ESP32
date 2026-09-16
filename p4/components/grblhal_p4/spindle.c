/* SPDX-License-Identifier: GPL-3.0-or-later
 * Encoder-only external spindle. M3/M4 arm an expected direction; no relay/PWM.
 * The phase reference is derived from the calibrated A/B count, not an index pin.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"
#include "esp_timer.h"
#include "spindle.h"
#include "grbl/protocol.h"
#include "grbl/report.h"
#include "feedback.h"

extern void p4_motion_fault(void);
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool ready;
static volatile bool tracking, waiting;
static spindle_state_t commanded;
static int32_t last_raw;
static int64_t accumulated, origin;
static uint32_t phase, last_poll, last_change, rpm_time;
static uint32_t index_phase;
static int64_t rpm_count, furthest;
static float measured_rpm;
static spindle_data_t foreground_data, interrupt_data;
float p4_spindle_rpm(void) { return measured_rpm; }

// PCNT resets at +/-30000. A higher-priority step IRQ can observe the
// reset before the PCNT ISR updates its software accumulator. Fold that
// transient +/-30000 jump; active sampling is far more frequent than 15000
// encoder counts. 30000 is also exactly 25 calibrated spindle revolutions.
static int32_t IRAM_ATTR count_delta(int32_t current, int32_t previous)
{
    int32_t delta = (int32_t)((uint32_t)current - (uint32_t)previous);
    if (delta > 15000) delta -= 30000;
    else if (delta < -15000) delta += 30000;
    return delta;
}
static int64_t IRAM_ATTR position(void)
{
    if (!ready) return 0;
    portENTER_CRITICAL(&lock);
    int32_t raw = p4_encoder_count();
    int64_t result = accumulated + count_delta(raw, last_raw);
    portEXIT_CRITICAL(&lock);
    return result;
}
int64_t p4_spindle_position(void) { return position(); }
static int64_t IRAM_ATTR oriented_position(void)
{ int64_t p = position(); return commanded.ccw ? -p : p; }
static int64_t IRAM_ATTR floor_turn(int64_t counts)
{ return counts >= 0 ? counts / P4_ENCODER_CPR : -((-counts + P4_ENCODER_CPR - 1) / P4_ENCODER_CPR); }
bool p4_spindle_waiting_index(void) { return waiting; }
bool p4_spindle_near_index(void)
{
    // A one-tick sleep near a revolution boundary adds variable phase lag.
    // Poll only within ~2 ms of either side of the boundary, yielding normally
    // elsewhere. A stopped encoder cannot trap this task in a busy wait.
    if (!waiting || hal.get_elapsed_ticks() - last_change > 10 || fabsf(measured_rpm) < 30)
        return false;
    int64_t counts = oriented_position() - index_phase;
    unsigned remainder = counts - floor_turn(counts) * P4_ENCODER_CPR;
    unsigned guard = (unsigned)ceilf(fabsf(measured_rpm) * P4_ENCODER_CPR / 30000.0f) + 2;
    if (guard > P4_ENCODER_CPR / 4) guard = P4_ENCODER_CPR / 4;
    return remainder <= guard || remainder >= P4_ENCODER_CPR - guard;
}
static spindle_data_t *IRAM_ATTR get_data(spindle_data_request_t request)
{
    // Separate return objects: the step interrupt can preempt a foreground read.
    spindle_data_t *data = xPortInIsrContext() ? &interrupt_data : &foreground_data;
    int64_t counts = oriented_position();
    if (request == SpindleData_AngularPosition)
        data->angular_position = (float)(counts - origin) / P4_ENCODER_CPR;
    else {
        data->index_count = (uint32_t)floor_turn(counts - index_phase);
        data->pulse_count = (uint32_t)counts;
        data->rpm = fabsf(measured_rpm);
        data->ccw = measured_rpm < 0;
        data->state_programmed = commanded;
    }
    return data;
}
static void reset_data(void)
{
    int64_t counts = oriented_position();
    index_phase = phase;
    origin = floor_turn(counts - index_phase) * P4_ENCODER_CPR + index_phase;
    waiting = true;
}
static void set_state(spindle_ptrs_t *spindle, spindle_state_t state, float rpm)
{ commanded = state; }
static spindle_state_t get_state(spindle_ptrs_t *spindle) { return commanded; }
static void spindle_off(spindle_ptrs_t *spindle) { commanded.value = 0; }
void p4_spindle_init(void)
{
    static const spindle_ptrs_t spindle = {
        .type = SpindleType_Basic, .ref_id = SPINDLE_ONOFF0_DIR,
        .cap.direction = On,
        .set_state = set_state, .get_state = get_state, .esp32_off = spindle_off,
        .get_data = get_data, .reset_data = reset_data,
    };
    hal.spindle_data.get = get_data;
    hal.spindle_data.reset = reset_data;
    hal.driver_cap.spindle_encoder = On;
    spindle_register(&spindle, "External spindle (encoder only)");
}
void p4_spindle_ready(void)
{
    last_raw = p4_encoder_count();
    accumulated = last_raw;
    ready = true;
}
void IRAM_ATTR p4_spindle_idle(void) { tracking = waiting = false; }
void IRAM_ATTR p4_spindle_block(stepper_t *stepper)
{
    if (!stepper->new_block) return;
    tracking = stepper->exec_segment->spindle_sync;
    waiting = false;
    if (tracking) furthest = oriented_position();
}
void p4_spindle_poll(void)
{
    if (!ready) return;
    uint32_t now = hal.get_elapsed_ticks();
    if (now - last_poll >= 2) {
        last_poll = now;
        portENTER_CRITICAL(&lock);
        int32_t raw = p4_encoder_count();
        int32_t delta = count_delta(raw, last_raw);
        accumulated += delta;
        last_raw = raw;
        int64_t counts = accumulated;
        portEXIT_CRITICAL(&lock);
        if (delta) last_change = now;
        if (now - rpm_time >= 50) {
            measured_rpm = (float)(counts - rpm_count) * 60000.0f / (P4_ENCODER_CPR * (now - rpm_time));
            rpm_count = counts; rpm_time = now;
        }
        int64_t oriented = commanded.ccw ? -counts : counts;
        if (tracking) {
            if (oriented > furthest) furthest = oriented;
            if (now - last_change > 100) { p4_motion_fault(); }
            else if (furthest - oriented > 3) { p4_motion_fault(); }
        }
    }
    // Core's index wait calls this hook but does not service '?' itself.
    if (waiting && (sys.rt_exec_state & EXEC_STATUS_REPORT)) {
        system_clear_exec_state_flag(EXEC_STATUS_REPORT);
        report_realtime_status(hal.stream.write_all, &hal.stream.report);
    }
}
