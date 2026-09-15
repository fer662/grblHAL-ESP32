/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grbl/grbllib.h"
#include "esp_log.h"
#include "bridge.h"
#include "storage.h"
#include "network.h"
static void controller(void *arg)
{
    grbl_enter();
    vTaskDelete(NULL);
}
void app_main(void)
{
    // UART0 carries the grbl protocol. Concurrent SDK logs would corrupt it.
    esp_log_level_set("*", ESP_LOG_NONE);
    h5_storage_init();
    ESP_LOGI("H5_P4", "grblHAL: %s", H5_BENCH_ONLY ? "bench enables locked" : "axis controls enabled");
    h5_network_start();
    configASSERT(xTaskCreatePinnedToCore(controller, "grblHAL", 16384, NULL, 5, NULL, 1) == pdPASS);
    h5_ui_start();
}
