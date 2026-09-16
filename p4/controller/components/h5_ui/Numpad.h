#ifndef NUMPAD_H
#define NUMPAD_H

#include "lvgl.h"
#include <functional>
#include <string>

class Numpad {
public:
  enum Action {
    DPAD_MOVEMENT_LEFT = 0,
    DPAD_MOVEMENT_RIGHT,
    DPAD_MOVEMENT_UP,
    DPAD_MOVEMENT_DOWN,
    DPAD_LIMIT_LEFT,
    DPAD_LIMIT_RIGHT,
    DPAD_LIMIT_UP,
    DPAD_LIMIT_DOWN,
    PASSES_SETTING,
    THREADING_STARTS_SETTING,
    CONE_RATIO_SETTING,
    LIMIT_X_MIN, LIMIT_X_MAX, LIMIT_Z_MIN, LIMIT_Z_MAX
  };

  using NumpadCallback = std::function<void(float value, Action action)>;

  Numpad(lv_obj_t *parent, NumpadCallback callback = nullptr);
  ~Numpad();

  void show(Action action, const char *prompt = nullptr);
  void hide();
  bool isVisible() const;

  void setCallback(NumpadCallback callback);

private:
  lv_obj_t *container;
  lv_obj_t *promptLabel;
  lv_obj_t *displayLabel;
  lv_obj_t *buttons[16]; // 4x4 grid layout
  lv_obj_t *enterButton;
  lv_obj_t *cancelButton;

  Action currentAction;
  std::string currentValue;
  NumpadCallback callback;

  void createUI(lv_obj_t *parent);
  void updatePrompt();
  void updateDisplay();
  void addDigit(char digit);
  void addDecimalPoint();
  void backspace();
  void toggleSign();
  void enter();
  void cancel();

  static void button_event_cb(lv_event_t *e);
  static void enter_event_cb(lv_event_t *e);
  static void cancel_event_cb(lv_event_t *e);

  std::string getPromptText(Action action) const;
};

#endif // NUMPAD_H
