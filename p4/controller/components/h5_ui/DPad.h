#ifndef D_PAD_H
#define D_PAD_H

#include "lvgl.h"
#include <array>
#include "ui_support.h"

class DPad {
public:
  enum Direction { BTN_UP = 0, BTN_RIGHT, BTN_DOWN, BTN_LEFT };

  using ButtonDownCallback = void (*)(Direction id, void *userData);
  using ButtonUpCallback = void (*)(Direction id, void *userData);

  DPad(lv_obj_t *parent, ButtonDownCallback downCb = nullptr,
       ButtonUpCallback upCb = nullptr, ButtonUpCallback endstopUpCb = nullptr,
       void *userData = nullptr);

  lv_obj_t *getContainer() { return container; }
  lv_obj_t *getButton(Direction id) { return buttons[id]; }
  lv_obj_t *getEndstopButton(Direction id) { return endstopButtons[id]; }

  void setButtonColor(lv_color_t color);
  void setButtonDownCallback(ButtonDownCallback cb, void *userData = nullptr);
  void setButtonUpCallback(ButtonUpCallback cb, void *userData = nullptr);
  void setEndstopButtonUpCallback(ButtonUpCallback cb,
                                  void *userData = nullptr);

  static void polygonPoints(Direction dir, lv_area_t coords,
                            lv_point_t points[3]);
  static void endstopPolygonPoints(Direction dir, lv_area_t coords,
                                   lv_point_t points[4]);
  void update();

private:
  lv_obj_t *container;
  std::array<lv_obj_t *, 4> buttons;
  std::array<lv_obj_t *, 4> endstopButtons;
  std::array<lv_obj_t *, 4> endstopLabels;
  ButtonDownCallback buttonDownCallback;
  ButtonUpCallback buttonUpCallback;
  ButtonUpCallback endstopButtonUpCallback;
  void *userData;

  // Cached values to avoid unnecessary updates
  std::array<String, 4> lastEndstopTexts;

  static void draw_event_cb(lv_event_t *e);
  static void hit_test_cb(lv_event_t *e);
  static void press_event_cb(lv_event_t *e);
  static void endstop_draw_event_cb(lv_event_t *e);
  static void endstop_hit_test_cb(lv_event_t *e);
  static void endstop_press_event_cb(lv_event_t *e);
};

#endif // D_PAD_H
