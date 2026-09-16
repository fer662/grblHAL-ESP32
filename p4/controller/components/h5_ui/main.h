#pragma once
#include "ui_support.h"
#include "Pitch.h"
#include "config.h"
#include "Axis.h"
extern int mode, measure, turnPasses, starts;
extern bool isOn, auxForward, buzzerEnabled, jogContinuous;
extern long dupr, moveStep;
extern float coneRatio;
extern PitchType pitchType;
extern Axis x, z;
void setDupr(long value);
void setModeFromTask(int value);
void buttonOnOffPress(bool on);
void buttonMoveStepPress();
void buttonMeasurePress();
void setTurnPasses(int value);
void setStarts(int value);
void setAuxForward(bool value);
void setConeRatio(float value);
void setLeftStop(Axis *a, long value);
void setRightStop(Axis *a, long value);
String getAxisPos(Axis *a);
String getAxisLeftStop(Axis *a);
String getAxisRightStop(Axis *a);
String getAxisStopDiff(Axis *a);
bool isPassMode();
int getApproxRpm();
void h5_ui_sync();
void h5_ui_jog(Axis *a, int sign, bool pressed);
void h5_ui_show_update();
