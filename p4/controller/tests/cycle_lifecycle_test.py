#!/usr/bin/env python3
"""Run the production profile state machine with a simulated command/motion owner."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'main/cycle.c').read_text()
source = source[source.index('static portMUX_TYPE lock'):source.index('status_code_t h5_cycle_command')]
harness = r'''
#include "bridge.h"
#include "follow.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
enum {X_AXIS=0,Z_AXIS=2,STATE_IDLE=0,STATE_CYCLE=1,STATE_HOLD=2};
enum {EXEC_MOTION_CANCEL=1,EXEC_STOP=2,CMD_RESET=24};
enum {Status_GcodeSpindleNotRunning=99,Status_GcodeMaxFeedRateExceeded=100};
static struct {bool abort,alarm;int32_t position[3];struct {bool execute_hold;} step_control;} sys;
static struct {struct {bool scaling_active;} modal;} gc_state;
static struct {struct {double steps_per_mm,max_rate,acceleration;} axis[3];} settings;
static void discard(const char *s) {(void)s;}
static struct {struct {void (*write)(const char *);} stream;} hal={{discard}};
static unsigned state,rt,owner,submissions,phase_commands,plunges;
static double rpm;
static bool stepping,planner,index_wait,profile_mode,braking;
static h5_status_t status;
static char queued[128];
static unsigned target_stage;
static void h5_critical_enter(int *l,int line) {(void)l;(void)line;}
static void h5_critical_exit(int *l) {(void)l;}
static unsigned state_get(void) {return state;}
static bool st_is_stepping(void) {return stepping;}
static bool plan_get_current_block(void) {return planner;}
bool h5_follow_busy(void) {return false;}
bool h5_follow_selected(void) {return false;}
void h5_follow_clear(void) {}
void h5_follow_cancel(void) {}
void h5_follow_reset(void) {}
void h5_follow_poll(void) {}
void h5_follow_snapshot(h5_cycle_status_t *s) {(void)s;}
bool h5_axis_change_pending(void) {return false;}
bool h5_operation_claim(unsigned o) {if(owner)return false;owner=o;return true;}
void h5_operation_release(unsigned o) {if(owner==o)owner=0;}
static bool h5_update_active(void) {return false;}
static bool h5_serial_pending(void) {return false;}
bool h5_bridge_empty(void) {return !queued[0];}
void h5_bridge_discard_cycle_commands(void) {queued[0]=0;}
void h5_bridge_snapshot(h5_status_t *s) {*s=status;}
static float h5_spindle_rpm(void) {return rpm;}
static float h5_spindle_profile_rpm(void) {return rpm;}
static void h5_spindle_profile(bool on) {profile_mode=on;braking=false;}
static bool h5_spindle_waiting_index(void) {return index_wait;}
static void h5_spindle_follow_braking(void) {braking=true;}
static void h5_spindle_command(unsigned s,char *line) {(void)s;(void)line;}
static void system_set_exec_state_flag(unsigned mask) {rt|=mask;}
static void protocol_enqueue_realtime_command(unsigned c) {assert(c==CMD_RESET);rt|=4;}
uint32_t h5_bridge_cycle_submit(const char *line) {
 assert(!queued[0]);snprintf(queued,sizeof queued,"%s",line);return ++submissions;
}
SOURCE
static void complete(void) {
 char axis;double value,x,z,feed;
 assert(queued[0]);
 if(stage==4)plunges++;
 if(stage==5)phase_commands++;
 if(sscanf(queued,"G90G94G53G0%c%lf",&axis,&value)==2 ||
    sscanf(queued,"G90G95G53G1%c%lfF%lf",&axis,&value,&feed)==3) {
  unsigned a=axis=='X'?0:2;sys.position[a]=lround(value*settings.axis[a].steps_per_mm);
 }
 if(sscanf(queued,"G90G95G53G1X%lfZ%lfF%lf",&x,&z,&feed)==3) {
  sys.position[0]=lround(x*1200);sys.position[2]=lround(z*200);
 }
 if(sscanf(queued,"G91G33%c%lfK%lf",&axis,&value,&feed)==3) {
  unsigned a=axis=='X'?0:2;sys.position[a]+=lround(value*settings.axis[a].steps_per_mm);
 }
 status.completed_id=command_id;status.command_status=0;queued[0]=0;
 state=STATE_IDLE;stepping=planner=index_wait=false;
}
static void until_cut(void) {
 for(unsigned n=0;n<1000;n++) {
  h5_cycle_poll();
  if(stage==7 && queued[0])return;
  if(queued[0])complete();
 }
 assert(!"Cut not submitted");
}
static void reset_core(void) {
 assert(rt&(EXEC_STOP|4));
 queued[0]=0;state=STATE_IDLE;stepping=planner=index_wait=false;
 memset(&status,0,sizeof status);sys.abort=false;sys.step_control.execute_hold=false;rt=0;
 h5_cycle_reset();
}
static void interrupt_cut(double next_rpm,double x,double z,bool at_index) {
 assert(stage==7 && queued[0]);queued[0]=0;
 status.completed_id=command_id;status.command_status=0;
 sys.position[0]=lround(x*1200);sys.position[2]=lround(z*200);
 state=STATE_CYCLE;stepping=!at_index;planner=true;index_wait=at_index;
 rpm=next_rpm;h5_cycle_poll();assert(pausing && (rt&EXEC_MOTION_CANCEL) && braking);
 if(!at_index) {state=STATE_IDLE;stepping=planner=false;h5_cycle_poll();}
 reset_core();assert(h5_cycle_busy() && recovering && stage==0 && owner==H5_OWNER_PROFILE);
}
static void new_cycle(unsigned op) {
 h5_cycle_cancel();internal_reset=false;h5_cycle_reset();
 queued[0]=0;memset(&status,0,sizeof status);memset(&sys,0,sizeof sys);
 state=STATE_IDLE;stepping=planner=index_wait=false;submissions=plunges=phase_commands=rt=0;rpm=0;
 settings.axis[0].steps_per_mm=1200;settings.axis[2].steps_per_mm=200;
 settings.axis[0].max_rate=300;settings.axis[2].max_rate=960;
 settings.axis[0].acceleration=500*3600;settings.axis[2].acceleration=100*3600;
 h5_cycle_config_t c={.operation=op,.aux_forward=true,.passes=2,.starts=1,.pitch=.1,
  .x_min=0,.x_max=1,.z_min=0,.z_max=10,.rpm_limit=1};
 assert(h5_cycle_request(&c));
}
int main(void) {
 (void)target_stage;
 for(unsigned op=H5_TURN;op<=H5_ELLIPSE;op++) {
  new_cycle(op);
  // Starting with spindle off positions/plunges once, then waits indefinitely.
  for(unsigned n=0;n<100;n++) {h5_cycle_poll();if(queued[0])complete();}
  assert(h5_cycle_busy() && stage==5 && !queued[0] && plunges==1);
  unsigned before=submissions;
  for(unsigned n=0;n<10000;n++)h5_cycle_poll();
  assert(submissions==before && !rt);
  rpm=5;until_cut();assert(h5_cycle_busy() && profile_mode);
  double x=axis_position('X'),z=axis_position('Z');
  if(op==H5_FACE || op==H5_CUT)x=.25;
  else if(op==H5_ELLIPSE) {double feed;h5_cycle_point(&plan,0,plan.segments/2,&x,&z,&feed);}
  else z=5;
  interrupt_cut(0,x,z,false);
  for(unsigned n=0;n<20;n++) {h5_cycle_poll();if(queued[0])complete();}
  assert(stage==5 && !queued[0] && pass==0 && start==0 && plunges==1);
  rpm=5;until_cut();assert(plunges==1); // no second plunge on resume
  // Reverse while cutting: decelerate, keep depth and retrace toward start.
  interrupt_cut(-5,x,z,false);until_cut();assert(reverse_cut && plunges==1);
  if(op!=H5_ELLIPSE) {complete();h5_cycle_poll();}
  else for(unsigned n=0;n<1000 && stage!=5;n++) {if(queued[0])complete();h5_cycle_poll();}
  assert(stage==5 && h5_cycle_busy() && pass==0 && plunges==1);
  before=submissions;for(unsigned n=0;n<100;n++)h5_cycle_poll();assert(submissions==before);
  rpm=5;until_cut();assert(!reverse_cut && plunges==1);
  // Speed well above the preview's legacy cap doesn't cancel the pass.
  rpm=500;h5_cycle_poll();assert(!pausing && !stopping && h5_cycle_busy());
  complete();
  for(unsigned n=0;n<10000 && h5_cycle_busy();n++) {h5_cycle_poll();if(queued[0])complete();}
  assert(!h5_cycle_busy() && !owner && plunges==2);
 }
 // An index wait may outlast a stopped spindle without a reset or lost pass.
 new_cycle(H5_THREAD);rpm=5;until_cut();queued[0]=0;
 status.completed_id=command_id;state=STATE_CYCLE;planner=index_wait=true;rpm=0;
 for(unsigned n=0;n<1000;n++)h5_cycle_poll();assert(!rt && h5_cycle_busy());
 // Explicit STOP still cancels an index wait and cannot auto-resume.
 h5_cycle_cancel();h5_cycle_poll();assert(rt&EXEC_STOP);reset_core();assert(!h5_cycle_busy());
 // Actual axis-rate constraint keeps the operation armed instead of rejecting it.
 new_cycle(H5_THREAD);rpm=20000;
 for(unsigned n=0;n<100;n++) {h5_cycle_poll();if(queued[0])complete();}
 assert(stage==5 && h5_cycle_busy() && !rt);rpm=5;until_cut();
 // STOP wins over a pending automatic recovery.
 interrupt_cut(0,axis_position('X'),5,false);h5_cycle_cancel();h5_cycle_poll();reset_core();
 assert(!h5_cycle_busy() && !owner);
 // Reaching the cutting endpoint and stopping the spindle simultaneously
 // must still run the programmed retract, rather than waiting at depth.
 new_cycle(H5_THREAD);rpm=5;until_cut();complete();rpm=0;h5_cycle_poll();
 assert(stage==8 && queued[0] && !pausing);
 puts("PASS: all profiles arm stopped, wait, resume, retrace, retain depth/pass, ignore legacy RPM caps; STOP cancels");
}
'''.replace('SOURCE', source)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'components/h5_ui'),
                    str(path / 'test.c'), str(root / 'components/h5_ui/cycle_plan.c'), '-lm', '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
