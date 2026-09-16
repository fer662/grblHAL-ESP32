/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cycle_plan.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CLOSE(a,b) assert(fabs((a)-(b)) < .000001)
int main(void)
{
    h5_cycle_config_t c={.threading=true,.aux_forward=true,.passes=4,.starts=2,.pitch=.5,
        .x_min=-1,.x_max=1,.z_min=10,.z_max=110,.rpm_limit=360};
    h5_cycle_machine_t m={.x=0,.z=0,.rpm=300,.z_acceleration=50,.z_max_rate=960,.z_steps_mm=200,.x_acceleration=25,.x_max_rate=60,.x_steps_mm=1200};
    h5_cycle_plan_t p; char error[96];
    assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    assert(p.indexed);
    CLOSE(p.lead,1); CLOSE(p.cut_acceleration,50);
    CLOSE(p.approach,10.005); CLOSE(p.finish,110); CLOSE(p.takeup,10);
    CLOSE(p.clearance,-1.5); CLOSE(h5_cycle_depth(&p,0),-.5); CLOSE(h5_cycle_depth(&p,3),1);
    assert(h5_cycle_phase(&p,0)==6); assert(h5_cycle_phase(&p,1)==606);
    for (int spindle=-1;spindle<=1;spindle+=2) for(int pitch=-1;pitch<=1;pitch+=2) {
        m.rpm=300*spindle; c.pitch=.5*pitch;
        assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
        assert(p.direction==spindle*pitch);
        CLOSE((p.approach-p.cut_start)*p.direction,.005);
        CLOSE(p.finish,p.cut_end); CLOSE(p.takeup,p.cut_start);
        assert(p.approach>=c.z_min && p.approach<=c.z_max);
        assert((p.finish-p.approach)*p.direction>0);
        CLOSE(h5_cycle_depth(&p,3),1);
    }
    c.aux_forward=false; assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    CLOSE(p.clearance,1.5); CLOSE(h5_cycle_depth(&p,0),.5); CLOSE(h5_cycle_depth(&p,3),-1);
    c.starts=7; c.pitch=.1;
    assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
    for(unsigned i=0;i<7;i++) {
        double phase=h5_cycle_phase(&p,i)/1200.0-p.direction*(p.approach-p.cut_start)/p.lead-(double)i/7;
        assert(fabs(phase-round(phase)) <= .5/1200+.0000001);
    }
    unsigned phases[7]; for(unsigned i=0;i<7;i++) phases[i]=h5_cycle_phase(&p,i);
    double previous_approach=p.approach;
    c.rpm_limit=600; assert(h5_cycle_plan(&c,&m,&p,error,sizeof error));
    CLOSE(p.approach,previous_approach); // RPM changes do not move the tool-entry target.
    for(unsigned i=0;i<7;i++) assert(h5_cycle_phase(&p,i)==phases[i]);
    c.rpm_limit=360;
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
    CLOSE(p.approach,0); CLOSE(p.finish,1); CLOSE(h5_cycle_depth(&p,0),5);
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
            CLOSE(p.lead,.1);
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
    // Moving entry/exit: actual stations, fixed across passes, bounded in both directions.
    c=(h5_cycle_config_t){.operation=H5_THREAD,.passes=5,.starts=1,.pitch=.5,.aux_forward=true,
        .x_min=0,.x_max=1,.z_min=0,.z_max=10,.rpm_limit=125};
    m.rpm=100;m.z_acceleration=100;
    for(int dir=-1;dir<=1;dir+=2) {
        c.pitch=.5*dir;
        assert(h5_cycle_plan(&c,&m,&p,error,sizeof error));
        assert((p.full_end-p.full_begin)*dir>0);
        for(unsigned pass=0;pass<c.passes;pass++) {
            double last_z=p.approach;
            for(unsigned point=0;point<=H5_THREAD_BLOCKS;point++) {
                double x,z;h5_thread_point(&p,pass,point,&x,&z);
                assert(z>=0 && z<=10 && x>=-.5 && x<=1);
                assert((z-last_z)*dir>=-1e-9);last_z=z;
                if(point<=1 || point>=H5_THREAD_BLOCKS-1) CLOSE(x,p.clearance);
                if(point==H5_THREAD_RAMP_SEGMENTS+1) {CLOSE(z,p.full_begin);CLOSE(x,h5_cycle_depth(&p,pass));}
                if(point==H5_THREAD_RAMP_SEGMENTS+2) {CLOSE(z,p.full_end);CLOSE(x,h5_cycle_depth(&p,pass));}
            }
        }
        unsigned phase=h5_cycle_phase(&p,0);
        m.z_acceleration=50;assert(h5_cycle_plan(&c,&m,&p,error,sizeof error));
        assert(h5_cycle_phase(&p,0)==phase);m.z_acceleration=100;
    }
    // At the legacy 1 mm/s X rate this setup cannot fit moving entry in 10 mm.
    c.pitch=.5;c.rpm_limit=564;m.rpm=451;
    assert(!h5_cycle_plan(&c,&m,&p,error,sizeof error));
    assert(strstr(error,"moving X entry/retract"));
    c.z_max=40;assert(h5_cycle_plan(&c,&m,&p,error,sizeof error));
    c.z_max=10;m.x_max_rate=300;
    assert(h5_cycle_plan(&c,&m,&p,error,sizeof error));
    CLOSE(p.full_begin,2.945);CLOSE(p.full_end,7.060);CLOSE(p.exit_end,9.880);
    m.x_max_rate=0;assert(!h5_cycle_plan(&c,&m,&p,error,sizeof error));
    puts("PASS: moving-entry geometry, physical rate feasibility, fixed phase, all profile bounds and clearance");
}
