/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cycle_plan.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}
// Acceleration / constant-speed / deceleration, sampled into native line blocks.
// Z stays spindle-synchronous throughout. Round each time interval upward to Z
// steps, so discretization never shortens the requested acceleration duration.
typedef struct { double accel_z, cruise_z, length; } thread_ramp_t;
static void thread_ramp_point(thread_ramp_t, unsigned, double *, double *);
static thread_ramp_t thread_ramp(const h5_cycle_plan_t *p, unsigned pass)
{
    double depth = round(h5_cycle_depth(p, pass)*p->x_steps_mm)/p->x_steps_mm;
    double clear = round(p->clearance*p->x_steps_mm)/p->x_steps_mm;
    // Two X steps reserve the maximum endpoint rounding difference in a chord.
    double stroke = fabs(depth-clear) + 2/p->x_steps_mm;
    double peak = fmin(p->thread_x_rate, sqrt(stroke*p->thread_x_acceleration));
    double accel_time = peak/p->thread_x_acceleration;
    double cruise_time = fmax(0, stroke/peak-accel_time);
    double z_speed = p->lead*p->config.rpm_limit/60;
    thread_ramp_t r;
    r.accel_z = fmax(1, ceil(z_speed*accel_time*p->z_steps_mm/H5_THREAD_ACCEL_SEGMENTS))
                  *H5_THREAD_ACCEL_SEGMENTS/p->z_steps_mm;
    r.cruise_z = fmax(1, ceil(z_speed*cruise_time*p->z_steps_mm))/p->z_steps_mm;
    for (;;) {
        r.length = 2*r.accel_z+r.cruise_z;
        if (!isfinite(r.length) || r.length > 300) return r; // Caller rejects travel overflow.
        bool fits = true;
        // At very short acceleration intervals, a single rounded X step can
        // exceed the speed cap. Check both directions of the actual quantized
        // ramp and lengthen only the offending interval by one Z-step group.
        for (unsigned reverse=0;reverse<2 && fits;reverse++) {
            double origin = reverse ? depth : clear, target = reverse ? clear : depth;
            double previous_x=origin, previous_z=0;
            for (unsigned i=1;i<=H5_THREAD_RAMP_SEGMENTS;i++) {
                double fraction,z;thread_ramp_point(r,i,&fraction,&z);
                double x=round((origin+(target-origin)*fraction)*p->x_steps_mm)/p->x_steps_mm;
                if (fabs(x-previous_x)*z_speed > p->thread_x_rate*(z-previous_z)+1e-10) {
                    if(i==H5_THREAD_ACCEL_SEGMENTS+1) r.cruise_z += 1/p->z_steps_mm;
                    else r.accel_z += H5_THREAD_ACCEL_SEGMENTS/p->z_steps_mm;
                    fits=false;break;
                }
                previous_x=x;previous_z=z;
            }
        }
        if(fits) return r;
    }
}
void h5_thread_stations(const h5_cycle_plan_t *p, unsigned pass, double *begin, double *end)
{
    thread_ramp_t r = thread_ramp(p, pass);
    *begin = p->entry_begin + p->direction*r.length;
    *end = p->exit_end - p->direction*r.length;
}
bool h5_cycle_plan(const h5_cycle_config_t *c, const h5_cycle_machine_t *m, h5_cycle_plan_t *p, char *error,
                   size_t size)
{
    memset(p, 0, sizeof(*p));
    const double values[] = {c->pitch,
                             c->x_min,
                             c->x_max,
                             c->z_min,
                             c->z_max,
                             c->rpm_limit,
                             m->x,
                             m->z,
                             m->rpm,
                             m->z_acceleration,
                             m->z_max_rate,
                             m->z_steps_mm,
                             m->x_acceleration,
                             m->x_max_rate,
                             m->x_steps_mm};
    for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++)
        if (!isfinite(values[i]) || fabs(values[i]) > 100000)
            return fail(error, size, "Invalid cycle coordinates or settings");
    if (!c->passes || c->passes > 999 || !c->starts || c->starts > 124 || fabs(c->pitch) < .0001)
        return fail(error, size, "Set pitch, passes and starts");
    if (c->operation > H5_ELLIPSE)
        return fail(error, size, "Unknown cycle operation");
    if (c->x_min >= c->x_max || (c->operation != H5_CUT && c->z_min >= c->z_max))
        return fail(error, size, "Set both X and Z machining bounds");
    if (fabs(m->rpm) < 30 || c->rpm_limit < fabs(m->rpm))
        return fail(error, size, "Spindle must run within the selected RPM limit");
    if (m->z_acceleration <= 0 || m->z_steps_mm <= 0 || m->z_max_rate <= 0)
        return fail(error, size, "Invalid Z motion settings");
    p->config = *c;
    if (c->threading)
        p->config.operation = H5_THREAD;
    bool face = p->config.operation == H5_FACE, cut = p->config.operation == H5_CUT;
    p->cut_axis = face || cut ? 'X' : 'Z';
    p->depth_axis = face || cut ? 'Z' : 'X';
    // Thread needs phase registration; its synchronization travel must fit
    // inside the entered bounds. Ordinary turning uses G95 feed like facing.
    p->indexed = p->config.operation == H5_THREAD;
    double rate = face || cut ? m->x_max_rate : m->z_max_rate;
    double steps = face || cut ? m->x_steps_mm : m->z_steps_mm;
    double acceleration = face || cut ? m->x_acceleration : m->z_acceleration;
    if (rate <= 0 || steps <= 0 || acceleration <= 0)
        return fail(error, size, "Invalid cutting axis settings");
    p->starts = p->config.operation == H5_THREAD ? c->starts : 1;
    p->lead = fabs(c->pitch) * p->starts;
    if (p->lead * c->rpm_limit > rate * .89)
        return fail(error, size, "Reduce pitch, starts or spindle RPM");
    p->spindle_direction = m->rpm > 0 ? 1 : -1;
    p->direction = (c->pitch > 0 ? 1 : -1) * p->spindle_direction;
    double main_min = face || cut ? c->x_min : c->z_min, main_max = face || cut ? c->x_max : c->z_max;
    double depth_min = face ? c->z_min : c->x_min, depth_max = face ? c->z_max : c->x_max;
    p->cut_start = p->direction > 0 ? main_min : main_max;
    p->cut_end = p->direction > 0 ? main_max : main_min;
    p->depth_start = c->aux_forward ? depth_min : depth_max;
    p->depth_end = c->aux_forward ? depth_max : depth_min;
    p->clearance = p->depth_start + (c->aux_forward ? -.5 : .5);
    p->cut_acceleration = acceleration;
    p->approach = p->takeup = p->cut_start;
    p->finish = p->cut_end;
    if (p->indexed) {
        // Return to the boundary, then take up one step in the cutting
        // direction. Acceleration and braking consume usable thread length;
        // neither is permission to travel beyond a cleared shoulder.
        p->approach += p->direction / steps;
        if (m->x_steps_mm <= 0 || m->x_max_rate <= 0 || m->x_acceleration <= 0)
            return fail(error, size, "Invalid X motion settings for moving thread infeed");
        p->x_steps_mm = m->x_steps_mm;
        p->z_steps_mm = steps;
        p->thread_x_rate = m->x_max_rate/60;
        p->thread_x_acceleration = m->x_acceleration;
        // Z run-up and braking stay at clearance, inside the entered bounds.
        double v = p->lead * c->rpm_limit / 60;
        double run = (ceil(v * v / (2 * acceleration) * steps) + 1) / steps;
        double ramp = thread_ramp(p, c->passes-1).length;
        double required = ramp;
        // Step rounding can make a shallower transition slightly longer. Check
        // every pass before accepting, while the preview remains final-pass geometry.
        for(unsigned pass=0;pass+1<c->passes;pass++)
            required=fmax(required,thread_ramp(p,pass).length);
        if (fabs(p->finish - p->approach) < 2 * (run + required) + 1 / steps - 1e-9) {
            snprintf(error, size, "Thread needs %.3f mm Z at %.0f RPM for moving X entry/retract; reduce RPM",
                     2 * (run + required) + 2 / steps, c->rpm_limit);
            return false;
        }
        p->entry_begin = p->approach + p->direction * run;
        p->full_begin = p->entry_begin + p->direction * ramp;
        p->exit_end = p->finish - p->direction * run;
        p->full_end = p->exit_end - p->direction * ramp;
    }
    if (cut)
        p->depth_start = p->depth_end = p->clearance = m->z;
    if (p->config.operation == H5_ELLIPSE) {
        // Preserve H5's spindle-progress parameter: each pass is a scaled
        // quarter ellipse. Arc feed varies so equal parameter increments
        // consume equal spindle revolutions.
        p->depth_start = c->x_min;
        p->depth_end = c->x_max;
        p->clearance = c->x_min - .5;
        double ratio = (c->x_max - c->x_min) / (c->z_max - c->z_min);
        if (p->lead * c->rpm_limit * M_PI / 2 > .89 * m->z_max_rate ||
            p->lead * c->rpm_limit * M_PI / 2 * ratio > .89 * m->x_max_rate)
            return fail(error, size, "Ellipse exceeds X/Z feed limits; reduce RPM or pitch");
        double radius = fmax(c->x_max - c->x_min, c->z_max - c->z_min);
        p->segments = (unsigned)fmax(8, ceil(M_PI / 2 * sqrt(radius / (8 * .002))));
        if (p->segments > 256)
            return fail(error, size, "Ellipse needs too many segments");
    }
    double main_low = fmin(p->takeup, p->finish), main_high = fmax(p->takeup, p->finish);
    double depth_low = fmin(p->depth_start, fmin(p->depth_end, p->clearance));
    double depth_high = fmax(p->depth_start, fmax(p->depth_end, p->clearance));
    double xmin = fmin(m->x, face || cut ? main_low : depth_low),
           xmax = fmax(m->x, face || cut ? main_high : depth_high);
    double zmin = fmin(m->z, face || cut ? depth_low : main_low),
           zmax = fmax(m->z, face || cut ? depth_high : main_high);
    if (xmax - xmin > 100 || zmax - zmin > 300)
        return fail(error, size, "Cycle including clearance exceeds travel span");
    if (error && size)
        error[0] = 0;
    return true;
}
double h5_cycle_depth(const h5_cycle_plan_t *p, unsigned pass)
{
    return p->depth_start + (p->depth_end - p->depth_start) * (pass + 1) / p->config.passes;
}
unsigned h5_cycle_phase(const h5_cycle_plan_t *p, unsigned start)
{
    // Reference the entered start bound, regardless of the approach offset.
    // Choose each start directly to avoid cumulative rounding (e.g. seven starts).
    double offset = p->direction * (p->approach - p->cut_start);
    long phase = lround(1200.0 * ((double)start / p->starts + offset / p->lead));
    return (unsigned)((phase % 1200 + 1200) % 1200);
}

const char *h5_cycle_name(h5_cycle_operation_t op)
{
    static const char *names[] = {"Turn", "Thread", "Face", "Cut", "Ellipse"};
    return op <= H5_ELLIPSE ? names[op] : "Unknown";
}
void h5_cycle_point(const h5_cycle_plan_t *p, unsigned pass, unsigned segment, double *x, double *z,
                    double *feed)
{
    double fraction = (pass + 1.0) / p->config.passes;
    double dx = (p->depth_end - p->depth_start) * fraction, dz = (p->cut_end - p->cut_start) * fraction;
    double t = M_PI / 2 * segment / p->segments,
           previous = M_PI / 2 * (segment ? segment - 1 : 0) / p->segments;
    double a = p->config.aux_forward ? sin(t) : 1 - cos(t), b = p->config.aux_forward ? 1 - cos(t) : sin(t);
    double pa = p->config.aux_forward ? sin(previous) : 1 - cos(previous),
           pb = p->config.aux_forward ? 1 - cos(previous) : sin(previous);
    *x = p->depth_start + dx * b;
    *z = p->cut_end - dz + dz * a;
    *feed = p->lead * hypot(dx * (b - pb), dz * (a - pa)) / (fabs(dz) / p->segments);
}

// Distance along a trapezoidal-velocity X transition, parameterized by Z.
static void thread_ramp_point(thread_ramp_t r, unsigned point, double *fraction, double *z)
{
    double shape;
    if (point <= H5_THREAD_ACCEL_SEGMENTS) {
        double t = (double)point/H5_THREAD_ACCEL_SEGMENTS;
        *z = r.accel_z*t;
        shape = .5*r.accel_z*t*t;
    } else if (point == H5_THREAD_ACCEL_SEGMENTS+1) {
        *z = r.accel_z+r.cruise_z;
        shape = .5*r.accel_z+r.cruise_z;
    } else {
        double t = (double)(point-H5_THREAD_ACCEL_SEGMENTS-1)/H5_THREAD_ACCEL_SEGMENTS;
        *z = r.accel_z+r.cruise_z+r.accel_z*t;
        shape = r.accel_z+r.cruise_z-.5*r.accel_z*(1-t)*(1-t);
    }
    *fraction = shape/(r.accel_z+r.cruise_z);
}
void h5_thread_point(const h5_cycle_plan_t *p, unsigned pass, unsigned point, double *x, double *z)
{
    double depth = round(h5_cycle_depth(p,pass)*p->x_steps_mm)/p->x_steps_mm;
    double clear = round(p->clearance*p->x_steps_mm)/p->x_steps_mm;
    thread_ramp_t r = thread_ramp(p,pass);
    double begin,end;
    h5_thread_stations(p,pass,&begin,&end);
    if (point == 0) { *x = clear; *z = p->approach; }
    else if (point == 1) { *x = clear; *z = p->entry_begin; }
    else if (point <= H5_THREAD_RAMP_SEGMENTS + 1) {
        double fraction,dz;thread_ramp_point(r,point-1,&fraction,&dz);
        *x = clear + (depth-clear)*fraction;
        *z = p->entry_begin + p->direction*dz;
    } else if (point == H5_THREAD_RAMP_SEGMENTS + 2) { *x = depth; *z = end; }
    else if (point < H5_THREAD_BLOCKS) {
        double fraction,dz;thread_ramp_point(r,point-H5_THREAD_RAMP_SEGMENTS-2,&fraction,&dz);
        *x = depth + (clear-depth)*fraction;
        *z = end + p->direction*dz;
    } else { *x = clear; *z = p->finish; }
    *x = round(*x*p->x_steps_mm)/p->x_steps_mm;
    *z = round(*z*p->z_steps_mm)/p->z_steps_mm;
}
