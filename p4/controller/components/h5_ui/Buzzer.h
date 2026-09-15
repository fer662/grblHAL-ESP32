#pragma once
extern bool buzzerEnabled;
extern "C" void h5_audio_tone(unsigned hz,unsigned ms);
class Buzzer {
public:
    static Buzzer &getInstance() { static Buzzer buzzer; return buzzer; }
    void beepSuccess() { if(buzzerEnabled) h5_audio_tone(1200,70); }
    void beginContinuousBeep(unsigned hz) { if(buzzerEnabled) h5_audio_tone(hz,0); }
    void endContinuousBeep() { h5_audio_tone(0,0); }
};
