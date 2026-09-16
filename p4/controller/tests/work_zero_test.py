#!/usr/bin/env python3
"""Exercise the production core-thread zero gate with a parser spy; no hardware."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'main/driver.c').read_text()
handler = source[source.index('static status_code_t zero_work_axis('):source.index('static status_code_t command(')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int sys_state_t;
typedef int status_code_t;
enum {STATE_IDLE,STATE_CYCLE,STATE_HOLD,STATE_ALARM};
enum {Status_OK,Status_InvalidStatement,Status_IdleError,Status_SettingDisabled};
static bool timer,stepping,planner,cycle,updating,pending,ready;
static struct {bool abort; int alarm;} sys;
static unsigned calls;
static int parser_result;
static char received[40];
static bool h5_motion_idle(void) {return !timer;}
static bool st_is_stepping(void) {return stepping;}
static bool plan_get_current_block(void) {return planner;}
static bool h5_cycle_busy(void) {return cycle;}
static bool h5_update_active(void) {return updating;}
static bool h5_axis_change_pending(void) {return pending;}
static bool h5_ui_ready(void) {return ready;}
static status_code_t gc_execute_block(char *s) {calls++;strcpy(received,s);return parser_result;}
HANDLER
int main(void) {
 ready=true;
 assert(zero_work_axis(STATE_IDLE,'X')==Status_OK);
 assert(calls==1 && !strcmp(received,"G54G10L20P1X0"));
 assert(zero_work_axis(STATE_IDLE,'Z')==Status_OK);
 assert(calls==2 && !strcmp(received,"G54G10L20P1Z0"));
 assert(zero_work_axis(STATE_IDLE,'Y')==Status_InvalidStatement && calls==2);
 for(int state=STATE_CYCLE;state<=STATE_ALARM;state++)
  assert(zero_work_axis(state,'X')==Status_IdleError && calls==2);
 // A queued planner block with idle state is still busy; UI sampling is not authority.
 bool *blocked[]={&timer,&stepping,&planner,&cycle,&updating,&pending,&sys.abort};
 for(unsigned i=0;i<sizeof blocked/sizeof blocked[0];i++) {
  *blocked[i]=true;assert(zero_work_axis(STATE_IDLE,'X')==Status_IdleError && calls==2);*blocked[i]=false;
 }
 ready=false;assert(zero_work_axis(STATE_IDLE,'X')==Status_IdleError && calls==2);ready=true;
 sys.alarm=1;assert(zero_work_axis(STATE_IDLE,'X')==Status_IdleError && calls==2);sys.alarm=0;
 parser_result=Status_SettingDisabled;
 assert(zero_work_axis(STATE_IDLE,'X')==Status_SettingDisabled && calls==3);
 puts("PASS: core-thread zero gate, exact native G54/G10 requests, queued-motion rejection, parser error propagation");
}
'''.replace('HANDLER',handler)
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.c').write_text(harness)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
