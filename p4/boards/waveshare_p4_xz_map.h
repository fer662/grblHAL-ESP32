/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
// Example external X/Z breakout wiring, not a universal Waveshare pinout.
#define P4_BOARD_NAME "Waveshare ESP32-P4 X/Z example"
#define P4_X_STEP 49
#define P4_X_DIR 31
#define P4_X_ENABLE 47
#define P4_X_ENABLE_OFF 1
#define P4_Z_STEP 28
#define P4_Z_DIR 29
#define P4_Z_ENABLE 30
#define P4_Z_ENABLE_OFF 0
#define P4_ENCODER_A 3
#define P4_ENCODER_B 2
#define P4_ENCODER_CPR 1200 // effective counts per revolution with x2 decoding
#define P4_IO_LDO_CHANNEL 4
#define P4_IO_LDO_MV 3300
