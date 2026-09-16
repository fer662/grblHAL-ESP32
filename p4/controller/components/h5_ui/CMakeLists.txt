idf_component_register(SRCS
    AxisControls.cpp CompactTabSelector.cpp DPad.cpp Numpad.cpp PitchPicker.cpp LimitEditor.cpp
    Pitch.cpp SettingsScreen.cpp NormalOperationMode.cpp LVCallbackWrapper.cpp
    StateMachine.cpp display.cpp ui.cpp cycle_plan.c audio.cpp
    INCLUDE_DIRS "."
    REQUIRES lvgl__lvgl esp32_p4_wifi6_touch_lcd_x esp_timer)
