#!/usr/bin/env python3
"""Exercise production UI jog submission with a simulated bridge; no hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'components/h5_ui/ui.cpp').read_text()
functions = source[source.index('static void send(const char *line)'):source.index('void manualMoveAxis(')]
harness = r'''
#include <cassert>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "preferences.h"
using String = std::string;
struct Axis {char name; long motorSteps, screwPitch, pos=0, leftStop=LONG_MAX, rightStop=LONG_MIN, originPos=0; bool disabled=false;};
Axis x={'X',1200,10000}, z={'Z',400,20000};
struct Status {bool ready=true, moving=false, held=false; int alarm=0; uint32_t command_id=0,completed_id=0,sampled_completed_id=0;float steps_per_mm[3]={1200,0,200};long position[3]={0,0,0};};
static Status status, published;
static String notice;
static uint32_t last_command, single_jog_id;
static Axis *held_axis;
static bool continuous_jog, jogContinuous=true;
static bool jogLimitsEnabled=true, updating;
static constexpr int MEASURE_METRIC=0;
static int measure=MEASURE_METRIC;
static long moveStep=10000;
static constexpr float MAX_TRAVEL_MM_X=100, MAX_TRAVEL_MM_Z=300;
static bool follow, axis_pending, accept=true;
static unsigned cancels, releases;
static uint32_t next_id;
static std::vector<String> submitted;
struct Manual {char axis;int sign;double distance;bool held;};
static std::vector<Manual> manual;
static uint32_t h5_bridge_submit(const char *s) {if(!accept)return 0;submitted.emplace_back(s);return ++next_id;}
static void h5_bridge_snapshot(Status *s) {*s=published;}
static void h5_bridge_cancel() {cancels++;}
static bool h5_cycle_busy() {return follow;}
static bool h5_follow_busy() {return follow;}
static bool h5_axis_change_pending() {return axis_pending;}
static bool h5_update_active() {return updating;}
static bool h5_follow_jog(char a,int s,double d,bool held) {if(!accept)return false;manual.push_back({a,s,d,held});return true;}
static void h5_follow_release() {releases++;}
FUNCTIONS
static void reset() {
 status=published=Status{};notice.clear();last_command=single_jog_id=next_id=0;
 held_axis=nullptr;continuous_jog=false;jogContinuous=true;moveStep=10000;
 jogLimitsEnabled=true;updating=false;measure=MEASURE_METRIC;x.originPos=z.originPos=0;
 follow=axis_pending=false;accept=true;cancels=releases=0;submitted.clear();manual.clear();
 x.pos=z.pos=0;x.leftStop=z.leftStop=LONG_MAX;x.rightStop=z.rightStop=LONG_MIN;x.disabled=z.disabled=false;
}
static double distance() {double d=0;assert(!submitted.empty());assert(sscanf(submitted.back().c_str(),"$J=G21G91%*c%lf",&d)==1);return d;}
int main() {
 // Hold is independent of the selected step, including inch increments.
 for(long step:{10000L,1000L,100L,25400L,2540L,254L}) {
  reset();moveStep=step;h5_ui_jog(&x,1,true);assert(distance()==100);
  h5_ui_jog(&x,1,false);assert(cancels==1 && !held_axis);
 }
 // Single means a complete selected increment, without repeat or release cancellation.
 for(long step:{10000L,1000L,100L,25400L,2540L,254L}) {
  reset();jogContinuous=false;moveStep=step;h5_ui_jog(&z,-1,true);
  assert(fabs(distance()+step/10000.0)<1e-6);
  for(int i=0;i<20;i++)h5_ui_jog(&z,-1,true);
  assert(submitted.size()==1);h5_ui_jog(&z,-1,false);assert(!cancels && !held_axis);
 }
 // Fast taps cannot queue steps based on stale position, even if ACK precedes publication.
 reset();jogContinuous=false;h5_ui_jog(&x,1,true);h5_ui_jog(&x,1,false);
 h5_ui_jog(&x,1,true);assert(submitted.size()==1 && !held_axis);
 published.completed_id=1;h5_ui_jog(&x,1,true);assert(submitted.size()==1);
 published.command_id=1;h5_ui_jog(&x,1,true);assert(submitted.size()==1);
 published.sampled_completed_id=1;published.moving=true;h5_ui_jog(&x,1,true);assert(submitted.size()==1);
 published.moving=false;x.pos=1200;x.leftStop=1800;h5_ui_jog(&x,1,true);
 assert(submitted.size()==2 && distance()==.5);h5_ui_jog(&x,1,false);
 // An explicit stop clears a queued single-step reservation, including before ACK.
 cancel_ui_motion();assert(single_jog_id==0);h5_ui_jog(&x,-1,true);assert(submitted.size()==3);
 reset();x.disabled=true;h5_ui_jog(&x,1,true);assert(submitted.empty());
 x.disabled=false;axis_pending=true;h5_ui_jog(&x,1,true);assert(submitted.empty());
 axis_pending=false;published.alarm=1;h5_ui_jog(&x,1,true);assert(submitted.empty());
 reset();published.ready=false;h5_ui_jog(&x,1,true);assert(submitted.empty());
 reset();x.leftStop=0;h5_ui_jog(&x,1,true);assert(submitted.empty());
 reset();accept=false;jogContinuous=false;h5_ui_jog(&x,1,true);assert(!single_jog_id);
 // Changing mode affects the next press, not release of an already-held jog.
 reset();h5_ui_jog(&x,1,true);jogContinuous=false;h5_ui_jog(&x,1,false);assert(cancels==1);
 reset();jogContinuous=false;h5_ui_jog(&x,1,true);jogContinuous=true;h5_ui_jog(&x,1,false);assert(!cancels);
 // Assisted feed receives held=false for one-shot displacement, held=true for Hold.
 for(bool hold:{false,true}) {
  reset();follow=true;jogContinuous=hold;moveStep=1000;h5_ui_jog(&z,1,true);
  assert(manual.size()==1 && manual[0].held==hold);
  assert(fabs(manual[0].distance-(hold?300:.1))<1e-6);
  h5_ui_jog(&z,1,false);assert(releases==(hold?1u:0u) && !cancels);
 }
 reset();follow=true;accept=false;h5_ui_jog(&z,1,true);h5_ui_jog(&z,1,false);assert(!releases && !held_axis);
 // Legacy ui_v1 layout survives: the new field occupies an old zeroed byte.
 struct Legacy {uint32_t version;int32_t mode,measure,pitch_type,pitch,move_step,passes,starts;float cone_ratio;uint8_t aux_forward,sound,reserved[2];};
 static_assert(sizeof(Legacy)==sizeof(h5_preferences_t));
 Legacy old{};old.version=1;old.pitch=15000;old.move_step=1000;old.reserved[0]=0;
 h5_preferences_t p{};memcpy(&p,&old,sizeof(p));assert(p.jog_mode==0 && p.pitch==15000 && p.move_step==1000);
 p.jog_mode=1;memcpy(&old,&p,sizeof(old));assert(old.reserved[0]==1 && old.pitch==15000);
 // Manual bypass retains endpoints; assisted requests use their own bounded path.
 reset();x.leftStop=600;x.rightStop=-600;
 assert(h5_ui_set_jog_limits(false));assert(x.leftStop==600&&x.rightStop==-600);
 jogContinuous=false;h5_ui_jog(&x,1,true);assert(distance()==1);h5_ui_jog(&x,1,false);
 assert(!h5_ui_set_jog_limits(true)); // Unacknowledged request.
 published.sampled_completed_id=1;published.moving=true;assert(!h5_ui_set_jog_limits(true));
 published.moving=false;assert(h5_ui_set_jog_limits(true));assert(distance_to_stop(&x,1,1)==.5);
 follow=true;assert(!h5_ui_set_jog_limits(false));follow=false;
 updating=true;assert(!h5_ui_set_jog_limits(false));updating=false;
 // Atomic apply, ordering, clearing, units, origin offsets and numeric range checks.
 reset();long bounds[]={-1200,2400,-6000,0};assert(!h5_ui_apply_limits(bounds));
 assert(x.rightStop==-1200&&x.leftStop==2400&&z.rightStop==-6000&&z.leftStop==0&&submitted.empty());
 long invalid[]={3600,2400,-6000,200};assert(h5_ui_apply_limits(invalid));assert(x.rightStop==-1200&&z.leftStop==0);
 published.moving=true;assert(h5_ui_apply_limits(bounds));published.moving=false;
 bounds[0]=LONG_MIN;bounds[1]=LONG_MAX;assert(!h5_ui_apply_limits(bounds));assert(x.rightStop==LONG_MIN&&x.leftStop==LONG_MAX);
 long raw=0;x.originPos=-1200;assert(h5_ui_limit_steps(&x,-2,&raw)&&raw==-1200);assert(h5_ui_limit_coordinate(&x,raw)==-2);
 measure=1;z.originPos=200;assert(h5_ui_limit_steps(&z,-1,&raw)&&raw==-5280);assert(fabs(h5_ui_limit_coordinate(&z,raw)+1)<1e-9);
 assert(!h5_ui_limit_steps(&x,INFINITY,&raw)&&!h5_ui_limit_steps(&x,NAN,&raw)&&!h5_ui_limit_steps(&x,1e20,&raw));
}
'''.replace('FUNCTIONS', functions)
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / 'jog.cpp').write_text(harness)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'components/h5_ui'), str(temp / 'jog.cpp'), '-o', str(temp / 'jog')], check=True)
    subprocess.run([str(temp / 'jog')], check=True)
follow = (root / 'main/follow.c').read_text()
follow_functions = follow[follow.index('bool h5_follow_jog('):follow.index('void h5_follow_reset(')]
follow_harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
static bool active=true, manual_pending,manual_held,release_requested;
static char jog_axis;
static int jog_sign,lock;
static double jog_distance;
static bool h5_follow_busy(void) {return active;}
static void h5_critical_enter(int *l,int site) {(void)l;(void)site;}
static void h5_critical_exit(int *l) {(void)l;}
FUNCTIONS
int main(void) {
 assert(h5_follow_jog('Z',1,300,true));
 assert(manual_pending && manual_held && !release_requested);
 // The service has not polled yet: release must prevent starting this Hold.
 h5_follow_release();assert(!manual_pending && !manual_held && release_requested);
 assert(h5_follow_jog('X',-1,.1,false));
 assert(manual_pending && !manual_held && !release_requested);
 assert(jog_axis=='X' && jog_sign==-1 && jog_distance==.1);
 active=false;assert(!h5_follow_jog('X',1,.1,true));
}
'''.replace('FUNCTIONS', follow_functions)
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / 'follow.c').write_text(follow_harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(temp / 'follow.c'), '-o', str(temp / 'follow')], check=True)
    subprocess.run([str(temp / 'follow')], check=True)
print('PASS: Hold/Single, units, release, pending/active tap rejection, limits, cancellation, assisted-feed routing, release-before-start and preference compatibility')
