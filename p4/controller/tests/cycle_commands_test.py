#!/usr/bin/env python3
"""Run production cycle command generation on the host; no hardware connection."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/cycle.c').read_text()
emit = source[source.index('static void emit(void)'):source.index('static void poll_cycle(void)')]
harness = r'''
#include "cycle_plan.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static h5_cycle_plan_t plan;
static unsigned stage, pass, start, segment;
static uint32_t command_id;
static bool waiting_ack, busy=true;
static struct { bool abort; } sys;
static const char *names[15];
static char emitted[128];
static bool h5_cycle_busy(void) { return busy; }
static float h5_spindle_rpm(void) { return 400; }
static uint32_t h5_bridge_cycle_submit(const char *line) {
    snprintf(emitted,sizeof emitted,"%s",line); return 1;
}
static void message(const char *text,bool active) { (void)text; (void)active; }
static void h5_cycle_cancel(void) { assert(!"Unexpected submission failure"); }
EMIT
static void issue(unsigned next) { stage=next; emitted[0]=0; emit(); assert(emitted[0]); }
static double position[2];
static void move(unsigned next) {
    issue(next);
    char axis; double value,x,z,feed;
    if(sscanf(emitted,"G90G94G53G0%c%lf",&axis,&value)==2) position[axis=='X'?0:1]=value;
    else if(sscanf(emitted,"G91G95G1X%lfZ%lfF%lf",&x,&z,&feed)==3) {
        position[0]+=x; position[1]+=z; assert(feed>0);
    } else if(sscanf(emitted,"G91G33%c%lfK%lf",&axis,&value,&feed)==3) {
        assert(plan.indexed && fabs(feed-plan.lead)<1e-6);
        position[axis=='X'?0:1]+=value;
    } else if(!strcmp(emitted,"$P4THREADPASS")) {
        assert(plan.indexed);
        for(unsigned point=0;point<=H5_THREAD_BLOCKS;point++) {
            h5_thread_point(&plan,pass,point,&x,&z);
            assert(z>=fmin(plan.cut_start,plan.cut_end)-1e-6 && z<=fmax(plan.cut_start,plan.cut_end)+1e-6);
        }
        position[0]=x;position[1]=z;
    } else {
        assert(sscanf(emitted,"G91G95G1%c%lfF%lf",&axis,&value,&feed)==3);
        position[axis=='X'?0:1]+=value; assert(fabs(feed-.1)<1e-6);
    }
    double cutting=position[plan.cut_axis=='X'?0:1];
    double low=fmin(plan.cut_start,plan.cut_end), high=fmax(plan.cut_start,plan.cut_end);
    assert(cutting>=low-1e-4 && cutting<=high+1e-4);
}
int main(void) {
    h5_cycle_machine_t m={.x=1,.z=10,.rpm=400,.z_acceleration=50,.z_max_rate=960,.z_steps_mm=200,
        .x_acceleration=25,.x_max_rate=60,.x_steps_mm=1200};
    char error[96];
    for(unsigned op=H5_TURN;op<=H5_ELLIPSE;op++) {
        for(int spindle=-1;spindle<=1;spindle+=2)
        for(int sign=-1;sign<=1;sign+=2)
        for(unsigned aux=0;aux<2;aux++) {
            h5_cycle_config_t c={.operation=op,.passes=3,.starts=1,.pitch=.1*sign,.aux_forward=aux,
                .x_min=0,.x_max=1,.z_min=0,.z_max=10,.rpm_limit=500};
            m.rpm=400*spindle;
            if(op==H5_CUT) c.z_min=c.z_max=m.z;
            assert(h5_cycle_plan(&c,&m,&plan,error,sizeof error));
            position[0]=m.x; position[1]=m.z;
            start=segment=pass=0;
            issue(0); move(1); move(2); move(3);
            for(pass=0;pass<c.passes;pass++) {
                if(pass && op==H5_ELLIPSE) { move(2); move(3); }
                if(plan.indexed) assert(fabs(position[1]-plan.approach)<1e-6);
                move(4);
                if(plan.indexed) {
                    assert(fabs(position[0]-plan.thread_clearance)<1e-6);
                    assert(fabs(position[1]-plan.approach)<1e-6); // X air approach did not move Z.
                }
                issue(5);
                if(!plan.indexed) assert(!strcmp(emitted,"$P4PHASE=0"));
                issue(6);
                for(segment=0;segment<(op==H5_ELLIPSE?plan.segments:1);segment++) move(7);
                double expected=op==H5_CUT ? plan.cut_start+(plan.cut_end-plan.cut_start)*(pass+1)/c.passes : plan.cut_end;
                assert(fabs(position[plan.cut_axis=='X'?0:1]-expected)<1e-4);
                if(plan.indexed) {
                    assert(fabs(position[1]-plan.finish)<1e-6);
                    assert(fabs(position[0]-plan.thread_clearance)<1e-6); // Near-surface withdrawal only.
                }
                move(8);
                if(plan.indexed) {
                    assert(fabs(position[1]-plan.finish)<1e-6);
                    assert(fabs(position[0]-plan.clearance)<1e-6);
                }
                move(9); move(10);
            }
            move(11); move(12); issue(13); issue(14);
            if(op==H5_CUT) assert(fabs(position[1]-m.z)<1e-6);
        }
    }
    // Multi-start Thread takes up inside the span and retains G33 lead/registration.
    h5_cycle_config_t c={.threading=true,.passes=4,.starts=2,.pitch=.5,.aux_forward=true,
        .x_min=-1,.x_max=1,.z_min=10,.z_max=80,.rpm_limit=360};
    m.rpm=300; assert(h5_cycle_plan(&c,&m,&plan,error,sizeof error));
    pass=start=segment=0; issue(2); assert(!strcmp(emitted,"G90G94G53G0Z10.000000"));
    issue(3); assert(!strcmp(emitted,"G90G94G53G0Z10.005000"));
    issue(5); assert(!strcmp(emitted,"$P4PHASE=6"));
    issue(7); assert(!strcmp(emitted,"$P4THREADPASS"));
    start=1; issue(5); assert(!strcmp(emitted,"$P4PHASE=606"));
    sys.abort=true; emitted[0]=0; emit(); assert(!emitted[0]);
    sys.abort=false; busy=false; emit(); assert(!emitted[0]);
    puts("PASS: generated moves for all profiles stay in cutting-axis bounds; multi-start G33 phase");
}
'''.replace('EMIT', emit)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'components/h5_ui'), str(path / 'test.c'),
                    str(root / 'components/h5_ui/cycle_plan.c'), '-lm', '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
