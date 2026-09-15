#include "AxisControls.h"
#include "App_Style.h"
#include "Axis.h"
#include "LVCallbackWrapper.h"
#include "lv_conf.h"
#include "main.h"

AxisControls::AxisControls(lv_obj_t *parent, Axis *axis,
                           AxisControlsToggleCallback toggleCb,
                           AxisControlsZeroCallback zeroCb)
    : container(nullptr), axis(axis), toggleCallback(toggleCb),
      zeroCallback(zeroCb), lastPositionText(""), lastDisabledState(false) {

  auto height = 80;

  container = lv_obj_create(parent);
  lv_obj_set_layout(container, LV_LAYOUT_FLEX);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_pad_row(container, 0, 0);
  lv_obj_set_style_pad_column(container, 0, 0);
  lv_obj_set_style_radius(container, 5, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(container, true, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 1, 0);
  lv_obj_set_style_border_color(container, APP_COLOR_BORDER, 0);

  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(container,
                    LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLLABLE |
                        LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_set_size(container, 304, height);

  char name[2] = {axis->name, '\0'};
  char zeroButtonText[3] = {axis->name, '0', '\0'};

  toggleButton = lv_btn_create(container);
  lv_obj_set_size(toggleButton, 80, height);
  lv_obj_set_style_radius(toggleButton, 0, 0);
  lv_obj_align(toggleButton, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *toggleButtonLabel = lv_label_create(toggleButton);
  lv_label_set_text(toggleButtonLabel, name);
  lv_obj_center(toggleButtonLabel);

  zeroButton = lv_btn_create(container);
  lv_obj_set_style_pad_all(zeroButton, 4, 0);
  lv_obj_set_size(zeroButton, 224, height);
  lv_obj_align(zeroButton, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(zeroButton, 0, 0);

  positionLabel = lv_label_create(zeroButton);
  lv_obj_set_style_text_font(positionLabel, &lv_font_montserrat_28, 0);
  lv_label_set_text(positionLabel, zeroButtonText);
  lv_obj_align(positionLabel, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *zeroLabel = lv_label_create(zeroButton);
  lv_label_set_text(zeroLabel, zeroButtonText);
  lv_obj_align(zeroLabel, LV_ALIGN_BOTTOM_RIGHT, -4, 0);
  lv_obj_set_style_text_font(zeroLabel, &lv_font_montserrat_16, 0);

  LVCallbackWrapper::add(toggleButton, LV_EVENT_CLICKED,
                         [this](lv_event_t *e) { toggleCallback(); });

  LVCallbackWrapper::add(zeroButton, LV_EVENT_CLICKED,
                         [this](lv_event_t *e) { zeroCallback(); });

  update(true);
}

void AxisControls::update(bool force) {
  // Check if position text has changed
  auto currentPositionText = getAxisPos(axis);
  if (currentPositionText != lastPositionText || force) {
    lv_label_set_text(positionLabel, currentPositionText.c_str());
    lastPositionText = currentPositionText;
  }

  // Check if disabled state has changed
  bool currentDisabledState = axis->disabled;
  if (currentDisabledState != lastDisabledState || force) {
    lv_obj_set_style_bg_color(
        toggleButton,
        currentDisabledState ? APP_COLOR_ERROR : APP_COLOR_SUCCESS, 0);
    lastDisabledState = currentDisabledState;
  }
}

void AxisControls::setToggleCallback(AxisControlsToggleCallback cb) {
  toggleCallback = cb;
}

void AxisControls::setZeroCallback(AxisControlsZeroCallback cb) {
  zeroCallback = cb;
}
