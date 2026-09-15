#!/usr/bin/env python3
"""Compile the actual P4 enable callback against GPIO stubs; never touches hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/driver.c').read_text()
start = source.index('static void IRAM_ATTR enable(')
body = source.index('{', start)
level = 1
end = body + 1
while level:
    level += (source[end] == '{') - (source[end] == '}')
    end += 1
callback = source[start:source.index('static void IRAM_ATTR reset_direction', end)]
start = source.index('static status_code_t validate(')
validation = source[start:source.index('static bool setup(', start)]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define IRAM_ATTR
#define H5_X_ENABLE 47
#define H5_Z_ENABLE 30
static bool fault, outputs_ready;
typedef union { uint8_t mask; struct { uint8_t x:1,y:1,z:1,unused:5; }; } axes_signals_t;
static axes_signals_t enable_invert = {.mask=1};
static axes_signals_t enables_requested;
static uint8_t disabled_requested, disabled_applied;
static bool axis_stop_requested, axis_change_pending;
#define X_AXIS_BIT 1
#define Z_AXIS_BIT 4
#define EXEC_STOP 1
#define CMD_STOP 0x19
#define STATE_IDLE 0
#define STATE_ALARM 1
#define STATE_CYCLE 2
static struct { unsigned rt_exec_state; } sys;
static bool timer_active, stepping, planner_active, cycle_active;
static unsigned state, stops, cancels, brakes;
static bool h5_motion_idle(void) { return !timer_active; }
static bool st_is_stepping(void) { return stepping; }
static bool plan_get_current_block(void) { return planner_active; }
static bool h5_cycle_busy(void) { return cycle_active; }
static unsigned state_get(void) { return state; }
static void h5_cycle_cancel(void) { cancels++; }
static void h5_spindle_follow_braking(void) { brakes++; }
static void protocol_enqueue_realtime_command(unsigned command)
{ assert(command==CMD_STOP); sys.rt_exec_state|=EXEC_STOP; stops++; }
static unsigned nesting, writes, pins[64], GPIO;
static void irq_disable(void) { nesting++; }
static void irq_enable(void) { assert(nesting); nesting--; }
static void gpio_ll_set_level(unsigned *gpio, unsigned pin, unsigned value)
{ (void)gpio; assert(nesting); assert(pin==47 || pin==30); assert(value<=1);pins[pin]=value;writes++; }
CALLBACK
typedef int status_code_t;
typedef int parser_state_t;
typedef int spindle_t;
typedef struct { bool G1; } modal_groups_t;
typedef struct {
    struct { bool x,y,z,u,v,w; } words;
    struct { int motion, plane_select; } modal;
    int non_modal_command;
} parser_block_t;
enum { Status_Unhandled, Status_IdleError, Status_SettingDisabled, Status_GcodeUnsupportedCommand };
enum { MotionMode_CwArc=2, MotionMode_CcwArc, MotionMode_Threading };
enum { NonModal_GoHome_0=28, NonModal_GoHome_1=30, PlaneSelect_ZX=18 };
static bool h5_update_active(void) { return false; }
static bool h5_ui_ready(void) { return true; }
static void discard_report(const char *s) { (void)s; }
static struct { struct { void (*write)(const char *); } stream; } hal={{discard_report}};
VALIDATION
int main(void)
{
    for (unsigned inverted=0; inverted<8; inverted++) {
        enable_invert.mask=inverted;
        for (unsigned ready=0; ready<2; ready++) {
            outputs_ready=ready;
            for (unsigned failed=0; failed<2; failed++) {
                fault=failed;
                for (unsigned hold=0; hold<2; hold++) {
                    for (unsigned requested=0; requested<8; requested++) {
                      for (unsigned disabled=0; disabled<8; disabled++) {
                        disabled_applied=disabled;
                        writes=0;
                        enable((axes_signals_t){.mask=requested},hold);
                        bool blocked=H5_BENCH_ONLY || failed || !ready;
                        unsigned effective=requested&~disabled;
                        assert(pins[47]==(blocked ? 1 : ((effective^inverted)&1)));
                        assert(pins[30]==(blocked ? 0 : (((effective^inverted)>>2)&1)));
                        assert(enables_requested.mask==requested);
                        assert(writes==2 && nesting==0);
                      }
                    }
                }
            }
        }
    }
    // A fault must remove an enable that was already asserted.
    enable_invert.mask=1;outputs_ready=true;fault=false;
    enable((axes_signals_t){.mask=5},false);
    fault=true;
    enable((axes_signals_t){.mask=5},true);
    assert(pins[47]==1 && pins[30]==0);

    // Disable while moving: retain torque through STOP dispatch, deceleration,
    // final pulse and planner cleanup. Core STOP itself has a separate test.
    disabled_applied=0;fault=false;state=STATE_CYCLE;
    timer_active=stepping=planner_active=cycle_active=true;
    enable((axes_signals_t){.mask=5},false);
    h5_axis_set_disabled('X',true);
    assert(h5_axis_change_pending() && !disabled_applied);
    axis_enable_poll();
    assert(stops==1 && cancels==1 && brakes==1 && !disabled_applied);
    axis_enable_poll(); // STOP still queued
    assert(stops==1 && !disabled_applied);
    sys.rt_exec_state=0;
    axis_enable_poll(); // still decelerating
    assert(!disabled_applied);
    state=STATE_IDLE;stepping=planner_active=cycle_active=false;
    axis_enable_poll(); // final pulse still asserted
    assert(!disabled_applied);
    timer_active=false;planner_active=true;
    axis_enable_poll(); // queued planner blocks must be discarded first
    assert(!disabled_applied);
    planner_active=false;
    axis_enable_poll();
    assert(disabled_applied==1 && !h5_axis_change_pending());
    assert(pins[47]==1 && pins[30]==!H5_BENCH_ONLY);
    // Later wakes and idle-hold callbacks must not re-energize disabled X.
    enable((axes_signals_t){.mask=5},false);
    enable((axes_signals_t){.mask=5},true);
    assert(pins[47]==1 && pins[30]==!H5_BENCH_ONLY);
    // Rapid toggles latch a stop even when the final request returns to ON.
    h5_axis_set_disabled('X',false);
    h5_axis_set_disabled('X',true);
    h5_axis_set_disabled('X',false);
    axis_enable_poll();
    assert(stops==2 && disabled_applied==1);
    axis_enable_poll();
    assert(disabled_applied==1);
    sys.rt_exec_state=0;axis_enable_poll();
    assert(!disabled_applied && !h5_axis_change_pending());
    assert(pins[47]==H5_BENCH_ONLY && pins[30]==!H5_BENCH_ONLY);
    // Simultaneous X/Z requests and a newer request during cancellation.
    h5_axis_set_disabled('X',true);h5_axis_set_disabled('Z',true);
    axis_enable_poll();sys.rt_exec_state=0;
    h5_axis_set_disabled('X',false);
    axis_enable_poll();
    assert(h5_axis_change_pending() && !disabled_applied);
    sys.rt_exec_state=0;axis_enable_poll();
    assert(disabled_applied==4 && !h5_axis_change_pending());
    assert(pins[47]==H5_BENCH_ONLY && pins[30]==0);
    h5_axis_set_disabled('Z',true);h5_axis_set_disabled('Y',true);
    assert(!h5_axis_change_pending()); // redundant/invalid request
    // Core enable resets cannot clear the user's choice.
    enable((axes_signals_t){0},false);
    enable((axes_signals_t){.mask=5},false);
    assert(disabled_applied==4 && pins[30]==0);
    // A disabled Z permits X-only moves but rejects Z and implicit travel.
    modal_groups_t commands={.G1=true};
    parser_block_t block={.words.x=true};
    assert(validate(&commands,0,&block,0)==Status_Unhandled);
    block.words.z=true;
    assert(validate(&commands,0,&block,0)==Status_SettingDisabled);
    block=(parser_block_t){.non_modal_command=NonModal_GoHome_0};
    assert(validate(&commands,0,&block,0)==Status_SettingDisabled);
    block.non_modal_command=NonModal_GoHome_1;
    assert(validate(&commands,0,&block,0)==Status_SettingDisabled);
    block=(parser_block_t){.modal={MotionMode_CwArc,PlaneSelect_ZX}};
    assert(validate(&commands,0,&block,0)==Status_SettingDisabled);
    block.modal.motion=MotionMode_CcwArc;
    assert(validate(&commands,0,&block,0)==Status_SettingDisabled);
    h5_axis_set_disabled('Z',false);
    block=(parser_block_t){.words.x=true};
    assert(validate(&commands,0,&block,0)==Status_IdleError);
}
'''.replace('CALLBACK', callback).replace('VALIDATION', validation)
with tempfile.TemporaryDirectory(prefix='h5-enable-test-') as directory:
    code=Path(directory)/'test.c'; code.write_text(harness)
    for bench in (0,1):
        executable=Path(directory)/f'enable-{bench}'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                        f'-DH5_BENCH_ONLY={bench}',str(code),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)
print('PASS: GPIO polarity/disable masks, stop-before-release, queue/pulse drain, rapid toggles, reset persistence, fault and bench lock')
