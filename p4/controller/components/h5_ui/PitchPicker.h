#ifndef PITCH_PICKER_H
#define PITCH_PICKER_H

#include <functional>
#include <vector>
#include "OperationMode.h"
#include "Pitch.h"
#include "StateMachine.h"
#include "lvgl.h"
#include "main.h"

// Forward declarations
extern long dupr;
class StateMachine;
class Display;
typedef struct _lv_obj_t lv_obj_t;

// Pitch picker class for selecting thread pitches
class PitchPicker {
public:
  explicit PitchPicker();
  ~PitchPicker();

  void createPitchScreen(lv_obj_t *parent);
  void show();
  void hide();
  bool isVisible() const { return visible; }

  // Callback for when pitch is selected
  std::function<void(long, PitchType)> onPitchSelected;

  static lv_color_t colorForPitchType(PitchType pitchType);
  static lv_color_t colorForPitchSign(long dupr);

private:
  lv_obj_t *pitchScreen = nullptr;
  lv_obj_t *tabView = nullptr;
  lv_obj_t *currentPitchLabel = nullptr;
  bool visible = false;

  // Store button labels persistently
  std::vector<const char *> turningLabels;
  std::vector<const char *> metricLabels;
  std::vector<const char *> imperialLabels;

  // Track current tab (excluding Back tab)
  int currentTabIndex = 0;

  void createTabContent();
  void createTurningTab(lv_obj_t *parent);
  void createMetricTab(lv_obj_t *parent);
  void createImperialTab(lv_obj_t *parent);
  void createBackTab(lv_obj_t *parent);
  void updateCurrentPitchDisplay();

  // Helper function to configure button matrix styling
  void configureButtonMatrix(lv_obj_t *buttonMatrix);

  // Utility functions
  long convertMmPerTurnToDupr(float mmPerTurn);
  long convertTpiToDupr(float tpi);
};

#endif // PITCH_PICKER_H
