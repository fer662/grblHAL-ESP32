#!/usr/bin/env python3
"""Run the production pulse/enable callbacks with host GPIO/timer stubs.

No hardware access. Checks reset ordering, direction setup, fault inhibition,
and both compile-time enable policies after extracting the application layer.
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'components/grblhal_p4/driver.c').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]

harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define IRAM_ATTR
#define P4_X_STEP 49
#define P4_X_DIR 31
#define P4_X_ENABLE 47
#define P4_X_ENABLE_OFF 1
#define P4_Z_STEP 28
#define P4_Z_DIR 29
#define P4_Z_ENABLE 30
#define P4_Z_ENABLE_OFF 0
#define X_AXIS_BIT 1
#define Z_AXIS_BIT 4
#define P4_STEP_HZ 10000000UL
#define ESP_OK 0
#define Alarm_MotorFault 1
static int GPIO;
static unsigned gpio[64], alarm_code, timer_stops, spindle_idle_calls;
static void gpio_ll_set_level(void *dev, unsigned pin, unsigned level)
{ (void)dev; gpio[pin] = level; }
static unsigned gpio_ll_get_level(void *dev, unsigned pin)
{ (void)dev; return gpio[pin]; }
typedef union {
    uint8_t mask;
    struct { uint8_t x:1, y:1, z:1; };
} axes_signals_t;
typedef struct { axes_signals_t step_out, dir_out, dir_changed; } stepper_t;
typedef int gptimer_handle_t;
typedef struct { uint64_t count_value; } gptimer_alarm_event_data_t;
typedef struct {
    uint64_t alarm_count, reload_count;
    struct { bool auto_reload_on_alarm; } flags;
} gptimer_alarm_config_t;
static gptimer_handle_t step_timer = 1, pulse_timer = 2;
static uint64_t now, alarm_at;
static bool alarm_enabled;
static int gptimer_get_raw_count(gptimer_handle_t timer, uint64_t *out)
{ (void)timer; *out = now; return ESP_OK; }
static int gptimer_set_alarm_action(gptimer_handle_t timer, const gptimer_alarm_config_t *alarm)
{ (void)timer; alarm_enabled = alarm != 0; alarm_at = alarm ? alarm->alarm_count : 0; return ESP_OK; }
static void gptimer_stop(gptimer_handle_t timer) { (void)timer; timer_stops++; }
static void system_set_exec_alarm(unsigned code) { alarm_code = code; }
static void irq_disable(void) {}
static void irq_enable(void) {}
static void p4_spindle_idle(void) { spindle_idle_calls++; }
static void p4_spindle_block(stepper_t *stepper) { (void)stepper; }
static bool running, fault, outputs_ready, reset_after_pulse;
static unsigned pulse_phase;
static uint32_t pulse_ticks = 100, direction_ticks = 50, tick_period = 10000;
static uint64_t pulse_started;
static uint8_t direction;
static axes_signals_t step_invert, direction_invert, pending_steps;
static axes_signals_t enable_invert = {.mask=1};
typedef struct { unsigned count; } pulse_trace_t;
static pulse_trace_t trace_x, trace_z;
static void trace_edge(pulse_trace_t *trace, uint64_t time) { (void)time; trace->count++; }
void p4_motion_fault(void);
'''
a = source.index('static volatile struct {')
b = source.index('\nstatic uint64_t pulse_started', a)
harness += '\n' + source[a:b] + '\n'
for signature in [
    'static void IRAM_ATTR steps_write(',
    'static void IRAM_ATTR enable(',
    'static void IRAM_ATTR reset_direction(',
    'static void IRAM_ATTR idle(',
    'void IRAM_ATTR p4_motion_fault(',
    'static void IRAM_ATTR schedule_pulse(',
    'static void IRAM_ATTR assert_pulse(',
    'static bool IRAM_ATTR pulse_alarm(',
    'static void IRAM_ATTR pulse_start(',
    'static void IRAM_ATTR cycles(',
]:
    harness += function(signature) + '\n'
harness += r'''
static void reset(void)
{
    memset(gpio, 0, sizeof(gpio));
    memset((void *)&diag, 0, sizeof(diag));
    fault = running = outputs_ready = reset_after_pulse = false;
    pulse_phase = alarm_code = timer_stops = 0;
    direction = 0; now = 1000;
    step_invert.mask = direction_invert.mask = 0;
    enable_invert.mask = 1;
}
int main(void)
{
    reset();
    enable((axes_signals_t){.mask=5}, false);
    assert(gpio[P4_X_ENABLE] == 1 && gpio[P4_Z_ENABLE] == 0);
    outputs_ready = true;
    enable((axes_signals_t){.mask=5}, false);
    assert(gpio[P4_X_ENABLE] == (P4_BENCH_ONLY ? 1 : 0));
    assert(gpio[P4_Z_ENABLE] == (P4_BENCH_ONLY ? 0 : 1));
    enable((axes_signals_t){.mask=0}, false);
    assert(gpio[P4_X_ENABLE] == 1 && gpio[P4_Z_ENABLE] == 0);

    reset(); running = true;
    stepper_t step = {.step_out.mask=1, .dir_out.mask=1, .dir_changed.mask=1};
    pulse_start(&step);
    assert(step.dir_changed.mask == 0 && pulse_phase == 1);
    assert(gpio[P4_X_DIR] == 1 && gpio[P4_X_STEP] == 0);
    assert(alarm_at == now + direction_ticks);
    now += direction_ticks;
    pulse_alarm(pulse_timer, 0, 0);
    assert(pulse_phase == 2 && gpio[P4_X_STEP] == 1);
    assert(diag.x_pulses == 1 && diag.x_position == -1);
    idle(true);
    assert(!running && timer_stops == 1 && reset_after_pulse);
    assert(gpio[P4_X_STEP] == 1 && gpio[P4_X_DIR] == 1);
    now += pulse_ticks;
    pulse_alarm(pulse_timer, 0, 0);
    assert(gpio[P4_X_STEP] == 0 && gpio[P4_X_DIR] == 1 && pulse_phase == 3);
    now += direction_ticks;
    pulse_alarm(pulse_timer, 0, 0);
    assert(gpio[P4_X_DIR] == 0 && pulse_phase == 0 && !reset_after_pulse);

    reset(); running = true;
    step.dir_changed.mask = 1;
    pulse_start(&step);
    idle(true);
    assert(pulse_phase == 0 && !alarm_enabled && gpio[P4_X_STEP] == 0);
    assert(diag.x_pulses == 0);

    reset(); outputs_ready = running = true;
    step.dir_changed.mask = 0;
    pulse_start(&step);
    pulse_start(&step); // Cannot overlap an asserted pulse.
    assert(fault && alarm_code == Alarm_MotorFault && diag.overlaps == 1);
    enable((axes_signals_t){.mask=5}, false);
    assert(gpio[P4_X_ENABLE] == 1 && gpio[P4_Z_ENABLE] == 0);

    reset(); cycles(249);
    assert(fault && alarm_code == Alarm_MotorFault);
    reset(); running = true; now = 990; cycles(1000);
    assert(fault && diag.late == 1 && diag.deadline_kind == 1);
    reset(); now = 0; cycles(1000);
    assert(!fault && tick_period == 1000 && alarm_at == 1000);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / 'test.c').write_text(harness)
    for bench in (0, 1):
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', f'-DP4_BENCH_ONLY={bench}',
                        str(root / 'test.c'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True, cwd=root)
print('PASS: enable policies, DIR setup/hold, reset pulse completion, overlap/deadline faults')
