#pragma once
#include "ui_support.h"
// Display model only. No pins, ISR state, timers or driver ownership here.
struct Axis {
    char name;
    float motorSteps, screwPitch;
    long pos = 0;
    long leftStop = LONG_MAX, rightStop = LONG_MIN;
    bool disabled = false;
};
void markAxis0(Axis *axis);
void manualMoveAxis(Axis *axis, float offset_mm);
void setAxisDisabled(Axis *axis, bool disabled);
