#pragma once

#include <stdint.h>
#include <stdbool.h>

#define LED_STRIP_GPIO      GPIO_NUM_48
#define LED_NUM             30
#define LED_BRIGHTNESS_MAX  100

typedef enum {
    LED_EFFECT_NONE,
    LED_EFFECT_RAINBOW,
    LED_EFFECT_BREATH,
} led_effect_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t brightness;
    bool    on;
    led_effect_t effect;
} led_state_t;

void led_init(void);
void led_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_set_brightness(uint8_t level);
void led_turn_on(void);
void led_turn_off(void);
void led_toggle(void);
void led_brightness_up(void);
void led_brightness_down(void);
void led_set_effect(led_effect_t effect);
void led_task_tick(void);
