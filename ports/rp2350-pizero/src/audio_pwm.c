// Mono PWM sound for the RP2350-PiZero port: a 10-bit PWM on one GPIO
// (DSP_AUDIO_PIN, GPIO18 = pin 12 of the 40-pin header by default; add an RC
// low-pass filter, e.g. 1 kOhm + 10 nF, before an amplifier). Two DMA
// buffers paced by a DMA timer feed the PWM level; their interrupt refills
// them from a ring buffer that the emulator writes.
#include "audio_pwm.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"

#define RING_SIZE 8192u  // samples, power of two
#define DMA_SAMPLES 256u
#define PWM_WRAP 1023u

static int16_t g_ring[RING_SIZE];
static volatile uint32_t g_read = 0, g_write = 0;
static uint32_t g_dma_buf[2][DMA_SAMPLES] __attribute__((aligned(4)));
static int g_chan[2] = {-1, -1};
static uint g_slice, g_channel;
static int g_rate = 0;
static int16_t g_last = 0;

static void fill(uint32_t* buf) {
    uint32_t r = g_read;
    const uint32_t w = g_write;
    for (unsigned i = 0; i < DMA_SAMPLES; ++i) {
        if (r != w) {
            g_last = g_ring[r & (RING_SIZE - 1)];
            ++r;
        }
        const uint32_t level = ((uint32_t)(g_last + 32768) * (PWM_WRAP + 1)) >> 16;
        // The DMA writes the whole compare register: put the level on the
        // channel in use (A = low half, B = high half).
        buf[i] = g_channel == PWM_CHAN_A ? level : level << 16;
    }
    g_read = r;
}

static void __isr dma_handler(void) {
    for (int i = 0; i < 2; ++i) {
        if (dma_irqn_get_channel_status(DSP_AUDIO_DMA_IRQ, (uint)g_chan[i])) {
            dma_irqn_acknowledge_channel(DSP_AUDIO_DMA_IRQ, (uint)g_chan[i]);
            fill(g_dma_buf[i]);
            dma_channel_set_read_addr((uint)g_chan[i], g_dma_buf[i], false);
        }
    }
}

int audio_pwm_init(void) {
    gpio_set_function(DSP_AUDIO_PIN, GPIO_FUNC_PWM);
    g_slice = pwm_gpio_to_slice_num(DSP_AUDIO_PIN);
    g_channel = pwm_gpio_to_channel(DSP_AUDIO_PIN);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, PWM_WRAP);
    pwm_init(g_slice, &cfg, true);
    pwm_set_chan_level(g_slice, g_channel, (PWM_WRAP + 1) / 2);

    // Sample clock: sys_clk / den with a DMA pacing timer (den <= 65535).
    const uint32_t sys = clock_get_hz(clk_sys);
    uint32_t den = (sys + DSP_AUDIO_RATE / 2) / DSP_AUDIO_RATE;
    if (den > 65535) den = 65535;
    g_rate = (int)(sys / den);
    const int timer = dma_claim_unused_timer(true);
    dma_timer_set_fraction((uint)timer, 1, (uint16_t)den);

    g_chan[0] = dma_claim_unused_channel(true);
    g_chan[1] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; ++i) {
        fill(g_dma_buf[i]);
        dma_channel_config c = dma_channel_get_default_config((uint)g_chan[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, dma_get_timer_dreq((uint)timer));
        channel_config_set_chain_to(&c, (uint)g_chan[i ^ 1]);
        dma_channel_configure((uint)g_chan[i], &c, &pwm_hw->slice[g_slice].cc, g_dma_buf[i], DMA_SAMPLES,
                              false);
        dma_irqn_set_channel_enabled(DSP_AUDIO_DMA_IRQ, (uint)g_chan[i], true);
    }
    irq_add_shared_handler(dma_get_irq_num(DSP_AUDIO_DMA_IRQ), dma_handler,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(dma_get_irq_num(DSP_AUDIO_DMA_IRQ), true);
    dma_channel_start((uint)g_chan[0]);
    return g_rate;
}

int audio_pwm_rate(void) { return g_rate; }

void audio_pwm_push(const int16_t* samples, size_t count) {
    uint32_t w = g_write;
    for (size_t i = 0; i < count; ++i) {
        if (w - g_read >= RING_SIZE) break;  // full: drop the rest
        g_ring[w & (RING_SIZE - 1)] = samples[i];
        ++w;
    }
    g_write = w;
}

size_t audio_pwm_queued(void) { return (size_t)(g_write - g_read); }
