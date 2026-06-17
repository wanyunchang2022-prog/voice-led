#pragma once

#include <stdint.h>

#define SPK_BCK_PIN     GPIO_NUM_10
#define SPK_WS_PIN      GPIO_NUM_11
#define SPK_DIN_PIN     GPIO_NUM_13

void audio_init(void);
void audio_play_beep(int freq_hz, int duration_ms, int volume_pct);
void audio_play_wake_prompt(void);
void audio_play_cmd_confirm(void);
void audio_play_startup(void);
