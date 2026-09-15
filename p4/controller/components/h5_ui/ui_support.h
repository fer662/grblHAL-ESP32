#pragma once
#include <string>
#include <cstdio>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include "esp_timer.h"
using String = std::string;
inline unsigned long millis() { return esp_timer_get_time() / 1000; }
inline String format_decimal(double value, int digits) {
    char text[48]; snprintf(text, sizeof(text), "%.*f", digits, value); return text;
}
