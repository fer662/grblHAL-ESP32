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
void p4_feedback_init(void)
{
    x_counter = counter(P4_X_STEP, -1, false);
    z_counter = counter(P4_Z_STEP, -1, false);
    // x2 A/B decoding; effective counts/revolution belong to the board configuration.
    spindle_counter = counter(P4_ENCODER_A, P4_ENCODER_B, true);
}
void p4_feedback_read(int *x_pulses, int *z_pulses, int *encoder)
{
    ESP_ERROR_CHECK(pcnt_unit_get_count(x_counter, x_pulses));
    ESP_ERROR_CHECK(pcnt_unit_get_count(z_counter, z_pulses));
    ESP_ERROR_CHECK(pcnt_unit_get_count(spindle_counter, encoder));
}

int32_t IRAM_ATTR p4_encoder_count(void)
{
    int count = 0;
    if (spindle_counter) pcnt_unit_get_count(spindle_counter, &count);
    return count;
}
