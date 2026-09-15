/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
// Core configuration belongs to this port, never to edits in the core submodule.
#define N_AXIS 3 // grblHAL retains XYZ indexing; the P4 driver rejects Y motion.
#define DEFAULT_LATHE_MODE 1
#define LATHE_UVW_OPTION 1
#define NGC_EXPRESSIONS_ENABLE 1
#define COMPATIBILITY_LEVEL 0
#define DEFAULT_X_STEPS_PER_MM 1200.0f
#define DEFAULT_Z_STEPS_PER_MM 200.0f
#define DEFAULT_X_MAX_RATE 60.0f
#define DEFAULT_Z_MAX_RATE 960.0f
#define DEFAULT_X_ACCELERATION 25.0f
#define DEFAULT_Z_ACCELERATION 50.0f
#define DEFAULT_STEP_PULSE_MICROSECONDS 10.0f
#define DEFAULT_STEP_PULSE_DELAY 5.0f
#define DEFAULT_STEPPER_IDLE_LOCK_TIME 255
#define DEFAULT_HOMING_ENABLE 0
#define DEFAULT_ENABLE_SIGNALS_INVERT_MASK 1
#define DEFAULT_DIR_SIGNALS_INVERT_MASK 4 // H5 positive direction: X low, Z high
#define DEFAULT_STEP_SIGNALS_INVERT_MASK 0
#define BUILD_INFO "H5_P4_BENCH_MOTOR_ENABLES_LOCKED"
// Deliberate compile-time gate: this port is not yet a machine-ready controller.
#define H5_BENCH_ONLY 1
#define H5_STEP_HZ 10000000UL
#define H5_X_STEP 49
#define H5_X_DIR 31
#define H5_X_ENABLE 47
#define H5_Z_STEP 28
#define H5_Z_DIR 29
#define H5_Z_ENABLE 30
#define H5_ENCODER_A 3
#define H5_ENCODER_B 2
#define H5_ENCODER_CPR 1200

#define SPINDLE_SYNC_ENABLE 1
#define SPINDLE_SYNC_FEED_FORWARD 1
#define SPINDLE_SYNC_INDEX_ORIGIN 1
#define DEFAULT_SPINDLE_SYNC_P_GAIN 0.25f
#define DEFAULT_SPINDLE_PPR H5_ENCODER_CPR
