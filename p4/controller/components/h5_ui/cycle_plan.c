/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cycle_plan.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool fail(char *error, size_t size, const char *message)
{ snprintf(error, size, "%s", message); return false; }
bool h5_cycle_plan(const h5_cycle_config_t *c, const h5_cycle_machine_t *m, h5_cycle_plan_t *p, char *error, size_t size)
{
    memset(p, 0, sizeof(*p));
    const double values[] = {c->pitch,c->x_min,c->x_max,c->z_min,c->z_max,c->rpm_limit,
        m->x,m->z,m->rpm,m->z_acceleration,m->z_max_rate,m->z_steps_mm};
    for (unsigned i=0; i<sizeof(values)/sizeof(*values); i++)
        if (!isfinite(values[i]) || fabs(values[i]) > 100000)
            return fail(error,size,"Invalid cycle coordinates or settings");
    if (!c->passes || c->passes > 999 || !c->starts || c->starts > 124 || fabs(c->pitch) < .0001)
        return fail(error,size,"Set pitch, passes and starts");
    if (c->x_min >= c->x_max || c->z_min >= c->z_max)
        return fail(error,size,"Set both X and Z machining bounds");
    if (fabs(m->rpm) < 30 || c->rpm_limit < fabs(m->rpm))
        return fail(error,size,"Spindle must run within the selected RPM limit");
    if (m->z_acceleration <= 0 || m->z_steps_mm <= 0 || m->z_max_rate <= 0)
        return fail(error,size,"Invalid Z motion settings");
    p->config=*c;
    p->starts=c->threading ? c->starts : 1;
    p->lead=fabs(c->pitch)*p->starts;
    if (p->lead*c->rpm_limit > m->z_max_rate * .99)
        return fail(error,size,"Reduce pitch, starts or spindle RPM");
    p->spindle_direction=m->rpm > 0 ? 1 : -1;
    p->direction=(c->pitch > 0 ? 1 : -1)*p->spindle_direction;
    p->z_start=p->direction > 0 ? c->z_min : c->z_max;
    p->z_end=p->direction > 0 ? c->z_max : c->z_min;
    p->x_start=c->aux_forward ? c->x_min : c->x_max;
    p->x_end=c->aux_forward ? c->x_max : c->x_min;
    p->clearance=p->x_start+(c->aux_forward ? -.5 : .5);
    double v=p->lead*c->rpm_limit/60, deficit=v*v/(2*m->z_acceleration);
    // Reserve settling and braking space at the ceiling RPM for every pass.
    // Round lead-in to real Z steps so phase uses the executed approach point.
    p->lead_in=ceil((fmax(2*p->lead,4*deficit+.25*v)+.01)*m->z_steps_mm)/m->z_steps_mm;
    p->run_out=ceil((deficit+.25*v+.01)*m->z_steps_mm)/m->z_steps_mm;
    p->approach=p->z_start-p->direction*p->lead_in;
    p->finish=p->z_end+p->direction*p->run_out;
    p->takeup=p->approach-p->direction/m->z_steps_mm;
    double xmin=fmin(m->x,fmin(c->x_min,p->clearance)), xmax=fmax(m->x,fmax(c->x_max,p->clearance));
    double zmin=fmin(m->z,fmin(p->takeup,p->finish)), zmax=fmax(m->z,fmax(p->takeup,p->finish));
    if (xmax-xmin > 100 || zmax-zmin > 300)
        return fail(error,size,"Cycle including clearance exceeds travel span");
    if (error && size) error[0]=0;
    return true;
}
double h5_cycle_depth(const h5_cycle_plan_t *p, unsigned pass)
{ return p->x_start+(p->x_end-p->x_start)*(pass+1)/p->config.passes; }
unsigned h5_cycle_phase(const h5_cycle_plan_t *p, unsigned start)
{
    // Reference the cutting start, independent of the lead-in distance. Choose
    // each start directly, avoiding cumulative rounding with e.g. seven starts.
    long phase=lround(1200.0*((double)start/p->starts-p->lead_in/p->lead));
    return (unsigned)((phase%1200+1200)%1200);
}
