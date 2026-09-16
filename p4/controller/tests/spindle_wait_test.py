#!/usr/bin/env python3
"""Exercise the core index wait and P4 slow-encoder RPM handling without hardware."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
core = (root / '../../main/grbl/state_machine.c').resolve().read_text()
a = core.index('uint32_t ms = hal.get_elapsed_ticks();', core.index('case STATE_CYCLE:'))
b = core.index('} else if(block->spindle.hal->get_data(SpindleData_RPM)', a)
wait = core[a:b]
spindle = (root / 'main/spindle.c').read_text()
a = spindle.index('float IRAM_ATTR h5_spindle_profile_rpm(')
b = spindle.index('// PCNT resets', a)
rpm = spindle[a:b]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#define IRAM_ATTR
enum {SpindleData_Counters,Alarm_Spindle,EXEC_RESET=1,EXEC_STOP=2};
static uint32_t now,last_change,edge_interval_ms,stop_at,period;
static float measured_rpm,last_edge_rpm;
static bool alarm,woke;
static unsigned sys_state;
static struct {unsigned rt_exec_state;} sys;
static uint32_t ticks(void) {return now;}
static void realtime(unsigned state) {(void)state;now+=100;if(stop_at && now>=stop_at)sys.rt_exec_state|=EXEC_STOP;}
static struct {uint32_t (*get_elapsed_ticks)(void);} hal={ticks};
static struct {void (*on_execute_realtime)(unsigned);} grbl={realtime};
static void system_raise_alarm(unsigned type) {(void)type;alarm=true;}
static void system_set_exec_state_flag(unsigned flags) {sys.rt_exec_state|=flags;}
static struct data {uint32_t index_count;} data;
static struct data *get_data(unsigned request) {(void)request;data.index_count=now/period;return &data;}
static void reset_data(void) {}
static struct driver {void (*reset_data)(void);struct data *(*get_data)(unsigned);} driver={reset_data,get_data};
static struct block {struct {struct driver *hal;} spindle;} block_data={{&driver}},*block=&block_data;
static void run_wait(void) {WAIT woke=true;}
RPM
static void reset(void) {now=stop_at=0;period=30000;alarm=woke=false;sys.rt_exec_state=0;}
int main(void) {
 reset();run_wait();
 if(SPINDLE_SYNC_INDEX_TIMEOUT_MS)assert(alarm && !woke && now>5000);
 else assert(!alarm && woke && now==60000); // two revolutions at 2 RPM
 reset();stop_at=2000;run_wait();assert(!alarm && !woke && (sys.rt_exec_state&EXEC_RESET));
 // Startup zero, genuine stop, both directions and sparse hand-turn counts.
 now=1000;last_change=1000;edge_interval_ms=0;measured_rpm=0;last_edge_rpm=0;
 assert(h5_spindle_profile_rpm()==0);
 edge_interval_ms=100;last_edge_rpm=.5;now=1150;assert(h5_spindle_profile_rpm()==.5);
 last_edge_rpm=-.5;assert(h5_spindle_profile_rpm()==-.5);
 now=1300;assert(h5_spindle_profile_rpm()==0);
 measured_rpm=400;assert(h5_spindle_profile_rpm()==400);
 measured_rpm=0;last_edge_rpm=400;now=1001;assert(h5_spindle_profile_rpm()==0);
 puts("PASS: index wait timeout option, responsive STOP, zero startup and sparse low-RPM counts");
}
'''.replace('WAIT', wait).replace('\nRPM\n', '\n' + rpm + '\n')
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    for timeout in (5000, 0):
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        f'-DSPINDLE_SYNC_INDEX_TIMEOUT_MS={timeout}', str(path / 'test.c'), '-lm', '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
