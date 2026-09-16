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
    // Only threading needs phase registration and travel outside the cut for
    // synchronization. Ordinary turning uses bounded G95 feed like facing.
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
    double v = p->lead * c->rpm_limit / 60, deficit = v * v / (2 * acceleration);
    if (p->indexed) {
        p->lead_in = ceil((fmax(2 * p->lead, 4 * deficit + .25 * v) + .01) * steps) / steps;
        p->run_out = ceil((deficit + .25 * v + .01) * steps) / steps;
    }
    p->approach = p->cut_start - p->direction * p->lead_in;
    p->finish = p->cut_end + p->direction * p->run_out;
    p->takeup = p->approach - (p->indexed ? p->direction / steps : 0);
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
    // Reference the cutting start, independent of the lead-in distance. Choose
    // each start directly, avoiding cumulative rounding with e.g. seven starts.
    long phase = lround(1200.0 * ((double)start / p->starts - p->lead_in / p->lead));
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
