/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "main.h"
#include "bridge.h"
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
static lv_obj_t *status_label, *update_panel;
static String notice;
static uint32_t last_command, last_completed, last_generation;
static Axis *held_axis;
static int held_sign;
static uint32_t last_jog_time;
static bool continuous_jog;
static Display display;
static StateMachine screens(display);

static void send(const char *line)
{
    uint32_t id = h5_bridge_submit(line);
    if (id) { last_command = id; notice.clear(); }
    else notice = "Command queue is full";
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
void setDupr(long value) { dupr = value; }
void setTurnPasses(int value) { turnPasses = value; }
void setStarts(int value) { starts = value; }
void setAuxForward(bool value) { auxForward = value; }
void setConeRatio(float value) { coneRatio = value; }
bool isPassMode() { return mode == MODE_TURN || mode == MODE_FACE || mode == MODE_THREAD || mode == MODE_CUT || mode == MODE_ELLIPSE; }
int getApproxRpm() { return (int)lroundf(fabsf(status.rpm)); }
void setModeFromTask(int value)
{
    if (value != mode) { h5_bridge_cancel(); held_axis = nullptr; isOn = false; }
    mode = value;
    notice.clear();
}
void buttonOnOffPress(bool on)
{
    if (!on) { h5_bridge_cancel(); held_axis = nullptr; isOn = false; return; }
    if (mode == MODE_ASYNC && dupr) {
        jog(&z, dupr > 0 ? 1 : -1, MAX_TRAVEL_MM_Z, fabsf(dupr / 10000.0f) * 60.0f);
        isOn = true;
    } else notice = "Spindle-synchronized operations are not available in this build yet";
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
    h5_bridge_snapshot(&status);
    if (status.stream_generation != last_generation) {
        held_axis = nullptr;
        isOn = false;
        last_generation = status.stream_generation;
    }
    x.pos = status.position[0];
    z.pos = status.position[2];
    if (status.completed_id != last_completed) {
        last_completed = status.completed_id;
        if (status.command_status) notice = "Command rejected (" + std::to_string(status.command_status) + ")";
    }
    if (isOn && !status.moving && status.completed_id >= last_command) isOn = false;
    if (held_axis && !continuous_jog && !status.moving && status.completed_id >= last_command && millis() - last_jog_time >= 150) {
        jog(held_axis, held_sign, moveStep / 10000.0f, held_axis == &x ? 60 : 960);
        last_jog_time = millis();
    }
    if (status_label) {
        String text = status.ready ? status.state : "Starting controller";
        text += "  |  Motor outputs disabled";
        if (!notice.empty()) text += "\n" + notice;
        lv_label_set_text(status_label, text.c_str());
    }
}
void h5_ui_show_update()
{
    h5_bridge_cancel();
    held_axis = nullptr;
    if (!update_panel) {
        update_panel = lv_obj_create(lv_scr_act());
        lv_obj_set_size(update_panel, 700, 280);
        lv_obj_center(update_panel);
        lv_obj_t *label = lv_label_create(update_panel);
        lv_obj_set_width(label, 650);
        lv_label_set_text(label, "Firmware update\n\nUSB installation is available.\nWireless updates will be enabled after the OTA partition migration.");
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 15);
        lv_obj_t *close = lv_btn_create(update_panel);
        lv_obj_set_size(close, 160, 55);
        lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -5);
        lv_obj_t *text = lv_label_create(close); lv_label_set_text(text, "CLOSE"); lv_obj_center(text);
        lv_obj_add_event_cb(close, [](lv_event_t *) { lv_obj_add_flag(update_panel, LV_OBJ_FLAG_HIDDEN); }, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_clear_flag(update_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(update_panel);
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
    h5_ui_sync();
    bsp_display_unlock();
    bsp_display_backlight_on();
    ESP_LOGI("H5_UI", "READY: H5 touchscreen on grblHAL; 1280x800; eight operation tabs");
    ui_ready.store(true);
    for (;;) {
        bsp_display_lock(portMAX_DELAY);
        char action;
        while (xQueueReceive(test_actions, &action, 0) == pdTRUE) {
            OperationMode *screen = screens.getCurrentMode();
            if (screen && strcmp(screen->getName(), "Normal Operation") == 0)
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
    test_actions = xQueueCreate(8, sizeof(char));
    configASSERT(test_actions);
    configASSERT(xTaskCreatePinnedToCore(ui_task, "H5_UI", 16384, nullptr, 2, nullptr, 0) == pdPASS);
}

extern "C" bool h5_ui_ready(void) { return ui_ready.load(); }
extern "C" uint32_t h5_ui_updates(void) { return ui_updates.load(std::memory_order_relaxed); }
extern "C" bool h5_ui_test_action(char action)
{
    return ui_ready.load() && strchr("123405", action) && xQueueSend(test_actions, &action, 0) == pdTRUE;
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
