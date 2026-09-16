/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cycle_plan.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define CLOSE(a,b) assert(fabs((a)-(b)) < .000001)
int main(void)
{
    h5_cycle_config_t c={.threading=true,.aux_forward=true,.passes=4,.starts=2,.pitch=.5,
        .x_min=-1,.x_max=1,.z_min=10,.z_max=20,.rpm_limit=360};
    h5_cycle_machine_t m={.x=0,.z=0,.rpm=300,.z_acceleration=50,.z_max_rate=960,.z_steps_mm=200};
    h5_cycle_plan_t p; char error[96];
    assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    assert(p.indexed);
    CLOSE(p.lead,1); CLOSE(p.lead_in,2.95); CLOSE(p.run_out,1.87);
    CLOSE(p.approach,7.05); CLOSE(p.finish,21.87); CLOSE(p.takeup,7.045);
    CLOSE(p.clearance,-1.5); CLOSE(h5_cycle_depth(&p,0),-.5); CLOSE(h5_cycle_depth(&p,3),1);
    assert(h5_cycle_phase(&p,0)==60); assert(h5_cycle_phase(&p,1)==660);
    for (int spindle=-1;spindle<=1;spindle+=2) for(int pitch=-1;pitch<=1;pitch+=2) {
        m.rpm=300*spindle; c.pitch=.5*pitch;
        assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
        assert(p.direction==spindle*pitch);
        assert((p.cut_start-p.approach)*p.direction > 0);
        assert((p.finish-p.cut_end)*p.direction > 0);
        CLOSE(h5_cycle_depth(&p,3),1);
    }
    c.aux_forward=false; assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    CLOSE(p.clearance,1.5); CLOSE(h5_cycle_depth(&p,0),.5); CLOSE(h5_cycle_depth(&p,3),-1);
    c.starts=7; c.pitch=.1;
    assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    for(unsigned i=0;i<7;i++) {
        double phase=h5_cycle_phase(&p,i)/1200.0+p.lead_in/p.lead-(double)i/7;
        assert(fabs(phase-round(phase)) <= .5/1200+.0000001);
    }
    c.threading=false; assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error))); CLOSE(p.lead,.1); assert(p.starts==1);
    h5_cycle_config_t valid=c;
    c.pitch=NAN; assert(!h5_cycle_plan(&c,&m,&p,error,sizeof(error))); c=valid;
    c.pitch=100; assert(!h5_cycle_plan(&c,&m,&p,error,sizeof(error))); c=valid;
    c.z_max=310; assert(!h5_cycle_plan(&c,&m,&p,error,sizeof(error))); c=valid;
    c.x_min=c.x_max; assert(!h5_cycle_plan(&c,&m,&p,error,sizeof(error))); c=valid;
    c.passes=0; assert(!h5_cycle_plan(&c,&m,&p,error,sizeof(error))); c=valid;
    c.starts=125; assert(!h5_cycle_plan(&c,&m,&p,error,sizeof(error))); c=valid;
    m.rpm=361; assert(!h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    m.rpm=0; assert(!h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    m=(h5_cycle_machine_t){.x=0,.z=0,.rpm=300,.z_acceleration=50,.z_max_rate=960,.z_steps_mm=200,
        .x_acceleration=25,.x_max_rate=60,.x_steps_mm=1200};
    c=(h5_cycle_config_t){.operation=H5_FACE,.passes=2,.starts=1,.pitch=.1,.aux_forward=true,
        .x_min=0,.x_max=1,.z_min=0,.z_max=10,.rpm_limit=360};
    assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    assert(p.cut_axis=='X' && p.depth_axis=='Z' && !p.indexed);
    CLOSE(p.approach,0); CLOSE(p.finish,1); CLOSE(p.run_out,0); CLOSE(h5_cycle_depth(&p,0),5);
    c.operation=H5_CUT;c.z_min=c.z_max=0;
    assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));CLOSE(p.clearance,m.z);
    c.operation=H5_ELLIPSE;c.z_max=10;
    for (int direction=0;direction<2;direction++) {
        c.aux_forward=direction;
        assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
        double px,pz,feed,x,z,revolutions=0;
        h5_cycle_point(&p,0,0,&px,&pz,&feed);CLOSE(px,0);CLOSE(pz,5);
        for(unsigned i=1;i<=p.segments;i++) {
            h5_cycle_point(&p,0,i,&x,&z,&feed);
            assert(x>=px && z>=pz && feed>0);
            revolutions+=hypot(x-px,z-pz)/feed;px=x;pz=z;
        }
        CLOSE(x,.5);CLOSE(z,10);CLOSE(revolutions,50);
    }
    // Regression: ordinary cuts must not inherit Thread's lead-in, run-out or
    // one-step approach outside the cutting axis bounds, in either direction.
    for (unsigned op=H5_TURN; op<=H5_ELLIPSE; op++) {
        if (op==H5_THREAD) continue;
        for (int spindle=-1; spindle<=1; spindle+=2)
        for (int pitch=-1; pitch<=1; pitch+=2)
        for (unsigned aux=0; aux<2; aux++) {
            c=(h5_cycle_config_t){.operation=op,.passes=3,.starts=7,.pitch=.1*pitch,.aux_forward=aux,
                .x_min=0,.x_max=1,.z_min=0,.z_max=10,.rpm_limit=500};
            if(op==H5_CUT) c.z_min=c.z_max=4;
            m.rpm=400*spindle; m.z=4;
            assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
            assert(!p.indexed && p.starts==1);
            CLOSE(p.lead,.1); CLOSE(p.lead_in,0); CLOSE(p.run_out,0);
            CLOSE(p.approach,p.cut_start); CLOSE(p.takeup,p.cut_start); CLOSE(p.finish,p.cut_end);
            if(op==H5_CUT) { CLOSE(p.clearance,4); CLOSE(p.depth_start,4); CLOSE(p.depth_end,4); }
            else CLOSE(fabs(p.clearance-p.depth_start),.5); // Preserve tool clearance.
            if(op==H5_ELLIPSE) for(unsigned pass=0; pass<c.passes; pass++)
                for(unsigned segment=0; segment<=p.segments; segment++) {
                    double x,z,feed; h5_cycle_point(&p,pass,segment,&x,&z,&feed);
                    assert(x>=-1e-9 && x<=1+1e-9 && z>=-1e-9 && z<=10+1e-9);
                }
        }
    }
    // Even a single-step-long Turn has no programmed travel outside its ends.
    c=(h5_cycle_config_t){.operation=H5_TURN,.passes=1,.starts=1,.pitch=.1,
        .x_min=0,.x_max=1,.z_min=0,.z_max=.005,.rpm_limit=500};
    m.rpm=-400; assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    CLOSE(p.approach,.005); CLOSE(p.takeup,.005); CLOSE(p.finish,0);
    puts("PASS: bounded non-thread profiles, preserved clearance, thread phase and invalid envelopes");
}
