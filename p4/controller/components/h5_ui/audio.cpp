/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>
#include <math.h>
static QueueHandle_t commands;
static std::atomic<bool> available{false}, initialized{false};
struct tone {
    unsigned hz, ms;
};
static void audio_task(void *)
{
    auto speaker = bsp_audio_codec_speaker_init();
    esp_codec_dev_sample_info_t format = {};
    format.sample_rate = 16000;
    format.channel = 1;
    format.bits_per_sample = 16;
    if (!speaker || esp_codec_dev_open(speaker, &format) != ESP_CODEC_DEV_OK) {
        initialized = true;
        vTaskDelete(nullptr);
        return;
    }
    esp_codec_dev_set_out_vol(speaker, 40);
    available = true;
    initialized = true;
    tone current = {};
    unsigned remaining = 0, phase = 0;
    int16_t samples[256];
    for (;;) {
        tone incoming;
        if (xQueueReceive(commands, &incoming, current.hz ? 0 : portMAX_DELAY) == pdTRUE) {
            current = incoming;
            remaining = current.ms * 16;
            phase = 0;
        }
        if (!current.hz)
            continue;
        for (unsigned i = 0; i < 256; i++) {
            float envelope = current.ms ? fminf(1, remaining / 128.0f) : 1;
            samples[i] =
                current.ms && !remaining
                    ? 0
                    : (int16_t)(2500 * envelope * sinf(2 * M_PI * current.hz * (phase++ % 16000) / 16000));
            if (remaining)
                remaining--;
        }
        if (esp_codec_dev_write(speaker, samples, sizeof(samples)) != ESP_CODEC_DEV_OK) {
            available = false;
            current.hz = 0;
        }
        if (current.ms && !remaining)
            current.hz = 0;
    }
}
extern "C" void h5_audio_start(void)
{
    commands = xQueueCreate(1, sizeof(tone));
    if (!commands || xTaskCreatePinnedToCore(audio_task, "H5_audio", 4096, nullptr, 1, nullptr, 0) != pdPASS)
        initialized = true;
}
extern "C" void h5_audio_tone(unsigned hz, unsigned ms)
{
    if (commands) {
        tone command = {hz, ms};
        xQueueOverwrite(commands, &command);
    }
}
extern "C" bool h5_audio_ready(void)
{
    return available.load();
}

extern "C" bool h5_audio_initialized(void) { return initialized.load(); }
