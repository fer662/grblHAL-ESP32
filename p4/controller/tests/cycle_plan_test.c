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
    CLOSE(p.lead,1); CLOSE(p.lead_in,2.95); CLOSE(p.run_out,1.87);
    CLOSE(p.approach,7.05); CLOSE(p.finish,21.87); CLOSE(p.takeup,7.045);
    CLOSE(p.clearance,-1.5); CLOSE(h5_cycle_depth(&p,0),-.5); CLOSE(h5_cycle_depth(&p,3),1);
    assert(h5_cycle_phase(&p,0)==60); assert(h5_cycle_phase(&p,1)==660);
    for (int spindle=-1;spindle<=1;spindle+=2) for(int pitch=-1;pitch<=1;pitch+=2) {
        m.rpm=300*spindle; c.pitch=.5*pitch;
        assert(h5_cycle_plan(&c,&m,&p,error,sizeof(error)));
        assert(p.direction==spindle*pitch);
        assert((p.z_start-p.approach)*p.direction > 0);
        assert((p.finish-p.z_end)*p.direction > 0);
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
    puts("PASS: cycle geometry, directions, radial depths, phase registration and invalid envelopes");
}
