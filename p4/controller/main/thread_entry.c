/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thread_entry.h"
#include "spindle.h"
#include "grbl/planner.h"
#include "grbl/protocol.h"
#include "grbl/motion_control.h"
#include "grbl/machine_limits.h"
#include "grbl/stepper.h"
#include "grbl/state_machine.h"
#include <math.h>
#include <string.h>

// Queue both blocks before the protocol starts either. The native planner and
// step ISR own every pulse and the X-to-Z handoff; no foreground command gap.
static status_code_t queue_entry(double x, double z, double lead)
{
    if (state_get() != STATE_IDLE || st_is_stepping() || plan_get_current_block() ||
        plan_get_block_buffer_available() < 2 || sys.abort || sys.alarm)
        return Status_IdleError;
    float rpm = fabsf(h5_spindle_profile_rpm());
    if (!(rpm > 0)) return Status_GcodeSpindleNotRunning;
    if (!isfinite(x) || !isfinite(z) || !isfinite(lead) || lead <= 0 ||
        lead * rpm > settings.axis[Z_AXIS].max_rate)
        return Status_GcodeMaxFeedRateExceeded;
    float target[N_AXIS];
    system_convert_array_steps_to_mpos(target, sys.position);
    if (lroundf(x * settings.axis[X_AXIS].steps_per_mm) == sys.position[X_AXIS] ||
        lroundf(z * settings.axis[Z_AXIS].steps_per_mm) == sys.position[Z_AXIS])
        return Status_InvalidStatement;
    // Validate both native machine targets before placing either in the queue.
    // A rejected Z target must never leave an executable X plunge behind.
    float cut_target[N_AXIS];
    target[X_AXIS] = x;
    memcpy(cut_target, target, sizeof(target)); cut_target[Z_AXIS] = z;
    limits_soft_check(target, (planner_cond_t){0});
    if (sys.abort || sys.alarm) return Status_IdleError;
    limits_soft_check(cut_target, (planner_cond_t){0});
    if (sys.abort || sys.alarm) return Status_IdleError;
    plan_line_data_t data;
    plan_data_init(&data);
    data.spindle = *gc_spindle_get(0);
    data.spindle.state.synchronized = Off;
    data.condition.no_feed_override = On;
    data.overrides = sys.override.control;
    data.overrides.sync = On;
    data.overrides.feed_rates_disable = data.overrides.spindle_rpm_disable = On;
    data.overrides.feed_hold_disable = On;
    data.feed_rate = settings.axis[X_AXIS].max_rate;
    data.sync_preload = true;
    target[X_AXIS] = x;
    if (!plan_buffer_line(target, &data)) return Status_InvalidStatement;
    plan_block_t *entry = plan_get_current_block();
    // Trapezoidal or triangular rest-to-rest motion, using native rounded steps
    // and the planner's actual rate/acceleration, never the unrounded UI distance.
    double speed = plan_compute_profile_nominal_speed(entry) / 60.0;
    double acceleration = entry->acceleration / 3600.0;
    double duration = entry->millimeters <= speed * speed / acceleration
        ? 2 * sqrt(entry->millimeters / acceleration)
        : entry->millimeters / speed + speed / acceleration;
    data.sync_preload = false;
    data.sync_preloaded = true;
    data.spindle.state.synchronized = On;
    data.feed_rate = lead;
    target[Z_AXIS] = z;
    // Nonzero rounded motion and two free slots were checked before queuing.
    if (!plan_buffer_line(target, &data)) {
        plan_reset(); plan_sync_position();
        return Status_InvalidStatement;
    }
    h5_spindle_entry_arm(duration, lead, settings.axis[Z_AXIS].acceleration / 3600.0);
    memcpy(gc_state.position, target, sizeof(target));
    return Status_OK;
}

status_code_t h5_thread_entry_execute(double x, double z, double lead)
{
    gc_override_flags_t overrides = sys.override.control;
    status_code_t status = queue_entry(x, z, lead);
    if (status == Status_OK) {
        // Like G33, restore override controls only after the synchronized pass.
        // The realtime hook remains active for STOP and spindle pause/reversal.
        protocol_buffer_synchronize();
        mc_override_ctrl_update(overrides);
        gc_sync_position();
    }
    return status;
}
