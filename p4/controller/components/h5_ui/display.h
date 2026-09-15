#pragma once
#include "ui_support.h"
class Display {
public:
    void begin();
    void update() {}
    void showTransitionScreen() {}
    void (*rpmPwmCallback)(uint8_t) = nullptr;
};
