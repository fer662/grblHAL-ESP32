#pragma once
#include "Numpad.h"
#include "Axis.h"
#include <array>
#include <memory>

class LimitEditor {
public:
  explicit LimitEditor(lv_obj_t *parent);
  ~LimitEditor();
  void show();
private:
  lv_obj_t *panel, *message, *units;
  std::array<lv_obj_t *, 4> values;
  std::array<lv_obj_t *, 2> spans;
  std::array<long, 4> draft;
  std::unique_ptr<Numpad> keypad;
  int editMeasure = 0;
  void refresh();
  void enterValue(unsigned index);
  void useCurrent(unsigned index);
  void apply();
};
