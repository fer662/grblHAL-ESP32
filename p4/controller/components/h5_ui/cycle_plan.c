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
    if (m->z_acceleration <= 0 || m->z_steps_mm <= 0 || m->z_max_rate <= 0)
        return fail(error, size, "Invalid Z motion settings");
    p->config = *c;
    if (c->threading)
        p->config.operation = H5_THREAD;
    bool face = p->config.operation == H5_FACE, cut = p->config.operation == H5_CUT;
    p->cut_axis = face || cut ? 'X' : 'Z';
    p->depth_axis = face || cut ? 'Z' : 'X';
    // Thread needs phase registration. Ordinary turning uses G95 like facing.
    p->indexed = p->config.operation == H5_THREAD;
    double rate = face || cut ? m->x_max_rate : m->z_max_rate;
    double steps = face || cut ? m->x_steps_mm : m->z_steps_mm;
    double acceleration = face || cut ? m->x_acceleration : m->z_acceleration;
    if (rate <= 0 || steps <= 0 || acceleration <= 0)
        return fail(error, size, "Invalid cutting axis settings");
    p->starts = p->config.operation == H5_THREAD ? c->starts : 1;
    p->lead = fabs(c->pitch) * p->starts;
    p->spindle_direction = m->rpm < 0 ? -1 : 1;
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
        // Return to the boundary, then take up one step inside the cut.
        // Geometry does not depend on the spindle speed at preview/startup.
        p->approach += p->direction / steps;
        if ((p->finish - p->approach) * p->direction < 1 / steps - 1e-9)
            return fail(error, size, "Thread travel needs a takeup step and a cutting step");
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
    return h5_cycle_phase_at(p, start, p->approach, p->spindle_direction);
}
unsigned h5_cycle_phase_at(const h5_cycle_plan_t *p, unsigned start, double position, int spindle_direction)
{
    // Reference the entered start bound, regardless of the approach offset.
    // Choose each start directly to avoid cumulative rounding (e.g. seven starts).
    double offset = p->direction * (position - p->cut_start);
    long phase = lround(1200.0 * spindle_direction * p->spindle_direction *
                       ((double)start / p->starts + offset / p->lead));
    return (unsigned)((phase % 1200 + 1200) % 1200);
}

const char *h5_cycle_name(h5_cycle_operation_t op)
{
    static const char *names[] = {"Turn", "Thread", "Face", "Cut", "Ellipse"};
    return op <= H5_ELLIPSE ? names[op] : "Unknown";
}
unsigned h5_cycle_segment_at(const h5_cycle_plan_t *p, unsigned pass, double x, double z)
{
    // Locate the stopped point, not the last queued chord: lookahead can be far ahead.
    unsigned nearest = 0;
    double best = INFINITY, ax, az, feed;
    h5_cycle_point(p, pass, 0, &ax, &az, &feed);
    for (unsigned i = 0; i < p->segments; i++) {
        double bx, bz;
        h5_cycle_point(p, pass, i + 1, &bx, &bz, &feed);
        double dx = bx - ax, dz = bz - az;
        double t = fmax(0, fmin(1, ((x - ax) * dx + (z - az) * dz) / (dx * dx + dz * dz)));
        double distance = hypot(x - ax - t * dx, z - az - t * dz);
        if (distance < best) { best = distance; nearest = i; }
        ax = bx; az = bz;
    }
    return nearest;
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
