#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "OperationMode.h"
#include <memory>

// Forward declarations
class Display;

// State machine manager
class StateMachine {
public:
  explicit StateMachine(Display &display);
  ~StateMachine();

  void switchMode(std::unique_ptr<OperationMode> newMode);
  void update();
  void updateDisplay();
  OperationMode *getCurrentMode() const { return currentMode.get(); }

  // Mode factories
  std::unique_ptr<OperationMode> createBootMenuMode();
  std::unique_ptr<OperationMode> createNormalOperationMode();
  std::unique_ptr<OperationMode> createFirmwareUpdateMode();

private:
  Display &display;
  std::unique_ptr<OperationMode> currentMode;
};

#endif // STATE_MACHINE_H
