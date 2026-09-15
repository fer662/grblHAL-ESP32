#include "SettingsScreen.h"
#include "App_Style.h"
#include "Buzzer.h"
#include "LVCallbackWrapper.h"
#include "main.h"

SettingsScreen::SettingsScreen()
    : parent(nullptr), settingsScreen(nullptr), settingsContainer(nullptr),
      titleLabel(nullptr), closeButton(nullptr), closeLabel(nullptr),
      buzzerToggle(nullptr), buzzerLabel(nullptr), visible(false),
      settingsClosedCallback(nullptr) {}

SettingsScreen::~SettingsScreen() {
  if (settingsScreen != nullptr) {
    lv_obj_del(settingsScreen);
    settingsScreen = nullptr;
  }
}

void SettingsScreen::createSettingsScreen(lv_obj_t *parent) {
  this->parent = parent;

  // Create main settings screen (full screen)
  settingsScreen = lv_obj_create(parent);
  lv_obj_set_size(settingsScreen, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(settingsScreen, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(settingsScreen, APP_COLOR_BG_PRIMARY, 0);
  lv_obj_set_style_border_width(settingsScreen, 0, 0);
  lv_obj_set_style_radius(settingsScreen, 0, 0);

  // Disable scrolling on main screen
  lv_obj_set_scroll_dir(settingsScreen, LV_DIR_NONE);
  lv_obj_clear_flag(settingsScreen, LV_OBJ_FLAG_SCROLLABLE);

  createSettingsContainer();

  // Initially hidden
  lv_obj_add_flag(settingsScreen, LV_OBJ_FLAG_HIDDEN);
}

void SettingsScreen::createSettingsContainer() {
  // Create settings container (full screen)
  settingsContainer = lv_obj_create(settingsScreen);
  lv_obj_set_size(settingsContainer, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(settingsContainer, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(settingsContainer, lv_color_hex(0x00000000),
                            0); // Transparent
  lv_obj_set_style_border_width(settingsContainer, 0, 0);
  lv_obj_set_style_radius(settingsContainer, 0, 0);
  lv_obj_set_style_pad_all(settingsContainer, 20, 0);

  // Disable scrolling
  lv_obj_set_scroll_dir(settingsContainer, LV_DIR_NONE);
  lv_obj_clear_flag(settingsContainer, LV_OBJ_FLAG_SCROLLABLE);

  // Create title
  titleLabel = lv_label_create(settingsContainer);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 10);
  lv_label_set_text(titleLabel, "SETTINGS");
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);

  // Create close button
  closeButton = lv_btn_create(settingsContainer);
  lv_obj_set_size(closeButton, 80, 35);
  lv_obj_align(closeButton, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_set_style_radius(closeButton, 5, 0);
  lv_obj_set_style_bg_color(closeButton, APP_COLOR_ERROR, 0);
  lv_obj_set_style_border_width(closeButton, 1, 0);
  lv_obj_set_style_border_color(closeButton, APP_COLOR_BORDER, 0);

  closeLabel = lv_label_create(closeButton);
  lv_obj_center(closeLabel);
  lv_label_set_text(closeLabel, "CLOSE");
  lv_obj_set_style_text_color(closeLabel, lv_color_hex(0xFFFFFF), 0);

  LVCallbackWrapper::add(closeButton, LV_EVENT_CLICKED, [this](lv_event_t *e) {
    this->onCloseButtonClick(e);
  });

  createBuzzerSetting();
}

void SettingsScreen::createBuzzerSetting() {
  // Create buzzer setting container
  lv_obj_t *buzzerContainer = lv_obj_create(settingsContainer);
  lv_obj_set_size(buzzerContainer, SCREEN_WIDTH - 40, 50);
  lv_obj_align(buzzerContainer, LV_ALIGN_TOP_MID, 0, 80);
  lv_obj_set_style_bg_color(buzzerContainer, lv_color_hex(0x2C2C2C), 0);
  lv_obj_set_style_border_width(buzzerContainer, 1, 0);
  lv_obj_set_style_border_color(buzzerContainer, APP_COLOR_BORDER, 0);
  lv_obj_set_style_radius(buzzerContainer, 5, 0);
  lv_obj_set_style_pad_all(buzzerContainer, 10, 0);

  // Buzzer label
  buzzerLabel = lv_label_create(buzzerContainer);
  lv_obj_align(buzzerLabel, LV_ALIGN_LEFT_MID, 10, 0);
  lv_label_set_text(buzzerLabel, "Buzzer");
  lv_obj_set_style_text_color(buzzerLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(buzzerLabel, &lv_font_montserrat_14, 0);

  // Buzzer toggle switch
  buzzerToggle = lv_switch_create(buzzerContainer);
  lv_obj_align(buzzerToggle, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_bg_color(buzzerToggle, APP_COLOR_SUCCESS, LV_PART_MAIN);
  lv_obj_set_style_bg_color(buzzerToggle, lv_color_hex(0xFFFFFF),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(buzzerToggle, lv_color_hex(0xFFFFFF), LV_PART_KNOB);

  LVCallbackWrapper::add(buzzerToggle, LV_EVENT_VALUE_CHANGED,
                         [this](lv_event_t *e) { this->onBuzzerToggle(e); });

  updateBuzzerToggle();
}

void SettingsScreen::showSettingsScreen() {
  if (settingsScreen != nullptr) {
    lv_obj_clear_flag(settingsScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settingsScreen);
    visible = true;
    updateBuzzerToggle();
  }
}

void SettingsScreen::hideSettingsScreen() {
  if (settingsScreen != nullptr) {
    lv_obj_add_flag(settingsScreen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
  }
}

bool SettingsScreen::isVisible() const { return visible; }

void SettingsScreen::setSettingsClosedCallback(
    SettingsClosedCallback callback) {
  settingsClosedCallback = callback;
}

void SettingsScreen::onCloseButtonClick(lv_event_t *e) {
  hideSettingsScreen();
  Buzzer::getInstance().beepSuccess();

  // Call the callback if it's set
  if (settingsClosedCallback) {
    settingsClosedCallback();
  }
}

void SettingsScreen::onBuzzerToggle(lv_event_t *e) {
  buzzerEnabled = lv_obj_has_state(buzzerToggle, LV_STATE_CHECKED);
  Buzzer::getInstance().beepSuccess();
}

void SettingsScreen::updateBuzzerToggle() {
  if (buzzerToggle != nullptr) {
    if (buzzerEnabled) lv_obj_add_state(buzzerToggle, LV_STATE_CHECKED);
    else lv_obj_clear_state(buzzerToggle, LV_STATE_CHECKED);
  }
}
