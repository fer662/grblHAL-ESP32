#pragma once
// UI sound events are isolated from timing and will use the board audio service.
class Buzzer {
public:
    static Buzzer &getInstance() { static Buzzer buzzer; return buzzer; }
    void beepSuccess() {}
    void beginContinuousBeep(unsigned) {}
    void endContinuousBeep() {}
};
