#pragma once
#include "ui_support.h"
#include "Pitch.h"
#include "config.h"
#include "Axis.h"
extern int mode, measure, turnPasses, starts;
extern bool isOn, auxForward, buzzerEnabled, jogContinuous;
extern bool jogLimitsEnabled;
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
bool h5_ui_limits_editable();
bool h5_ui_set_jog_limits(bool enabled);
double h5_ui_limit_coordinate(Axis *axis, long steps);
bool h5_ui_limit_steps(Axis *axis, double coordinate, long *steps);
const char *h5_ui_apply_limits(const long limits[4]); // X-, X+, Z-, Z+; nullptr on success.
