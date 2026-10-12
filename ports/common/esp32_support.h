#pragma once

// Pieces shared by the ESP32 ports (ttgo-vga32, esp32-cyd), on the
// Arduino-ESP32 2.0.x core (ESP-IDF 4.4).

#include <cstddef>
#include <cstdint>

#include "core/embedded_media.h"

namespace dsp {
namespace port {
namespace esp32 {

// Sound through the built-in DAC with I2S0: `dac_gpio` is 25 (DAC1, TTGO
// VGA32) or 26 (DAC2, CYD speaker amplifier). A task on core 0 feeds the I2S
// DMA from a ring buffer. use_apll must stay false when FabGL drives VGA.
void audio_begin(int rate, int dac_gpio);
void audio_push(const int16_t* samples, size_t count);
size_t audio_queued();

// Serves the embedded media read-only under `prefix` ("/embedded") through
// the ESP-IDF VFS.
bool mount_embedded(const DspEmbeddedFile* files, size_t count, const char* prefix);

}  // namespace esp32
}  // namespace port
}  // namespace dsp
