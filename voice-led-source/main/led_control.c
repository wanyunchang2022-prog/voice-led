#include "led_control.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_ctrl";

static led_strip_handle_t led_strip;
static led_state_t state = {
    .r = 255,
    .g = 255,
    .b = 255,
    .brightness = 80,
    .on = false,
    .effect = LED_EFFECT_NONE,
};

static uint32_t hue = 0;
static bool breath_dir_up = true;

void led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_NUM,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip));
    led_turn_off();
    ESP_LOGI(TAG, "LED strip initialized (%d LEDs)", LED_NUM);
}

static void led_update(void)
{
    if (!state.on) {
        for (int i = 0; i < LED_NUM; i++) {
            led_strip_set_pixel(led_strip, i, 0, 0, 0);
        }
        led_strip_refresh(led_strip);
        return;
    }

    uint8_t r = state.r * state.brightness / LED_BRIGHTNESS_MAX;
    uint8_t g = state.g * state.brightness / LED_BRIGHTNESS_MAX;
    uint8_t b = state.b * state.brightness / LED_BRIGHTNESS_MAX;

    for (int i = 0; i < LED_NUM; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
    led_strip_refresh(led_strip);
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    state.r = r;
    state.g = g;
    state.b = b;
    state.effect = LED_EFFECT_NONE;
    if (!state.on) {
        state.on = true;
    }
    led_update();
    ESP_LOGI(TAG, "Color set to R:%d G:%d B:%d", r, g, b);
}

void led_set_brightness(uint8_t level)
{
    if (level > LED_BRIGHTNESS_MAX) level = LED_BRIGHTNESS_MAX;
    state.brightness = level;
    led_update();
    ESP_LOGI(TAG, "Brightness set to %d", level);
}

void led_turn_on(void)
{
    state.on = true;
    led_update();
    ESP_LOGI(TAG, "LED on");
}

void led_turn_off(void)
{
    state.on = false;
    state.effect = LED_EFFECT_NONE;
    led_update();
    ESP_LOGI(TAG, "LED off");
}

void led_toggle(void)
{
    if (state.on) {
        led_turn_off();
    } else {
        led_turn_on();
    }
}

void led_brightness_up(void)
{
    uint8_t new = state.brightness + 10;
    if (new > LED_BRIGHTNESS_MAX) new = LED_BRIGHTNESS_MAX;
    led_set_brightness(new);
}

void led_brightness_down(void)
{
    int16_t new = (int16_t)state.brightness - 10;
    if (new < 0) new = 0;
    led_set_brightness((uint8_t)new);
}

void led_set_effect(led_effect_t effect)
{
    state.effect = effect;
    if (!state.on) state.on = true;
    ESP_LOGI(TAG, "Effect set to %d", effect);
}

static uint32_t hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v)
{
    uint8_t r, g, b;
    uint16_t hi = (h / 60) % 6;
    uint16_t f = h % 60;
    uint8_t p = v * (255 - s) / 255;
    uint8_t q = v * (255 - s * f / 60) / 255;
    uint8_t t = v * (255 - s * (60 - f) / 60) / 255;

    switch (hi) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }

    return (r << 16) | (g << 8) | b;
}

void led_task_tick(void)
{
    if (!state.on) return;

    switch (state.effect) {
        case LED_EFFECT_NONE:
            break;

        case LED_EFFECT_RAINBOW: {
            for (int i = 0; i < LED_NUM; i++) {
                uint32_t rgb = hsv_to_rgb((hue + i * 360 / LED_NUM) % 360, 255, 255);
                uint8_t r = ((rgb >> 16) & 0xFF) * state.brightness / LED_BRIGHTNESS_MAX;
                uint8_t g = ((rgb >> 8) & 0xFF) * state.brightness / LED_BRIGHTNESS_MAX;
                uint8_t b = (rgb & 0xFF) * state.brightness / LED_BRIGHTNESS_MAX;
                led_strip_set_pixel(led_strip, i, r, g, b);
            }
            led_strip_refresh(led_strip);
            hue = (hue + 1) % 360;
            break;
        }

        case LED_EFFECT_BREATH: {
            static uint8_t breath_val = 0;
            if (breath_dir_up) {
                breath_val += 2;
                if (breath_val >= LED_BRIGHTNESS_MAX) {
                    breath_val = LED_BRIGHTNESS_MAX;
                    breath_dir_up = false;
                }
            } else {
                if (breath_val < 2) {
                    breath_val = 0;
                    breath_dir_up = true;
                } else {
                    breath_val -= 2;
                }
            }
            uint8_t br = state.brightness * breath_val / LED_BRIGHTNESS_MAX;
            uint8_t r = state.r * br / LED_BRIGHTNESS_MAX;
            uint8_t g = state.g * br / LED_BRIGHTNESS_MAX;
            uint8_t b = state.b * br / LED_BRIGHTNESS_MAX;
            for (int i = 0; i < LED_NUM; i++) {
                led_strip_set_pixel(led_strip, i, r, g, b);
            }
            led_strip_refresh(led_strip);
            break;
        }
    }
}
