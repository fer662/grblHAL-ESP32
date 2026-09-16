/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grbl/grbllib.h"
#include "storage.h"
#include "esp_log.h"
static void controller(void *arg)
{
    grbl_enter();
    vTaskDelete(NULL);
}
void app_main(void)
{
    // UART0 is exclusively the grbl protocol after application startup.
    esp_log_level_set("*", ESP_LOG_NONE);
    p4_storage_init();
    configASSERT(xTaskCreatePinnedToCore(controller, "grblHAL", 16384, NULL, 5, NULL, 1) == pdPASS);
}
