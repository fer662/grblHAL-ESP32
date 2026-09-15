#ifndef CONFIG_H
#define CONFIG_H

#include "ui_support.h"

/* Placeholder pins; replace with actual pins for your board */
#define TFT_WIDTH 1280
#define TFT_HEIGHT 800

/* Change values in this section to suit your hardware. */

// Define your hardware parameters here.
// 600 step spindle optical rotary encoder.
const int ENCODER_PPR = 600;

// Number of impulses encoder can issue without movement of the spindle
const int ENCODER_BACKLASH = 3;

// Spindle rotary encoder pins. Swap values if the rotation direction is wrong.
#define ENC_A 3
#define ENC_B 2

// Main lead screw (Z) parameters.
const long SCREW_Z_DU =
    20000; // 2mm ACME screw in deci-microns (10^-7 of a meter)
const long MOTOR_STEPS_Z = 400;
const long SPEED_START_Z =
    MOTOR_STEPS_Z; // Initial speed of a motor, steps / second.

    // Acceleration of a motor, steps / second ^ 2.
const long ACCELERATION_Z = 25 * MOTOR_STEPS_Z;
// Maximum speed of a motor during manual move, steps / second.
const long SPEED_MANUAL_MOVE_Z = 8 * MOTOR_STEPS_Z;
const bool INVERT_Z = false; // change (true/false) if the carriage moves e.g.
                             // "left" when you press "right".
const bool NEEDS_REST_Z =
    false; // Set to false for closed-loop drivers, true for open-loop.
const long MAX_TRAVEL_MM_Z = 300; // Lathe bed doesn't allow to travel more than
                                  // this in one go, 30cm / ~1 foot
const long BACKLASH_DU_Z = 0; // 0mm backlash in deci-microns (10^-7 of a meter)
const char NAME_Z =
    'Z'; // Text shown on screen before axis position value, GCode axis name

// Cross-slide lead screw (X) parameters.
// 1mm pitch metric screw in deci-microns (10^-7 of a meter)
const long SCREW_X_DU = 10000;
// Steps per revolution of the screw = MOTOR_STEPS_PER_REVOLUTION * GEAR_RATIO (3:1 gearbox)
const long MOTOR_STEPS_X = 400 * 3;
const long SPEED_START_X =
    MOTOR_STEPS_X; // Initial speed of a motor, steps / second.
const long ACCELERATION_X =
    25 * MOTOR_STEPS_X; // Acceleration of a motor, steps / second ^ 2.
     // Maximum speed of a motor during manual move, steps / second
const long SPEED_MANUAL_MOVE_X = MOTOR_STEPS_X;
const bool INVERT_X = true; // change (true/false) if the carriage moves e.g.
                            // "left" when you press "right".
const bool NEEDS_REST_X = false; // Set to false for all kinds of drivers or X
                                 // will be unlocked when not moving.
const long MAX_TRAVEL_MM_X =
    100; // Cross slide doesn't allow to travel more than this in one go, 10cm
const long BACKLASH_DU_X =
    0; // 0.15mm backlash in deci-microns (10^-7 of a meter)
const char NAME_X =
    'X'; // Text shown on screen before axis position value, GCode axis name

// Manual stepping with left/right/up/down buttons. Only used when step isn't
// default continuous (1mm or 0.1").
const long STEP_TIME_MS =
    500; // Time in milliseconds it should take to make 1 manual step.
const long DELAY_BETWEEN_STEPS_MS =
    80; // Time in milliseconds to wait between steps.

// Connect to WiFi and expose web UI to control and receive GCode.
extern const char *SSID;
extern const char *PASSWORD;

/* Changing anything below shouldn't be needed for basic use. */

// Configuration for axis connected to Y. This is uncommon. Dividing head (C)
// motor parameters. Throughout the configuration below we assume 1mm = 1degree
// of rotation, so 1du = 0.0001degree.
const bool ACTIVE_Y = false; // Whether the axis is connected
const bool ROTARY_Y = true;  // Whether the axis is rotary or linear
const long MOTOR_STEPS_Y =
    300; // Number of motor steps for 1 rotation of the the worm gear screw
         // (full step with 20:30 reduction)
const long SCREW_Y_DU =
    20000; // Degrees multiplied by 10000 that the spindle travels per 1 turn of
           // the worm gear. 2 degrees.
const long SPEED_START_Y = 1600; // Initial speed of a motor, steps / second.
const long ACCELERATION_Y =
    16000; // Acceleration of a motor, steps / second ^ 2.
const long SPEED_MANUAL_MOVE_Y =
    3200; // Maximum speed of a motor during manual move, steps / second.
const bool INVERT_Y = false; // change (true/false) if the carriage moves e.g.
                             // "left" when you press "right".
const bool NEEDS_REST_Y =
    false; // Set to false for closed-loop drivers. Open-loop: true if you need
           // holding torque, false otherwise.
const long MAX_TRAVEL_MM_Y = 360; // Probably doesn't make sense to ask the
                                  // dividin head to travel multiple turns.
const long BACKLASH_DU_Y = 0;     // Assuming no backlash on the worm gear
const char NAME_Y =
    'Y'; // Text shown on screen before axis position value, GCode axis name

// Manual handwheels. Ignore if you don't have them installed.
const float PULSE_PER_REVOLUTION = 600; // PPR of handwheels.

const int ENCODER_STEPS_INT =
    ENCODER_PPR *
    2; // Number of encoder impulses PCNT counts per revolution of the spindle
const int ENCODER_FILTER = 1;   // Encoder pulses shorter than this will be
                                // ignored. Clock cycles, 1 - 1023.
const int PCNT_LIM = 31000;     // Limit used in hardware pulse counter logic.
const int PCNT_CLEAR = 30000;   // Limit where we reset hardware pulse counter
                                // value to avoid overflow. Less than PCNT_LIM.
const long DUPR_MAX = 254000;   // No more than 1 inch pitch
const int32_t STARTS_MAX = 124; // No more than 124-start thread
const long PASSES_MAX = 999;    // No more turn or face passes than this
const long SAFE_DISTANCE_DU = 5000; // Step back 0.5mm from the material when
                                    // moving between cuts in automated modes
const long SAVE_DELAY_US = 5000000; // Wait 5s after last save and last change
                                    // of saveable data before saving again
const long DIRECTION_SETUP_DELAY_US =
    5; // Stepper driver needs some time to adjust to direction change
const long STEPPED_ENABLE_DELAY_MS =
    100; // Delay after stepper is enabled and before issuing steps

// Version of the pref storage format, should be changed when
// non-backward-compatible changes are made to the storage logic, resulting in
// Preferences wipe on first start.
#define PREFERENCES_VERSION 1
#define PREF_NAMESPACE "h5"

// GCode-related constants.
const float LINEAR_INTERPOLATION_PRECISION =
    0.1; // 0 < x <= 1, smaller values make for quicker G0 and G1 moves
const long GCODE_WAIT_EPSILON_STEPS = 10;
const bool SPINDLE_PAUSES_GCODE =
    true;                     // pause GCode execution when spindle stops
const int GCODE_MIN_RPM = 30; // pause GCode execution if RPM is below this

// To be incremented whenever a measurable improvement is made.
#define SOFTWARE_VERSION 8

// To be changed whenever a different PCB / encoder / stepper / ... design is
// used.
#define HARDWARE_VERSION 5

#define Z_ENA 30
#define Z_DIR 29
#define Z_STEP 28

#define Z_SCALE_ENABLED false
#define Z_PULSE_A -1
#define Z_PULSE_B -1

#define X_ENA 47
#define X_DIR 31
#define X_STEP 49

#define X_SCALE_ENABLED false
#define X_PULSE_A -1
#define X_PULSE_B -1

#define Y_ENA 1
#define Y_DIR 2
#define Y_STEP 17

#define Y_SCALE_ENABLED false
#define Y_PULSE_A -1
#define Y_PULSE_B -1

#define B_LEFT 21  // Left arrow - controls Z axis movement to the left
#define B_RIGHT 22 // Right arrow - controls Z axis movement to the right
#define B_UP 23    // Up arrow - controls X axis movement forwards
#define B_DOWN 24  // Down arrow - controls X axis movement backwards
#define B_MINUS 45 // Numpad minus - recrements the pitch or number of passes
#define B_PLUS 44  // Numpad plus - increments the pitch or number of passes
#define B_ON 30    // Enter - starts operation or mode
#define B_OFF 27   // ESC - stops operation or mode
#define B_STOPL 65 // a - sets left stop
#define B_STOPR 68 // d - sets right stop
#define B_STOPU 87 // w - sets forward stop
#define B_STOPD 83 // s - sets rear stop
#define B_DISPL                                                                \
  12 // Win - changes info displayed in the bottom line (angle, rpm, ...)
#define B_STEP                                                                 \
  64 // Tilda - changes distance moved when movement buttons are used
#define B_SETTINGS 14 // Context menu - not used currently
#define B_MEASURE 77  // m - controls metric / imperial / tpi
#define B_REVERSE 82  // r - changes pitch sign (left / right thread)
#define B_DIAMETER                                                             \
  79 // o - sets X0 so that centerline is at the middle of a given diameter
     // value
#define B_0 48 // 0 top row - for number entry
#define B_1 49 // 1 top row
#define B_2 50 // ...
#define B_3 51
#define B_4 52
#define B_5 53
#define B_6 54
#define B_7 55
#define B_8 56
#define B_9 57
#define B_BACKSPACE 28     // removes the last entered number
#define B_MODE_GEARS 97    // F1 - sets the mode to gearbox
#define B_MODE_TURN 98     // F2 - ...
#define B_MODE_FACE 99     // F3
#define B_MODE_CONE 100    // F4
#define B_MODE_CUT 101     // F5
#define B_MODE_THREAD 102  // F6
#define B_MODE_ASYNC 103   // F7
#define B_MODE_ELLIPSE 104 // F8
#define B_MODE_GCODE 105   // F9
#define B_MODE_Y 106       // F10
#define B_X 88             // x - zeroes X axis
#define B_Z 90             // z - zeroes Z axis
#define B_X_ENA 67         // c - enables / disables X axis
#define B_Z_ENA 81         // q - enables / disables Z axis

#define PREF_VERSION "v"
#define PREF_DUPR "d"
#define PREF_POS_Z "zp"
#define PREF_LEFT_STOP_Z "zls"
#define PREF_RIGHT_STOP_Z "zrs"
#define PREF_ORIGIN_POS_Z "zpo"
#define PREF_POS_GLOBAL_Z "zpg"
#define PREF_MOTOR_POS_Z "zpm"
#define PREF_DISABLED_Z "zd"
#define PREF_POS_X "xp"
#define PREF_LEFT_STOP_X "xls"
#define PREF_RIGHT_STOP_X "xrs"
#define PREF_ORIGIN_POS_X "xpo"
#define PREF_POS_GLOBAL_X "xpg"
#define PREF_MOTOR_POS_X "xpm"
#define PREF_DISABLED_X "xd"
#define PREF_POS_Y "y1p"
#define PREF_LEFT_STOP_Y "y1ls"
#define PREF_RIGHT_STOP_Y "y1rs"
#define PREF_ORIGIN_POS_Y "y1po"
#define PREF_POS_GLOBAL_Y "y1pg"
#define PREF_MOTOR_POS_Y "y1pm"
#define PREF_DISABLED_Y "y1d"
#define PREF_SPINDLE_POS "sp"
#define PREF_SPINDLE_POS_AVG "spa"
#define PREF_OUT_OF_SYNC "oos"
#define PREF_SPINDLE_POS_GLOBAL "spg"
#define PREF_SHOW_ANGLE "ang"
#define PREF_SHOW_TACHO "rpm"
#define PREF_STARTS "sta"
#define PREF_MODE "mod"
#define PREF_MEASURE "mea"
#define PREF_CONE_RATIO "cr"
#define PREF_TURN_PASSES "tp"
#define PREF_MOVE_STEP "ms"
#define PREF_AUX_FORWARD "af"
#define PREF_BUZZER_ENABLED "be"
#define PREF_PITCH_TYPE "pt"

#define MOVE_STEP_1 10000 // 1mm
#define MOVE_STEP_2 1000  // 0.1mm
#define MOVE_STEP_3 100   // 0.01mm

#define MOVE_STEP_IMP_1 25400 // 1/10"
#define MOVE_STEP_IMP_2 2540  // 1/100"
#define MOVE_STEP_IMP_3 254   // 1/1000" also known as 1 thou

#define MODE_NORMAL 0
#define MODE_ASYNC 2
#define MODE_CONE 3
#define MODE_TURN 4
#define MODE_FACE 5
#define MODE_CUT 6
#define MODE_THREAD 7
#define MODE_ELLIPSE 8
#define MODE_GCODE 9
#define MODE_Y 10

#define MEASURE_METRIC 0
#define MEASURE_INCH 1
#define MEASURE_TPI 2

#define ESTOP_NONE 0
#define ESTOP_POS 2
#define ESTOP_MARK_ORIGIN 3
#define ESTOP_ON_OFF 4
#define ESTOP_OFF_MANUAL_MOVE 5

#define TIMER_FREQ 1000000 // 1MHz async timer frequency

extern const float
    ENCODER_STEPS_FLOAT; // Convenience float version of ENCODER_STEPS_INT
extern const long
    RPM_BULK; // Measure RPM averaged over this number of encoder pulses

#endif
