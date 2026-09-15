/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grbl/grbllib.h"
#include "esp_log.h"
#include "bridge.h"
static void controller(void *arg)
{
    grbl_enter();
    vTaskDelete(NULL);
}
void app_main(void)
{
    // UART0 carries the grbl protocol. Concurrent SDK logs would corrupt it.
    esp_log_level_set("*", ESP_LOG_NONE);
    // No NVS initialization, erase or writes in the bench build.
    ESP_LOGI("H5_P4", "grblHAL bench build: driver enables locked inactive");
    configASSERT(xTaskCreatePinnedToCore(controller, "grblHAL", 16384, NULL, 5, NULL, 1) == pdPASS);
    h5_ui_start();
}
