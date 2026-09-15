#include "DPad.h"
#include "App_Style.h"
#include "lvgl.h"
#include "main.h"
#include "ui_support.h"
#include <array>

constexpr int baseLength(int height) { return 2 * height; }

static const int arrowLength = 66;
static const int arrowWidth = baseLength(arrowLength);
static const int triangleHeight = 66;
static const int offsetFromCenter = 3;
static const int endstopThickness = 40;
static const int endstopToArrowMargin = 0;
static const int trapeziumInnerLength =
    baseLength(triangleHeight + endstopToArrowMargin);
static const int trapeziumOuterLength =
    baseLength(triangleHeight + endstopToArrowMargin + endstopThickness);

const int containerSize = arrowLength * 2 + offsetFromCenter * 2 +
                          endstopThickness * 2 + endstopToArrowMargin * 2;
DPad::DPad(lv_obj_t *parent, ButtonDownCallback downCb, ButtonUpCallback upCb,
           ButtonUpCallback endstopUpCb, void *userData)
    : buttonDownCallback(downCb), buttonUpCallback(upCb),
      endstopButtonUpCallback(endstopUpCb), userData(userData) {

  // Initialize cached values
  lastEndstopTexts.fill("");
  container = lv_obj_create(parent);

  lv_obj_set_size(container, containerSize, containerSize);
  lv_obj_center(container);

  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(container, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_clear_flag(container,
                    LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLLABLE |
                        LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ONE);

  // Create 4 directional buttons
  for (int i = 0; i < 4; i++) {
    buttons[i] = lv_btn_create(container);

    lv_obj_add_flag(buttons[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(buttons[i], LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_set_style_radius(buttons[i], 0, 0);
    lv_obj_set_style_bg_opa(buttons[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttons[i], 0, 0);
    lv_obj_set_style_text_color(buttons[i], lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_width(buttons[i], 0, 0);
    lv_obj_set_style_outline_width(buttons[i], 0, 0);
    lv_obj_set_style_outline_opa(buttons[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(buttons[i], LV_OPA_TRANSP, 0);

    // Add event callbacks for press and release
    lv_obj_add_event_cb(buttons[i], press_event_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(buttons[i], press_event_cb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(buttons[i], press_event_cb, LV_EVENT_PRESS_LOST, this);
    lv_obj_add_event_cb(buttons[i], hit_test_cb, LV_EVENT_HIT_TEST,
                        (void *)(intptr_t)i);
    lv_obj_add_event_cb(buttons[i], draw_event_cb, LV_EVENT_DRAW_MAIN,
                        (void *)(intptr_t)i);

    // Position buttons
    switch (i) {
    case BTN_UP:
      lv_obj_set_size(buttons[i], arrowWidth, arrowLength);
      lv_obj_align(buttons[i], LV_ALIGN_TOP_MID, 0,
                   endstopThickness + endstopToArrowMargin);
      break;
    case BTN_RIGHT:
      lv_obj_set_size(buttons[i], arrowLength, arrowWidth);
      lv_obj_align(buttons[i], LV_ALIGN_RIGHT_MID,
                   -endstopThickness - endstopToArrowMargin, 0);
      break;
    case BTN_DOWN:
      lv_obj_set_size(buttons[i], arrowWidth, arrowLength);
      lv_obj_align(buttons[i], LV_ALIGN_BOTTOM_MID, 0,
                   -endstopThickness - endstopToArrowMargin);
      break;
    case BTN_LEFT:
      lv_obj_set_size(buttons[i], arrowLength, arrowWidth);
      lv_obj_align(buttons[i], LV_ALIGN_LEFT_MID,
                   endstopThickness + endstopToArrowMargin, 0);
      break;
    }
  }

  // Create 4 endstop buttons (trapeziums)
  for (int i = 0; i < 4; i++) {
    endstopButtons[i] = lv_btn_create(container);

    lv_obj_add_flag(endstopButtons[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(endstopButtons[i], LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_set_style_radius(endstopButtons[i], 0, 0);
    lv_obj_set_style_bg_opa(endstopButtons[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(endstopButtons[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(endstopButtons[i], 0, 0);
    lv_obj_set_style_outline_width(endstopButtons[i], 0, 0);
    lv_obj_set_style_outline_opa(endstopButtons[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(endstopButtons[i], LV_OPA_TRANSP, 0);

    // Add event callbacks for press and release
    lv_obj_add_event_cb(endstopButtons[i], endstop_press_event_cb,
                        LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(endstopButtons[i], endstop_press_event_cb,
                        LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(endstopButtons[i], endstop_hit_test_cb,
                        LV_EVENT_HIT_TEST, (void *)(intptr_t)i);
    lv_obj_add_event_cb(endstopButtons[i], endstop_draw_event_cb,
                        LV_EVENT_DRAW_MAIN, (void *)(intptr_t)i);

    switch (i) {
    case BTN_UP:
      lv_obj_set_size(endstopButtons[i], containerSize, endstopThickness);
      lv_obj_align(endstopButtons[i], LV_ALIGN_TOP_MID, 0, 0);
      break;
    case BTN_RIGHT:
      lv_obj_set_size(endstopButtons[i], endstopThickness, containerSize);
      lv_obj_align(endstopButtons[i], LV_ALIGN_RIGHT_MID, 0, 0);
      break;
    case BTN_DOWN:
      lv_obj_set_size(endstopButtons[i], containerSize, endstopThickness);
      lv_obj_align(endstopButtons[i], LV_ALIGN_BOTTOM_MID, 0, 0);
      break;
    case BTN_LEFT:
      lv_obj_set_size(endstopButtons[i], endstopThickness, containerSize);
      lv_obj_align(endstopButtons[i], LV_ALIGN_LEFT_MID, 0, 0);
      break;
    }

    // Create labels in the container (not inside endstop buttons)
    endstopLabels[i] = lv_label_create(container);
    lv_label_set_text(endstopLabels[i], "-10.12");
    lv_obj_set_style_text_color(endstopLabels[i], lv_color_hex(0xFFFFFF), 0);

    // Position labels in the joint space between endstop and arrow buttons
    switch (i) {
    case BTN_UP:
      // Position above the arrow button, below the endstop
      lv_obj_align(endstopLabels[i], LV_ALIGN_TOP_MID, 0,
                   5); // 5px margin from endstop
      break;
    case BTN_RIGHT:
      // Position to the right of the arrow button, left of the endstop
      lv_obj_align(endstopLabels[i], LV_ALIGN_RIGHT_MID, -5,
                   0); // 5px margin from endstop
      break;
    case BTN_DOWN:
      // Position below the arrow button, above the endstop
      lv_obj_align(endstopLabels[i], LV_ALIGN_BOTTOM_MID, 0,
                   -5); // 5px margin from endstop
      break;
    case BTN_LEFT:
      // Position to the left of the arrow button, right of the endstop
      lv_obj_align(endstopLabels[i], LV_ALIGN_LEFT_MID, 5,
                   0); // 5px margin from endstop
      break;
    }
  }
}

void DPad::update() {
  // Check and update each endstop label only if text has changed
  auto currentLeftZ = getAxisLeftStop(&z);
  if (currentLeftZ != lastEndstopTexts[BTN_LEFT]) {
    lv_label_set_text(endstopLabels[BTN_LEFT], currentLeftZ.c_str());
    lastEndstopTexts[BTN_LEFT] = currentLeftZ;
  }

  auto currentRightZ = getAxisRightStop(&z);
  if (currentRightZ != lastEndstopTexts[BTN_RIGHT]) {
    lv_label_set_text(endstopLabels[BTN_RIGHT], currentRightZ.c_str());
    lastEndstopTexts[BTN_RIGHT] = currentRightZ;
  }

  auto currentLeftX = getAxisLeftStop(&x);
  if (currentLeftX != lastEndstopTexts[BTN_UP]) {
    lv_label_set_text(endstopLabels[BTN_UP], currentLeftX.c_str());
    lastEndstopTexts[BTN_UP] = currentLeftX;
  }

  auto currentRightX = getAxisRightStop(&x);
  if (currentRightX != lastEndstopTexts[BTN_DOWN]) {
    lv_label_set_text(endstopLabels[BTN_DOWN], currentRightX.c_str());
    lastEndstopTexts[BTN_DOWN] = currentRightX;
  }
}

void DPad::setButtonColor(lv_color_t color) {
  for (auto button : buttons) {
    lv_obj_set_style_bg_color(button, color, 0);
  }
}

void DPad::setButtonDownCallback(ButtonDownCallback cb, void *userData) {
  buttonDownCallback = cb;
  this->userData = userData;
}

void DPad::setButtonUpCallback(ButtonUpCallback cb, void *userData) {
  buttonUpCallback = cb;
  this->userData = userData;
}

void DPad::setEndstopButtonUpCallback(ButtonUpCallback cb, void *userData) {
  endstopButtonUpCallback = cb;
  this->userData = userData;
}

void DPad::press_event_cb(lv_event_t *e) {
  DPad *self = (DPad *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  uint32_t event_code = lv_event_get_code(e);

  for (int i = 0; i < 4; i++) {
    if (btn == self->buttons[i]) {
      if (event_code == LV_EVENT_PRESSED && self->buttonDownCallback) {
        self->buttonDownCallback((Direction)i, self->userData);
      } else if ((event_code == LV_EVENT_RELEASED || event_code == LV_EVENT_PRESS_LOST) && self->buttonUpCallback) {
        self->buttonUpCallback((Direction)i, self->userData);
      }
      break;
    }
  }
}

void DPad::polygonPoints(Direction dir, lv_area_t coords,
                         lv_point_t points[3]) {
  switch (dir) {
  case BTN_UP: {
    lv_coord_t center_x = (coords.x1 + coords.x2) / 2;
    lv_coord_t center_y = coords.y2;

    // CCW: tip → bottom-left → bottom-right
    points[0] = {center_x, center_y}; // tip
    points[1] = {(lv_coord_t)(center_x - arrowWidth / 2),
                 (lv_coord_t)(center_y - triangleHeight)}; // bottom-left
    points[2] = {(lv_coord_t)(center_x + arrowWidth / 2),
                 (lv_coord_t)(center_y - triangleHeight)}; // bottom-right
    break;
  }
  case BTN_DOWN: {
    lv_coord_t center_x = (coords.x1 + coords.x2) / 2;
    lv_coord_t center_y = coords.y1;

    // CCW: tip → top-left → top-right
    points[0] = {center_x, center_y}; // tip
    points[1] = {(lv_coord_t)(center_x - arrowWidth / 2),
                 (lv_coord_t)(center_y + triangleHeight)}; // top-left
    points[2] = {(lv_coord_t)(center_x + arrowWidth / 2),
                 (lv_coord_t)(center_y + triangleHeight)}; // top-right
    break;
  }
  case BTN_LEFT: {
    lv_coord_t center_x = coords.x2;
    lv_coord_t center_y = (coords.y1 + coords.y2) / 2;

    // CCW: tip → bottom-right → top-right
    points[0] = {center_x, center_y}; // tip
    points[1] = {(lv_coord_t)(center_x - triangleHeight),
                 (lv_coord_t)(center_y + arrowWidth / 2)}; // bottom-right
    points[2] = {(lv_coord_t)(center_x - triangleHeight),
                 (lv_coord_t)(center_y - arrowWidth / 2)}; // top-right
    break;
  }
  case BTN_RIGHT: {
    lv_coord_t center_x = coords.x1;
    lv_coord_t center_y = (coords.y1 + coords.y2) / 2;

    // CCW: tip → top-left → bottom-left
    points[0] = {center_x, center_y}; // tip
    points[1] = {(lv_coord_t)(center_x + triangleHeight),
                 (lv_coord_t)(center_y - arrowWidth / 2)}; // top-left
    points[2] = {(lv_coord_t)(center_x + triangleHeight),
                 (lv_coord_t)(center_y + arrowWidth / 2)}; // bottom-left
    break;
  }
  }
}

void DPad::draw_event_cb(lv_event_t *e) {
  Direction dir = (Direction)(intptr_t)lv_event_get_user_data(e);
  lv_obj_t *obj = lv_event_get_target(e);

  lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);

  lv_coord_t width = lv_area_get_width(&coords);
  lv_coord_t height = lv_area_get_height(&coords);

  lv_coord_t square_part_length = arrowLength - triangleHeight;

  // Define 3 points for the triangle
  lv_point_t points[3];

  polygonPoints(dir, coords, points);
  // Draw polygon edges with lv_canvas_draw_line (safe way)
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = lv_obj_has_state(obj, LV_STATE_PRESSED)
                          ? COLOR_PRESSED(APP_COLOR_WARNING)
                          : APP_COLOR_WARNING;
  rect_dsc.radius = 0;
  lv_draw_polygon(draw_ctx, &rect_dsc, points, 3);
}

void DPad::hit_test_cb(lv_event_t *e) {
  lv_hit_test_info_t *info = (lv_hit_test_info_t *)lv_event_get_param(e);
  lv_obj_t *obj = lv_event_get_target(e);

  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);

  Direction dir = (Direction)(intptr_t)lv_event_get_user_data(e);

  lv_point_t points[3];
  polygonPoints(dir, coords, points);

  // --- Point-in-triangle test using barycentric coordinates ---
  lv_coord_t test_x = info->point->x;
  lv_coord_t test_y = info->point->y;

  // Get triangle vertices (assuming points[0] is the tip, points[1] and
  // points[2] are base)
  lv_coord_t x1 = points[0].x, y1 = points[0].y; // tip
  lv_coord_t x2 = points[1].x, y2 = points[1].y; // base vertex 1
  lv_coord_t x3 = points[2].x, y3 = points[2].y; // base vertex 2

  // Calculate barycentric coordinates
  int32_t denominator = ((y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3));

  // Avoid division by zero
  if (denominator == 0) {
    info->res = false;
    return;
  }

  int32_t w1 = ((y2 - y3) * (test_x - x3) + (x3 - x2) * (test_y - y3));
  int32_t w2 = ((y3 - y1) * (test_x - x3) + (x1 - x3) * (test_y - y3));

  // Convert to barycentric coordinates
  int32_t b1 = w1 * 1000 / denominator; // Scale by 1000 to avoid floating point
  int32_t b2 = w2 * 1000 / denominator;
  int32_t b3 = 1000 - b1 - b2;

  // Point is inside if all barycentric coordinates are >= 0
  bool inside = (b1 >= 0) && (b2 >= 0) && (b3 >= 0);
  info->res = inside; // always set
}

void DPad::endstopPolygonPoints(Direction dir, lv_area_t coords,
                                lv_point_t points[4]) {
  lv_coord_t square_part_length = arrowLength - triangleHeight;
  static int16_t gap = (containerSize - trapeziumOuterLength) / 2;
  switch (dir) {
  case BTN_UP: {
    lv_coord_t center_x = (coords.x1 + coords.x2) / 2;

    // CCW bottom-left → bottom-right → top-right → top-left
    points[0] = {(lv_coord_t)(center_x - trapeziumInnerLength / 2),
                 coords.y2}; // bottom-left
    points[1] = {(lv_coord_t)(center_x + trapeziumInnerLength / 2),
                 coords.y2};                                // bottom-right
    points[2] = {(lv_coord_t)(coords.x2 - gap), coords.y1}; // top-right
    points[3] = {(lv_coord_t)(coords.x1 + gap), coords.y1}; // top-left
    break;
  }
  case BTN_DOWN: {
    lv_coord_t center_x = (coords.x1 + coords.x2) / 2;

    // CCW top-left → top-right → bottom-right → bottom-left
    points[0] = {(lv_coord_t)(center_x - trapeziumInnerLength / 2),
                 coords.y1}; // top-left
    points[1] = {(lv_coord_t)(center_x + trapeziumInnerLength / 2),
                 coords.y1};                                // top-right
    points[2] = {(lv_coord_t)(coords.x2 - gap), coords.y2}; // bottom-right
    points[3] = {(lv_coord_t)(coords.x1 + gap), coords.y2}; // bottom-left
    break;
  }
  case BTN_LEFT: {
    lv_coord_t center_y = (coords.y1 + coords.y2) / 2;
    lv_coord_t triangle_base_x = coords.x1 + endstopThickness;

    // CCW top-left → bottom-left → bottom-right → top-right
    points[0] = {coords.x1, (lv_coord_t)(coords.y1 + gap)}; // top-left
    points[1] = {coords.x1, (lv_coord_t)(coords.y2 - gap)}; // bottom-left
    points[2] = {
        triangle_base_x,
        (lv_coord_t)(center_y + trapeziumInnerLength / 2)}; // bottom-right
    points[3] = {triangle_base_x, (lv_coord_t)(center_y - trapeziumInnerLength /
                                                              2)}; // top-right
    break;
  }
  case BTN_RIGHT: {
    lv_coord_t center_y = (coords.y1 + coords.y2) / 2;
    lv_coord_t triangle_base_x = coords.x2 - endstopThickness;

    // CCW top-left → top-right → bottom-right → bottom-left
    points[0] = {triangle_base_x,
                 (lv_coord_t)(center_y - trapeziumInnerLength / 2)}; // top-left
    points[1] = {coords.x2, (lv_coord_t)(coords.y1 + gap)}; // top-right
    points[2] = {coords.x2, (lv_coord_t)(coords.y2 - gap)}; // bottom-right
    points[3] = {
        triangle_base_x,
        (lv_coord_t)(center_y + trapeziumInnerLength / 2)}; // bottom-left
    break;
  }
  }
}

void DPad::endstop_draw_event_cb(lv_event_t *e) {
  Direction dir = (Direction)(intptr_t)lv_event_get_user_data(e);
  lv_obj_t *obj = lv_event_get_target(e);

  lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);

  // Define 4 points for the trapezium shape
  lv_point_t points[4];
  endstopPolygonPoints(dir, coords, points);

  // Draw polygon with red color
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = lv_obj_has_state(obj, LV_STATE_PRESSED)
                          ? lv_color_darken(lv_color_hex(0xFF0000), 20)
                          : lv_color_hex(0xFF0000);
  rect_dsc.radius = 0;
  lv_draw_polygon(draw_ctx, &rect_dsc, points, 4);
}

void DPad::endstop_hit_test_cb(lv_event_t *e) {

  lv_hit_test_info_t *info = (lv_hit_test_info_t *)lv_event_get_param(e);
  lv_obj_t *obj = lv_event_get_target(e);

  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);

  Direction dir = (Direction)(intptr_t)lv_event_get_user_data(e);

  lv_point_t points[4];
  endstopPolygonPoints(dir, coords, points);

  // Ray-casting point-in-polygon test
  bool inside = false;
  lv_coord_t test_x = info->point->x;
  lv_coord_t test_y = info->point->y;

  int nvert = 4;
  for (int i = 0, j = nvert - 1; i < nvert; j = i++) {
    lv_coord_t xi = points[i].x;
    lv_coord_t yi = points[i].y;
    lv_coord_t xj = points[j].x;
    lv_coord_t yj = points[j].y;

    // Skip horizontal edges to avoid division by zero
    if (yj != yi) {
      bool intersect =
          ((yi > test_y) != (yj > test_y)) &&
          (test_x < (lv_coord_t)(xi + (int32_t)(xj - xi) * (test_y - yi) /
                                          (int32_t)(yj - yi)));
      if (intersect)
        inside = !inside;
    }
  }

  info->res = inside;
}

void DPad::endstop_press_event_cb(lv_event_t *e) {
  DPad *self = (DPad *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  uint32_t event_code = lv_event_get_code(e);

  for (int i = 0; i < 4; i++) {
    if (btn == self->endstopButtons[i]) {
      if (event_code == LV_EVENT_PRESSED && self->buttonDownCallback) {
        // self->buttonDownCallback((Direction)i, self->userData);
      } else if (event_code == LV_EVENT_RELEASED && self->endstopButtonUpCallback) {
        self->endstopButtonUpCallback((Direction)i, self->userData);
      }
      break;
    }
  }
}
