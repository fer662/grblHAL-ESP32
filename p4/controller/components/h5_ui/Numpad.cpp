#include "Numpad.h"
#include "App_Style.h"
#include "Buzzer.h"
#include "lv_conf.h"
#include "lvgl.h"
#include "ui_support.h"

Numpad::Numpad(lv_obj_t *parent, NumpadCallback callback)
    : container(nullptr), promptLabel(nullptr), displayLabel(nullptr),
      currentAction(DPAD_MOVEMENT_LEFT), currentValue(""), callback(callback) {
  createUI(parent);
}

Numpad::~Numpad() {
  if (container) {
    lv_obj_del(container);
  }
}

void Numpad::createUI(lv_obj_t *parent) {
  // Create main container - fullscreen
  container = lv_obj_create(parent);
  lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(container, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(container, lv_color_hex(0x2C2C2C), 0);
  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_radius(container, 0, 0);

  // Prevent scrolling
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLL_ONE);

  // Create prompt label
  promptLabel = lv_label_create(container);
  lv_obj_set_width(promptLabel, SCREEN_WIDTH - 40);
  lv_obj_set_style_text_color(promptLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(promptLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(promptLabel, LV_FONT_BIG, 0);
  lv_obj_align(promptLabel, LV_ALIGN_TOP_MID, 0, 32);

  // Create display label
  displayLabel = lv_label_create(container);
  lv_obj_set_size(displayLabel, 688, 64);
  lv_obj_set_style_pad_all(displayLabel, 16, 0);
  lv_obj_set_style_bg_color(displayLabel, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(displayLabel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(displayLabel, 1, 0);
  lv_obj_set_style_border_color(displayLabel, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_color(displayLabel, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_align(displayLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(displayLabel, LV_FONT_BIG, 0);
  lv_obj_align(displayLabel, LV_ALIGN_TOP_MID, 0, 96);
  lv_label_set_text(displayLabel, "0");

  // Create number buttons layout with wider buttons and special positioning
  const char *buttonLabels[] = {"7", "8", "9", "4", "5", "6",
                                "1", "2", "3", "0", ".", "Del"};
  int buttonWidth = 160;
  int buttonHeight = 92;
  int buttonSpacing = 16;
  int sideButtonWidth = buttonWidth;
  int sideButtonHeight = 2 * buttonHeight + buttonSpacing;
  int numpadWidth =
      4 * buttonWidth + 3 * buttonSpacing; // 3 columns of number buttons
  int totalWidth =
      numpadWidth;
  int startX = (SCREEN_WIDTH - totalWidth) / 2; // Center with equal margins
  int startY = 188;

  // Create number buttons (0-11: 7,8,9,4,5,6,1,2,3,0,.,⌫)
  for (int i = 0; i < 12; i++) {
    buttons[i] = lv_btn_create(container);
    lv_obj_set_size(buttons[i], buttonWidth, buttonHeight);

    // Position number buttons in 4 rows of 3 columns
    int row = i / 3;
    int col = i % 3;
    int x = startX + col * (buttonWidth + buttonSpacing);
    int y = startY + row * (buttonHeight + buttonSpacing);

    lv_obj_set_pos(buttons[i], x, y);

    // Style buttons
    lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0x404040), 0);
    lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0x606060),
                              LV_STATE_PRESSED);
    lv_obj_set_style_text_color(buttons[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(buttons[i], 5, 0);

    // Add label to button
    lv_obj_t *btnLabel = lv_label_create(buttons[i]);
    lv_label_set_text(btnLabel, buttonLabels[i]);
    lv_obj_center(btnLabel);

    // Add event callback
    lv_obj_add_event_cb(buttons[i], button_event_cb, LV_EVENT_SHORT_CLICKED,
                        this);
  }

  // Create Enter button (spans 2 rows, positioned on the right)
  buttons[12] = lv_btn_create(container);
  lv_obj_set_size(buttons[12], sideButtonWidth, sideButtonHeight);
  lv_obj_set_pos(buttons[12], startX + 3 * (buttonWidth + buttonSpacing),
                 startY);
  lv_obj_set_style_bg_color(buttons[12], lv_color_hex(0x006600), 0);
  lv_obj_set_style_bg_color(buttons[12], lv_color_hex(0x008800),
                            LV_STATE_PRESSED);
  lv_obj_set_style_text_color(buttons[12], lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(buttons[12], 5, 0);
  lv_obj_t *enterLabel = lv_label_create(buttons[12]);
  lv_label_set_text(enterLabel, "Enter");
  lv_obj_center(enterLabel);
  lv_obj_add_event_cb(buttons[12], button_event_cb, LV_EVENT_SHORT_CLICKED,
                      this);

  // Create Cancel button (spans 2 rows, positioned below Enter)
  buttons[13] = lv_btn_create(container);
  lv_obj_set_size(buttons[13], sideButtonWidth, sideButtonHeight);
  lv_obj_set_pos(buttons[13], startX + 3 * (buttonWidth + buttonSpacing),
                 startY + sideButtonHeight + buttonSpacing);
  lv_obj_set_style_bg_color(buttons[13], lv_color_hex(0x660000), 0);
  lv_obj_set_style_bg_color(buttons[13], lv_color_hex(0x880000),
                            LV_STATE_PRESSED);
  lv_obj_set_style_text_color(buttons[13], lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(buttons[13], 5, 0);
  lv_obj_t *cancelLabel = lv_label_create(buttons[13]);
  lv_label_set_text(cancelLabel, "Cancel");
  lv_obj_center(cancelLabel);
  lv_obj_add_event_cb(buttons[13], button_event_cb, LV_EVENT_SHORT_CLICKED,
                      this);

  // Hide unused buttons
  for (int i = 14; i < 16; i++) {
    buttons[i] = lv_btn_create(container);
    lv_obj_add_flag(buttons[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Create dummy enter and cancel buttons for compatibility (hidden)
  enterButton = lv_btn_create(container);
  lv_obj_add_flag(enterButton, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(enterButton, enter_event_cb, LV_EVENT_SHORT_CLICKED,
                      this);

  cancelButton = lv_btn_create(container);
  lv_obj_add_flag(cancelButton, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(cancelButton, cancel_event_cb, LV_EVENT_SHORT_CLICKED,
                      this);
}

void Numpad::show(Action action) {
  currentAction = action;
  currentValue = "";
  updatePrompt();
  updateDisplay();
  lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(container);
}

void Numpad::hide() { lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN); }

bool Numpad::isVisible() const {
  return !lv_obj_has_flag(container, LV_OBJ_FLAG_HIDDEN);
}

void Numpad::setCallback(NumpadCallback callback) { this->callback = callback; }

void Numpad::updatePrompt() {
  lv_label_set_text(promptLabel, getPromptText(currentAction).c_str());
}

void Numpad::updateDisplay() {
  if (currentValue.empty()) {
    lv_label_set_text(displayLabel, "0");
  } else {
    lv_label_set_text(displayLabel, currentValue.c_str());
  }
}

void Numpad::addDigit(char digit) {
  if (currentValue.length() < 10) { // Limit input length
    if (currentValue == "0") {
      currentValue = digit;
    } else {
      currentValue += digit;
    }
    updateDisplay();
  }
}

void Numpad::addDecimalPoint() {
  if (currentValue.find('.') == std::string::npos &&
      currentValue.length() < 9) {
    if (currentValue.empty()) {
      currentValue = "0.";
    } else {
      currentValue += ".";
    }
    updateDisplay();
  }
}

void Numpad::backspace() {
  if (!currentValue.empty()) {
    currentValue.pop_back();
    updateDisplay();
  }
}

void Numpad::enter() {
  if (callback) {
    float value = currentValue.empty() ? 0.0f : strtof(currentValue.c_str(), nullptr);
    callback(value, currentAction);
  }
  hide();
}

void Numpad::cancel() { hide(); }

void Numpad::button_event_cb(lv_event_t *e) {
  Numpad *self = (Numpad *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);

  // Find which button was clicked
  for (int i = 0; i < 16; i++) {
    if (btn == self->buttons[i]) {
      // Button layout: {"7", "8", "9", "4", "5", "6", "1", "2", "3", "0", ".",
      // "Del", "Enter", "Cancel", "", ""}
      if (i < 3) { // 7, 8, 9 (first row)
        char digit = '7' + i;
        self->addDigit(digit);
      } else if (i >= 3 && i < 6) { // 4, 5, 6 (second row)
        char digit = '4' + (i - 3);
        self->addDigit(digit);
      } else if (i >= 6 && i < 9) { // 1, 2, 3 (third row)
        char digit = '1' + (i - 6);
        self->addDigit(digit);
      } else if (i == 9) { // 0 (fourth row, first column)
        self->addDigit('0');
      } else if (i == 10) { // Decimal point (fourth row, second column)
        self->addDecimalPoint();
      } else if (i == 11) { // Backspace (fourth row, third column)
        self->backspace();
      } else if (i == 12) { // Enter (right side, spans 2 rows)
        self->enter();
      } else if (i == 13) { // Cancel (right side, spans 2 rows)
        self->cancel();
      }

      // Add buzzer feedback for all button presses
      Buzzer::getInstance().beepSuccess();

      // i == 14 and i == 15 are unused buttons, do nothing
      break;
    }
  }
}

void Numpad::enter_event_cb(lv_event_t *e) {
  Numpad *self = (Numpad *)lv_event_get_user_data(e);
  self->enter();
}

void Numpad::cancel_event_cb(lv_event_t *e) {
  Numpad *self = (Numpad *)lv_event_get_user_data(e);
  self->cancel();
}

std::string Numpad::getPromptText(Action action) const {
  switch (action) {
  case DPAD_MOVEMENT_LEFT:
    return "OFFSET FOR LEFT MOVEMENT?";
  case DPAD_MOVEMENT_RIGHT:
    return "OFFSET FOR RIGHT MOVEMENT?";
  case DPAD_MOVEMENT_UP:
    return "OFFSET FOR UP MOVEMENT?";
  case DPAD_MOVEMENT_DOWN:
    return "OFFSET FOR DOWN MOVEMENT?";
  case DPAD_LIMIT_LEFT:
    return "OFFSET FOR LEFT ENDSTOP?";
  case DPAD_LIMIT_RIGHT:
    return "OFFSET FOR RIGHT ENDSTOP?";
  case DPAD_LIMIT_UP:
    return "OFFSET FOR UP ENDSTOP?";
  case DPAD_LIMIT_DOWN:
    return "OFFSET FOR DOWN ENDSTOP?";
  case PASSES_SETTING:
    return "NUMBER OF PASSES?";
  case THREADING_STARTS_SETTING:
    return "NUMBER OF THREADING STARTS?";
  case CONE_RATIO_SETTING:
    return "CONE RATIO?";
  default:
    return "VALUE?";
  }
}
