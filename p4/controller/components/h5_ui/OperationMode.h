#ifndef OPERATION_MODE_H
#define OPERATION_MODE_H

#include "ui_support.h"
#include <memory>

// Forward declarations
class Display;

// Base interface for operation modes
class OperationMode {
public:
  virtual ~OperationMode() = default;
  virtual void initialize() = 0;
  virtual void update() = 0;
  virtual void updateDisplay() = 0;
  virtual void cleanup() = 0;
  virtual const char *getName() const = 0;
};

// Forward declarations for mode classes
class BootMenuMode;
class NormalOperationMode;
class FirmwareUpdateMode;

#endif // OPERATION_MODE_H
