#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef DSP_AUDIO_PIN
#define DSP_AUDIO_PIN 18
#endif
#ifndef DSP_AUDIO_RATE
#define DSP_AUDIO_RATE 22050
#endif
#ifndef DSP_AUDIO_DMA_IRQ
#define DSP_AUDIO_DMA_IRQ 1  // DMA_IRQ_1, on core 0 (PicoDVI uses DMA_IRQ_0 on core 1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Starts the PWM output; returns the exact sample rate.
int audio_pwm_init(void);
int audio_pwm_rate(void);
void audio_pwm_push(const int16_t* samples, size_t count);
size_t audio_pwm_queued(void);

#ifdef __cplusplus
}
#endif
