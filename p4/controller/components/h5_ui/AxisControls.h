#ifndef AXIS_CONTROLS_H
#define AXIS_CONTROLS_H

#include "lvgl.h"
#include <array>
#include <functional>
#include "ui_support.h"

struct Axis;
class AxisControls {
public:
  using AxisControlsToggleCallback = std::function<void()>;
  using AxisControlsZeroCallback = std::function<void()>;

  AxisControls(lv_obj_t *parent, Axis *axis,
               AxisControlsToggleCallback toggleCb = nullptr,
               AxisControlsZeroCallback zeroCb = nullptr);

  void update(bool force = false);

  lv_obj_t *getContainer() { return container; }

  void setToggleCallback(AxisControlsToggleCallback cb);
  void setZeroCallback(AxisControlsZeroCallback cb);

private:
  lv_obj_t *container;
  lv_obj_t *toggleButton;
  lv_obj_t *zeroButton;
  lv_obj_t *positionLabel;
  Axis *axis;
  AxisControlsToggleCallback toggleCallback;
  AxisControlsZeroCallback zeroCallback;

  // Cached values to avoid unnecessary updates
  String lastPositionText;
  bool lastDisabledState;
};

#endif // AXIS_CONTROLS_H
