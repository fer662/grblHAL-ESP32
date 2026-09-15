/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "esp_attr.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "feedback.h"

static pcnt_unit_handle_t x_counter, z_counter, spindle_counter;
static pcnt_unit_handle_t counter(int pulse, int direction, bool quadrature)
{
    pcnt_unit_handle_t unit;
    pcnt_unit_config_t config = {
        .high_limit = 30000, .low_limit = -30000, .flags.accum_count = true,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&config, &unit));
    pcnt_chan_config_t channel_config = {
        .edge_gpio_num = pulse, .level_gpio_num = direction,
        .flags.io_loop_back = !quadrature,
    };
    pcnt_channel_handle_t channel;
    ESP_ERROR_CHECK(pcnt_new_channel(unit, &channel_config, &channel));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        quadrature ? PCNT_CHANNEL_EDGE_ACTION_DECREASE : PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        quadrature ? PCNT_CHANNEL_LEVEL_ACTION_INVERSE : PCNT_CHANNEL_LEVEL_ACTION_KEEP));
    // Accumulator extends the hardware counter across limits without read/clear races.
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(unit, 30000));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(unit, -30000));
    ESP_ERROR_CHECK(pcnt_unit_enable(unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unit));
    ESP_ERROR_CHECK(pcnt_unit_start(unit));
    return unit;
}
void h5_feedback_init(void)
{
    x_counter = counter(H5_X_STEP, -1, false);
    z_counter = counter(H5_Z_STEP, -1, false);
    // Same x2 A/B decoding and 1200 effective counts/rev as committed H5.
    spindle_counter = counter(H5_ENCODER_A, H5_ENCODER_B, true);
}
void h5_feedback_read(int *x_pulses, int *z_pulses, int *encoder)
{
    ESP_ERROR_CHECK(pcnt_unit_get_count(x_counter, x_pulses));
    ESP_ERROR_CHECK(pcnt_unit_get_count(z_counter, z_pulses));
    ESP_ERROR_CHECK(pcnt_unit_get_count(spindle_counter, encoder));
}

static void encoder_edge(int pin, int level)
{
    gpio_set_level(pin, level);
    esp_rom_delay_us(2);
}
static void revolutions(bool forward)
{
    // 16000 quadrature cycles = 32000 counts, crossing the 30000 limit.
    for (unsigned i = 0; i < 16000; i++) {
        if (forward) {
            encoder_edge(H5_ENCODER_B, 1);
            encoder_edge(H5_ENCODER_A, 1);
            encoder_edge(H5_ENCODER_B, 0);
            encoder_edge(H5_ENCODER_A, 0);
        } else {
            encoder_edge(H5_ENCODER_A, 1);
            encoder_edge(H5_ENCODER_B, 1);
            encoder_edge(H5_ENCODER_A, 0);
            encoder_edge(H5_ENCODER_B, 0);
        }
    }
}
bool h5_feedback_selftest(void)
{
    gpio_set_level(H5_ENCODER_A, 0);
    gpio_set_level(H5_ENCODER_B, 0);
    ESP_ERROR_CHECK(gpio_set_direction(H5_ENCODER_A, GPIO_MODE_INPUT_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(H5_ENCODER_B, GPIO_MODE_INPUT_OUTPUT));
    esp_rom_delay_us(10);
    int start, forward, reverse, final;
    ESP_ERROR_CHECK(pcnt_unit_get_count(spindle_counter, &start));
    revolutions(true);
    ESP_ERROR_CHECK(pcnt_unit_get_count(spindle_counter, &forward));
    revolutions(false);
    revolutions(false);
    ESP_ERROR_CHECK(pcnt_unit_get_count(spindle_counter, &reverse));
    revolutions(true);
    ESP_ERROR_CHECK(pcnt_unit_get_count(spindle_counter, &final));
    ESP_ERROR_CHECK(gpio_set_direction(H5_ENCODER_A, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(H5_ENCODER_B, GPIO_MODE_INPUT));
    return forward - start == 32000 && reverse - start == -32000 && final == start;
}

int32_t IRAM_ATTR h5_encoder_count(void)
{
    int count = 0;
    if (spindle_counter) pcnt_unit_get_count(spindle_counter, &count);
    return count;
}
