#include "PitchPicker.h"
#include "App_Style.h"
#include "Buzzer.h"
#include "LVCallbackWrapper.h"
#include "config.h"

PitchPicker::PitchPicker() {}

PitchPicker::~PitchPicker() {
  if (pitchScreen != nullptr) {
    lv_obj_del(pitchScreen);
    pitchScreen = nullptr;
  }
}

void PitchPicker::createPitchScreen(lv_obj_t *parent) {
  // Create the pitch screen
  pitchScreen = lv_obj_create(parent);
  lv_obj_set_size(pitchScreen, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_bg_color(pitchScreen, lv_color_hex(0xFF0000), 0);
  lv_obj_set_style_border_width(pitchScreen, 0, 0);
  lv_obj_set_style_pad_all(pitchScreen, 0, 0);

  LVCallbackWrapper::add(pitchScreen, LV_EVENT_DELETE, [this](lv_event_t *e) {

    this->pitchScreen = nullptr;
  });

  // Create tab view for pitch options
  tabView = lv_tabview_create(pitchScreen, LV_DIR_TOP, 60);
  lv_obj_set_size(tabView, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(tabView, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(tabView, lv_color_hex(0x2C2C2C), 0);
  lv_obj_clear_flag(lv_tabview_get_content(tabView),
                    LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLLABLE |
                        LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ONE);

  // Create tabs
  lv_obj_t *turningTab = lv_tabview_add_tab(tabView, "Turning\nFeeds");
  lv_obj_t *metricTab = lv_tabview_add_tab(tabView, "Metric\nThreads");
  lv_obj_t *imperialTab = lv_tabview_add_tab(tabView, "Imperial\nThreads");

  // Create tab content

  createTurningTab(turningTab);

  createMetricTab(metricTab);

  createImperialTab(imperialTab);

  // Add back tab and handle tab selection
  lv_obj_t *backTab = lv_tabview_add_tab(tabView, "Back");

  // Handle tab selection - back tab (index 3) should hide the picker
  LVCallbackWrapper::add(
      tabView, LV_EVENT_VALUE_CHANGED, [this](lv_event_t *e) {
        uint32_t activeTab = lv_tabview_get_tab_act((lv_obj_t *)e->target);

        Buzzer::getInstance().beepSuccess();
        if (activeTab == 3) { // Back tab
          this->hide();
        } else if (activeTab < 3) { // Only remember valid tabs (0, 1, 2)
          // Remember the current tab (excluding Back tab)
          this->currentTabIndex = activeTab;
        }
      });

  // Initially hide the pitch screen
  lv_obj_add_flag(pitchScreen, LV_OBJ_FLAG_HIDDEN);
}

lv_color_t PitchPicker::colorForPitchType(PitchType pitchType) {
  switch (pitchType) {
  case PITCH_TYPE_MM_PER_TURN:
    return APP_COLOR_MM;
  case PITCH_TYPE_TPI:
    return APP_COLOR_IMPERIAL;
  case PITCH_TYPE_INCHES_PER_TURN:
    return APP_COLOR_IMPERIAL;
  default:
    return APP_COLOR_BG_PRIMARY;
  }
}

lv_color_t PitchPicker::colorForPitchSign(long dupr) {
  if (dupr > 0) {
    return APP_COLOR_SUCCESS;
  } else {
    return APP_COLOR_ERROR;
  }
}

void PitchPicker::createTurningTab(lv_obj_t *parent) {
  // Create buttons for common turning pitches (mm per turn)
  const float turningPitches[] = {0.05f,  0.075f, 0.1f,   0.125f, 0.15f,
                                  0.175f, 0.2f,   0.225f, 0.25f};
  lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);

  lv_obj_t *buttonMatrix = lv_btnmatrix_create(parent);
  lv_obj_set_size(buttonMatrix, SCREEN_WIDTH, SCREEN_HEIGHT - 60);

  // Create persistent button labels
  turningLabels.clear();
  turningLabels.push_back("0.05 mm/turn");
  turningLabels.push_back("0.075 mm/turn");
  turningLabels.push_back("0.10 mm/turn");
  turningLabels.push_back("\n");
  turningLabels.push_back("0.125 mm/turn");
  turningLabels.push_back("0.15 mm/turn");
  turningLabels.push_back("0.175 mm/turn");
  turningLabels.push_back("\n");
  turningLabels.push_back("0.20 mm/turn");
  turningLabels.push_back("0.225 mm/turn");
  turningLabels.push_back("0.25 mm/turn");
  turningLabels.push_back("\n");
  turningLabels.push_back(""); // End marker

  lv_btnmatrix_set_map(buttonMatrix, turningLabels.data());

  // Configure button matrix styling using helper function
  configureButtonMatrix(buttonMatrix);

  // Set colors
  lv_obj_set_style_bg_color(
      buttonMatrix, colorForPitchType(PITCH_TYPE_MM_PER_TURN), LV_PART_ITEMS);
  // lv_obj_set_style_bg_color(buttonMatrix, lv_color_hex(0x388E3C),
  //                           LV_PART_ITEMS | LV_STATE_PRESSED);

  // Handle button clicks
  LVCallbackWrapper::add(
      buttonMatrix, LV_EVENT_VALUE_CHANGED,
      [this, turningPitches](lv_event_t *e) {
        uint32_t id = lv_btnmatrix_get_selected_btn((lv_obj_t *)e->target);
        if (id < sizeof(turningPitches) / sizeof(turningPitches[0])) {
          float pitchValue = turningPitches[id];
          long duprValue = convertMmPerTurnToDupr(pitchValue);
          if (onPitchSelected) {
            onPitchSelected(duprValue, PITCH_TYPE_MM_PER_TURN);
          }
          Buzzer::getInstance().beepSuccess();
          this->hide();
        }
      });
}

void PitchPicker::createMetricTab(lv_obj_t *parent) {
  // Create buttons for common metric thread pitches (mm per turn)
  const float metricPitches[] = {
      0.25f, 0.3f, 0.4f, 0.5f, 0.7f, 0.75f, 0.8f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f,
  };
  lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);

  lv_obj_t *buttonMatrix = lv_btnmatrix_create(parent);
  lv_obj_set_size(buttonMatrix, SCREEN_WIDTH, SCREEN_HEIGHT - 60);

  // Create persistent button labels
  metricLabels.clear();
  metricLabels.push_back("0.25 mm/turn");
  metricLabels.push_back("0.30 mm/turn");
  metricLabels.push_back("0.40 mm/turn");
  metricLabels.push_back("\n");
  metricLabels.push_back("0.50 mm/turn");
  metricLabels.push_back("0.70 mm/turn");
  metricLabels.push_back("0.75 mm/turn");
  metricLabels.push_back("\n");
  metricLabels.push_back("0.80 mm/turn");
  metricLabels.push_back("1.00 mm/turn");
  metricLabels.push_back("1.25 mm/turn");
  metricLabels.push_back("\n");
  metricLabels.push_back("1.50 mm/turn");
  metricLabels.push_back("2.00 mm/turn");
  metricLabels.push_back("2.50 mm/turn");
  metricLabels.push_back(""); // End marker

  lv_btnmatrix_set_map(buttonMatrix, metricLabels.data());
  lv_obj_set_style_bg_color(
      buttonMatrix, colorForPitchType(PITCH_TYPE_MM_PER_TURN), LV_PART_ITEMS);
  // lv_obj_set_style_bg_color(buttonMatrix, lv_color_hex(0x1976D2),
  //                           LV_PART_ITEMS | LV_STATE_PRESSED);

  // Configure button matrix styling using helper function
  configureButtonMatrix(buttonMatrix);

  // Handle button clicks
  LVCallbackWrapper::add(
      buttonMatrix, LV_EVENT_VALUE_CHANGED,
      [this, metricPitches](lv_event_t *e) {
        uint32_t id = lv_btnmatrix_get_selected_btn((lv_obj_t *)e->target);
        if (id < sizeof(metricPitches) / sizeof(metricPitches[0])) {
          float pitchValue = metricPitches[id];
          long duprValue = convertMmPerTurnToDupr(pitchValue);
          if (onPitchSelected) {
            onPitchSelected(duprValue, PITCH_TYPE_MM_PER_TURN);
          }
          Buzzer::getInstance().beepSuccess();
          this->hide();
        }
      });
}

void PitchPicker::createImperialTab(lv_obj_t *parent) {
  // Create buttons for common imperial thread pitches (TPI)
  const float imperialPitches[] = {
      6.0f,  7.0f,  8.0f,  9.0f,  10.0f, 11.0f,
      12.0f, 13.0f, 14.0f, 16.0f, 18.0f, 20.0f,
  };
  lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);

  lv_obj_t *buttonMatrix = lv_btnmatrix_create(parent);
  lv_obj_set_size(buttonMatrix, SCREEN_WIDTH, SCREEN_HEIGHT - 60);

  // Create persistent button labels
  imperialLabels.clear();
  imperialLabels.push_back("6 TPI");
  imperialLabels.push_back("7 TPI");
  imperialLabels.push_back("8 TPI");
  imperialLabels.push_back("\n");
  imperialLabels.push_back("9 TPI");
  imperialLabels.push_back("10 TPI");
  imperialLabels.push_back("11 TPI");
  imperialLabels.push_back("\n");
  imperialLabels.push_back("12 TPI");
  imperialLabels.push_back("13 TPI");
  imperialLabels.push_back("14 TPI");
  imperialLabels.push_back("\n");
  imperialLabels.push_back("16 TPI");
  imperialLabels.push_back("18 TPI");
  imperialLabels.push_back("20 TPI");
  imperialLabels.push_back(""); // End marker

  lv_btnmatrix_set_map(buttonMatrix, imperialLabels.data());

  lv_obj_set_style_bg_color(buttonMatrix, colorForPitchType(PITCH_TYPE_TPI),
                            LV_PART_ITEMS);
  // lv_obj_set_style_bg_color(buttonMatrix, lv_color_hex(0xF57C00),
  //                           LV_PART_ITEMS | LV_STATE_PRESSED);
  // Configure button matrix styling using helper function
  configureButtonMatrix(buttonMatrix);

  // Handle button clicks
  LVCallbackWrapper::add(
      buttonMatrix, LV_EVENT_VALUE_CHANGED,
      [this, imperialPitches](lv_event_t *e) {
        uint32_t id = lv_btnmatrix_get_selected_btn((lv_obj_t *)e->target);
        if (id < sizeof(imperialPitches) / sizeof(imperialPitches[0])) {
          float pitchValue = imperialPitches[id];
          long duprValue = convertTpiToDupr(pitchValue);
          if (onPitchSelected) {
            onPitchSelected(duprValue, PITCH_TYPE_TPI);
          }
          Buzzer::getInstance().beepSuccess();
          this->hide();
        }
      });
}

void PitchPicker::configureButtonMatrix(lv_obj_t *buttonMatrix) {
  // Configure button matrix layout
  lv_btnmatrix_set_btn_ctrl_all(buttonMatrix, LV_BTNMATRIX_CTRL_CLICK_TRIG |
                                                  LV_BTNMATRIX_CTRL_NO_REPEAT);

  // Try to eliminate spacing by setting button matrix properties
  lv_obj_set_style_pad_all(buttonMatrix, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(buttonMatrix, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_column(buttonMatrix, 3, LV_PART_MAIN);
  lv_obj_set_style_border_width(buttonMatrix, 0, LV_PART_ITEMS);
  lv_obj_set_style_border_width(buttonMatrix, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(buttonMatrix, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(buttonMatrix, 0, LV_PART_ITEMS);
}

void PitchPicker::updateCurrentPitchDisplay() {
  if (currentPitchLabel != nullptr) {
    float currentMmPerTurn = (dupr * 0.0001); // Convert to mm per turn
    String currentPitchText = format_decimal(currentMmPerTurn, 3) + " mm/turn";
    lv_label_set_text(currentPitchLabel, currentPitchText.c_str());
  }
}

void PitchPicker::show() {
  if (!visible) {
    updateCurrentPitchDisplay();
    lv_obj_clear_flag(pitchScreen, LV_OBJ_FLAG_HIDDEN);

    // Instead of loading as a new screen, make it visible on top
    // This should preserve the input handling context
    lv_obj_move_foreground(pitchScreen);

    // Show the last remembered tab (excluding Back tab)

    // Safety check: ensure we never show the Back tab or invalid indices
    if (currentTabIndex < 0 || currentTabIndex >= 3) {
      currentTabIndex = 0;
    }

    lv_tabview_set_act(tabView, currentTabIndex, LV_ANIM_OFF);

    visible = true;
  }
}

void PitchPicker::hide() {
  if (visible) {
    lv_obj_add_flag(pitchScreen, LV_OBJ_FLAG_HIDDEN);
    // No need to load a different screen, just hide this one
    visible = false;
  }
}

long PitchPicker::convertMmPerTurnToDupr(float mmPerTurn) {
  return (long)(mmPerTurn * 10000.0f); // Convert to DUPR units
}

long PitchPicker::convertTpiToDupr(float tpi) {
  float mmPerTurn = 25.4f / tpi; // Convert TPI to mm per turn
  return convertMmPerTurnToDupr(mmPerTurn);
}
