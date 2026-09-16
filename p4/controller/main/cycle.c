/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cycle.h"
#include "follow.h"
#include "freertos/FreeRTOS.h"
#include "critical.h"
#include "grbl/planner.h"
#include "grbl/protocol.h"
#include "grbl/state_machine.h"
#include "grbl/stepper.h"
#include "serial.h"
#include "spindle.h"
#include "update.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static h5_cycle_config_t request;
static h5_cycle_status_t published;
static bool pending, stopping, owns_stream, advance_requested;
static h5_cycle_plan_t plan;
static unsigned stage, pass, start, segment;
static uint32_t command_id;
static bool waiting_ack, cancel_requested, trace_enabled;
static char stop_reason[96] = "Cycle cancelled";
static const char *const names[] = {
    "Setup",   "Retract", "Approach", "Take up",         "Infeed",        "Register",      "Spindle", "Cut",
    "Retract", "Return",  "Take up",  "Return to start", "Finish infeed", "Restore phase", "Finish"};

bool h5_cycle_busy(void)
{
    h5_critical_enter(&lock, 2000 + __LINE__);
    bool busy = published.active;
    h5_critical_exit(&lock);
    return busy || h5_follow_busy();
}
bool h5_cycle_owns_stream(void)
{
    h5_critical_enter(&lock, 2000 + __LINE__);
    bool owns = owns_stream;
    h5_critical_exit(&lock);
    return owns || h5_follow_busy();
}
void h5_cycle_snapshot(h5_cycle_status_t *s)
{
    if (h5_follow_selected()) {
        h5_follow_snapshot(s);
        return;
    }
    h5_critical_enter(&lock, 2000 + __LINE__);
    *s = published;
    h5_critical_exit(&lock);
}
bool h5_cycle_request(const h5_cycle_config_t *c)
{
    if (h5_follow_busy() || h5_update_active() || h5_axis_change_pending())
        return false;
    if (!h5_operation_claim(H5_OWNER_PROFILE))
        return false;
    h5_follow_clear();
    h5_critical_enter(&lock, 2000 + __LINE__);
    bool ok = !published.active;
    if (ok) {
        request = *c;
        memcpy(stop_reason, "Cycle cancelled", sizeof("Cycle cancelled"));
        pending = true;
        stopping = advance_requested = false;
        published.active = true;
        published.pass = published.start = 0;
        memcpy(published.message, "Preparing cycle", sizeof("Preparing cycle"));
    }
    h5_critical_exit(&lock);
    if (!ok)
        h5_operation_release(H5_OWNER_PROFILE);
    return ok;
}
bool h5_cycle_advance(void)
{
    h5_critical_enter(&lock, 2000 + __LINE__);
    bool ok = published.active;
    if (ok)
        advance_requested = true;
    h5_critical_exit(&lock);
    return ok;
}
void h5_cycle_cancel(void)
{
    if (h5_follow_busy()) {
        h5_follow_cancel();
        return;
    }
    h5_critical_enter(&lock, 2000 + __LINE__);
    if (published.active)
        stopping = true;
    h5_critical_exit(&lock);
}
static void message(const char *text, bool active)
{
    char next[sizeof(published.message)] = {0};
    snprintf(next, sizeof(next), "%s", text);
    h5_critical_enter(&lock, 2000 + __LINE__);
    published.active = active;
    if (!active)
        owns_stream = false;
    published.pass = pass < plan.config.passes ? pass + 1 : plan.config.passes;
    published.start = start + 1;
    memcpy(published.message, next, sizeof(next));
    h5_critical_exit(&lock);
    if (!active)
        h5_operation_release(H5_OWNER_PROFILE);
    char line[180];
    snprintf(line, sizeof(line), "[H5CYCLE:PASS:%u|START:%u|STAGE:%s]\r\n", pass + 1, start + 1, text);
    hal.stream.write(line);
}
void h5_cycle_reset(void)
{
    h5_follow_reset();
    h5_operation_release(H5_OWNER_PROFILE);
    h5_critical_enter(&lock, 2000 + __LINE__);
    if (published.active)
        memcpy(published.message, stop_reason, sizeof(published.message));
    published.active = pending = stopping = owns_stream = false;
    h5_critical_exit(&lock);
    waiting_ack = cancel_requested = false;
    command_id = 0;
}
static bool idle(void)
{
    return state_get() == STATE_IDLE && !st_is_stepping() && !plan_get_current_block();
}
static void emit(void)
{
    if (!h5_cycle_busy() || sys.abort)
        return;
    char line[116];
    bool cut = plan.config.operation == H5_CUT, ellipse = plan.config.operation == H5_ELLIPSE;
    double approach = plan.approach, infeed = h5_cycle_depth(&plan, pass), endpoint = plan.finish;
    if (cut)
        endpoint = plan.cut_start + (plan.cut_end - plan.cut_start) * (pass + 1.0) / plan.config.passes;
    if (ellipse) {
        double unused;
        h5_cycle_point(&plan, pass, 0, &infeed, &approach, &unused);
    }
    switch (stage) {
    case 0:
        snprintf(line, sizeof(line), "G21G18G8G90G94");
        break;
    case 1:
    case 8:
        snprintf(line, sizeof(line), "G90G94G53G0%c%.6f", plan.depth_axis, plan.clearance);
        break;
    case 2:
    case 9:
        snprintf(line, sizeof(line), "G90G94G53G0%c%.6f", plan.cut_axis,
                 plan.indexed ? plan.takeup : approach);
        break;
    case 3:
    case 10:
        snprintf(line, sizeof(line), "G90G94G53G0%c%.6f", plan.cut_axis, approach);
        break;
    case 4:
        snprintf(line, sizeof(line), "G90G94G53G0%c%.6f", plan.depth_axis, infeed);
        break;
    case 5:
        snprintf(line, sizeof(line), "$P4PHASE=%u", h5_cycle_phase(&plan, start));
        break;
    case 6:
        snprintf(line, sizeof(line), "M%dS%.3f", plan.spindle_direction > 0 ? 3 : 4, fabs(h5_spindle_rpm()));
        break;
    case 7:
        if (ellipse) {
            double x, z, feed, px, pz, unused;
            h5_cycle_point(&plan, pass, segment + 1, &x, &z, &feed);
            h5_cycle_point(&plan, pass, segment, &px, &pz, &unused);
            snprintf(line, sizeof(line), "G91G95G1X%.6fZ%.6fF%.6f", x - px, z - pz, feed);
        } else if (plan.indexed)
            snprintf(line, sizeof(line), "G91G33%c%.6fK%.6f", plan.cut_axis, endpoint - approach, plan.lead);
        else
            snprintf(line, sizeof(line), "G91G95G1%c%.6fF%.6f", plan.cut_axis, endpoint - approach,
                     plan.lead);
        break;
    case 11:
        snprintf(line, sizeof(line), "G90G94G53G0%c%.6f", plan.cut_axis, plan.cut_start);
        break;
    case 12:
        snprintf(line, sizeof(line), "G90G94G53G0%c%.6f", plan.depth_axis, plan.depth_start);
        break;
    case 13:
        snprintf(line, sizeof(line), "$P4PHASE=0");
        break;
    default:
        snprintf(line, sizeof(line), "G90G94M5");
        break;
    }
    command_id = h5_bridge_cycle_submit(line);
    if (!command_id) {
        message("Cycle command queue failed", true);
        h5_cycle_cancel();
        return;
    }
    waiting_ack = true;
    message(names[stage], true);
}
static void poll_cycle(void)
{
    h5_critical_enter(&lock, 2000 + __LINE__);
    bool active = published.active, stop = stopping, begin = pending;
    h5_cycle_config_t config = request;
    if (begin)
        pending = false;
    h5_critical_exit(&lock);
    if (!active)
        return;
    if (begin) {
        pass = start = stage = segment = 0;
        waiting_ack = cancel_requested = false;
        char error[96];
        h5_cycle_machine_t machine = {.x = (double)sys.position[0] / settings.axis[0].steps_per_mm,
                                      .z = (double)sys.position[2] / settings.axis[2].steps_per_mm,
                                      .rpm = h5_spindle_rpm(),
                                      .z_acceleration = settings.axis[2].acceleration / 3600.0,
                                      .z_max_rate = settings.axis[2].max_rate,
                                      .z_steps_mm = settings.axis[2].steps_per_mm,
                                      .x_acceleration = settings.axis[0].acceleration / 3600.0,
                                      .x_max_rate = settings.axis[0].max_rate,
                                      .x_steps_mm = settings.axis[0].steps_per_mm};
        if (!idle() || !h5_bridge_empty() || h5_serial_pending()) {
            message("Stop other motion before starting cycle", false);
            return;
        }
        if (gc_state.modal.scaling_active
#ifdef ROTATION_ENABLE
            || gc_state.modal.g5x_offset.data.rotation != 0
#endif
        ) {
            message("Cancel coordinate scaling/rotation before a cycle", false);
            return;
        }
        if (!h5_cycle_plan(&config, &machine, &plan, error, sizeof(error))) {
            message(error, false);
            return;
        }
        h5_critical_enter(&lock, 2000 + __LINE__);
        owns_stream = true;
        h5_critical_exit(&lock);
        char info[320];
        snprintf(info, sizeof(info),
                 "[H5PLAN:LEAD:%.6f|APPROACH:%.6f|FINISH:%.6f|CLEARANCE:%.6f|RPM_"
                 "LIMIT:%.3f|CUT_LENGTH:%.6f|ACCEL:%.3f]\r\n",
                 plan.lead, plan.approach, plan.finish, plan.clearance,
                 config.rpm_limit, fabs(plan.finish-plan.approach), plan.cut_acceleration);
        hal.stream.write(info);
    }
    if (sys.alarm) {
        message("Cycle stopped by controller fault", false);
        return;
    }
    if (sys.abort) {
        message(stop ? stop_reason : "Cycle reset", false);
        return;
    }
    if (!stop &&
        (state_get() == STATE_HOLD || (stage >= 1 && (h5_spindle_rpm() * plan.spindle_direction < 30 ||
                                                      fabs(h5_spindle_rpm()) > plan.config.rpm_limit)))) {
        snprintf(stop_reason, sizeof(stop_reason), "Spindle outside RPM range or feed hold");
        message(stop_reason, true);
        h5_cycle_cancel();
        stop = true;
    }
    if (stop) {
        if (!cancel_requested) {
            h5_bridge_discard_cycle_commands();
            // G33 intentionally disables feed hold. Motion cancel still uses
            // the core's normal deceleration, then we discard the stopped pass.
            system_set_exec_state_flag(EXEC_MOTION_CANCEL);
            cancel_requested = true;
            message("Stopping cycle", true);
        }
        // The core's index-wait loop explicitly handles EXEC_STOP by resetting
        // before st_wake_up: no STEP output has started in that case.
        if (h5_spindle_waiting_index())
            system_set_exec_state_flag(EXEC_STOP);
        // Timer shutdown precedes the core's cycle-complete handling. Wait for
        // both, otherwise mc_reset correctly reports an in-motion reset alarm.
        else if (state_get() == STATE_IDLE && !st_is_stepping() && !sys.step_control.execute_hold)
            protocol_enqueue_realtime_command(CMD_RESET);
        return;
    }
    if (waiting_ack) {
        h5_status_t status;
        h5_bridge_snapshot(&status);
        if (status.completed_id != command_id)
            return;
        if (status.command_status) {
            snprintf(stop_reason, sizeof(stop_reason), "Cycle command rejected (%d)", status.command_status);
            message(stop_reason, true);
            h5_cycle_cancel();
            return;
        }
        if (stage == 7 && plan.config.operation == H5_ELLIPSE && segment + 1 < plan.segments) {
            // Keep lookahead populated across chords; waiting for Idle here
            // would force a stop at every point on the ellipse.
            segment++;
            waiting_ack = false;
            emit();
            return;
        }
        if (!idle())
            return;
        waiting_ack = false;
        if (trace_enabled) {
            char done[180];
            snprintf(done, sizeof(done), "[H5DONE:PASS:%u|START:%u|STAGE:%s|X:%.6f|Z:%.6f]\r\n", pass + 1,
                     start + 1, names[stage], (double)sys.position[0] / settings.axis[0].steps_per_mm,
                     (double)sys.position[2] / settings.axis[2].steps_per_mm);
            hal.stream.write(done);
            if (stage == 7 && plan.indexed) {
                char sync[] = "P4SYNC", points[] = "P4SYNCTRACE";
                h5_spindle_command(STATE_IDLE, sync);
                h5_spindle_command(STATE_IDLE, points);
            }
        }
        if (stage == 10) {
            if (++start == plan.starts) {
                start = 0;
                pass++;
                h5_critical_enter(&lock, 2000 + __LINE__);
                bool advance = advance_requested;
                advance_requested = false;
                h5_critical_exit(&lock);
                if (advance && pass + 1 < plan.config.passes)
                    pass++;
            }
            stage = pass < plan.config.passes ? (plan.config.operation == H5_ELLIPSE ? 2 : 4) : 11;
            segment = 0;
        } else if (stage == 14) {
            message("Cycle complete", false);
            return;
        } else
            stage++;
    }
    if (idle())
        emit();
}
void h5_cycle_poll(void)
{
    // UART writes can call the realtime hook while making room in the FIFO.
    // Keep a diagnostic/report from recursively advancing the same transition.
    static bool entered;
    if (entered)
        return;
    entered = true;
    h5_follow_poll();
    poll_cycle();
    entered = false;
}
status_code_t h5_cycle_command(sys_state_t state, char *line)
{
    if (!strcmp(line, "P4ADVANCE"))
        return h5_cycle_advance() ? Status_OK : Status_IdleError;
    if (!strcmp(line, "P4RELEASE")) {
        h5_follow_release();
        return Status_OK;
    }
    if (!strncmp(line, "P4MANUAL=", 9)) {
        char axis, extra;
        int sign;
        double distance;
        unsigned held;
        if (sscanf(line + 9, "%c,%d,%lf,%u%c", &axis, &sign, &distance, &held, &extra) != 4 || held > 1)
            return Status_InvalidStatement;
        return h5_follow_jog(axis, sign, distance, held) ? Status_OK : Status_IdleError;
    }
    if (!strncmp(line, "P4FOLLOW=", 9)) {
        h5_follow_config_t c = {0};
        unsigned forward;
        char extra;
        if (sscanf(line + 9, "%u,%lf,%lf,%u,%lf,%lf,%lf,%lf%c", &c.mode, &c.pitch, &c.ratio, &forward,
                   &c.x_min, &c.x_max, &c.z_min, &c.z_max, &extra) != 8 ||
            forward > 1)
            return Status_InvalidStatement;
        c.aux_forward = forward;
        return h5_follow_request(&c) ? Status_OK : Status_IdleError;
    }
    if (!strncmp(line, "P4CYCLETRACE=", 13)) {
        if (state != STATE_IDLE || h5_cycle_busy())
            return Status_IdleError;
        if (strcmp(line + 13, "0") && strcmp(line + 13, "1"))
            return Status_InvalidStatement;
        trace_enabled = line[13] == '1';
        return Status_OK;
    }
    if (!strcmp(line, "P4CYCLE")) {
        h5_cycle_status_t s;
        h5_cycle_snapshot(&s);
        char text[180];
        snprintf(text, sizeof(text), "[H5CYCLE:ACTIVE:%u|PASS:%u|START:%u|STAGE:%s]\r\n", s.active, s.pass,
                 s.start, s.message);
        hal.stream.write(text);
        return Status_OK;
    }
    if (strncmp(line, "P4CYCLE=", 8))
        return Status_Unhandled;
    if (state != STATE_IDLE)
        return Status_IdleError;
    h5_cycle_config_t c = {0};
    unsigned thread, forward;
    char extra;
    if (sscanf(line + 8, "%u,%lf,%u,%u,%u,%lf,%lf,%lf,%lf,%lf%c", &thread, &c.pitch, &c.passes, &c.starts,
               &forward, &c.x_min, &c.x_max, &c.z_min, &c.z_max, &c.rpm_limit, &extra) != 10 ||
        thread > H5_ELLIPSE || forward > 1)
        return Status_InvalidStatement;
    c.operation = thread;
    c.threading = thread == H5_THREAD;
    c.aux_forward = forward;
    return h5_cycle_request(&c) ? Status_OK : Status_IdleError;
}
