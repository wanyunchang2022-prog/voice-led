#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_sr.h"

#include "led_control.h"
#include "audio_output.h"

static const char *TAG = "voice_led";

// Pin Definitions — INMP441 I2S microphone
#define I2S_BCK_PIN     GPIO_NUM_4
#define I2S_WS_PIN      GPIO_NUM_5
#define I2S_DIN_PIN     GPIO_NUM_6

// Audio parameters
#define SAMPLE_RATE     16000

// ESP-SR audio buffer: 30ms of 16kHz 16-bit mono
#define SR_FRAME_MS     30
#define SR_FRAME_LEN    (SAMPLE_RATE / 1000 * SR_FRAME_MS)

// Command IDs (matched to set_word_list order)
enum {
    CMD_TURN_ON,
    CMD_TURN_OFF,
    CMD_RED,
    CMD_GREEN,
    CMD_BLUE,
    CMD_WHITE,
    CMD_YELLOW,
    CMD_PURPLE,
    CMD_BRIGHT_UP,
    CMD_BRIGHT_DOWN,
    CMD_MAX_BRIGHT,
    CMD_MIN_BRIGHT,
    CMD_RAINBOW,
    CMD_BREATH,
    CMD_COUNT
};

static const char *CMD_WORDS[CMD_COUNT] = {
    "开灯",
    "关灯",
    "红灯",
    "绿灯",
    "蓝灯",
    "白色",
    "黄色",
    "紫色",
    "亮度加",
    "亮度减",
    "最亮",
    "最暗",
    "彩虹",
    "呼吸",
};

// ESP-SR model handles
static model_iface_data_t *wakenet = NULL;
static model_iface_data_t *multinet = NULL;
static esp_sr_iface_t *wn_iface = NULL;
static esp_sr_iface_t *mn_iface = NULL;

// I2S handles
static i2s_chan_handle_t rx_handle = NULL;

// Voice control state
static bool listening = false;
static int cmd_timeout = 0;

// ── I2S Microphone (INMP441) ──────────────────────────────────────────────

static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S mic ready @ %d Hz", SAMPLE_RATE);
}

// ── ESP-SR (Offline Chinese Speech Recognition) ──────────────────────────

static void sr_init(void)
{
    srmodel_list_t *models = esp_sr_zh_init(SR_LANGUAGE_ZH_CN, NULL, 0);
    if (models == NULL) {
        ESP_LOGE(TAG, "esp_sr_zh_init failed — check partition table / model files");
        return;
    }

    for (int i = 0; i < models->num; i++) {
        esp_sr_iface_t *iface = (esp_sr_iface_t *)models->model[i]->interface;
        if (strcmp(iface->func_name, WAKENET_FUNCTION_NAME) == 0) {
            wakenet = models->model[i];
            wn_iface = iface;
            wn_iface->create(wakenet, NULL, 0);
            ESP_LOGI(TAG, "WakeNet: %s", wn_iface->func_name);
        }
        if (strcmp(iface->func_name, MULTINET_FUNCTION_NAME) == 0) {
            multinet = models->model[i];
            mn_iface = iface;
            mn_iface->create(multinet, NULL, 0);
            ESP_LOGI(TAG, "MultiNet: %s", mn_iface->func_name);
        }
    }

    if (!wakenet || !multinet) {
        ESP_LOGE(TAG, "WakeNet or MultiNet model not found");
        return;
    }

    mn_iface->set_word_list(multinet, CMD_COUNT, (char **)CMD_WORDS);
    ESP_LOGI(TAG, "SR ready: %d commands loaded", CMD_COUNT);
}

// ── Command Handler ───────────────────────────────────────────────────────

static void handle_command(int id)
{
    ESP_LOGI(TAG, "Execute: %s", CMD_WORDS[id]);
    switch (id) {
        case CMD_TURN_ON:     led_turn_on(); break;
        case CMD_TURN_OFF:    led_turn_off(); break;
        case CMD_RED:         led_set_color(255, 0, 0); break;
        case CMD_GREEN:       led_set_color(0, 255, 0); break;
        case CMD_BLUE:        led_set_color(0, 0, 255); break;
        case CMD_WHITE:       led_set_color(255, 255, 255); break;
        case CMD_YELLOW:      led_set_color(255, 255, 0); break;
        case CMD_PURPLE:      led_set_color(128, 0, 128); break;
        case CMD_BRIGHT_UP:   led_brightness_up(); break;
        case CMD_BRIGHT_DOWN: led_brightness_down(); break;
        case CMD_MAX_BRIGHT:  led_set_brightness(LED_BRIGHTNESS_MAX); break;
        case CMD_MIN_BRIGHT:  led_set_brightness(10); break;
        case CMD_RAINBOW:     led_set_effect(LED_EFFECT_RAINBOW); break;
        case CMD_BREATH:      led_set_effect(LED_EFFECT_BREATH); break;
        default:              ESP_LOGW(TAG, "Unknown cmd %d", id); break;
    }
}

// ── Voice Control Task ────────────────────────────────────────────────────

static void voice_task(void *arg)
{
    int16_t *buf = heap_caps_malloc(SR_FRAME_LEN * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL);
    assert(buf);

    ESP_LOGI(TAG, "Say '嗨乐鑫' to wake up...");

    while (1) {
        size_t bytes = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, buf,
                                         SR_FRAME_LEN * sizeof(int16_t),
                                         &bytes, pdMS_TO_TICKS(200));
        if (ret != ESP_OK || bytes < SR_FRAME_LEN * sizeof(int16_t)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        int samples = bytes / sizeof(int16_t);

        if (!listening) {
            // ── Wake word detection ──
            int r = wn_iface->detect(wakenet, buf, samples);
            if (r > 0) {
                ESP_LOGI(TAG, "Wake word detected!");
                listening = true;
                cmd_timeout = 100;

                // Audio + LED feedback
                audio_play_wake_prompt();
                for (int i = 0; i < 2; i++) {
                    led_set_color(0, 255, 0);
                    vTaskDelay(pdMS_TO_TICKS(80));
                    led_turn_off();
                    vTaskDelay(pdMS_TO_TICKS(80));
                }
                led_set_color(0, 255, 0);
                ESP_LOGI(TAG, "Listening for command...");
            }
        } else {
            // ── Command recognition ──
            int score = 0;
            int cmd_id = mn_iface->detect_with_score(multinet, buf, samples, &score);
            if (cmd_id >= 0 && score > 50) {
                ESP_LOGI(TAG, "CMD: %s (score=%d)", CMD_WORDS[cmd_id], score);

                // Audio + LED feedback
                audio_play_cmd_confirm();
                led_set_color(0, 0, 255);
                vTaskDelay(pdMS_TO_TICKS(80));

                handle_command(cmd_id);
                listening = false;
                ESP_LOGI(TAG, "Back to wake word mode");
                continue;
            }

            if (--cmd_timeout <= 0) {
                listening = false;
                ESP_LOGI(TAG, "Command timeout — back to wake word");
            }
        }

        led_task_tick();
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    free(buf);
    vTaskDelete(NULL);
}

// ── Entry Point ───────────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "Voice-Controlled LED Strip starting...");

    ESP_ERROR_CHECK(nvs_flash_init());

    led_init();
    i2s_init();
    audio_init();
    sr_init();

    // Boot confirmation: audio + LED
    audio_play_startup();
    led_set_color(0, 255, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    led_set_color(0, 0, 255);
    vTaskDelay(pdMS_TO_TICKS(200));
    led_set_color(255, 255, 255);
    vTaskDelay(pdMS_TO_TICKS(200));
    led_turn_off();

    xTaskCreatePinnedToCore(voice_task, "voice_ctrl", 8192, NULL, 5, NULL, 1);
}
