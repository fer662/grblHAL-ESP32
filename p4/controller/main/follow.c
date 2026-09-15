/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "grbl/hal.h"
#include "follow.h"
#include "freertos/FreeRTOS.h"
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
static double anchor_z, anchor_counts, slope, target_x, target_z, lead;
static int direction;
static int64_t previous_counts;
static bool idle(void) { return state_get() == STATE_IDLE && !st_is_stepping() && !plan_get_current_block(); }
static double pos(unsigned axis) { return (double)sys.position[axis] / settings.axis[axis].steps_per_mm; }
static void say(const char *s, bool active)
{
    portENTER_CRITICAL(&lock);
    published.active = active;
    snprintf(published.message, sizeof(published.message), "%s", s);
    portEXIT_CRITICAL(&lock);
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
    portENTER_CRITICAL(&lock);
    bool b = published.active;
    portEXIT_CRITICAL(&lock);
    return b;
}
bool h5_follow_manual_held(void)
{
    portENTER_CRITICAL(&lock);
    bool held = published.active && manual_held && !stop_requested;
    portEXIT_CRITICAL(&lock);
    return held;
}
bool h5_follow_selected(void)
{
    portENTER_CRITICAL(&lock);
    bool b = selected;
    portEXIT_CRITICAL(&lock);
    return b;
}
void h5_follow_clear(void)
{
    portENTER_CRITICAL(&lock);
    selected = false;
    portEXIT_CRITICAL(&lock);
}
void h5_follow_snapshot(h5_cycle_status_t *s)
{
    portENTER_CRITICAL(&lock);
    *s = published;
    portEXIT_CRITICAL(&lock);
}
bool h5_follow_request(const h5_follow_config_t *c)
{
    if (h5_cycle_busy() || h5_update_active())
        return false;
    if (!h5_operation_claim(H5_OWNER_FOLLOW))
        return false;
    portENTER_CRITICAL(&lock);
    request = *c;
    pending = selected = published.active = true;
    stop_requested = manual_pending = manual_held = release_requested = parameters_pending = false;
    snprintf(published.message, sizeof(published.message), "Preparing assisted feed");
    portEXIT_CRITICAL(&lock);
    return true;
}
void h5_follow_cancel(void)
{
    portENTER_CRITICAL(&lock);
    stop_requested = true;
    manual_held = manual_pending = false;
    portEXIT_CRITICAL(&lock);
}
bool h5_follow_update(double pitch, double ratio, bool aux_forward)
{
    if (!isfinite(pitch) || fabs(pitch) < .0001 || fabs(pitch) > 100000 || !isfinite(ratio) ||
        fabs(ratio) > 100000) {
        h5_follow_cancel();
        return false;
    }
    portENTER_CRITICAL(&lock);
    bool active = published.active && !stop_requested;
    if (active) {
        request.pitch = pitch;
        request.ratio = ratio;
        request.aux_forward = aux_forward;
        parameter_generation++;
        parameters_pending = true;
    }
    portEXIT_CRITICAL(&lock);
    return active;
}
bool h5_follow_jog(char axis, int sign, double distance, bool held)
{
    if (!h5_follow_busy() || (axis != 'X' && axis != 'Z') || !isfinite(distance) || distance <= 0)
        return false;
    portENTER_CRITICAL(&lock);
    jog_axis = axis;
    jog_sign = sign > 0 ? 1 : -1;
    jog_distance = distance;
    manual_pending = true;
    manual_held = held;
    release_requested = false;
    portEXIT_CRITICAL(&lock);
    return true;
}
void h5_follow_release(void)
{
    portENTER_CRITICAL(&lock);
    manual_held = false;
    release_requested = true;
    portEXIT_CRITICAL(&lock);
}
void h5_follow_reset(void)
{
    if (!h5_follow_busy())
        return;
    waiting_ack = stopping = manual_motion = false;
    stage = 0;
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
static void poll(void)
{
    if (!h5_follow_busy() || sys.abort)
        return;
    portENTER_CRITICAL(&lock);
    bool begin = pending, stop = stop_requested, jog = manual_pending, held = manual_held,
         release = release_requested, update = parameters_pending;
    uint32_t generation = parameter_generation;
    h5_follow_config_t c = request;
    if (begin)
        pending = false;
    portEXIT_CRITICAL(&lock);
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
        portENTER_CRITICAL(&lock);
        if (parameter_generation == generation)
            parameters_pending = false;
        portEXIT_CRITICAL(&lock);
        h5_spindle_follow(config.mode != 2);
        slope = c.mode == 1 ? -c.ratio / 2 * (c.aux_forward ? 1 : -1) : 0;
        anchor_z = pos(2);
        anchor_counts = h5_spindle_position();
        previous_counts = (int64_t)anchor_counts;
        char reference[180];
        snprintf(reference, sizeof(reference), "[H5FOLLOWREF:Z:%.6f|COUNTS:%.0f|PITCH:%.6f]\r\n", anchor_z,
                 anchor_counts, config.pitch);
        hal.stream.write(reference);
        stage = 0;
        stopping = internal_reset = waiting_ack = manual_motion = false;
        say("Armed; waiting for spindle", true);
    }
    double rpm = h5_spindle_rpm();
    int64_t counts = h5_spindle_position();
    bool reversing =
        stage && config.mode != 2 && ((rpm * direction < 30) || (counts - previous_counts) * direction < -2);
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
    if (stop || stopping || (jog && !manual_motion && stage) || reversing || (manual_motion && release)) {
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
        if (!idle())
            return;
        waiting_ack = false;
        if (manual_motion) {
            manual_motion = false;
            stage = 0;
            say("Manual move complete", true);
            return;
        }
        if (++stage == 4) {
            stage = 0;
            say("At bound; waiting for spindle reversal", true);
            return;
        }
    }
    if (!idle())
        return;
    char line[118];
    if (jog) {
        portENTER_CRITICAL(&lock);
        char axis = jog_axis;
        int sign = jog_sign;
        double distance = jog_distance;
        manual_pending = false;
        release_requested = false;
        portEXIT_CRITICAL(&lock);
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
        if (config.mode != 2 && fabs(rpm) < 30)
            return;
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
