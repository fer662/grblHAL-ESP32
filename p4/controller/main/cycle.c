/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "cycle.h"
#include "spindle.h"
#include "serial.h"
#include "grbl/planner.h"
#include "grbl/protocol.h"
#include "grbl/state_machine.h"
#include "grbl/stepper.h"

static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static h5_cycle_config_t request;
static h5_cycle_status_t published;
static bool pending, stopping, owns_stream;
static h5_cycle_plan_t plan;
static unsigned stage, pass, start;
static uint32_t command_id;
static bool waiting_ack, cancel_requested, trace_enabled;
static char stop_reason[96]="Cycle cancelled";
static const char *const names[]={"Setup","Retract","Approach","Take up","Infeed","Register","Spindle","Cut","Retract","Return","Take up","Return to start","Finish infeed","Restore phase","Finish"};

bool h5_cycle_busy(void)
{ portENTER_CRITICAL(&lock); bool busy=published.active; portEXIT_CRITICAL(&lock); return busy; }
bool h5_cycle_owns_stream(void)
{ portENTER_CRITICAL(&lock); bool owns=owns_stream; portEXIT_CRITICAL(&lock); return owns; }
void h5_cycle_snapshot(h5_cycle_status_t *s)
{ portENTER_CRITICAL(&lock); *s=published; portEXIT_CRITICAL(&lock); }
bool h5_cycle_request(const h5_cycle_config_t *c)
{
    portENTER_CRITICAL(&lock);
    bool ok=!published.active;
    if (ok) { request=*c; snprintf(stop_reason,sizeof(stop_reason),"Cycle cancelled"); pending=true; stopping=false; published.active=true; published.pass=published.start=0; snprintf(published.message,sizeof(published.message),"Preparing cycle"); }
    portEXIT_CRITICAL(&lock);
    return ok;
}
void h5_cycle_cancel(void)
{ portENTER_CRITICAL(&lock); if (published.active) stopping=true; portEXIT_CRITICAL(&lock); }
static void message(const char *text, bool active)
{
    portENTER_CRITICAL(&lock);
    published.active=active; if (!active) owns_stream=false; published.pass=pass < plan.config.passes ? pass+1 : plan.config.passes; published.start=start+1;
    snprintf(published.message,sizeof(published.message),"%s",text);
    portEXIT_CRITICAL(&lock);
    char line[180]; snprintf(line,sizeof(line),"[H5CYCLE:PASS:%u|START:%u|STAGE:%s]\r\n",pass+1,start+1,text); hal.stream.write(line);
}
void h5_cycle_reset(void)
{
    portENTER_CRITICAL(&lock);
    if (published.active) snprintf(published.message,sizeof(published.message),"%s",stop_reason);
    published.active=pending=stopping=owns_stream=false;
    portEXIT_CRITICAL(&lock);
    waiting_ack=cancel_requested=false; command_id=0;
}
static bool idle(void)
{ return state_get()==STATE_IDLE && !st_is_stepping() && !plan_get_current_block(); }
static void emit(void)
{
    if (!h5_cycle_busy() || sys.abort) return;
    char line[116];
    switch(stage) {
        case 0: snprintf(line,sizeof(line),"G21G18G8G90G94"); break;
        case 1: case 8: snprintf(line,sizeof(line),"G53G0X%.6f",plan.clearance); break;
        case 2: case 9: snprintf(line,sizeof(line),"G53G0Z%.6f",plan.takeup); break;
        case 3: case 10: snprintf(line,sizeof(line),"G53G0Z%.6f",plan.approach); break;
        case 4: snprintf(line,sizeof(line),"G53G0X%.6f",h5_cycle_depth(&plan,pass)); break;
        case 5: snprintf(line,sizeof(line),"$P4PHASE=%u",h5_cycle_phase(&plan,start)); break;
        case 6: snprintf(line,sizeof(line),"M%dS%.3f",plan.spindle_direction > 0 ? 3 : 4, fabs(h5_spindle_rpm())); break;
        case 7: snprintf(line,sizeof(line),"G91G33Z%.6fK%.6f",plan.finish-plan.approach,plan.lead); break;
        case 11: snprintf(line,sizeof(line),"G53G0Z%.6f",plan.z_start); break;
        case 12: snprintf(line,sizeof(line),"G53G0X%.6f",plan.x_start); break;
        case 13: snprintf(line,sizeof(line),"$P4PHASE=0"); break;
        default: snprintf(line,sizeof(line),"G90G94M5"); break;
    }
    command_id=h5_bridge_cycle_submit(line);
    if (!command_id) { message("Cycle command queue failed",true); h5_cycle_cancel(); return; }
    waiting_ack=true;
    message(names[stage],true);
}
static void poll_cycle(void)
{
    portENTER_CRITICAL(&lock);
    bool active=published.active, stop=stopping, begin=pending;
    h5_cycle_config_t config=request;
    if (begin) pending=false;
    portEXIT_CRITICAL(&lock);
    if (!active) return;
    if (begin) {
        pass=start=stage=0; waiting_ack=cancel_requested=false;
        char error[96];
        h5_cycle_machine_t machine={
            .x=(double)sys.position[0]/settings.axis[0].steps_per_mm,
            .z=(double)sys.position[2]/settings.axis[2].steps_per_mm,
            .rpm=h5_spindle_rpm(), .z_acceleration=settings.axis[2].acceleration/3600.0,
            .z_max_rate=settings.axis[2].max_rate,.z_steps_mm=settings.axis[2].steps_per_mm};
        if (!idle() || !h5_bridge_empty() || h5_serial_pending()) { message("Stop other motion before starting cycle",false); return; }
        if (gc_state.modal.scaling_active
#ifdef ROTATION_ENABLE
            || gc_state.modal.g5x_offset.data.rotation != 0
#endif
        ) { message("Cancel coordinate scaling/rotation before a cycle",false); return; }
        if (!h5_cycle_plan(&config,&machine,&plan,error,sizeof(error))) { message(error,false); return; }
        portENTER_CRITICAL(&lock); owns_stream=true; portEXIT_CRITICAL(&lock);
        char info[240]; snprintf(info,sizeof(info),"[H5PLAN:LEAD:%.6f|LEAD_IN:%.6f|RUN_OUT:%.6f|APPROACH:%.6f|FINISH:%.6f|CLEARANCE:%.6f|RPM_LIMIT:%.3f]\r\n",plan.lead,plan.lead_in,plan.run_out,plan.approach,plan.finish,plan.clearance,config.rpm_limit); hal.stream.write(info);
    }
    if (sys.alarm) { message("Cycle stopped by controller fault",false); return; }
    if (sys.abort) { message(stop ? stop_reason : "Cycle reset",false); return; }
    if (!stop && (state_get()==STATE_HOLD || (stage>=1 &&
        (h5_spindle_rpm()*plan.spindle_direction < 30 || fabs(h5_spindle_rpm()) > plan.config.rpm_limit)))) {
        snprintf(stop_reason,sizeof(stop_reason),"Spindle outside RPM range or feed hold");
        message(stop_reason,true);
        h5_cycle_cancel(); stop=true;
    }
    if (stop) {
        if (!cancel_requested) {
            h5_bridge_discard_cycle_commands();
            // G33 intentionally disables feed hold. Motion cancel still uses
            // the core's normal deceleration, then we discard the stopped pass.
            system_set_exec_state_flag(EXEC_MOTION_CANCEL);
            cancel_requested=true;
            message("Stopping cycle",true);
        }
        // The core's index-wait loop explicitly handles EXEC_STOP by resetting
        // before st_wake_up: no STEP output has started in that case.
        if (h5_spindle_waiting_index()) system_set_exec_state_flag(EXEC_STOP);
        // Timer shutdown precedes the core's cycle-complete handling. Wait for
        // both, otherwise mc_reset correctly reports an in-motion reset alarm.
        else if (state_get()==STATE_IDLE && !st_is_stepping() && !sys.step_control.execute_hold)
            protocol_enqueue_realtime_command(CMD_RESET);
        return;
    }
    if (waiting_ack) {
        h5_status_t status; h5_bridge_snapshot(&status);
        if (status.completed_id != command_id) return;
        if (status.command_status) { snprintf(stop_reason,sizeof(stop_reason),"Cycle command rejected (%d)",status.command_status); message(stop_reason,true); h5_cycle_cancel(); return; }
        if (!idle()) return;
        waiting_ack=false;
        if (trace_enabled) {
            char done[180]; snprintf(done,sizeof(done),"[H5DONE:PASS:%u|START:%u|STAGE:%s|X:%.6f|Z:%.6f]\r\n",pass+1,start+1,names[stage],(double)sys.position[0]/settings.axis[0].steps_per_mm,(double)sys.position[2]/settings.axis[2].steps_per_mm); hal.stream.write(done);
            if (stage==7) {
                char sync[]="P4SYNC", points[]="P4SYNCTRACE";
                h5_spindle_command(STATE_IDLE,sync); h5_spindle_command(STATE_IDLE,points);
            }
        }
        if (stage==10) {
            if (++start==plan.starts) { start=0; pass++; }
            stage=pass < plan.config.passes ? 4 : 11;
        } else if (stage==14) { message("Cycle complete",false); return; }
        else stage++;
    }
    if (idle()) emit();
}
void h5_cycle_poll(void)
{
    // UART writes can call the realtime hook while making room in the FIFO.
    // Keep a diagnostic/report from recursively advancing the same transition.
    static bool entered;
    if (entered) return;
    entered=true;
    poll_cycle();
    entered=false;
}
status_code_t h5_cycle_command(sys_state_t state, char *line)
{
    if (!strncmp(line,"P4CYCLETRACE=",13)) {
        if (state != STATE_IDLE || h5_cycle_busy()) return Status_IdleError;
        if (strcmp(line+13,"0") && strcmp(line+13,"1")) return Status_InvalidStatement;
        trace_enabled=line[13]=='1'; return Status_OK;
    }
    if (!strcmp(line,"P4CYCLE")) {
        h5_cycle_status_t s; h5_cycle_snapshot(&s);
        char text[180]; snprintf(text,sizeof(text),"[H5CYCLE:ACTIVE:%u|PASS:%u|START:%u|STAGE:%s]\r\n",s.active,s.pass,s.start,s.message); hal.stream.write(text); return Status_OK;
    }
    if (strncmp(line,"P4CYCLE=",8)) return Status_Unhandled;
    if (state != STATE_IDLE) return Status_IdleError;
    h5_cycle_config_t c={0}; unsigned thread,forward; char extra;
    if (sscanf(line+8,"%u,%lf,%u,%u,%u,%lf,%lf,%lf,%lf,%lf%c",&thread,&c.pitch,&c.passes,&c.starts,&forward,&c.x_min,&c.x_max,&c.z_min,&c.z_max,&c.rpm_limit,&extra)!=10 || thread>1 || forward>1)
        return Status_InvalidStatement;
    c.threading=thread; c.aux_forward=forward;
    return h5_cycle_request(&c) ? Status_OK : Status_IdleError;
}
