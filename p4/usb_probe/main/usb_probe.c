/* SPDX-License-Identifier: GPL-3.0-or-later
 * Waveshare P4 USB installation probe. Not a grblHAL motion driver.
 * No stepping, spindle control, Wi-Fi, display initialization or NVS writes.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_ldo_regulator.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define X_ENABLE GPIO_NUM_47
#define Z_ENABLE GPIO_NUM_30

static esp_ldo_channel_handle_t io_ldo4, io_ldo5;

static void disable_motion_outputs(void)
{
    // Latch inactive enable levels before switching the pins to outputs.
    ESP_ERROR_CHECK(gpio_set_level(X_ENABLE, 1)); // H5 X: active low
    ESP_ERROR_CHECK(gpio_set_level(Z_ENABLE, 0)); // H5 Z: active high
    gpio_config_t enable = {
        .pin_bit_mask = (1ULL << X_ENABLE) | (1ULL << Z_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&enable));
    esp_ldo_channel_config_t ldo4 = {.chan_id = 4, .voltage_mv = 3300};
    esp_ldo_channel_config_t ldo5 = {.chan_id = 5, .voltage_mv = 3300};
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo4, &io_ldo4));
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo5, &io_ldo5));
    // STEP and DIR pins deliberately remain inputs. No pulse source exists.
}

static void report_info(void)
{
    esp_chip_info_t chip;
    uint32_t flash_size = 0;
    esp_chip_info(&chip);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));
    printf("H5_P4_USB_PROBE version=0.1.0 idf=%s cores=%u revision=%u "
           "flash_bytes=%" PRIu32 " reset=%d uptime_ms=%" PRId64
           " motion=DISABLED grblhal=NOT_STARTED\n",
           esp_get_idf_version(), chip.cores, chip.revision, flash_size,
           (int)esp_reset_reason(), esp_timer_get_time() / 1000);
    fflush(stdout);
}

void app_main(void)
{
    disable_motion_outputs();
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
    report_info();
    printf("Commands: PING, INFO. Motor power must remain off during this probe.\n");
    char command[32];
    size_t used = 0;
    bool overflow = false;
    int64_t heartbeat = esp_timer_get_time();
    for (;;) {
        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100)) == 1) {
            if (ch == '\n' || ch == '\r') {
                if (used || overflow) {
                    command[used] = '\0';
                    if (!overflow && strcmp(command, "PING") == 0)
                        puts("PONG H5_P4_USB_PROBE");
                    else if (!overflow && strcmp(command, "INFO") == 0)
                        report_info();
                    else
                        puts("ERROR supported_commands=PING,INFO");
                    fflush(stdout);
                    used = 0;
                    overflow = false;
                }
            } else if (used < sizeof(command) - 1) {
                command[used++] = (char)ch;
            } else {
                overflow = true;
            }
        }
        if (esp_timer_get_time() - heartbeat >= 2000000) {
            heartbeat = esp_timer_get_time();
            report_info();
        }
    }
}
