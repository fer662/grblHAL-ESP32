/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thread_path.h"
#include "bridge.h"
#include "update.h"
#include "grbl/planner.h"
#include "grbl/stepper.h"
#include "grbl/machine_limits.h"
#include "grbl/motion_control.h"
#include "grbl/protocol.h"
#include "grbl/state_machine.h"
#include <math.h>
#include <string.h>

// Called only by the core system-command dispatcher for the owning assisted
// cycle. Fill the entire native planner before allowing any cycle start. G33
// text commands cannot do this: each one synchronizes/drains the planner.
status_code_t h5_thread_execute(const h5_cycle_plan_t *p, unsigned pass)
{
    if (!p->indexed || pass >= p->config.passes || state_get() != STATE_IDLE ||
        !h5_motion_idle() || st_is_stepping() || plan_get_current_block() ||
        !h5_motion_axes_available() || h5_update_active() || sys.abort || sys.alarm)
        return Status_IdleError;
    if (plan_get_block_buffer_available() < H5_THREAD_BLOCKS) return Status_Overflow;
    float points[H5_THREAD_BLOCKS+1][N_AXIS];
    int32_t x_steps[H5_THREAD_BLOCKS+1], z_steps[H5_THREAD_BLOCKS+1];
    for (unsigned i=0; i<=H5_THREAD_BLOCKS; i++) {
        double x,z;
        h5_thread_point(p,pass,i,&x,&z);
        for(unsigned axis=0;axis<N_AXIS;axis++) points[i][axis]=gc_state.position[axis];
        points[i][X_AXIS]=x; points[i][Z_AXIS]=z;
        x_steps[i]=lroundf(points[i][X_AXIS]*settings.axis[X_AXIS].steps_per_mm);
        z_steps[i]=lroundf(points[i][Z_AXIS]*settings.axis[Z_AXIS].steps_per_mm);
    }
    if (fabsf(points[0][X_AXIS] - sys.position[X_AXIS]/settings.axis[X_AXIS].steps_per_mm) > .51f/settings.axis[X_AXIS].steps_per_mm ||
        fabsf(points[0][Z_AXIS] - sys.position[Z_AXIS]/settings.axis[Z_AXIS].steps_per_mm) > .51f/settings.axis[Z_AXIS].steps_per_mm)
        return Status_IdleError;
    plan_line_data_t data;
    plan_data_init(&data);
    data.spindle = *gc_spindle_get(-1);
    if (!data.spindle.hal || !data.spindle.hal->get_data) return Status_GcodeUnsupportedCommand;
    spindle_data_t *encoder = data.spindle.hal->get_data(SpindleData_RPM);
    if (encoder->rpm < 30 || encoder->rpm > p->config.rpm_limit ||
        encoder->ccw != (p->spindle_direction < 0)) return Status_IdleError;
    data.spindle.css = NULL;
    data.spindle.state.synchronized = On;
    data.overrides = sys.override.control;
    data.overrides.sync = On;
    data.overrides.feed_rates_disable = data.overrides.spindle_rpm_disable = On;
    data.overrides.feed_hold_disable = data.condition.no_feed_override = On;
    // Validate every target before adding any motion. The synchronous fill below
    // never yields; a pending realtime start cannot launch an incomplete pass.
    for(unsigned i=1;i<=H5_THREAD_BLOCKS;i++) {
        // Use exactly the same step quantization as plan_buffer_line, including
        // float precision at large machine coordinates. Never divide by a lost Z step.
        if ((z_steps[i]-z_steps[i-1])*p->direction <= 0 ||
            z_steps[i] < llround(p->config.z_min*settings.axis[Z_AXIS].steps_per_mm) ||
            z_steps[i] > llround(p->config.z_max*settings.axis[Z_AXIS].steps_per_mm))
            return Status_InvalidStatement;
        limits_soft_check(points[i], data.condition);
        if(sys.abort || sys.alarm) return Status_IdleError;
    }
    gc_override_flags_t overrides = sys.override.control;
    gc_state.distance_per_rev = p->lead;
    for(unsigned i=1;i<=H5_THREAD_BLOCKS;i++) {
        double dx=(double)(x_steps[i]-x_steps[i-1])/settings.axis[X_AXIS].steps_per_mm;
        double dz=fabs((double)(z_steps[i]-z_steps[i-1])/settings.axis[Z_AXIS].steps_per_mm);
        data.feed_rate = p->lead * hypot(dx,dz)/dz; // Path lead preserves Z lead.
        data.sync_continuation = i>1;
        data.sync_offset = fabs((double)(z_steps[i-1]-z_steps[0])/settings.axis[Z_AXIS].steps_per_mm)/p->lead;
        if(!plan_buffer_line(points[i],&data)) {
            plan_reset(); sync_position();
            return Status_IdleError;
        }
    }
    bool done = protocol_buffer_synchronize(); // One index wait; one continuous pass.
    mc_override_ctrl_update(overrides);
    if(!sys.abort) gc_sync_position();
    return done && !sys.abort && !sys.alarm ? Status_OK : Status_IdleError;
}
