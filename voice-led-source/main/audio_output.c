#include "audio_output.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "audio";

#define SAMPLE_RATE     22050

static i2s_chan_handle_t tx_handle = NULL;

static void generate_sine(int16_t *buf, int samples, int freq_hz, int volume)
{
    float phase = 0;
    float inc  = 2.0f * 3.14159265f * freq_hz / SAMPLE_RATE;
    int    amp  = volume * 32767 / 100;

    for (int i = 0; i < samples; i++) {
        buf[i] = (int16_t)(sinf(phase) * amp);
        phase += inc;
        if (phase > 2.0f * 3.14159265f) phase -= 2.0f * 3.14159265f;
    }
}

void audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCK_PIN,
            .ws   = SPK_WS_PIN,
            .dout = SPK_DIN_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_LOGI(TAG, "MAX98357 ready @ %d Hz", SAMPLE_RATE);
}

void audio_play_beep(int freq_hz, int duration_ms, int volume_pct)
{
    int samples = SAMPLE_RATE * duration_ms / 1000;
    int16_t *buf = heap_caps_malloc(samples * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL);
    if (!buf) return;

    generate_sine(buf, samples, freq_hz, volume_pct);

    for (int i = 0; i < 120 && i < samples / 2; i++) {
        buf[i]               = buf[i] * i / 120;
        buf[samples - 1 - i] = buf[samples - 1 - i] * i / 120;
    }

    size_t written = 0;
    i2s_channel_write(tx_handle, buf, samples * sizeof(int16_t), &written, portMAX_DELAY);
    free(buf);
}

void audio_play_wake_prompt(void)
{
    audio_play_beep(880, 100, 40);
    vTaskDelay(pdMS_TO_TICKS(60));
    audio_play_beep(1320, 120, 40);
}

void audio_play_cmd_confirm(void)
{
    audio_play_beep(1100, 80, 35);
}

void audio_play_startup(void)
{
    audio_play_beep(440, 150, 30);
    vTaskDelay(pdMS_TO_TICKS(80));
    audio_play_beep(660, 150, 30);
    vTaskDelay(pdMS_TO_TICKS(80));
    audio_play_beep(880, 200, 35);
}
