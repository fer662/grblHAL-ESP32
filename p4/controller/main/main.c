/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grbl/grbllib.h"
#include "esp_log.h"
static void controller(void *arg)
{
    grbl_enter();
    vTaskDelete(NULL);
}
void app_main(void)
{
    // No NVS initialization, erase or writes in the bench build.
    ESP_LOGI("H5_P4", "grblHAL bench build: driver enables locked inactive");
    configASSERT(xTaskCreatePinnedToCore(controller, "grblHAL", 16384, NULL, 5, NULL, 1) == pdPASS);
}
