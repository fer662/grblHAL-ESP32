#ifndef NORMAL_OPERATION_MODE_H
#define NORMAL_OPERATION_MODE_H

#include "AxisControls.h"
#include "CompactTabSelector.h"
#include "DPad.h"
#include "Numpad.h"
#include "OperationMode.h"
#include "PitchPicker.h"
#include "SettingsScreen.h"
#include "StateMachine.h"
#include <memory>

// Forward declarations

class StateMachine;
class Display;
typedef struct _lv_obj_t lv_obj_t;

// Normal operation mode
class NormalOperationMode : public OperationMode {
public:
  void testJogEvent(char action); // Isolated bench: exercise the actual LVGL event path.
  explicit NormalOperationMode(StateMachine &stateMachine, Display &display);
  void initialize() override;
  void update() override;
  void updateDisplay() override;
  void cleanup() override;
  const char *getName() const override { return "Normal Operation"; }

private:
  StateMachine &stateMachine;
  Display &display;

  lv_obj_t *mainScreen = nullptr;
  std::unique_ptr<Numpad> numpad;

  // Compact tab selector
  std::unique_ptr<CompactTabSelector> tabSelector;
  lv_obj_t *tabContentContainer = nullptr;
  lv_obj_t *currentTabContent = nullptr;

  // RPM label
  lv_obj_t *rpmLabel = nullptr;

  // Responsiveness test counter
  lv_obj_t *fpsLabel = nullptr;
  int frameCount = 0;
  unsigned long lastFpsUpdate = 0;

  // Start/Stop button
  lv_obj_t *startStopButton = nullptr;
  bool startStopButtonState = false; // Track current button state
  lv_obj_t *stepButton = nullptr;
  lv_obj_t *jogModeButton = nullptr;
  lv_obj_t *jogModeLabel = nullptr;
  bool lastJogContinuous = true;

  // Pitch selection
  lv_obj_t *pitchContainer = nullptr;
  lv_obj_t *pitchButton = nullptr;
  lv_obj_t *pitchSignButton = nullptr;
  std::unique_ptr<PitchPicker> pitchPicker;

  // Settings screen
  std::unique_ptr<SettingsScreen> settingsScreen;
  int previousTabId; // Track previous tab when showing settings

  lv_obj_t *shiftButton = nullptr;
  lv_obj_t *passesButton = nullptr;

  // Cached values to avoid unnecessary updates
  bool lastPassesButtonVisible = false;
  int lastTurnPassesValue = -1;
  bool lastThreadingStartsButtonVisible = false;
  int lastStartsValue = -1;
  bool lastAuxToggleButtonVisible = true;
  bool lastAuxForwardState = false;
  bool lastConeRatioButtonVisible = true;
  float lastConeRatioValue = -1;
  int lastRpmValue = -1;
  long lastMoveStepValue = -1;
  int lastMeasureValue = -1;
  long lastDuprValue = -1;
  int lastPitchTypeValue = -1;
  lv_obj_t *threadingStartsButton = nullptr;
  lv_obj_t *auxToggleButton = nullptr;
  lv_obj_t *coneRatioButton = nullptr;
  lv_obj_t *rpmButton = nullptr;

  // DPad for manual control
  std::unique_ptr<DPad> dpad;

  std::unique_ptr<AxisControls> xAxisControls;
  std::unique_ptr<AxisControls> zAxisControls;

  bool shiftButtonState = false;

  void createMainScreen();
  void createStartStopButtons();
  void createPitchButtons();
  void createStepButton();
  void createJogModeButton();
  void createPassesButton();
  void createThreadingStartsButton();
  void createAuxToggleButton();
  void createConeRatioButton();
  void createRpmButton();
  void createShiftButton();
  void createDPad();
  void createAxisLabels();
  void showMainScreen();
  void handleNumpadCallback(float value, Numpad::Action action);
  void updateRpmDisplay();
  void updatePitchButtonText(bool force = false);
  void updateStepButton(bool force = false);
  void updateJogModeButton(bool force = false);
  void updatePassesButton(bool force = false);
  void updateThreadingStartsButton(bool force = false);
  void updateAuxToggleButton(bool force = false);
  void updateConeRatioButton(bool force = false);
  void updateRpmButton();
  void updateStartStopButton(bool force = false);
  void updateShiftButton();
  void updateAxisLabels();
  void repositionButtons(int mode);
  void onTabSelected(int tabId);
  void createTabContent(int tabId);
  void showSettingsScreen();
  int getModeFromTabId(int tabId);

  // Static callback for DPad
  static void dpadButtonDownCallback(DPad::Direction id, void *userData);
  static void dpadButtonUpCallback(DPad::Direction id, void *userData);
  static void dpadEndstopButtonUpCallback(DPad::Direction id, void *userData);
};

#endif // NORMAL_OPERATION_MODE_H
