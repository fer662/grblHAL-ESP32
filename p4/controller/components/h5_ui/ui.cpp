/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "main.h"
#include "lv_conf.h"
#include "bridge.h"
#include "follow.h"
#include "preferences.h"
#include "update.h"
#include "diagnostics.h"
#include "Buzzer.h"
#include "display.h"
#include "StateMachine.h"
#include "NormalOperationMode.h"
#include "App_Style.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include <atomic>

int mode = MODE_NORMAL, measure = MEASURE_METRIC, turnPasses = 1, starts = 1;
bool isOn = false, auxForward = false, buzzerEnabled = true;
long dupr = 1000, moveStep = MOVE_STEP_1;
float coneRatio = 1.0f;
PitchType pitchType = PITCH_TYPE_MM_PER_TURN;
Axis x = {'X', 1200, 10000}, z = {'Z', 400, 20000};
static h5_status_t status;
static std::atomic<bool> ui_ready{false};
static std::atomic<uint32_t> ui_updates{0};
static QueueHandle_t test_actions;
static lv_obj_t *status_label, *update_label, *update_close, *update_panel, *cycle_panel, *cycle_run;
static lv_obj_t *diagnostics_panel, *diagnostics_label, *diagnostics_back, *diagnostics_stop;
static void show_diagnostics();
static h5_cycle_config_t preview_config;
static bool cycle_selected;
static String notice;
static uint32_t last_command, last_completed, last_generation;
static Axis *held_axis;
static int held_sign;
static uint32_t last_jog_time;
static bool continuous_jog;
static Display display;
static StateMachine screens(display);

extern "C" void h5_audio_start(void);
extern "C" bool h5_audio_initialized(void);
static void send(const char *line)
{
    uint32_t id = h5_bridge_submit(line);
    if (id) { last_command = id; notice.clear(); }
    else notice = h5_cycle_busy() ? "Stop the cycle before jogging" : "Command queue is full";
}
static float steps_mm(const Axis *a)
{
    float configured = status.steps_per_mm[a == &x ? 0 : 2];
    return configured > 0 ? configured : a->motorSteps * 10000.0f / a->screwPitch;
}
static float distance_to_stop(Axis *a, int sign, float requested)
{
    if (sign > 0 && a->leftStop != LONG_MAX)
        requested = fminf(requested, fmaxf(0, (a->leftStop - a->pos) / steps_mm(a)));
    if (sign < 0 && a->rightStop != LONG_MIN)
        requested = fminf(requested, fmaxf(0, (a->pos - a->rightStop) / steps_mm(a)));
    return requested;
}
static void jog(Axis *a, int sign, float distance, float feed)
{
    if (!status.ready || status.alarm) { notice = "Controller is not ready"; return; }
    if (a->disabled) { notice = String(1, a->name) + " axis is disabled"; return; }
    distance = distance_to_stop(a, sign, distance);
    if (distance <= 0) return;
    char command[100];
    snprintf(command, sizeof(command), "$J=G21G91%c%.6fF%.3f", a->name, sign * distance, feed);
    send(command);
}
void h5_ui_jog(Axis *a, int sign, bool pressed)
{
    if (h5_follow_busy()) {
        if(!pressed) { h5_follow_release();held_axis=nullptr;return; }
        bool continuous=moveStep==MOVE_STEP_1 || moveStep==MOVE_STEP_IMP_1;
        h5_follow_jog(a->name,sign,continuous?(a==&x?MAX_TRAVEL_MM_X:MAX_TRAVEL_MM_Z):moveStep/10000.0,true);
        held_axis=a;held_sign=sign;continuous_jog=continuous;last_jog_time=millis();return;
    }
    if (!pressed) {
        if (held_axis == a) { held_axis = nullptr; h5_bridge_cancel(); }
        return;
    }
    if (held_axis) h5_bridge_cancel();
    held_axis = a;
    held_sign = sign;
    continuous_jog = moveStep == MOVE_STEP_1 || moveStep == MOVE_STEP_IMP_1;
    float distance = continuous_jog ? (a == &x ? MAX_TRAVEL_MM_X : MAX_TRAVEL_MM_Z) : moveStep / 10000.0f;
    jog(a, sign, distance, a == &x ? 60 : 960);
    last_jog_time = millis();
}
void manualMoveAxis(Axis *a, float mm)
{ jog(a, mm < 0 ? -1 : 1, fabsf(mm), a == &x ? 60 : 960); }
void markAxis0(Axis *a)
{
    if (status.moving || status.held) { notice = "Stop motion before zeroing"; return; }
    a->originPos = -a->pos;
}
void setAxisDisabled(Axis *a, bool disabled)
{
    held_axis = nullptr;
    h5_bridge_cancel();
    a->disabled = disabled;
    notice = disabled ? "Axis disabled" : "Axis available — motor outputs remain off";
}
void setLeftStop(Axis *a, long value) { held_axis = nullptr; h5_bridge_cancel(); a->leftStop = value; }
void setRightStop(Axis *a, long value) { held_axis = nullptr; h5_bridge_cancel(); a->rightStop = value; }
static String position_text(Axis *a, long position)
{
    double mm = position / steps_mm(a);
    return format_decimal(measure == MEASURE_METRIC ? mm : mm / 25.4, 3);
}
String getAxisPos(Axis *a) { return position_text(a, a->pos + a->originPos); }
String getAxisLeftStop(Axis *a) { return a->leftStop == LONG_MAX ? "-" : position_text(a, a->leftStop + a->originPos); }
String getAxisRightStop(Axis *a) { return a->rightStop == LONG_MIN ? "-" : position_text(a, a->rightStop + a->originPos); }
String getAxisStopDiff(Axis *a)
{ return a->leftStop == LONG_MAX || a->rightStop == LONG_MIN ? "-" : position_text(a, a->leftStop - a->rightStop); }
static void apply_feed_edit()
{
    if (h5_follow_busy()) {
        if (!h5_follow_update(dupr / 10000.0, coneRatio, auxForward))
            notice = "Feed stopped: select a valid nonzero pitch";
    } else if (h5_cycle_busy()) {
        h5_bridge_cancel();
        notice = "Cycle stopped: review changed parameters before restarting";
    }
}
void setDupr(long value) { if (dupr != value) { dupr = value; apply_feed_edit(); } }
void setTurnPasses(int value) { if (turnPasses != value) { turnPasses = value; if (h5_cycle_busy()) h5_bridge_cancel(); } }
void setStarts(int value) { if (starts != value) { starts = value; if (h5_cycle_busy()) h5_bridge_cancel(); } }
void setAuxForward(bool value) { if (auxForward != value) { auxForward = value; apply_feed_edit(); } }
void setConeRatio(float value) { if (coneRatio != value) { coneRatio = value; apply_feed_edit(); } }
bool isPassMode() { return mode == MODE_TURN || mode == MODE_FACE || mode == MODE_THREAD || mode == MODE_CUT || mode == MODE_ELLIPSE; }
int getApproxRpm() { return (int)lroundf(fabsf(status.rpm)); }
void setModeFromTask(int value)
{
    if (value != mode) { h5_bridge_cancel(); held_axis = nullptr; isOn = false; }
    mode = value;
    notice.clear();
}
static void preview_cycle()
{
    if (status.moving || status.held || status.alarm || !status.ready || h5_cycle_busy()) {
        notice = "Stop motion before preparing a cycle"; return;
    }
    if (x.disabled || z.disabled) { notice = "Both axes must be available"; return; }
    if (x.leftStop==LONG_MAX || x.rightStop==LONG_MIN || (mode!=MODE_CUT && (z.leftStop==LONG_MAX || z.rightStop==LONG_MIN))) {
        notice = "Set both X and Z machining bounds"; return;
    }
    preview_config = {};
    preview_config.threading = mode==MODE_THREAD;
    preview_config.operation = mode==MODE_FACE ? H5_FACE : mode==MODE_CUT ? H5_CUT : mode==MODE_ELLIPSE ? H5_ELLIPSE : mode==MODE_THREAD ? H5_THREAD : H5_TURN;
    preview_config.aux_forward = auxForward;
    preview_config.passes = turnPasses;
    preview_config.starts = mode==MODE_THREAD ? starts : 1;
    preview_config.pitch = dupr/10000.0;
    preview_config.x_min = x.rightStop/steps_mm(&x);
    preview_config.x_max = x.leftStop/steps_mm(&x);
    preview_config.z_min = z.rightStop/steps_mm(&z);
    preview_config.z_max = z.leftStop/steps_mm(&z);
    if (mode==MODE_CUT) preview_config.z_min=preview_config.z_max=z.pos/steps_mm(&z);
    double lead=fabs(preview_config.pitch)*preview_config.starts;
    preview_config.rpm_limit = lead > 0 ? fmin(ceil(fabs(status.rpm)*1.25), .88*status.max_rate[mode==MODE_FACE || mode==MODE_CUT ? 0 : 2]/lead) : 0;
    h5_cycle_machine_t machine = {x.pos/steps_mm(&x),z.pos/steps_mm(&z),status.rpm,
        status.acceleration[2],status.max_rate[2],steps_mm(&z),status.acceleration[0],status.max_rate[0],steps_mm(&x)};
    h5_cycle_plan_t plan;
    char error[96];
    if (!h5_cycle_plan(&preview_config,&machine,&plan,error,sizeof(error))) { notice=error; return; }
    if (cycle_panel) lv_obj_del(cycle_panel);
    cycle_panel=lv_obj_create(lv_scr_act());
    lv_obj_set_style_text_font(cycle_panel,LV_FONT_BIG,0);
    lv_obj_set_size(cycle_panel,1000,650); lv_obj_center(cycle_panel);
    lv_obj_move_foreground(cycle_panel);
    lv_obj_t *label=lv_label_create(cycle_panel);
    lv_obj_set_width(label,940);
    char text[1000];
    snprintf(text,sizeof(text),
        "%s cycle / disconnected bench\n\n%u depth passes x %u starts | lead %.4f mm/rev\n"
        "Machining %c: %.3f to %.3f mm\nApproach: %.3f mm | End: %.3f mm\n"
        "%c infeed: %.3f to %.3f mm | Retracted: %.3f mm\n"
        "Lead-in: %.3f mm | Run-out: %.3f mm\n\n"
        "Keep spindle between 30 and %.0f RPM in the current direction.\n"
        "Coordinates above are machine coordinates; X is radial.\n"
        "Approach and clearance extend beyond the machining bounds.\n"
        "STOP decelerates and cancels the pass; it does not resume mid-thread.",
        h5_cycle_name(preview_config.operation),preview_config.passes,plan.starts,plan.lead,
        plan.cut_axis,plan.cut_start,plan.cut_end,plan.approach,plan.finish,plan.depth_axis,plan.depth_start,plan.depth_end,plan.clearance,
        plan.lead_in,plan.run_out,preview_config.rpm_limit);
    lv_label_set_text(label,text); lv_obj_align(label,LV_ALIGN_TOP_LEFT,5,5);
    lv_obj_t *run=cycle_run=lv_btn_create(cycle_panel); lv_obj_set_size(run,300,65); lv_obj_align(run,LV_ALIGN_BOTTOM_RIGHT,-10,-10);
    lv_obj_t *run_text=lv_label_create(run); lv_label_set_text(run_text,"RUN BENCH CYCLE"); lv_obj_center(run_text);
    lv_obj_add_event_cb(run,[](lv_event_t *) {
        if (h5_cycle_request(&preview_config)) { cycle_selected=true; isOn=true; notice.clear(); }
        else notice="Another cycle is active";
        lv_obj_del(cycle_panel); cycle_panel=nullptr;
    },LV_EVENT_CLICKED,nullptr);
    lv_obj_t *close=lv_btn_create(cycle_panel); lv_obj_set_size(close,220,65); lv_obj_align(close,LV_ALIGN_BOTTOM_LEFT,10,-10);
    lv_obj_t *close_text=lv_label_create(close); lv_label_set_text(close_text,"CANCEL"); lv_obj_center(close_text);
    lv_obj_add_event_cb(close,[](lv_event_t *) { lv_obj_del(cycle_panel); cycle_panel=nullptr; },LV_EVENT_CLICKED,nullptr);
}
void buttonOnOffPress(bool on)
{
    if (!on) { h5_bridge_cancel(); held_axis = nullptr; isOn = false; return; }
    if (isPassMode()) { preview_cycle(); return; }
    if(z.disabled || (mode==MODE_CONE && x.disabled)) {notice="Required axis is disabled";return;}
    h5_follow_config_t config={};
    config.mode=mode==MODE_ASYNC?2:mode==MODE_CONE?1:0;config.pitch=dupr/10000.0;
    config.ratio=coneRatio;config.aux_forward=auxForward;
    config.x_min=x.rightStop==LONG_MIN?x.pos/steps_mm(&x)-MAX_TRAVEL_MM_X/2:x.rightStop/steps_mm(&x);
    config.x_max=x.leftStop==LONG_MAX?config.x_min+MAX_TRAVEL_MM_X:x.leftStop/steps_mm(&x);
    config.z_min=z.rightStop==LONG_MIN?z.pos/steps_mm(&z)-MAX_TRAVEL_MM_Z/2:z.rightStop/steps_mm(&z);
    config.z_max=z.leftStop==LONG_MAX?config.z_min+MAX_TRAVEL_MM_Z:z.leftStop/steps_mm(&z);
    if(h5_follow_request(&config)) {cycle_selected=true;isOn=true;} else notice="Controller is busy";
}
void buttonMoveStepPress()
{
    if (measure == MEASURE_METRIC)
        moveStep = moveStep == MOVE_STEP_1 ? MOVE_STEP_2 : moveStep == MOVE_STEP_2 ? MOVE_STEP_3 : MOVE_STEP_1;
    else moveStep = moveStep == MOVE_STEP_IMP_1 ? MOVE_STEP_IMP_2 : moveStep == MOVE_STEP_IMP_2 ? MOVE_STEP_IMP_3 : MOVE_STEP_IMP_1;
}
void buttonMeasurePress()
{
    measure = measure == MEASURE_METRIC ? MEASURE_INCH : MEASURE_METRIC;
    moveStep = measure == MEASURE_METRIC ? MOVE_STEP_1 : MOVE_STEP_IMP_1;
}
void h5_ui_sync()
{
    h5_preferences_t prefs={};prefs.version=1;prefs.mode=mode;prefs.measure=measure;prefs.pitch_type=pitchType;
    prefs.pitch=dupr;prefs.move_step=moveStep;prefs.passes=turnPasses;prefs.starts=starts;prefs.cone_ratio=coneRatio;
    prefs.aux_forward=auxForward;prefs.sound=buzzerEnabled;h5_preferences_set(&prefs);
    h5_bridge_snapshot(&status);
    if (status.stream_generation != last_generation) {
        if (!h5_follow_manual_held()) held_axis = nullptr;
        Buzzer::getInstance().endContinuousBeep();
        isOn = false;
        if (cycle_panel) { lv_obj_del(cycle_panel); cycle_panel=nullptr; }
        last_generation = status.stream_generation;
    }
    x.pos = status.position[0];
    z.pos = status.position[2];
    if (status.completed_id != last_completed) {
        last_completed = status.completed_id;
        if (status.command_status) notice = "Command rejected (" + std::to_string(status.command_status) + ")";
    }
    h5_cycle_status_t cycle; h5_cycle_snapshot(&cycle);
    if (cycle.active) {
        cycle_selected=true;
        isOn=true;
        notice=cycle.message;
        if(!h5_follow_busy()) notice+=" | Pass "+std::to_string(cycle.pass)+", start "+std::to_string(cycle.start);
    } else if (cycle_selected) { isOn=false; cycle_selected=false; notice=cycle.message; }
    else if (isOn && !status.moving && status.completed_id >= last_command) isOn = false;
    if (held_axis && !continuous_jog && !status.moving && status.completed_id >= last_command && millis() - last_jog_time >= 150) {
        if (h5_follow_busy()) h5_follow_jog(held_axis->name, held_sign, moveStep / 10000.0, true);
        else jog(held_axis, held_sign, moveStep / 10000.0f, held_axis == &x ? 60 : 960);
        last_jog_time = millis();
    }
    if(update_label) {
        h5_update_status_t update;h5_update_snapshot(&update);char text[400];
        snprintf(text,sizeof(text),"Firmware update\n\n%s\nIP: %s | Progress: %u%%\nPairing key: %s\n\nUse ota_upload.py with the application .bin file.\nClosing this window leaves an active upload running.",update.message,update.connected?update.ip:"Wi-Fi not connected",update.percent,update.active?update.key:"Open update mode to pair");
        lv_label_set_text(update_label,text);
    }
    if (diagnostics_label && !lv_obj_has_flag(diagnostics_panel, LV_OBJ_FLAG_HIDDEN)) {
        h5_diagnostics_t d; h5_diagnostics_snapshot(&d);
        h5_update_status_t net; h5_update_snapshot(&net);
        char text[900];
        snprintf(text,sizeof(text),
            "Diagnostics | %s | Motor enables locked\n"
            "Wi-Fi: %s\nhttp://%s:8080/diagnostics\n"
            "Encoder: %lld counts | %.2f RPM | %s\n"
            "Steps X: %lu / %ld   Z: %lu / %ld (issued / counted)\n"
            "Motion fault: %u | Sync: %s | Late: %lu | Overlap: %lu\n"
            "TMC5160: %s | Configured: %u | IOIN: %08lx\n"
            "CHOPCONF: %08lx | DRV_STATUS: %08lx\n"
            "Sample age: %lu ms | TMC read age: %lu ms\n"
            "Read-only access on this Wi-Fi for 30 minutes.\n"
            "BACK keeps logging available; STOP closes access.",
            h5_diagnostics_active()?"sharing":"sharing stopped", net.connected?"connected":"disconnected",
            net.connected?net.ip:"waiting-for-wifi", (long long)d.encoder, (double)d.rpm,
            d.simulated?"SIMULATED":"real encoder", (unsigned long)d.x_pulses,(long)d.x_counted,
            (unsigned long)d.z_pulses,(long)d.z_counted,d.fault,d.sync_fault,
            (unsigned long)d.late,(unsigned long)d.overlap,d.tmc_present?"present":"missing",
            d.tmc_configured,(unsigned long)d.tmc_ioin,(unsigned long)d.tmc_chopconf,(unsigned long)d.tmc_status,
            (unsigned long)((uint32_t)millis()-d.sampled_ms),(unsigned long)((uint32_t)millis()-d.tmc_sampled_ms));
        lv_label_set_text(diagnostics_label,text);
    }
    if (status_label) {
        String text = status.ready ? status.state : "Starting controller";
        text += "  |  Motor outputs disabled  |  Tap for diagnostics";
        if (!notice.empty()) text += "\n" + notice;
        lv_label_set_text(status_label, text.c_str());
    }
}
void h5_ui_show_update()
{
    h5_bridge_cancel();
    held_axis = nullptr;
    h5_update_request();
    if (!update_panel) {
        update_panel = lv_obj_create(lv_scr_act());
        lv_obj_set_size(update_panel, 1000, 430);
        lv_obj_set_style_text_font(update_panel,LV_FONT_BIG,0);
        lv_obj_center(update_panel);
        lv_obj_t *label = update_label = lv_label_create(update_panel);
        lv_obj_set_width(label, 940);
        lv_label_set_text(label, "Firmware update\n\nPreparing wireless update mode...");
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 15);
        lv_obj_t *close = update_close = lv_btn_create(update_panel);
        lv_obj_set_size(close, 160, 55);
        lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -5);
        lv_obj_t *text = lv_label_create(close); lv_label_set_text(text, "CLOSE"); lv_obj_center(text);
        lv_obj_add_event_cb(close, [](lv_event_t *) { h5_update_cancel(); lv_obj_add_flag(update_panel, LV_OBJ_FLAG_HIDDEN); }, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_clear_flag(update_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(update_panel);
}
static void show_diagnostics()
{
    h5_diagnostics_start();
    if (!diagnostics_panel) {
        diagnostics_panel=lv_obj_create(lv_scr_act());
        lv_obj_set_size(diagnostics_panel,1080,610);
        lv_obj_center(diagnostics_panel);
        lv_obj_set_style_text_font(diagnostics_panel,LV_FONT_BIG,0);
        diagnostics_label=lv_label_create(diagnostics_panel);
        lv_obj_set_width(diagnostics_label,1020);
        lv_obj_align(diagnostics_label,LV_ALIGN_TOP_MID,0,5);
        diagnostics_back=lv_btn_create(diagnostics_panel);
        lv_obj_set_size(diagnostics_back,220,55);
        lv_obj_align(diagnostics_back,LV_ALIGN_BOTTOM_LEFT,20,-5);
        lv_obj_t *label=lv_label_create(diagnostics_back);lv_label_set_text(label,"BACK");lv_obj_center(label);
        lv_obj_add_event_cb(diagnostics_back,[](lv_event_t *) {
            lv_obj_add_flag(diagnostics_panel,LV_OBJ_FLAG_HIDDEN);
        },LV_EVENT_CLICKED,nullptr);
        diagnostics_stop=lv_btn_create(diagnostics_panel);
        lv_obj_set_size(diagnostics_stop,220,55);
        lv_obj_align(diagnostics_stop,LV_ALIGN_BOTTOM_RIGHT,-20,-5);
        label=lv_label_create(diagnostics_stop);lv_label_set_text(label,"STOP SHARING");lv_obj_center(label);
        lv_obj_add_event_cb(diagnostics_stop,[](lv_event_t *) {
            h5_diagnostics_stop();lv_obj_add_flag(diagnostics_panel,LV_OBJ_FLAG_HIDDEN);
        },LV_EVENT_CLICKED,nullptr);
    }
    lv_obj_clear_flag(diagnostics_panel,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(diagnostics_panel);
}
static void ui_task(void *)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_90,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {.swap_xy = 1, .mirror_x = 1, .mirror_y = 0},
    };
    cfg.lv_adapter_cfg.task_core_id = 0;
    if (!bsp_display_start_with_config(&cfg)) { ESP_LOGE("H5_UI", "Display initialization failed"); vTaskDelete(nullptr); }
    bsp_display_lock(portMAX_DELAY);
    display.begin();
    screens.switchMode(screens.createNormalOperationMode());
    status_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(status_label, APP_COLOR_WARNING, 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(status_label, 1000);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_add_flag(status_label,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(status_label,[](lv_event_t *) { show_diagnostics(); },LV_EVENT_CLICKED,nullptr);
    h5_ui_sync();
    bsp_display_unlock();
    bsp_display_backlight_on();
    ESP_LOGI("H5_UI", "READY: H5 touchscreen on grblHAL; 1280x800; eight operation tabs");
    h5_audio_start();
    ui_ready.store(true);
    for (;;) {
        bsp_display_lock(portMAX_DELAY);
        char action;
        while (xQueueReceive(test_actions, &action, 0) == pdTRUE) {
            OperationMode *screen = screens.getCurrentMode();
            if (action=='6' && !h5_cycle_busy() && !status.moving) {
                // Explicit disconnected-bench fixture; normal UI uses the
                // operator's existing machining bounds and selected settings.
                mode=MODE_THREAD; dupr=5000; turnPasses=2; starts=2; auxForward=true;
                x.rightStop=0; x.leftStop=lround(steps_mm(&x)*.1);
                z.rightStop=0; z.leftStop=lround(steps_mm(&z)*6);
            }
            if(strchr("FCEGKA",action) && !h5_cycle_busy() && !status.moving) {
                mode=action=='F'?MODE_FACE:action=='C'?MODE_CUT:action=='E'?MODE_ELLIPSE:action=='G'?MODE_NORMAL:action=='K'?MODE_CONE:MODE_ASYNC;
                dupr=isPassMode()?500:1000;turnPasses=2;starts=1;auxForward=isPassMode();coneRatio=.2;
                x.rightStop=isPassMode()?0:-lround(steps_mm(&x));x.leftStop=lround(steps_mm(&x)*(isPassMode()?.1:1));
                z.rightStop=isPassMode()?0:-lround(steps_mm(&z)*2);z.leftStop=lround(steps_mm(&z)*(isPassMode()?1:2));
            }
            if(action=='N') { moveStep=MOVE_STEP_3;continue; }
            if(action=='+') { setDupr(dupr*2); continue; }
            if(action=='R' && !h5_cycle_busy() && !status.moving) {
                mode=MODE_NORMAL;dupr=1000;turnPasses=starts=1;auxForward=false;coneRatio=1;
                measure=MEASURE_METRIC;pitchType=PITCH_TYPE_MM_PER_TURN;moveStep=MOVE_STEP_1;buzzerEnabled=true;
                x.leftStop=z.leftStop=LONG_MAX;x.rightStop=z.rightStop=LONG_MIN;continue;
            }
            if(action=='V') {lv_event_send(status_label,LV_EVENT_CLICKED,nullptr);continue;}
            if(action=='B' && diagnostics_back) {lv_event_send(diagnostics_back,LV_EVENT_CLICKED,nullptr);continue;}
            if(action=='W' && diagnostics_stop) {lv_event_send(diagnostics_stop,LV_EVENT_CLICKED,nullptr);continue;}
            if(action=='U') {h5_ui_show_update();continue;}
            if(action=='Q' && update_close) {lv_event_send(update_close,LV_EVENT_CLICKED,nullptr);continue;}
            if (action=='7' && cycle_panel) lv_event_send(cycle_run,LV_EVENT_CLICKED,nullptr);
            else if (screen && strcmp(screen->getName(), "Normal Operation") == 0)
                static_cast<NormalOperationMode *>(screen)->testJogEvent(action);
        }
        screens.updateDisplay();
        ui_updates.fetch_add(1, std::memory_order_relaxed);
        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
extern "C" void h5_ui_start(void)
{
    h5_preferences_t prefs;
    if(h5_preferences_get(&prefs)) {
        mode=prefs.mode;measure=prefs.measure;pitchType=(PitchType)prefs.pitch_type;dupr=prefs.pitch;moveStep=prefs.move_step;
        turnPasses=prefs.passes;starts=prefs.starts;coneRatio=prefs.cone_ratio;auxForward=prefs.aux_forward;buzzerEnabled=prefs.sound;
    }
    test_actions = xQueueCreate(8, sizeof(char));
    configASSERT(test_actions);
    configASSERT(xTaskCreatePinnedToCore(ui_task, "H5_UI", 16384, nullptr, 2, nullptr, 0) == pdPASS);
}

extern "C" bool h5_ui_ready(void) { return ui_ready.load() && h5_audio_initialized() && h5_network_initialized(); }
extern "C" uint32_t h5_ui_updates(void) { return ui_updates.load(std::memory_order_relaxed); }
extern "C" bool h5_ui_test_action(char action)
{
    return ui_ready.load() && strchr("123405678FCEGKAUDQR+NVBW", action) && xQueueSend(test_actions, &action, 0) == pdTRUE;
}
extern "C" bool h5_ui_screenshot(void (*write)(const char *))
{
    if (!ui_ready.load() || bsp_display_lock(1000) != ESP_OK) return false;
    lv_obj_t *screen = lv_scr_act();
    uint32_t bytes = lv_snapshot_buf_size_needed(screen, LV_IMG_CF_TRUE_COLOR);
    void *buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_img_dsc_t image = {};
    bool ok = buffer && lv_snapshot_take_to_buf(screen, LV_IMG_CF_TRUE_COLOR, &image, buffer, bytes) == LV_RES_OK;
    bsp_display_unlock();
    if (ok) {
        char line[100];
        snprintf(line, sizeof(line), "[P4SCREEN:%u,%u|RGB565_RLE]\r\n", image.header.w, image.header.h);
        write(line);
        const uint16_t *pixels = static_cast<const uint16_t *>(buffer);
        unsigned total = image.header.w * image.header.h;
        unsigned used = 0;
        for (unsigned i = 0; i < total;) {
            uint16_t color = pixels[i];
            unsigned count = 1;
            while (i + count < total && count < 65535 && pixels[i + count] == color) count++;
            used += snprintf(line + used, sizeof(line) - used, "%04x%04x", count, color);
            i += count;
            if (used >= 80 || i == total) { strcpy(line + used, "\r\n"); write(line); used = 0; }
        }
        write("[P4SCREEN:END]\r\n");
    }
    free(buffer);
    return ok;
}
