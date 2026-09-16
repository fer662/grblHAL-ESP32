#include "bridge.h"
#include "NormalOperationMode.h"
#include "App_Style.h"
#include "Axis.h"
#include "Buzzer.h"
#include "LVCallbackWrapper.h"
#include "StateMachine.h"
#include "config.h"
#include "display.h"
#include "driver/gpio.h"
#include "lv_conf.h"
#include "main.h"




typedef enum {
  TAB_GEARBOX = 0,
  TAB_ASYNC = 1,
  TAB_CONE = 2,
  TAB_TURN = 3,
  TAB_FACE = 4,
  TAB_CUT = 5,
  TAB_THREAD = 6,
  TAB_ELLIPSE = 7,
  TAB_SETTINGS = 8,
  TAB_FW_UPDATE = 9,
} TabID;

static const int verticalSpacing = 16;
static const int buttonHeight = 80;

// Normal Operation Mode Implementation
NormalOperationMode::NormalOperationMode(StateMachine &stateMachine,
                                         Display &display)
    : stateMachine(stateMachine), display(display), previousTabId(TAB_GEARBOX) {
}

void NormalOperationMode::initialize() {
  // Restore large display buffers for smooth UI
  // display.setBufferSize(40); // Use large 80-line buffer for normal operation


  // Create main screen and numpad
  createMainScreen();
  // Create numpad
  numpad = std::make_unique<Numpad>(mainScreen,
                                    [this](float value, Numpad::Action action) {
                                      this->handleNumpadCallback(value, action);
                                    });
  limitEditor = std::make_unique<LimitEditor>(mainScreen);

  // Create pitch picker
  pitchPicker = std::make_unique<PitchPicker>();
  pitchPicker->createPitchScreen(mainScreen);
  pitchPicker->onPitchSelected = [this](long dupr, PitchType _pitchType) {
    setDupr(dupr);
    pitchType = _pitchType;
    this->updatePitchButtonText();
  };

  // Create settings screen
  settingsScreen = std::make_unique<SettingsScreen>();
  settingsScreen->createSettingsScreen(mainScreen);
  settingsScreen->setSettingsClosedCallback([this]() {
    // When settings are closed, the tab selector should already show the
    // correct tab since we never actually changed it to settings
  });

  showMainScreen();

  // Set up RPM PWM callback
  display.rpmPwmCallback = [](uint8_t value) {  };

  onTabSelected(TAB_GEARBOX);

  // Initial button positioning
  repositionButtons(MODE_NORMAL);
}

void NormalOperationMode::update() {} // Motion is owned by grblHAL.

void NormalOperationMode::updateDisplay() {

  h5_ui_sync();
  display.update();
  updateRpmDisplay();
  updatePitchButtonText();
  updateStepButton();
  updateJogModeButton();
  updateLimitControls();
  updateConeRatioButton();
  updateAuxToggleButton();
  updateThreadingStartsButton();
  updatePassesButton();
  updateStartStopButton();
  xAxisControls->update();
  zAxisControls->update();

  dpad->update();

  // Update FPS counter
  frameCount++;
  unsigned long currentTime = millis();
  if (currentTime - lastFpsUpdate >= 100) { // Update every 100ms
    if (fpsLabel != nullptr) {
      float fps = (frameCount * 1000.0f) / (currentTime - lastFpsUpdate);
      char buf[16];
      sprintf(buf, "%.1f FPS", fps);
      lv_label_set_text(fpsLabel, buf);
    }
    frameCount = 0;
    lastFpsUpdate = currentTime;
  }
}

void NormalOperationMode::createMainScreen() {
  mainScreen = lv_obj_create(nullptr);
  lv_obj_set_size(mainScreen, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_pad_all(mainScreen, 0, 0);
  lv_obj_set_style_border_width(mainScreen, 0, 0);
  lv_obj_set_style_text_font(mainScreen, LV_FONT_BIG, 0);

  lv_obj_clear_flag(mainScreen,
                    LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLLABLE |
                        LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ONE);

  lv_obj_t *scr = mainScreen;

  // Create compact tab selector
  tabSelector =
      std::make_unique<CompactTabSelector>(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
  tabSelector->setPosition(0, 0);

  // Add tabs to the selector
  tabSelector->addTab("Gearbox", "", false, 0);
  tabSelector->addTab("Async", "", false, 1);
  tabSelector->addTab("Cone", "", false, 2);
  tabSelector->addTab("Turn", "", false, 3);
  tabSelector->addTab("Face", "", false, 4);
  tabSelector->addTab("Cut", "", false, 5);
  tabSelector->addTab("Thread", "", false, 6);
  tabSelector->addTab("Ellipse", "", false, 7);
  tabSelector->addTab("Settings", "", false, 8);
  tabSelector->addTab("FW Update", "", false, 9);

  // Set callback for tab selection
  tabSelector->setTabSelectedCallback([this](int tabId) {
    // For TAB_SETTINGS, we need to capture the previous tab before the selector
    // changes
    if (tabId == TAB_SETTINGS) {
      // The selector has already changed to TAB_SETTINGS, so we need to restore
      // immediately We'll use the previousTabId we stored during initialization
      this->showSettingsScreen();
      // Restore the previous tab
      this->tabSelector->setSelectedTab(this->previousTabId);
    } else {
      this->onTabSelected(tabId);
    }
  });

  // Create RPM button in the navigation bar
  createRpmButton();

  // Create content container (takes up most of the screen)
  tabContentContainer = lv_obj_create(scr);
  lv_obj_set_size(tabContentContainer, SCREEN_WIDTH,
                  SCREEN_HEIGHT - 96); // Below the navigation bar
  lv_obj_align(tabContentContainer, LV_ALIGN_TOP_MID, 0,
               96); // Position below navigation
  lv_obj_set_style_bg_color(tabContentContainer, lv_color_hex(0x2C2C2C), 0);
  lv_obj_set_style_border_width(tabContentContainer, 0, 0);
  lv_obj_set_style_pad_all(tabContentContainer, 10, 0);
  lv_obj_set_style_radius(tabContentContainer, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(tabContentContainer, LV_DIR_NONE);

  // Create Start and Stop buttons that will be visible in all tabs
  createStartStopButtons();

  // Create pitch button
  createPitchButtons();

  // Create DPad for manual control
  createDPad();

  // Create step button
  createStepButton();
  createJogModeButton();
  createLimitControls();

  createConeRatioButton();

  createAuxToggleButton();

  createThreadingStartsButton();

  createPassesButton();

  createShiftButton();

  // Create axis controls
  xAxisControls = std::make_unique<AxisControls>(
      mainScreen, &x,
      []() {
        Buzzer::getInstance().beepSuccess();
        setAxisDisabled(&x, !x.disabled);

      },
      [this]() {
        Buzzer::getInstance().beepSuccess();
        markAxis0(&x);
      });

  zAxisControls = std::make_unique<AxisControls>(
      mainScreen, &z,
      []() {
        Buzzer::getInstance().beepSuccess();
        setAxisDisabled(&z, !z.disabled);

      },
      [this]() {
        Buzzer::getInstance().beepSuccess();
        markAxis0(&z);
      });

  lv_obj_align(xAxisControls->getContainer(), LV_ALIGN_TOP_LEFT, 10,
               5 + buttonHeight + verticalSpacing);
  lv_obj_align(zAxisControls->getContainer(), LV_ALIGN_TOP_LEFT, 10,
               5 + buttonHeight * 2 + verticalSpacing * 2);

  // Create initial tab content
  createTabContent(0);

  // Move tab selector to front so it appears on top
  lv_obj_move_foreground(tabSelector->getContainer());

  // Move RPM button to front
  lv_obj_move_foreground(rpmButton);

  // Move Start/Stop button to front so it's always visible
  lv_obj_move_foreground(startStopButton);

  // Move axis labels to front so they're always visible
  lv_obj_move_foreground(xAxisControls->getContainer());
  lv_obj_move_foreground(zAxisControls->getContainer());

  lv_obj_move_foreground(stepButton);
  lv_obj_move_foreground(coneRatioButton);
  lv_obj_move_foreground(auxToggleButton);
  lv_obj_move_foreground(threadingStartsButton);
  lv_obj_move_foreground(passesButton);
  lv_obj_move_foreground(pitchContainer);
  lv_obj_move_foreground(shiftButton);
  // Move DPad to front so it's always visible
  lv_obj_move_foreground(dpad->getContainer());

  // Create responsiveness test counter label
  fpsLabel = lv_label_create(mainScreen);
  lv_obj_align(fpsLabel, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_text_color(fpsLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(fpsLabel, LV_FONT_BIG, 0);
  lv_label_set_text(fpsLabel, "0.0 FPS");
  lv_obj_add_flag(fpsLabel, LV_OBJ_FLAG_HIDDEN); // Diagnostics already report UI responsiveness.
}

void NormalOperationMode::createStartStopButtons() {
  // Create a single Start/Stop button
  startStopButton = lv_btn_create(mainScreen);
  lv_obj_set_size(startStopButton, 140,
                  buttonHeight); // Match AxisControls width
  lv_obj_align(startStopButton, LV_ALIGN_TOP_LEFT, 10,
               5 + buttonHeight * 5 + verticalSpacing * 5); // At bottom
  lv_obj_set_style_border_width(startStopButton, 1, 0);
  lv_obj_set_style_border_color(startStopButton, APP_COLOR_BORDER, 0);
  lv_obj_set_style_radius(startStopButton, 5, 0);
  lv_obj_set_style_text_color(startStopButton, lv_color_hex(0xFFFFFF), 0);

  lv_obj_t *startStopLabel = lv_label_create(startStopButton);
  lv_obj_center(startStopLabel);

  LVCallbackWrapper::add(startStopButton, LV_EVENT_CLICKED,
                         [this](lv_event_t *e) {
                           buttonOnOffPress(!isOn);
                           this->updateStartStopButton();
                           Buzzer::getInstance().beepSuccess();
                         });

  // Initialize the button appearance
  updateStartStopButton(true);
}

void NormalOperationMode::createPassesButton() {
  // Create passes button
  passesButton = lv_btn_create(mainScreen);

  lv_obj_set_size(passesButton, 90, buttonHeight); // Same width as shift button
  lv_obj_align_to(
      passesButton, threadingStartsButton, LV_ALIGN_OUT_BOTTOM_MID, 0,
      verticalSpacing); // Below threading starts button with 5px margin
  lv_obj_set_style_radius(passesButton, 5, LV_PART_MAIN);
  lv_obj_set_style_text_color(passesButton, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(passesButton, 6, LV_PART_MAIN);
  lv_obj_set_style_border_width(passesButton, 1, 0);
  lv_obj_set_style_border_color(passesButton, APP_COLOR_BORDER, 0);

  lv_obj_t *passesValueLabel = lv_label_create(passesButton);
  lv_obj_align(passesValueLabel, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *passesLabel = lv_label_create(passesButton);
  lv_obj_align(passesLabel, LV_ALIGN_TOP_MID, 0, 0);
  lv_label_set_text(passesLabel, "PASSES");

  // Add click handler to show numpad
  LVCallbackWrapper::add(passesButton, LV_EVENT_CLICKED, [this](lv_event_t *e) {
    if (!h5_cycle_busy()) this->numpad->show(Numpad::PASSES_SETTING);
    Buzzer::getInstance().beepSuccess();
  });

  // Initialize the button appearance
  LVCallbackWrapper::add(passesButton, LV_EVENT_LONG_PRESSED, [](lv_event_t *) { h5_cycle_advance(); });
  updatePassesButton(true);
}

void NormalOperationMode::createThreadingStartsButton() {
  // Create threading starts button
  threadingStartsButton = lv_btn_create(mainScreen);

  lv_obj_set_size(threadingStartsButton, 90,
                  buttonHeight); // Same width as other buttons
  lv_obj_align_to(threadingStartsButton, auxToggleButton,
                  LV_ALIGN_OUT_BOTTOM_MID, 0,
                  verticalSpacing); // Below aux toggle button with 5px margin
  lv_obj_set_style_radius(threadingStartsButton, 5, LV_PART_MAIN);
  lv_obj_set_style_text_color(threadingStartsButton, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(threadingStartsButton, 6, LV_PART_MAIN);
  lv_obj_set_style_border_width(threadingStartsButton, 1, 0);
  lv_obj_set_style_border_color(threadingStartsButton, APP_COLOR_BORDER, 0);

  lv_obj_t *threadingStartsValueLabel = lv_label_create(threadingStartsButton);
  lv_obj_align(threadingStartsValueLabel, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *threadingStartsLabel = lv_label_create(threadingStartsButton);
  lv_obj_align(threadingStartsLabel, LV_ALIGN_TOP_MID, 0, 0);
  lv_label_set_text(threadingStartsLabel, "THREAD STARTS");

  // Add click handler to show numpad
  LVCallbackWrapper::add(threadingStartsButton, LV_EVENT_CLICKED,
                         [this](lv_event_t *e) {
                           this->numpad->show(Numpad::THREADING_STARTS_SETTING);
                           Buzzer::getInstance().beepSuccess();
                         });

  // Initialize the button appearance
  updateThreadingStartsButton(true);
}

void NormalOperationMode::createAuxToggleButton() {
  // Create aux toggle button
  auxToggleButton = lv_btn_create(mainScreen);

  lv_obj_set_size(auxToggleButton, 90,
                  buttonHeight); // Same width as other buttons
  lv_obj_align_to(auxToggleButton, coneRatioButton, LV_ALIGN_OUT_BOTTOM_MID, 0,
                  verticalSpacing);
  lv_obj_set_style_radius(auxToggleButton, 5, LV_PART_MAIN);
  lv_obj_set_style_text_color(auxToggleButton, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(auxToggleButton, 6, LV_PART_MAIN);
  lv_obj_set_style_border_width(auxToggleButton, 1, 0);
  lv_obj_set_style_border_color(auxToggleButton, APP_COLOR_BORDER, 0);

  lv_obj_t *auxToggleLabel = lv_label_create(auxToggleButton);
  lv_obj_align(auxToggleLabel, LV_ALIGN_CENTER, 0, 0);

  // Add click handler to toggle auxForward
  LVCallbackWrapper::add(auxToggleButton, LV_EVENT_CLICKED,
                         [this](lv_event_t *e) {
                           setAuxForward(!auxForward);
                           this->updateAuxToggleButton();
                           Buzzer::getInstance().beepSuccess();
                         });

  // Initialize the button appearance
  updateAuxToggleButton(true);
}

void NormalOperationMode::createConeRatioButton() {
  // Create cone ratio button
  coneRatioButton = lv_btn_create(mainScreen);

  lv_obj_set_size(coneRatioButton, 90,
                  buttonHeight); // Same width as other buttons
  lv_obj_align_to(coneRatioButton, rpmButton, LV_ALIGN_OUT_BOTTOM_MID, 0,
                  verticalSpacing); // Below RPM button with 5px margin
  lv_obj_set_style_radius(coneRatioButton, 5, LV_PART_MAIN);
  lv_obj_set_style_text_color(coneRatioButton, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(coneRatioButton, 6, LV_PART_MAIN);
  lv_obj_set_style_border_width(coneRatioButton, 1, 0);
  lv_obj_set_style_border_color(coneRatioButton, APP_COLOR_BORDER, 0);

  lv_obj_t *coneRatioValueLabel = lv_label_create(coneRatioButton);
  lv_obj_align(coneRatioValueLabel, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *coneRatioLabel = lv_label_create(coneRatioButton);
  lv_obj_align(coneRatioLabel, LV_ALIGN_TOP_MID, 0, 0);
  lv_label_set_text(coneRatioLabel, "CONE RATIO");

  // Add click handler to show numpad
  LVCallbackWrapper::add(coneRatioButton, LV_EVENT_CLICKED,
                         [this](lv_event_t *e) {
                           this->numpad->show(Numpad::CONE_RATIO_SETTING);
                           Buzzer::getInstance().beepSuccess();
                         });

  // Initialize the button appearance
  updateConeRatioButton(true);
}

void NormalOperationMode::createRpmButton() {
  // Create RPM button in the navigation bar
  rpmButton = lv_btn_create(tabSelector->getNavigationBar());

  lv_obj_set_size(rpmButton, 240, 80);
  lv_obj_set_style_radius(rpmButton, 5, LV_PART_MAIN);
  lv_obj_set_style_text_color(rpmButton, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(rpmButton, 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(rpmButton, 1, 0);
  lv_obj_set_style_border_color(rpmButton, APP_COLOR_BORDER, 0);
  lv_obj_set_style_flex_grow(rpmButton, 0, LV_PART_MAIN);

  lv_obj_align(rpmButton, LV_ALIGN_TOP_LEFT, 145, 4);

  lv_obj_t *rpmValueLabel = lv_label_create(rpmButton);
  lv_obj_align(rpmValueLabel, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *rpmLabel = lv_label_create(rpmButton);
  lv_obj_align(rpmLabel, LV_ALIGN_TOP_MID, 0, 0);
  lv_label_set_text(rpmLabel, "RPM");

  // Initialize the button appearance
  updateRpmButton();
}

void NormalOperationMode::createShiftButton() {
  shiftButton = lv_btn_create(mainScreen);
  lv_obj_set_size(shiftButton, 90, buttonHeight);
  lv_obj_align_to(shiftButton, startStopButton, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_style_border_width(startStopButton, 1, 0);
  lv_obj_set_style_border_color(shiftButton, APP_COLOR_BORDER, 0);
  lv_obj_set_style_radius(shiftButton, 5, 0);
  lv_obj_set_style_text_color(shiftButton, lv_color_hex(0xFFFFFF), 0);

  lv_obj_t *shiftLabel = lv_label_create(shiftButton);
  lv_obj_center(shiftLabel);
  updateShiftButton();

  LVCallbackWrapper::add(shiftButton, LV_EVENT_CLICKED, [this](lv_event_t *e) {
    shiftButtonState = !shiftButtonState;

    updateShiftButton();

    Buzzer::getInstance().beepSuccess();
  });
}

void NormalOperationMode::updateShiftButton() {
  lv_obj_t *shiftLabel = lv_obj_get_child(shiftButton, 0);

  if (shiftButtonState) {
    // Running state - show STOP in red
    lv_label_set_text(shiftLabel, "SHIFT ON");
    lv_obj_set_style_bg_color(shiftButton, lv_color_hex(0xF44336),
                              0); // Red
    lv_obj_set_style_bg_color(shiftButton, lv_color_hex(0xD32F2F),
                              LV_STATE_PRESSED);
  } else {
    // Stopped state - show START in green
    lv_label_set_text(shiftLabel, "SHIFT OFF");
    lv_obj_set_style_bg_color(shiftButton, lv_color_hex(0x4CAF50),
                              0); // Green
    lv_obj_set_style_bg_color(shiftButton, lv_color_hex(0x388E3C),
                              LV_STATE_PRESSED);
  }
}

void NormalOperationMode::createPitchButtons() {
  // Create container for pitch buttons
  pitchContainer = lv_obj_create(mainScreen);
  lv_obj_add_flag(pitchContainer, LV_OBJ_FLAG_CLICKABLE);
  const int signButtonWidth = 80;
  const int borderWidth = 2;
  const int pitchButtonWidth = 222;
  lv_obj_set_size(pitchContainer,
                  signButtonWidth + pitchButtonWidth + borderWidth,
                  buttonHeight);
  lv_obj_align(pitchContainer, LV_ALIGN_TOP_LEFT, 10,
               5 + buttonHeight * 3 +
                   verticalSpacing * 3); // Below zAxisControls
  lv_obj_set_style_bg_color(pitchContainer, lv_color_hex(0x777777),
                            0); // Transparent background
  // v_obj_set_style_bg_opa(pitchContainer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pitchContainer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(pitchContainer, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(pitchContainer, 5, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(pitchContainer, true, LV_PART_MAIN);
  lv_obj_set_style_border_width(pitchContainer, 1, 0);
  lv_obj_set_style_border_color(pitchContainer, APP_COLOR_BORDER, 0);
  lv_obj_clear_flag(pitchContainer,
                    LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLLABLE |
                        LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ONE);

  // Create pitch buttons inside the container
  pitchButton = lv_btn_create(pitchContainer);
  pitchSignButton = lv_btn_create(pitchContainer);

  lv_obj_set_size(pitchButton, pitchButtonWidth, buttonHeight);
  lv_obj_align(pitchButton, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(pitchButton, lv_color_hex(0x2196F3), 0); // Blue
  lv_obj_set_style_bg_color(pitchButton, lv_color_hex(0x1976D2),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(pitchButton, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(pitchButton, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(pitchButton, 6, LV_PART_MAIN);

  lv_obj_set_size(pitchSignButton, signButtonWidth, buttonHeight);
  lv_obj_align(pitchSignButton, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(pitchSignButton, lv_color_hex(0x2196F3), 0); // Blue
  lv_obj_set_style_bg_color(pitchSignButton, lv_color_hex(0x1976D2),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(pitchSignButton, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(pitchSignButton, lv_color_hex(0xFFFFFF), 0);

  lv_obj_t *pitchValueLabel = lv_label_create(pitchButton);
  lv_obj_align(pitchValueLabel, LV_ALIGN_BOTTOM_MID, 0, -3);

  lv_obj_t *pitchLabel = lv_label_create(pitchButton);
  lv_obj_align(pitchLabel, LV_ALIGN_TOP_MID, 0, 3);
  lv_label_set_text(pitchLabel, "PITCH");

  lv_obj_t *pitchSignLabel = lv_label_create(pitchSignButton);
  lv_label_set_text(pitchSignLabel, "+");
  lv_obj_align(pitchSignLabel, LV_ALIGN_CENTER, 0, 0);

  LVCallbackWrapper::add(pitchButton, LV_EVENT_CLICKED, [this](lv_event_t *e) {
    this->pitchPicker->show();
    Buzzer::getInstance().beepSuccess();
  });
  LVCallbackWrapper::add(pitchSignButton, LV_EVENT_CLICKED,
                         [this](lv_event_t *e) {
                           setDupr(-dupr);
                           this->updatePitchButtonText();
                           Buzzer::getInstance().beepSuccess();
                         });

  updatePitchButtonText(true);
}

void NormalOperationMode::createStepButton() {
  // Create container for step button
  stepButton = lv_btn_create(dpad->getContainer());

  // Fill the upper-left corner bounded by the outer limits and jog cross.
  // The other corner slots have the same 232 x 260 footprint.
  lv_obj_set_size(stepButton, 232, 260);
  lv_obj_set_pos(stepButton, 0, 0);
  lv_obj_set_style_radius(stepButton, 5, LV_PART_MAIN);
  lv_obj_set_style_text_color(stepButton, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(stepButton, 6, LV_PART_MAIN);
  lv_obj_set_style_border_width(stepButton, 1, 0);
  lv_obj_set_style_border_color(stepButton, APP_COLOR_BORDER, 0);

  lv_obj_t *stepValueLabel = lv_label_create(stepButton);
  lv_obj_align(stepValueLabel, LV_ALIGN_CENTER, 0, 22);

  lv_obj_t *stepLabel = lv_label_create(stepButton);
  lv_obj_align(stepLabel, LV_ALIGN_CENTER, 0, -22);
  lv_label_set_text(stepLabel, "STEP");

  LVCallbackWrapper::add(stepButton, LV_EVENT_SHORT_CLICKED,
                         [this](lv_event_t *e) {
                           buttonMoveStepPress();
                           this->updateStepButton();
                           Buzzer::getInstance().beepSuccess();
                         });

  LVCallbackWrapper::add(stepButton, LV_EVENT_LONG_PRESSED,
                         [this](lv_event_t *e) {
                           buttonMeasurePress();
                           this->updateStepButton();
                           Buzzer::getInstance().beepSuccess();
                         });
  updateStepButton(true);
}

void NormalOperationMode::createJogModeButton() {
  jogModeButton = lv_btn_create(dpad->getContainer());
  lv_obj_set_size(jogModeButton, 128, 128);
  lv_obj_set_pos(jogModeButton, 256, 298);
  lv_obj_set_style_radius(jogModeButton, 12, 0);
  lv_obj_set_style_bg_color(jogModeButton, APP_COLOR_BG_TERTIARY, 0);
  lv_obj_set_style_border_width(jogModeButton, 2, 0);
  lv_obj_set_style_border_color(jogModeButton, APP_COLOR_INFO, 0);
  lv_obj_set_style_pad_all(jogModeButton, 4, 0);
  jogModeLabel = lv_label_create(jogModeButton);
  lv_obj_set_style_text_font(jogModeLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_align(jogModeLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(jogModeLabel);
  LVCallbackWrapper::add(jogModeButton, LV_EVENT_CLICKED, [this](lv_event_t *) {
    jogContinuous = !jogContinuous;
    updateJogModeButton();
    Buzzer::getInstance().beepSuccess();
  });
  updateJogModeButton(true);
}

void NormalOperationMode::updateJogModeButton(bool force) {
  if (force || lastJogContinuous != jogContinuous) {
    lv_label_set_text(jogModeLabel, jogContinuous ? "JOG MODE\n\nHOLD" : "JOG MODE\n\nSINGLE\nSTEP");
    lastJogContinuous = jogContinuous;
  }
}

void NormalOperationMode::createDPad() {
  // Create DPad for manual control with button down and up callbacks
  dpad = std::make_unique<DPad>(
      mainScreen, &NormalOperationMode::dpadButtonDownCallback,
      &NormalOperationMode::dpadButtonUpCallback,
      &NormalOperationMode::dpadEndstopButtonUpCallback, this);

  // Use the full right pane, aligned with the left pane's top controls.
  lv_obj_align(dpad->getContainer(), LV_ALIGN_TOP_RIGHT, -24, 16);

  // Move DPad to front so it's always visible
  lv_obj_move_foreground(dpad->getContainer());
}

void NormalOperationMode::createLimitControls() {
  jogLimitsButton = lv_btn_create(dpad->getContainer());
  lv_obj_set_size(jogLimitsButton, 232, 260);
  lv_obj_set_pos(jogLimitsButton, 408, 0);
  lv_obj_set_style_radius(jogLimitsButton, 5, 0);
  jogLimitsLabel = lv_label_create(jogLimitsButton);
  lv_obj_set_style_text_align(jogLimitsLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(jogLimitsLabel);
  LVCallbackWrapper::add(jogLimitsButton, LV_EVENT_CLICKED, [this](lv_event_t *) {
    if (h5_ui_set_jog_limits(!jogLimitsEnabled)) {
      updateLimitControls(); dpad->update(); Buzzer::getInstance().beepSuccess();
    }
  });
  auto edit = lv_btn_create(dpad->getContainer());
  lv_obj_set_size(edit, 232, 260);
  lv_obj_set_pos(edit, 0, 464);
  lv_obj_set_style_radius(edit, 5, 0);
  lv_obj_set_style_bg_color(edit, APP_COLOR_INFO, 0);
  auto label = lv_label_create(edit);
  lv_label_set_text(label, "EDIT\nLIMITS");
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  LVCallbackWrapper::add(edit, LV_EVENT_CLICKED, [this](lv_event_t *) { limitEditor->show(); });
  updateLimitControls(true);
}

void NormalOperationMode::updateLimitControls(bool force) {
  if (force || lastJogLimitsEnabled != jogLimitsEnabled) {
    lv_label_set_text(jogLimitsLabel, jogLimitsEnabled ? "JOG LIMITS\n\nON" : "JOG LIMITS\n\nOFF");
    lv_obj_set_style_bg_color(jogLimitsButton, jogLimitsEnabled ? APP_COLOR_INFO : APP_COLOR_WARNING, 0);
    lv_obj_set_style_text_color(jogLimitsButton, jogLimitsEnabled ? lv_color_white() : lv_color_black(), 0);
    lastJogLimitsEnabled = jogLimitsEnabled;
  }
}

void NormalOperationMode::dpadButtonDownCallback(DPad::Direction id,
                                                 void *userData) {
  NormalOperationMode *self = static_cast<NormalOperationMode *>(userData);

  if (self->shiftButtonState && !isOn) {
    // Precision movement only enabled when OFF.
    // Show numpad for endstop configuration
    Numpad::Action action;
    switch (id) {
    case DPad::BTN_LEFT:
      action = Numpad::DPAD_MOVEMENT_LEFT;
      break;
    case DPad::BTN_RIGHT:
      action = Numpad::DPAD_MOVEMENT_RIGHT;
      break;
    case DPad::BTN_UP:
      action = Numpad::DPAD_MOVEMENT_UP;
      break;
    case DPad::BTN_DOWN:
      action = Numpad::DPAD_MOVEMENT_DOWN;
      break;
    default:
      return;
    }
    self->numpad->show(action);
  } else {
    // Button pressed down
    Buzzer::getInstance().beginContinuousBeep(2000);
    switch (id) {
    case DPad::BTN_UP:
      h5_ui_jog(&x, 1, true);
      break;
    case DPad::BTN_DOWN:
      h5_ui_jog(&x, -1, true);
      break;
    case DPad::BTN_LEFT:
      h5_ui_jog(&z, 1, true);
      break;
    case DPad::BTN_RIGHT:
      h5_ui_jog(&z, -1, true);
      break;
    default:
      return;
    }
  }
}

void NormalOperationMode::dpadButtonUpCallback(DPad::Direction id,
                                               void *userData) {
  NormalOperationMode *self = static_cast<NormalOperationMode *>(userData);

  // Button released
  Buzzer::getInstance().endContinuousBeep();
  switch (id) {
  case DPad::BTN_UP:
    h5_ui_jog(&x, 1, false);
    break;
  case DPad::BTN_DOWN:
    h5_ui_jog(&x, -1, false);
    break;
  case DPad::BTN_LEFT:
    h5_ui_jog(&z, 1, false);
    break;
  case DPad::BTN_RIGHT:
    h5_ui_jog(&z, -1, false);
    break;
  }
}

void NormalOperationMode::dpadEndstopButtonUpCallback(DPad::Direction id,
                                                      void *userData) {
  NormalOperationMode *self = static_cast<NormalOperationMode *>(userData);

  // Button released
  Buzzer::getInstance().beepSuccess();
  if (self->shiftButtonState) {
    // Show numpad for endstop configuration
    Numpad::Action action;
    switch (id) {
    case DPad::BTN_LEFT:
      action = Numpad::DPAD_LIMIT_LEFT;
      break;
    case DPad::BTN_RIGHT:
      action = Numpad::DPAD_LIMIT_RIGHT;
      break;
    case DPad::BTN_UP:
      action = Numpad::DPAD_LIMIT_UP;
      break;
    case DPad::BTN_DOWN:
      action = Numpad::DPAD_LIMIT_DOWN;
      break;
    default:
      return;
    }
    self->numpad->show(action);
  } else {
    switch (id) {
    case DPad::BTN_UP:
      setLeftStop(&x, x.leftStop == LONG_MAX ? x.pos : LONG_MAX);
      break;
    case DPad::BTN_DOWN:
      setRightStop(&x, x.rightStop == LONG_MIN ? x.pos : LONG_MIN);
      break;
    case DPad::BTN_LEFT:
      setLeftStop(&z, z.leftStop == LONG_MAX ? z.pos : LONG_MAX);
      break;
    case DPad::BTN_RIGHT:
      setRightStop(&z, z.rightStop == LONG_MIN ? z.pos : LONG_MIN);
      break;
    default:
      return;
    }
  }
}

void NormalOperationMode::handleNumpadCallback(float value,
                                               Numpad::Action action) {
  Buzzer::getInstance().beepSuccess();

  switch (action) {
  case Numpad::LIMIT_X_MIN:
  case Numpad::LIMIT_X_MAX:
  case Numpad::LIMIT_Z_MIN:
  case Numpad::LIMIT_Z_MAX:
    // Absolute endpoints belong to the limit editor's separate keypad.
    return;
  case Numpad::DPAD_LIMIT_LEFT: {
    long pos = z.pos + convertMmToDupr(value) / z.screwPitch * z.motorSteps;
    setLeftStop(&z, pos);
    break;
  }
  case Numpad::DPAD_LIMIT_RIGHT: {
    long pos = z.pos - convertMmToDupr(value) / z.screwPitch * z.motorSteps;
    setRightStop(&z, pos);
    break;
  }
  case Numpad::DPAD_LIMIT_UP: {
    long pos = x.pos + convertMmToDupr(value) / x.screwPitch * x.motorSteps;
    setLeftStop(&x, pos);
    break;
  }
  case Numpad::DPAD_LIMIT_DOWN: {
    long pos = x.pos - convertMmToDupr(value) / x.screwPitch * x.motorSteps;
    setRightStop(&x, pos);
    break;
  }
  case Numpad::DPAD_MOVEMENT_LEFT: {
    if (!isOn) {
      manualMoveAxis(&z, value);
    }
    break;
  }
  case Numpad::DPAD_MOVEMENT_RIGHT: {
    if (!isOn) {
      manualMoveAxis(&z, -value);
    }
    break;
  }
  case Numpad::DPAD_MOVEMENT_UP: {
    if (!isOn) {
      manualMoveAxis(&x, value);
    }
    break;
  }
  case Numpad::DPAD_MOVEMENT_DOWN: {
    if (!isOn) {
      manualMoveAxis(&x, -value);
    }
    break;
  }
  case Numpad::PASSES_SETTING: {
    // Validate and set passes value
    int newPasses = (int)value;
    if (newPasses >= 1 && newPasses <= PASSES_MAX) {
      setTurnPasses(newPasses);
    }
    break;
  }
  case Numpad::THREADING_STARTS_SETTING: {
    // Validate and set threading starts value
    int newStarts = (int)value;
    if (newStarts >= 1 && newStarts <= STARTS_MAX) {
      setStarts(newStarts);
    }
    break;
  }
  case Numpad::CONE_RATIO_SETTING: {
    // Validate and set cone ratio value
    if (value > 0) {
      setConeRatio(value);
    }
    break;
  }
  }

  // Update DPad display
  dpad->update();
}

void NormalOperationMode::showMainScreen() { lv_scr_load(mainScreen); }

void NormalOperationMode::updateRpmDisplay() {
  // Update RPM button display
  updateRpmButton();
}

void NormalOperationMode::updatePitchButtonText(bool force) {
  if (pitchButton != nullptr) {
    // Only update if dupr or pitchType changed
    if (dupr != lastDuprValue || pitchType != lastPitchTypeValue || force) {
      lv_obj_t *pitchLabel = lv_obj_get_child(pitchButton, 0);
      lv_obj_t *pitchSignLabel = lv_obj_get_child(pitchSignButton, 0);
      if (pitchLabel != nullptr) {
        if (dupr == 0) {
          lv_label_set_text(pitchLabel, "PITCH");
        } else {
          char buf[16];
          // We show the sign separately
          const long absDupr = abs(dupr);
          switch (pitchType) {
          case PITCH_TYPE_MM_PER_TURN: {
            float currentMmPerTurn = convertDuprToMmPerTurn(absDupr);
            String pitchText = format_decimal(currentMmPerTurn, 2) + " mm";
            lv_label_set_text(pitchLabel, pitchText.c_str());
            break;
          }
          case PITCH_TYPE_TPI: {
            float currentTpi = convertDuprToTpi(absDupr);
            sprintf(buf, "%0.0f TPI", currentTpi);
            lv_label_set_text(pitchLabel, buf);
            break;
          }
          case PITCH_TYPE_INCHES_PER_TURN: {
            float currentInchesPerTurn = convertDuprToInchesPerTurn(absDupr);
            sprintf(buf, "%.3f\"", currentInchesPerTurn);
            lv_label_set_text(pitchLabel, buf);
            break;
          }
          }
          auto color = PitchPicker::colorForPitchType(pitchType);
          lv_obj_set_style_bg_color(pitchButton, color, LV_PART_MAIN);
          lv_obj_set_style_bg_color(pitchButton, lv_color_darken(color, 20),
                                    LV_STATE_PRESSED);
        }
      }
      if (pitchSignLabel != nullptr) {
        lv_color_t color = PitchPicker::colorForPitchSign(dupr);
        lv_obj_set_style_bg_color(pitchSignButton, color, LV_PART_MAIN);
        lv_obj_set_style_bg_color(pitchSignButton, lv_color_darken(color, 20),
                                  LV_STATE_PRESSED);
        lv_label_set_text(pitchSignLabel, dupr > 0 ? "+" : "-");
      }

      // Update cached values
      lastDuprValue = dupr;
      lastPitchTypeValue = pitchType;
    }
  }
}

void NormalOperationMode::updateStepButton(bool force) {
  if (stepButton != nullptr) {
    // Only update if moveStep or measure changed
    if (moveStep != lastMoveStepValue || measure != lastMeasureValue || force) {
      lv_obj_t *stepLabel = lv_obj_get_child(stepButton, 0);
      char buf[16];
      if (measure == MEASURE_METRIC) {
        lv_obj_set_style_bg_color(stepButton, APP_COLOR_INFO, 0);
        sprintf(buf, "%0.2fmm", convertDuprToMmPerTurn(moveStep));
      } else {
        lv_obj_set_style_bg_color(stepButton, APP_COLOR_WARNING, 0);
        sprintf(buf, "%0.3f\"", convertDuprToInchesPerTurn(moveStep));
      }
      lv_label_set_text(stepLabel, buf);

      // Update cached values
      lastMoveStepValue = moveStep;
      lastMeasureValue = measure;
    }
  }
}

void NormalOperationMode::updatePassesButton(bool force) {
  if (passesButton != nullptr) {
    // Show/hide button based on mode - only update if visibility changed
    bool shouldShow = isPassMode();
    if (shouldShow != lastPassesButtonVisible || force) {
      if (shouldShow) {
        lv_obj_clear_flag(passesButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(passesButton, APP_COLOR_INFO, 0);
      } else {
        lv_obj_add_flag(passesButton, LV_OBJ_FLAG_HIDDEN);
      }
      lastPassesButtonVisible = shouldShow;
    }

    // Update passes value only if button is visible and value changed
    if ((shouldShow && (turnPasses != lastTurnPassesValue)) || force) {
      lv_obj_t *passesValueLabel =
          lv_obj_get_child(passesButton, 0); // Value label is child 0 (bottom)
      char buf[16];
      sprintf(buf, "%d", turnPasses);
      lv_label_set_text(passesValueLabel, buf);
      lastTurnPassesValue = turnPasses;
    }
  }
}

void NormalOperationMode::updateThreadingStartsButton(bool force) {
  if (threadingStartsButton != nullptr) {
    // Show/hide button based on mode - only update if visibility changed
    bool shouldShow = (mode == MODE_THREAD);
    if (shouldShow != lastThreadingStartsButtonVisible || force) {
      if (shouldShow) {
        lv_obj_clear_flag(threadingStartsButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(threadingStartsButton, APP_COLOR_INFO, 0);
      } else {
        lv_obj_add_flag(threadingStartsButton, LV_OBJ_FLAG_HIDDEN);
      }
      lastThreadingStartsButtonVisible = shouldShow;
    }

    // Update starts value only if button is visible and value changed
    if ((shouldShow && (starts != lastStartsValue)) || force) {
      lv_obj_t *threadingStartsValueLabel = lv_obj_get_child(
          threadingStartsButton, 0); // Value label is child 0 (bottom)
      char buf[16];
      sprintf(buf, "%d", starts);
      lv_label_set_text(threadingStartsValueLabel, buf);
      lastStartsValue = starts;
    }
  }
}

void NormalOperationMode::updateAuxToggleButton(bool force) {
  if (auxToggleButton != nullptr) {
    // Show/hide button based on mode - only update if visibility changed
    bool shouldShow = (mode == MODE_TURN || mode == MODE_FACE ||
                       mode == MODE_THREAD || mode == MODE_ELLIPSE);
    if (shouldShow != lastAuxToggleButtonVisible || force) {
      if (shouldShow) {
        lv_obj_clear_flag(auxToggleButton, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(auxToggleButton, LV_OBJ_FLAG_HIDDEN);
      }
      lastAuxToggleButtonVisible = shouldShow;
    }

    // Update button state only if button is visible and auxForward state
    // changed
    if ((shouldShow && (auxForward != lastAuxForwardState)) || force) {
      lv_obj_t *auxToggleLabel = lv_obj_get_child(auxToggleButton, 0);
      if (auxToggleLabel != nullptr) {
        // Set label text based on auxForward state
        if (auxForward) {
          lv_label_set_text(auxToggleLabel, "EXT");
          lv_obj_set_style_bg_color(auxToggleButton, APP_COLOR_INFO,
                                    0); // Blue for external
        } else {
          lv_label_set_text(auxToggleLabel, "INT");
          lv_obj_set_style_bg_color(auxToggleButton, APP_COLOR_WARNING,
                                    0); // Orange for internal
        }
      }
      lastAuxForwardState = auxForward;
    }
  }
}

void NormalOperationMode::updateConeRatioButton(bool force) {
  if (coneRatioButton != nullptr) {
    // Show/hide button based on mode - only update if visibility changed
    bool shouldShow = (mode == MODE_CONE);
    if (shouldShow != lastConeRatioButtonVisible || force) {
      if (shouldShow) {
        lv_obj_clear_flag(coneRatioButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(coneRatioButton, APP_COLOR_INFO, 0);
      } else {
        lv_obj_add_flag(coneRatioButton, LV_OBJ_FLAG_HIDDEN);
      }
      lastConeRatioButtonVisible = shouldShow;
    }

    // Update cone ratio value only if button is visible and value changed
    if ((shouldShow && (coneRatio != lastConeRatioValue)) || force) {
      lv_obj_t *coneRatioValueLabel = lv_obj_get_child(
          coneRatioButton, 0); // Value label is child 0 (bottom)
      char buf[16];
      sprintf(buf, "%.2f", coneRatio);
      lv_label_set_text(coneRatioValueLabel, buf);
      lastConeRatioValue = coneRatio;
    }
  }
}

void NormalOperationMode::updateRpmButton() {
  if (rpmButton != nullptr) {
    // Always show RPM button - only update if RPM value changed
    lv_obj_clear_flag(rpmButton, LV_OBJ_FLAG_HIDDEN);

    int currentRpm = getApproxRpm();
    if (currentRpm != lastRpmValue) {
      lv_obj_t *rpmValueLabel =
          lv_obj_get_child(rpmButton, 0); // Value label is child 0 (bottom)
      char buf[16];
      sprintf(buf, "%d", currentRpm);
      lv_label_set_text(rpmValueLabel, buf);
      lv_obj_set_style_bg_color(rpmButton, APP_COLOR_INFO, 0);
      lastRpmValue = currentRpm;
    }
  }
}

void NormalOperationMode::repositionButtons(int targetMode) {
  // Stable, roomy columns: position/feed controls, cycle settings, then jog.
  lv_obj_set_pos(xAxisControls->getContainer(), 24, 112);
  lv_obj_set_pos(zAxisControls->getContainer(), 24, 208);
  lv_obj_set_pos(pitchContainer, 24, 304);
  lv_obj_set_size(startStopButton, 304, 80);
  // Align the action row with the right pane's bottom edge (y = 740).
  lv_obj_set_pos(startStopButton, 24, 660);
  lv_obj_set_size(shiftButton, 240, 80);
  lv_obj_set_pos(shiftButton, 352, 660);

  int y = 112;
  auto place = [&y](lv_obj_t *button) {
    lv_obj_set_size(button, 240, buttonHeight);
    lv_obj_set_pos(button, 352, y);
    y += buttonHeight + verticalSpacing;
  };
  if (targetMode == MODE_CONE) place(coneRatioButton);
  if (targetMode == MODE_TURN || targetMode == MODE_FACE ||
      targetMode == MODE_THREAD || targetMode == MODE_ELLIPSE) place(auxToggleButton);
  if (targetMode == MODE_THREAD) place(threadingStartsButton);
  if (targetMode == MODE_TURN || targetMode == MODE_FACE || targetMode == MODE_CUT ||
      targetMode == MODE_THREAD || targetMode == MODE_ELLIPSE) place(passesButton);
}

void NormalOperationMode::updateStartStopButton(bool force) {
  if (startStopButton != nullptr) {
    // Only update if the state has changed or if forced
    if (force || startStopButtonState != isOn) {
      startStopButtonState = isOn;

      lv_obj_t *startStopLabel = lv_obj_get_child(startStopButton, 0);
      if (startStopLabel != nullptr) {
        if (isOn) {
          // Running state - show STOP in red
          lv_label_set_text(startStopLabel, "STOP");
          lv_obj_set_style_bg_color(startStopButton, lv_color_hex(0xF44336),
                                    0); // Red
          lv_obj_set_style_bg_color(startStopButton, lv_color_hex(0xD32F2F),
                                    LV_STATE_PRESSED);
        } else {
          // Stopped state - show START in green
          lv_label_set_text(startStopLabel, "START");
          lv_obj_set_style_bg_color(startStopButton, lv_color_hex(0x4CAF50),
                                    0); // Green
          lv_obj_set_style_bg_color(startStopButton, lv_color_hex(0x388E3C),
                                    LV_STATE_PRESSED);
        }
      }
    }
  }
}

void NormalOperationMode::cleanup() {
  // Clean up RPM PWM callback
  display.rpmPwmCallback = nullptr;
  limitEditor.reset();

  // Smart pointers (tabSelector and pitchPicker) are automatically cleaned
  // up when this object is destroyed, so no manual cleanup needed

  // Clean up all LVGL objects at once
  if (mainScreen != nullptr) {
    lv_obj_del(mainScreen);
    mainScreen = nullptr;
  }
}

void NormalOperationMode::onTabSelected(int tabId) {
  // Remember this tab as previous (except for FW_UPDATE)
  if (tabId != TAB_FW_UPDATE) {
    previousTabId = tabId;
  }

  switch (tabId) {
  case TAB_GEARBOX:
    setModeFromTask(MODE_NORMAL);
    break;
  case TAB_ASYNC:
    setModeFromTask(MODE_ASYNC);
    break;
  case TAB_CONE:
    setModeFromTask(MODE_CONE);
    break;
  case TAB_TURN:
    setModeFromTask(MODE_TURN);
    break;
  case TAB_FACE:
    setModeFromTask(MODE_FACE);
    break;
  case TAB_CUT:
    setModeFromTask(MODE_CUT);
    break;
  case TAB_THREAD:
    setModeFromTask(MODE_THREAD);
    break;
  case TAB_ELLIPSE:
    setModeFromTask(MODE_ELLIPSE);
    break;
  case TAB_FW_UPDATE:
    h5_ui_show_update();
    return;
  }

  // Reposition buttons after mode change to eliminate gaps
  // Skip repositioning for TAB_FW_UPDATE since it switches to a different mode
  if (tabId != TAB_FW_UPDATE) {
    int targetMode = MODE_NORMAL; // Default
    switch (tabId) {
    case TAB_GEARBOX:
      targetMode = MODE_NORMAL;
      break;
    case TAB_ASYNC:
      targetMode = MODE_ASYNC;
      break;
    case TAB_CONE:
      targetMode = MODE_CONE;
      break;
    case TAB_TURN:
      targetMode = MODE_TURN;
      break;
    case TAB_FACE:
      targetMode = MODE_FACE;
      break;
    case TAB_CUT:
      targetMode = MODE_CUT;
      break;
    case TAB_THREAD:
      targetMode = MODE_THREAD;
      break;
    case TAB_ELLIPSE:
      targetMode = MODE_ELLIPSE;
      break;
    }
    repositionButtons(targetMode);
  }

  createTabContent(tabId);
}

void NormalOperationMode::showSettingsScreen() {
  if (settingsScreen != nullptr) {
    settingsScreen->showSettingsScreen();
  }
}

int NormalOperationMode::getModeFromTabId(int tabId) {
  switch (tabId) {
  case TAB_GEARBOX:
    return MODE_NORMAL;
  case TAB_ASYNC:
    return MODE_ASYNC;
  case TAB_CONE:
    return MODE_CONE;
  case TAB_TURN:
    return MODE_TURN;
  case TAB_FACE:
    return MODE_FACE;
  case TAB_CUT:
    return MODE_CUT;
  case TAB_THREAD:
    return MODE_THREAD;
  case TAB_ELLIPSE:
    return MODE_ELLIPSE;
  default:
    return MODE_NORMAL;
  }
}

void NormalOperationMode::createTabContent(int tabId) {
  // Clear existing content
  if (currentTabContent != nullptr) {
    LVCallbackWrapper::remove_all(currentTabContent);
    lv_obj_del(currentTabContent);
    currentTabContent = nullptr;
  }

  // Create new content container
  currentTabContent = lv_obj_create(tabContentContainer);
  lv_obj_set_size(currentTabContent, LV_PCT(100), LV_PCT(100));
  lv_obj_align(currentTabContent, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(currentTabContent, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(currentTabContent, 0, 0);
  lv_obj_set_style_pad_all(currentTabContent, 0, 0);
  LVCallbackWrapper::add(
      currentTabContent, LV_EVENT_DELETE,
      [this](lv_event_t *e) { this->currentTabContent = nullptr; });

  switch (tabId) {
  case TAB_GEARBOX: // Manual
    break;
  case TAB_ASYNC: // Thread
    break;
  case TAB_CONE: // Turn
    break;
  case TAB_TURN: // Face
    break;
  case TAB_FACE: // FW Update
    break;
  case TAB_CUT: // Cut
    break;
  case TAB_THREAD: // FW Update
    break;
  case TAB_ELLIPSE: // Ellipse
    break;
  }
}

void NormalOperationMode::testJogEvent(char action) {
  int tab=-1;
  if(action=='6')tab=TAB_THREAD;
  else if(action=='F')tab=TAB_FACE;
  else if(action=='C')tab=TAB_CUT;
  else if(action=='E')tab=TAB_ELLIPSE;
  else if(action=='G')tab=TAB_GEARBOX;
  else if(action=='K')tab=TAB_CONE;
  else if(action=='A')tab=TAB_ASYNC;
  if(tab>=0) {tabSelector->setSelectedTab(tab);onTabSelected(tab);}
  if(action=='D') {lv_event_send(passesButton,LV_EVENT_LONG_PRESSED,nullptr);return;}
  if (tab>=0 || action == '8') { lv_event_send(startStopButton, LV_EVENT_CLICKED, nullptr); return; }
  const DPad::Direction directions[] = {DPad::BTN_UP, DPad::BTN_DOWN, DPad::BTN_LEFT, DPad::BTN_RIGHT};
  if (action >= '1' && action <= '4') {
    lv_event_send(dpad->getButton(directions[action - '1']), LV_EVENT_PRESSED, nullptr);
  } else if (action == '0' || action == '5') {
    for (DPad::Direction direction : directions)
      lv_event_send(dpad->getButton(direction), action == '0' ? LV_EVENT_RELEASED : LV_EVENT_PRESS_LOST, nullptr);
  }
}
