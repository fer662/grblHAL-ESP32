#include "StateMachine.h"
#include "NormalOperationMode.h"
#include "display.h"
StateMachine::StateMachine(Display &d) : display(d) {}
StateMachine::~StateMachine() = default;
void StateMachine::switchMode(std::unique_ptr<OperationMode> mode) {
    if (currentMode) currentMode->cleanup();
    currentMode = std::move(mode);
    currentMode->initialize();
}
void StateMachine::update() {}
void StateMachine::updateDisplay() { if (currentMode) currentMode->updateDisplay(); }
std::unique_ptr<OperationMode> StateMachine::createNormalOperationMode() {
    return std::make_unique<NormalOperationMode>(*this, display);
}
