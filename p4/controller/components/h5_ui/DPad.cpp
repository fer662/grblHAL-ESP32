#include "DPad.h"
#include "App_Style.h"
#include "lv_conf.h"
#include "main.h"

DPad::DPad(lv_obj_t *parent, ButtonDownCallback downCb, ButtonUpCallback upCb,
           ButtonUpCallback endstopUpCb, void *userData)
    : buttonDownCallback(downCb), buttonUpCallback(upCb),
      endstopButtonUpCallback(endstopUpCb), userData(userData) {
  lastEndstopTexts.fill("");
  container = lv_obj_create(parent);
  lv_obj_set_size(container, 640, 724);
  lv_obj_set_style_bg_color(container, APP_COLOR_BG_SECONDARY, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_radius(container, 16, 0);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

  // Keep H5's physical direction mapping: left is Z+, right is Z-.
  const char *labels[] = {LV_SYMBOL_UP "  X+", "Z-  " LV_SYMBOL_RIGHT,
                         LV_SYMBOL_DOWN "  X-", LV_SYMBOL_LEFT "  Z+"};
  const lv_point_t positions[] = {{244, 80}, {396, 272}, {244, 464}, {92, 272}};
  for (unsigned i = 0; i < 4; ++i) {
    buttons[i] = lv_btn_create(container);
    lv_obj_set_size(buttons[i], 152, 180);
    lv_obj_set_pos(buttons[i], positions[i].x, positions[i].y);
    lv_obj_set_style_radius(buttons[i], 12, 0);
    lv_obj_set_style_bg_color(buttons[i], APP_COLOR_WARNING, 0);
    lv_obj_set_style_bg_color(buttons[i], COLOR_PRESSED(APP_COLOR_WARNING), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(buttons[i], APP_COLOR_BG_TERTIARY, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(buttons[i], lv_color_black(), 0);
    lv_obj_set_style_text_color(buttons[i], APP_COLOR_TEXT_DISABLED, LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(buttons[i], 0, 0);
    lv_obj_set_style_text_font(buttons[i], &lv_font_montserrat_28, 0);
    lv_obj_clear_flag(buttons[i], LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLLABLE);
    auto label = lv_label_create(buttons[i]);
    lv_label_set_text(label, labels[i]);
    lv_obj_center(label);
    lv_obj_add_event_cb(buttons[i], press_event_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(buttons[i], press_event_cb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(buttons[i], press_event_cb, LV_EVENT_PRESS_LOST, this);
  }

  // Outer limits retain their directional placement around the enlarged jogs.
  const lv_point_t limitPositions[] = {{244, 0}, {560, 272}, {244, 660}, {0, 272}};
  for (unsigned i = 0; i < 4; ++i) {
    endstopButtons[i] = lv_btn_create(container);
    bool vertical = i == BTN_UP || i == BTN_DOWN;
    lv_obj_set_size(endstopButtons[i], vertical ? 152 : 80, vertical ? 64 : 180);
    lv_obj_set_pos(endstopButtons[i], limitPositions[i].x, limitPositions[i].y);
    lv_obj_set_style_radius(endstopButtons[i], 8, 0);
    lv_obj_set_style_bg_color(endstopButtons[i], APP_COLOR_BG_TERTIARY, 0);
    lv_obj_set_style_bg_color(endstopButtons[i], APP_COLOR_SECONDARY, LV_STATE_PRESSED);
    lv_obj_set_style_text_font(endstopButtons[i], &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_all(endstopButtons[i], 4, 0);
    endstopLabels[i] = lv_label_create(endstopButtons[i]);
    lv_obj_set_style_text_align(endstopLabels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(endstopLabels[i]);
    lv_obj_add_event_cb(endstopButtons[i], endstop_press_event_cb, LV_EVENT_CLICKED, this);
  }
  update();
}

void DPad::update() {
  const String values[] = {getAxisLeftStop(&x), getAxisRightStop(&z),
                           getAxisRightStop(&x), getAxisLeftStop(&z)};
  const char *names[] = {"X+ limit", "Z- limit", "X- limit", "Z+ limit"};
  const char *bypassed[] = {"X+ OFF", "Z- OFF", "X- OFF", "Z+ OFF"};
  for (unsigned i = 0; i < 4; ++i) {
    if (values[i] != lastEndstopTexts[i] || lastLimitsEnabled != jogLimitsEnabled) {
      String text = String(jogLimitsEnabled ? names[i] : bypassed[i]) + "\n" + values[i];
      lv_label_set_text(endstopLabels[i], text.c_str());
      lastEndstopTexts[i] = values[i];
      bool set = values[i] != "-";
      lv_obj_set_style_border_width(endstopButtons[i], set ? 2 : 0, 0);
      lv_obj_set_style_border_color(endstopButtons[i], jogLimitsEnabled ? APP_COLOR_WARNING : APP_COLOR_TEXT_DISABLED, 0);
    }
    bool disabled = (i == BTN_UP || i == BTN_DOWN) ? x.disabled : z.disabled;
    if (disabled) lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
    else lv_obj_clear_state(buttons[i], LV_STATE_DISABLED);
  }
  lastLimitsEnabled = jogLimitsEnabled;
}

void DPad::setButtonColor(lv_color_t color) {
  for (auto button : buttons) lv_obj_set_style_bg_color(button, color, 0);
}
void DPad::setButtonDownCallback(ButtonDownCallback cb, void *data) {
  buttonDownCallback = cb; userData = data;
}
void DPad::setButtonUpCallback(ButtonUpCallback cb, void *data) {
  buttonUpCallback = cb; userData = data;
}
void DPad::setEndstopButtonUpCallback(ButtonUpCallback cb, void *data) {
  endstopButtonUpCallback = cb; userData = data;
}
void DPad::press_event_cb(lv_event_t *e) {
  auto self = static_cast<DPad *>(lv_event_get_user_data(e));
  auto target = lv_event_get_target(e);
  auto code = lv_event_get_code(e);
  for (unsigned i = 0; i < 4; ++i) {
    if (target != self->buttons[i]) continue;
    if (code == LV_EVENT_PRESSED && self->buttonDownCallback)
      self->buttonDownCallback(static_cast<Direction>(i), self->userData);
    else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && self->buttonUpCallback)
      self->buttonUpCallback(static_cast<Direction>(i), self->userData);
    // Sliding out cancels this jog. A different direction requires lifting the
    // finger first, rather than starting another move in the same gesture.
    if (code == LV_EVENT_PRESS_LOST && lv_indev_get_act()) {
      // LVGL 8 can dispatch PRESSED to the new target in this same input tick.
      // Reset aborts that dispatch; wait_release blocks subsequent held ticks.
      auto input = lv_indev_get_act();
      lv_indev_reset(input, nullptr);
      lv_indev_wait_release(input);
    }
    break;
  }
}
void DPad::endstop_press_event_cb(lv_event_t *e) {
  auto self = static_cast<DPad *>(lv_event_get_user_data(e));
  for (unsigned i = 0; i < 4; ++i)
    if (lv_event_get_target(e) == self->endstopButtons[i] && self->endstopButtonUpCallback)
      self->endstopButtonUpCallback(static_cast<Direction>(i), self->userData);
}
