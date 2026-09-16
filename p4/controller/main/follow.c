/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "grbl/hal.h"
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
static h5_follow_config_t request, config;
static h5_cycle_status_t published;
static bool selected, pending, stop_requested, manual_pending, manual_held, release_requested;
static bool parameters_pending;
static uint32_t parameter_generation;
static char jog_axis;
static int jog_sign;
static double jog_distance;
static bool stopping, internal_reset, waiting_ack, manual_motion;
static unsigned stage;
static uint32_t command_id;
static double anchor_x, anchor_z, anchor_counts, slope, target_x, target_z, lead;
// G33 supplies powered spindle tracking. Below its acquisition range, feed
// measured encoder positions into the same native planner, without extrapolation.
// Stages 4/5 initialize/run position following; stages 0..3 are the G33 sequence.
static bool position_follow, position_registered;
static double submitted_x, submitted_z;
static uint32_t position_submit_time;
static int direction;
static int64_t previous_counts;
static bool idle(void) { return state_get() == STATE_IDLE && !st_is_stepping() && !plan_get_current_block(); }
static double pos(unsigned axis) { return (double)sys.position[axis] / settings.axis[axis].steps_per_mm; }
static void say(const char *s, bool active)
{
    char next[sizeof(published.message)] = {0};
    snprintf(next, sizeof(next), "%s", s);
    h5_critical_enter(&lock, 3000 + __LINE__);
    published.active = active;
    memcpy(published.message, next, sizeof(next));
    h5_critical_exit(&lock);
    if (!active) {
        h5_operation_release(H5_OWNER_FOLLOW);
        h5_spindle_follow(false);
    }
    char line[150];
    snprintf(line, sizeof(line), "[H5FOLLOW:ACTIVE:%u|STAGE:%s]\r\n", active, s);
    hal.stream.write(line);
}
bool h5_follow_busy(void)
{
    h5_critical_enter(&lock, 3000 + __LINE__);
    bool b = published.active;
    h5_critical_exit(&lock);
    return b;
}
bool h5_follow_manual_held(void)
{
    h5_critical_enter(&lock, 3000 + __LINE__);
    bool held = published.active && manual_held && !stop_requested;
    h5_critical_exit(&lock);
    return held;
}
bool h5_follow_selected(void)
{
    h5_critical_enter(&lock, 3000 + __LINE__);
    bool b = selected;
    h5_critical_exit(&lock);
    return b;
}
void h5_follow_clear(void)
{
    h5_critical_enter(&lock, 3000 + __LINE__);
    selected = false;
    h5_critical_exit(&lock);
}
void h5_follow_snapshot(h5_cycle_status_t *s)
{
    h5_critical_enter(&lock, 3000 + __LINE__);
    *s = published;
    h5_critical_exit(&lock);
}
bool h5_follow_request(const h5_follow_config_t *c)
{
    if (h5_cycle_busy() || h5_update_active() || h5_axis_change_pending())
        return false;
    if (!h5_operation_claim(H5_OWNER_FOLLOW))
        return false;
    h5_critical_enter(&lock, 3000 + __LINE__);
    request = *c;
    pending = selected = published.active = true;
    stop_requested = manual_pending = manual_held = release_requested = parameters_pending = false;
    memcpy(published.message, "Preparing assisted feed", sizeof("Preparing assisted feed"));
    h5_critical_exit(&lock);
    return true;
}
void h5_follow_cancel(void)
{
    h5_critical_enter(&lock, 3000 + __LINE__);
    stop_requested = true;
    manual_held = manual_pending = false;
    h5_critical_exit(&lock);
}
bool h5_follow_update(double pitch, double ratio, bool aux_forward)
{
    if (!isfinite(pitch) || fabs(pitch) < .0001 || fabs(pitch) > 100000 || !isfinite(ratio) ||
        fabs(ratio) > 100000) {
        h5_follow_cancel();
        return false;
    }
    h5_critical_enter(&lock, 3000 + __LINE__);
    bool active = published.active && !stop_requested;
    if (active) {
        request.pitch = pitch;
        request.ratio = ratio;
        request.aux_forward = aux_forward;
        parameter_generation++;
        parameters_pending = true;
    }
    h5_critical_exit(&lock);
    return active;
}
bool h5_follow_jog(char axis, int sign, double distance, bool held)
{
    if (!h5_follow_busy() || (axis != 'X' && axis != 'Z') || !isfinite(distance) || distance <= 0)
        return false;
    h5_critical_enter(&lock, 3000 + __LINE__);
    jog_axis = axis;
    jog_sign = sign > 0 ? 1 : -1;
    jog_distance = distance;
    manual_pending = true;
    manual_held = held;
    release_requested = false;
    h5_critical_exit(&lock);
    return true;
}
void h5_follow_release(void)
{
    h5_critical_enter(&lock, 3000 + __LINE__);
    manual_held = false;
    // A quick press/release can arrive before the grbl task starts the jog.
    // Discard that pending Hold request as well as stopping an active move.
    manual_pending = false;
    release_requested = true;
    h5_critical_exit(&lock);
}
void h5_follow_reset(void)
{
    if (!h5_follow_busy())
        return;
    waiting_ack = stopping = manual_motion = false;
    stage = 0;
    position_registered = false;
    if (internal_reset && !stop_requested && !sys.alarm) {
        internal_reset = false;
        h5_spindle_follow(config.mode != 2);
        say(manual_pending ? "Preparing manual override" : "Armed; waiting for spindle", true);
        return;
    }
    internal_reset = false;
    pending = false;
    say("Assisted feed stopped", false);
}
static void stop_motion(void)
{
    if (!stopping) {
        h5_spindle_follow_braking();
        h5_bridge_discard_cycle_commands();
        system_set_exec_state_flag(EXEC_MOTION_CANCEL);
        stopping = true;
        say("Decelerating assisted feed", true);
    }
    if (h5_spindle_waiting_index()) {
        internal_reset = !stop_requested;
        system_set_exec_state_flag(EXEC_STOP);
    } else if (state_get() == STATE_IDLE && !st_is_stepping() && !sys.step_control.execute_hold) {
        internal_reset = !stop_requested;
        protocol_enqueue_realtime_command(CMD_RESET);
    }
}
static void send(const char *line)
{
    command_id = h5_bridge_cycle_submit(line);
    waiting_ack = command_id != 0;
    if (!command_id) {
        h5_follow_cancel();
        say("Assisted command queue failed", true);
    }
}
// Rejoin the retained mechanical phase after braking or a manual move. Wait
// for the encoder to cross the phase of the stationary axis, in either direction;
// only whole revolutions may be discarded. Starting a new feed is already aligned.
static bool register_position(int64_t counts, int64_t previous)
{
    if (position_registered)
        return true;
    double physical = anchor_counts + (pos(2) - anchor_z) / config.pitch * H5_ENCODER_CPR;
    double turns = ((double)counts - physical) / H5_ENCODER_CPR;
    double whole = round(turns);
    double error_mm = (turns - whole) * config.pitch;
    double tolerance = .5 / settings.axis[2].steps_per_mm;
    if (slope)
        tolerance = fmin(tolerance, .5 / (settings.axis[0].steps_per_mm * fabs(slope)));
    if (fabs(error_mm) > tolerance) {
        double before = ((double)previous - physical) / H5_ENCODER_CPR;
        if (floor(before) == floor(turns))
            return false;
        whole = counts > previous ? floor(turns) : ceil(turns);
    }
    anchor_counts += whole * H5_ENCODER_CPR;
    anchor_x = pos(0) - (pos(2) - anchor_z) * slope;
    position_registered = true;
    say("Following spindle position", true);
    return true;
}
static void follow_position(int64_t counts, int64_t previous, double maximum)
{
    if (!register_position(counts, previous))
        return;
    // Two queued planner blocks plus its fixed step segment buffer bound the
    // old targets retained during a reversal. Never queue a future spindle angle.
    uint32_t now = hal.get_elapsed_ticks();
    if (now - position_submit_time < 20 ||
        plan_get_buffer_size() - plan_get_block_buffer_available() >= 2)
        return;
    double lo = config.z_min, hi = config.z_max;
    if (slope) {
        double a = anchor_z + (config.x_min - anchor_x) / slope;
        double b = anchor_z + (config.x_max - anchor_x) / slope;
        lo = fmax(lo, fmin(a, b));
        hi = fmin(hi, fmax(a, b));
    }
    if (lo > hi) {
        h5_follow_cancel();
        return;
    }
    double z = anchor_z + ((double)counts - anchor_counts) / H5_ENCODER_CPR * config.pitch;
    double bounded = fmin(hi, fmax(lo, z));
    // Original H5 discounts complete turns beyond a stop, retaining the partial
    // revolution. Reversal then waits only for that partial turn, not all the
    // turns made against the stop. Do this only once the axis reaches that stop.
    if (idle() && fabs(pos(2) - bounded) <= .5 / settings.axis[2].steps_per_mm) {
        double excess = (z - bounded) / config.pitch;
        double turns = trunc(excess);
        anchor_counts += turns * H5_ENCODER_CPR;
    }
    z = round(bounded * settings.axis[2].steps_per_mm) / settings.axis[2].steps_per_mm;
    double x = round((anchor_x + (bounded - anchor_z) * slope) * settings.axis[0].steps_per_mm) /
               settings.axis[0].steps_per_mm;
    if (z == submitted_z && x == submitted_x)
        return;
    // G53 avoids work offsets; G90 endpoints avoid incremental rounding drift.
    // The planner supplies axis acceleration, rate limits and reversal braking.
    char line[118];
    snprintf(line, sizeof(line), "G53G1X%.6fZ%.6fF%.3f", x, z, maximum * .89 * hypot(1, slope));
    submitted_x = x;
    submitted_z = z;
    position_submit_time = now;
    send(line);
}
static void poll(void)
{
    if (!h5_follow_busy() || sys.abort)
        return;
    h5_critical_enter(&lock, 3000 + __LINE__);
    bool begin = pending, stop = stop_requested, jog = manual_pending, held = manual_held,
         release = release_requested, update = parameters_pending;
    uint32_t generation = parameter_generation;
    h5_follow_config_t c = request;
    if (begin)
        pending = false;
    h5_critical_exit(&lock);
    if (sys.alarm) {
        say("Assisted feed fault", false);
        return;
    }
    if (update && !begin && (stage || stopping || !idle())) {
        stop_motion();
        return;
    }
    if (begin || update) {
        if (!idle() || !h5_bridge_empty() || h5_serial_pending() || gc_state.modal.scaling_active) {
            say("Stop other motion and cancel scaling first", false);
            return;
        }
        double values[] = {c.pitch, c.ratio, c.x_min, c.x_max, c.z_min, c.z_max};
        for (unsigned i = 0; i < 6; i++)
            if (!isfinite(values[i]) || fabs(values[i]) > 100000) {
                say("Invalid assisted feed settings", false);
                return;
            }
        if (c.mode > 2 || fabs(c.pitch) < .0001 || c.x_min >= c.x_max || c.z_min >= c.z_max ||
            c.x_max - c.x_min > 100 || c.z_max - c.z_min > 300) {
            say("Invalid assisted feed bounds or pitch", false);
            return;
        }
        config = c;
        h5_critical_enter(&lock, 3000 + __LINE__);
        if (parameter_generation == generation)
            parameters_pending = false;
        h5_critical_exit(&lock);
        h5_spindle_follow(config.mode != 2);
        slope = c.mode == 1 ? -c.ratio / 2 * (c.aux_forward ? 1 : -1) : 0;
        anchor_x = pos(0);
        anchor_z = pos(2);
        anchor_counts = h5_spindle_position();
        previous_counts = (int64_t)anchor_counts;
        char reference[180];
        snprintf(reference, sizeof(reference), "[H5FOLLOWREF:Z:%.6f|COUNTS:%.0f|PITCH:%.6f]\r\n", anchor_z,
                 anchor_counts, config.pitch);
        hal.stream.write(reference);
        stage = 0;
        position_follow = config.mode != 2 && fabs(h5_spindle_rpm()) < 30;
        position_registered = true;
        stopping = internal_reset = waiting_ack = manual_motion = false;
        say("Armed; waiting for spindle", true);
    }
    double rpm = h5_spindle_rpm();
    int64_t counts = h5_spindle_position();
    bool reversing =
        stage && stage < 4 && config.mode != 2 && ((rpm * direction < 30) || (counts - previous_counts) * direction < -2);
    int64_t previous = previous_counts;
    previous_counts = counts;
    double maximum = fmin(settings.axis[2].max_rate,
                          slope ? settings.axis[0].max_rate / fabs(slope) : settings.axis[2].max_rate);
    bool overspeed =
        config.mode == 2 ? fabs(config.pitch) * 60 > maximum * .89 : fabs(rpm * config.pitch) > maximum * .89;
    if (overspeed) {
        h5_follow_cancel();
        stop = true;
        say("Assisted feed exceeds configured axis rate", true);
    }
    if ((stage >= 4 && fabs(rpm) >= 35) || stop || stopping || (jog && !manual_motion && stage) || reversing || (manual_motion && release)) {
        stop_motion();
        return;
    }
    if (waiting_ack) {
        h5_status_t s;
        h5_bridge_snapshot(&s);
        if (s.completed_id != command_id)
            return;
        if (s.command_status) {
            h5_follow_cancel();
            say("Assisted command rejected", true);
            return;
        }
        if (stage != 5 && !idle())
            return;
        waiting_ack = false;
        if (stage == 4) {
            stage = 5;
            say(position_registered ? "Following spindle position" : "Waiting for spindle phase", true);
        } else if (stage == 5) {
            // An acknowledgment permits another bounded endpoint, not the
            // assumption that the previous endpoint has physically completed.
        } else if (manual_motion) {
            manual_motion = false;
            stage = 0;
            position_registered = false;
            say("Manual move complete", true);
            return;
        } else if (++stage == 4) {
            stage = 0;
            say("At bound; waiting for spindle reversal", true);
            return;
        }
    }
    if (stage == 5) {
        follow_position(counts, previous, maximum);
        return;
    }
    if (!idle())
        return;
    char line[118];
    if (jog) {
        h5_critical_enter(&lock, 3000 + __LINE__);
        char axis = jog_axis;
        int sign = jog_sign;
        double distance = jog_distance;
        manual_pending = false;
        release_requested = false;
        h5_critical_exit(&lock);
        unsigned a = axis == 'X' ? 0 : 2;
        double here = pos(a), bound = axis == 'X' ? (sign > 0 ? config.x_max : config.x_min)
                                                  : (sign > 0 ? config.z_max : config.z_min);
        // Z manual displacement uses whole leads, retaining the gear phase.
        if (axis == 'Z' && config.mode != 2)
            distance = ceil(distance / fabs(config.pitch)) * fabs(config.pitch);
        distance = fmin(distance, fmax(0, (bound - here) * sign));
        if (distance <= 0)
            return;
        snprintf(line, sizeof(line), "$J=G21G91%c%.6fF%.3f", axis, sign * distance,
                 settings.axis[a].max_rate);
        manual_motion = true;
        stage = 0;
        send(line);
        say("Manual override", true);
        return;
    }
    if (held)
        return;
    if (stage == 0) {
        if (config.mode != 2) {
            position_follow = position_follow ? fabs(rpm) < 35 : fabs(rpm) < 30;
            if (position_follow) {
                submitted_x = pos(0);
                submitted_z = pos(2);
                position_submit_time = hal.get_elapsed_ticks() - 20;
                stage = 4;
                send("G21G18G8G90G94M5");
                return;
            }
            position_registered = false;
        }
        direction = config.mode == 2 ? 1 : (rpm > 0 ? 1 : -1);
        int travel = (config.pitch > 0 ? 1 : -1) * direction;
        double z = pos(2), x = pos(0), distance = (travel > 0 ? config.z_max : config.z_min) - z;
        if (slope) {
            double available = ((distance * slope > 0 ? config.x_max : config.x_min) - x) / slope;
            if (fabs(available) < fabs(distance))
                distance = available;
        }
        if (distance * travel <= 1 / settings.axis[2].steps_per_mm)
            return;
        target_z = z + distance;
        target_x = x + distance * slope;
        lead = fabs(config.pitch) * hypot(1, slope);
        snprintf(line, sizeof(line), "G21G18G8G90G94M%dS%.3f", direction > 0 ? 3 : 4, fabs(rpm));
        send(line);
        say("Engaging assisted feed", true);
    } else if (stage == 1) {
        // Register physical spindle phase to the retained Z reference. Whole
        // revolutions at a bound do not accumulate a catch-up distance.
        double physical = anchor_counts + (pos(2) - anchor_z) / config.pitch * 1200;
        long phase = lround(direction * physical);
        phase = (phase % 1200 + 1200) % 1200;
        snprintf(line, sizeof(line), "$P4PHASE=%ld", phase);
        send(line);
    } else if (stage == 2) {
        if (config.mode == 2)
            snprintf(line, sizeof(line), "G91G94G1Z%.6fF%.6f", target_z - pos(2), fabs(config.pitch) * 60);
        else
            snprintf(line, sizeof(line), "G91G33X%.6fZ%.6fK%.6f", target_x - pos(0), target_z - pos(2), lead);
        send(line);
        say("Following", true);
    } else {
        snprintf(line, sizeof(line), "G90G94M5");
        send(line);
    }
}
void h5_follow_poll(void)
{
    static bool entered;
    if (entered)
        return;
    entered = true;
    poll();
    entered = false;
}
