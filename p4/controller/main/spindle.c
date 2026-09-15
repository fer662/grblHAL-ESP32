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
#include "diagnostics_internal.h"
#include "follow.h"
#include "grbl/protocol.h"
#include "grbl/report.h"
#include "feedback.h"

extern void h5_motion_fault(void);
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool ready;
static volatile bool tracking, waiting;
static volatile bool follow_mode, follow_braking;
static volatile float follow_last_rpm=30;
void h5_spindle_follow(bool enabled) {follow_mode=enabled;follow_braking=false;follow_last_rpm=30;}
void h5_spindle_follow_braking(void) {follow_braking=true;}
static spindle_state_t commanded;
static int32_t last_raw;
static int64_t accumulated, origin;
static uint32_t phase, last_poll, last_change, rpm_time;
static uint32_t index_phase;
static float phase_compensation, lead_distance;
static int64_t rpm_count, furthest;
static float measured_rpm;
static spindle_data_t foreground_data, interrupt_data;
static const char *sync_fault = "NONE";
static uint32_t block_pulses, axis_pulses[2];
static unsigned trace_axis=Z_AXIS;
static int64_t first_edge, last_edge;
typedef struct { uint32_t step; int64_t encoder; int64_t us; } sample_t;
static sample_t samples[512];
static unsigned sample_count;
static float block_pitch, block_steps_mm;
float h5_spindle_rpm(void) { return measured_rpm; }

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
    int32_t raw = h5_encoder_count();
    int64_t result = accumulated + count_delta(raw, last_raw);
    portEXIT_CRITICAL(&lock);
    return result;
}
int64_t h5_spindle_position(void) { return position(); }
static int64_t IRAM_ATTR oriented_position(void)
{ int64_t p = position(); return commanded.ccw ? -p : p; }
static int64_t IRAM_ATTR floor_turn(int64_t counts)
{ return counts >= 0 ? counts / H5_ENCODER_CPR : -((-counts + H5_ENCODER_CPR - 1) / H5_ENCODER_CPR); }
bool h5_spindle_waiting_index(void) { return waiting; }
bool h5_spindle_near_index(void)
{
    // A one-tick sleep near a revolution boundary adds variable phase lag.
    // Poll only within ~2 ms of either side of the boundary, yielding normally
    // elsewhere. A stopped encoder cannot trap this task in a busy wait.
    if (!waiting || hal.get_elapsed_ticks() - last_change > 10 || fabsf(measured_rpm) < 30)
        return false;
    int64_t counts = oriented_position() - index_phase;
    unsigned remainder = counts - floor_turn(counts) * H5_ENCODER_CPR;
    unsigned guard = (unsigned)ceilf(fabsf(measured_rpm) * H5_ENCODER_CPR / 30000.0f) + 2;
    if (guard > H5_ENCODER_CPR / 4) guard = H5_ENCODER_CPR / 4;
    return remainder <= guard || remainder >= H5_ENCODER_CPR - guard;
}
static spindle_data_t *IRAM_ATTR get_data(spindle_data_request_t request)
{
    // Separate return objects: the step interrupt can preempt a foreground read.
    spindle_data_t *data = xPortInIsrContext() ? &interrupt_data : &foreground_data;
    int64_t counts = oriented_position();
    if (request == SpindleData_AngularPosition)
        data->angular_position = (float)(counts - origin) / H5_ENCODER_CPR;
    else {
        data->index_count = (uint32_t)floor_turn(counts - index_phase);
        data->pulse_count = (uint32_t)counts;
        // A zero-RPM replan can leave a cancelled G33 waiting on an
        // effectively infinite step period. Assisted feed cancels on stop;
        // retain its last nonzero planning RPM until deceleration finishes.
        float rpm=fabsf(measured_rpm);
        data->rpm = follow_mode && (follow_braking || rpm<30) ? follow_last_rpm : rpm;
        data->ccw = measured_rpm < 0;
        data->state_programmed = commanded;
    }
    return data;
}
static void reset_data(void)
{
    int64_t counts = oriented_position();
    index_phase = phase;
    phase_compensation = lead_distance = 0;
    plan_block_t *block = plan_get_current_block();
    if (block && block->programmed_rate > 0 && block->acceleration > 0) {
        phase_compensation = fmaxf(0, st_get_spindle_sync_offset());
        // Use the exact speed already prepared by grbl. Recomputing nominal
        // speed here would also mutate the planner's RPM tracking state.
        float feed = sqrtf(2 * block->acceleration * phase_compensation); // mm/min
        // Start earlier in spindle phase to offset distance lost during the
        // planned acceleration ramp. Pitch/gearing/axis calibration are intact.
        int32_t advance = (int32_t)lroundf(phase_compensation * H5_ENCODER_CPR / block->programmed_rate);
        index_phase = (phase + H5_ENCODER_CPR - (advance % H5_ENCODER_CPR)) % H5_ENCODER_CPR;
        lead_distance = fmaxf(2 * block->programmed_rate, 4 * phase_compensation + feed / 240.0f);
    }
    origin = floor_turn(counts - index_phase) * H5_ENCODER_CPR + index_phase;
    waiting = true;
}
static void set_state(spindle_ptrs_t *spindle, spindle_state_t state, float rpm)
{ commanded = state; }
static spindle_state_t get_state(spindle_ptrs_t *spindle) { return commanded; }
static void spindle_off(spindle_ptrs_t *spindle) { commanded.value = 0; }
void h5_spindle_init(void)
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
    spindle_register(&spindle, "H5 external spindle (encoder only)");
}
void h5_spindle_ready(void)
{
    last_raw = h5_encoder_count();
    accumulated = last_raw;
    ready = true;
}
void IRAM_ATTR h5_spindle_idle(void) { tracking = waiting = false; }
void IRAM_ATTR h5_spindle_block(stepper_t *stepper)
{
    if (!stepper->new_block) return;
    tracking = stepper->exec_segment->spindle_sync;
    waiting = false;
    if (tracking) {
        block_pulses = sample_count = axis_pulses[0] = axis_pulses[1] = 0;
        trace_axis=stepper->exec_block->steps.value[X_AXIS] > stepper->exec_block->steps.value[Z_AXIS] ? X_AXIS : Z_AXIS;
        block_pitch = stepper->exec_block->programmed_rate;
        block_steps_mm = stepper->exec_block->steps_per_mm;
        first_edge = last_edge = 0;
        furthest = oriented_position();
    }
}
void IRAM_ATTR h5_spindle_edge(axes_signals_t steps)
{
    if (!tracking) return;
    axis_pulses[0] += steps.x; axis_pulses[1] += steps.z;
    if (!(steps.mask & (1U << trace_axis))) return;
    int64_t counts = oriented_position();
    if (!block_pulses) first_edge = counts;
    last_edge = counts;
    block_pulses++;
    if (sample_count < 512 && (block_pulses == 1 || block_pulses % 16 == 0))
        samples[sample_count++] = (sample_t){block_pulses, counts, esp_timer_get_time()};
}

// Bench generator: real A/B GPIO transitions counted by the existing PCNT unit.
// It never commands STEP/DIR and is only available behind H5_BENCH_ONLY.
#if !H5_BENCH_ONLY
#error "Remove the synthetic encoder before enabling machine outputs."
#endif
static gptimer_handle_t simulator;
static volatile unsigned gray_phase;
static volatile int sim_direction = 1;
static bool sim_running, change_pending;
static float sim_rpm;
static int next_rpm;
static uint32_t change_at;
static volatile uint32_t simulator_interval;
static uint32_t applied_interval;
static bool ramp_pending;
static float ramp_from, ramp_to, ramp_rate;
static uint32_t ramp_at, ramp_poll;
bool h5_spindle_simulator_active(void) { return simulator != NULL; }
static bool IRAM_ATTR sim_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *ctx)
{
    static const uint8_t gray[] = {0, 1, 3, 2}; // bit0 B, bit1 A: positive x2 counts
    gray_phase = (gray_phase + (sim_direction > 0 ? 1 : 3)) & 3;
    uint8_t bits = gray[gray_phase];
    gpio_ll_set_level(&GPIO, H5_ENCODER_A, !!(bits & 2));
    gpio_ll_set_level(&GPIO, H5_ENCODER_B, !!(bits & 1));
    uint32_t interval = simulator_interval;
    if (interval != applied_interval) {
        gptimer_alarm_config_t alarm = {.alarm_count = interval, .reload_count = 0, .flags.auto_reload_on_alarm = true};
        gptimer_set_alarm_action(timer, &alarm);
        applied_interval = interval;
    }
    return false;
}
static bool simulate(float rpm)
{
    if (!isfinite(rpm) || fabsf(rpm) > 600 || (rpm && fabsf(rpm) < 1)) return false;
    if (!simulator) {
        gptimer_config_t cfg = {.clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP, .resolution_hz = 10000000, .intr_priority = 2};
        if (gptimer_new_timer(&cfg, &simulator) != ESP_OK) return false;
        gptimer_event_callbacks_t cb = {.on_alarm = sim_alarm};
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(simulator, &cb, NULL));
        ESP_ERROR_CHECK(gptimer_enable(simulator));
        gpio_set_level(H5_ENCODER_A, 0); gpio_set_level(H5_ENCODER_B, 0);
        ESP_ERROR_CHECK(gpio_set_direction(H5_ENCODER_A, GPIO_MODE_INPUT_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_direction(H5_ENCODER_B, GPIO_MODE_INPUT_OUTPUT));
    }
    sim_rpm = rpm;
    if (!rpm) {
        if (sim_running) ESP_ERROR_CHECK(gptimer_stop(simulator));
        sim_running = false;
    }
    if (rpm) {
        sim_direction = rpm > 0 ? 1 : -1;
        uint32_t interval = (uint32_t)lroundf(300000000.0f / (H5_ENCODER_CPR * fabsf(rpm)));
        simulator_interval = interval;
        // Apply rate changes at the next quadrature edge, preserving phase.
        // Stopping/resetting the timer for each ramp update would lose time.
        if (sim_running) return true;
        applied_interval = interval;
        gptimer_alarm_config_t alarm = {.alarm_count = interval, .reload_count = 0, .flags.auto_reload_on_alarm = true};
        ESP_ERROR_CHECK(gptimer_set_raw_count(simulator, 0));
        ESP_ERROR_CHECK(gptimer_set_alarm_action(simulator, &alarm));
        ESP_ERROR_CHECK(gptimer_start(simulator));
        sim_running = true;
    }
    return true;
}
void h5_spindle_poll(void)
{
    if (!ready) return;
    uint32_t now = hal.get_elapsed_ticks();
    if (now - last_poll >= 2) {
        last_poll = now;
        portENTER_CRITICAL(&lock);
        int32_t raw = h5_encoder_count();
        int32_t delta = count_delta(raw, last_raw);
        accumulated += delta;
        last_raw = raw;
        int64_t counts = accumulated;
        portEXIT_CRITICAL(&lock);
        if (delta) last_change = now;
        if (now - rpm_time >= 50) {
            measured_rpm = (float)(counts - rpm_count) * 60000.0f / (H5_ENCODER_CPR * (now - rpm_time));
            if(follow_mode && !follow_braking && fabsf(measured_rpm)>=30)follow_last_rpm=fabsf(measured_rpm);
            rpm_count = counts; rpm_time = now;
        }
        int64_t oriented = commanded.ccw ? -counts : counts;
        if (tracking && !h5_follow_busy()) {
            if (oriented > furthest) furthest = oriented;
            if (now - last_change > 100) { sync_fault = "STALL"; h5_motion_fault(); }
            else if (furthest - oriented > 3) { sync_fault = "REVERSED"; h5_motion_fault(); }
        }
    }
    if (change_pending && (int32_t)(now - change_at) >= 0) {
        change_pending = false;
        simulate(next_rpm);
    }
    if (ramp_pending && (int32_t)(now - ramp_at) >= 0 && now - ramp_poll >= 2) {
        ramp_poll = now;
        float change = ramp_rate * (now - ramp_at) / 1000.0f;
        float distance = fabsf(ramp_to - ramp_from);
        if (change >= distance) { simulate(ramp_to); ramp_pending = false; }
        else simulate(ramp_from + copysignf(change, ramp_to - ramp_from));
    }
    // Core's index wait calls this hook but does not service '?' itself.
    if (waiting && (sys.rt_exec_state & EXEC_STATUS_REPORT)) {
        system_clear_exec_state_flag(EXEC_STATUS_REPORT);
        report_realtime_status(hal.stream.write_all, &hal.stream.report);
    }
}
status_code_t h5_spindle_command(sys_state_t state, char *line)
{
    if (!strcmp(line, "P4SYNC")) {
        char text[448];
        snprintf(text, sizeof(text), "[P4SYNC:RPM:%.2f|RAW:%lld|PHASE:%lu|TRACK:%u|WAIT:%u|FAULT:%s|SIM:%.3f|PULSES:%lu|FIRST:%lld|LAST:%lld|PITCH:%.6f|STEPS_MM:%.3f|INDEX_PHASE:%lu|COMP_MM:%.6f|LEAD_MM:%.6f|TRACE_AXIS:%c|AXIS_STEPS:%lu,%lu]\r\n",
            (double)measured_rpm, (long long)position(), (unsigned long)phase, tracking, waiting, sync_fault, sim_rpm,
            (unsigned long)block_pulses, (long long)first_edge, (long long)last_edge, (double)block_pitch, (double)block_steps_mm,
            (unsigned long)index_phase, (double)phase_compensation, (double)lead_distance, trace_axis==X_AXIS ? 'X' : 'Z', (unsigned long)axis_pulses[0], (unsigned long)axis_pulses[1]);
        hal.stream.write(text);
        return Status_OK;
    }
    if (!strcmp(line, "P4SYNCTRACE")) {
        if (state != STATE_IDLE) return Status_IdleError;
        char text[80];
        for (unsigned i = 0; i < sample_count; i++) {
            snprintf(text, sizeof(text), "[P4SYNCPOINT:%lu,%lld,%lld]\r\n", (unsigned long)samples[i].step, (long long)samples[i].encoder, (long long)samples[i].us);
            hal.stream.write(text);
        }
        return Status_OK;
    }
    if (!strncmp(line, "P4SIM=", 6)) {
        if (state != STATE_IDLE) return Status_IdleError;
        if (!strcmp(line + 6, "OFF")) {
            change_pending = ramp_pending = false;
            if (simulator) {
                if (sim_running) ESP_ERROR_CHECK(gptimer_stop(simulator));
                ESP_ERROR_CHECK(gptimer_disable(simulator));
                ESP_ERROR_CHECK(gptimer_del_timer(simulator));
                simulator = NULL;
                sim_running = false; sim_rpm = 0; gray_phase = 0;
                ESP_ERROR_CHECK(gpio_set_direction(H5_ENCODER_A, GPIO_MODE_INPUT));
                ESP_ERROR_CHECK(gpio_set_direction(H5_ENCODER_B, GPIO_MODE_INPUT));
            }
            return Status_OK;
        }
        char *end; long rpm = strtol(line + 6, &end, 10);
        if (*end || end == line + 6 || labs(rpm) > 600) return Status_InvalidStatement;
        change_pending = ramp_pending = false;
        return simulate(rpm) ? Status_OK : Status_InvalidStatement;
    }
    if (!strncmp(line, "P4SIMCHANGE=", 12)) {
        if (state != STATE_IDLE) return Status_IdleError;
        int rpm, delay; char extra;
        if (sscanf(line + 12, "%d,%d%c", &rpm, &delay, &extra) != 2 || abs(rpm) > 600 || (rpm && abs(rpm) < 1) || delay < 100 || delay > 30000)
            return Status_InvalidStatement;
        ramp_pending = false;
        next_rpm = rpm; change_at = hal.get_elapsed_ticks() + delay; change_pending = true;
        return Status_OK;
    }
    if (!strncmp(line, "P4SIMRAMP=", 10)) {
        if (state != STATE_IDLE || !sim_running) return Status_IdleError;
        int rpm, rate, delay; char extra;
        if (sscanf(line + 10, "%d,%d,%d%c", &rpm, &rate, &delay, &extra) != 3 ||
            abs(rpm) < 1 || abs(rpm) > 600 || rpm * sim_rpm <= 0 ||
            rate < 1 || rate > 1000 || delay < 100 || delay > 30000)
            return Status_InvalidStatement;
        change_pending = false;
        ramp_from = sim_rpm; ramp_to = rpm; ramp_rate = rate;
        ramp_at = hal.get_elapsed_ticks() + delay; ramp_pending = true;
        return Status_OK;
    }
    if (!strncmp(line, "P4PHASE=", 8)) {
        if (state != STATE_IDLE) return Status_IdleError;
        char *end; long value = strtol(line + 8, &end, 10);
        if (*end || end == line + 8 || value < 0 || value >= H5_ENCODER_CPR) return Status_InvalidStatement;
        phase = value;
        return Status_OK;
    }
    return Status_Unhandled;
}

void h5_spindle_snapshot(h5_diagnostics_t *s)
{
    s->encoder = position(); s->rpm = measured_rpm;
    s->tracking = tracking; s->waiting = waiting; s->simulated = h5_spindle_simulator_active();
    strncpy(s->sync_fault, sync_fault, sizeof(s->sync_fault) - 1);
}
