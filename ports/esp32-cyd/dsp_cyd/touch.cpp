#include "touch.h"

#include <Arduino.h>

#include "src/gen/dsp_port_config.h"

#ifndef DSP_TOUCH_CLK
#define DSP_TOUCH_CLK 25
#endif
#ifndef DSP_TOUCH_MOSI
#define DSP_TOUCH_MOSI 32
#endif
#ifndef DSP_TOUCH_MISO
#define DSP_TOUCH_MISO 39
#endif
#ifndef DSP_TOUCH_CS
#define DSP_TOUCH_CS 33
#endif
#ifndef DSP_TOUCH_IRQ
#define DSP_TOUCH_IRQ 36
#endif

namespace cyd {

namespace {

TouchCalibration g_cal;
int g_raw_x = 0, g_raw_y = 0;

// One 12-bit conversion: command byte out, 16 clocks in.
int transfer(uint8_t command) {
    for (int i = 7; i >= 0; --i) {
        digitalWrite(DSP_TOUCH_MOSI, (command >> i) & 1);
        digitalWrite(DSP_TOUCH_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(DSP_TOUCH_CLK, LOW);
        delayMicroseconds(1);
    }
    int value = 0;
    for (int i = 0; i < 16; ++i) {
        digitalWrite(DSP_TOUCH_CLK, HIGH);
        delayMicroseconds(1);
        value = (value << 1) | digitalRead(DSP_TOUCH_MISO);
        digitalWrite(DSP_TOUCH_CLK, LOW);
        delayMicroseconds(1);
    }
    return (value >> 3) & 0x0fff;
}

int scale(int v, int lo, int hi, int size) {
    if (hi == lo) return 0;
    const int s = (v - lo) * size / (hi - lo);
    return s < 0 ? 0 : (s >= size ? size - 1 : s);
}

}  // namespace

void touch_begin(const TouchCalibration& cal) {
    g_cal = cal;
    pinMode(DSP_TOUCH_CLK, OUTPUT);
    pinMode(DSP_TOUCH_MOSI, OUTPUT);
    pinMode(DSP_TOUCH_CS, OUTPUT);
    pinMode(DSP_TOUCH_MISO, INPUT);
    pinMode(DSP_TOUCH_IRQ, INPUT);
    digitalWrite(DSP_TOUCH_CS, HIGH);
    digitalWrite(DSP_TOUCH_CLK, LOW);
}

bool touch_read(int* x, int* y) {
    if (digitalRead(DSP_TOUCH_IRQ) != LOW) return false;
    digitalWrite(DSP_TOUCH_CS, LOW);
    int sx = 0, sy = 0;
    transfer(0xd1);  // discard the first (settling) conversions
    for (int i = 0; i < 4; ++i) {
        sx += transfer(0xd1);
        sy += transfer(0x91);
    }
    const int z1 = transfer(0xb1);
    transfer(0x90);  // power down, keep IRQ enabled
    digitalWrite(DSP_TOUCH_CS, HIGH);
    if (z1 < 100) return false;  // too light: not a real press
    g_raw_x = sx / 4;
    g_raw_y = sy / 4;
    int rx = g_raw_x, ry = g_raw_y;
    if (g_cal.swap_xy) std::swap(rx, ry);
    int px = scale(rx, g_cal.x_min, g_cal.x_max, 320);
    int py = scale(ry, g_cal.y_min, g_cal.y_max, 240);
    if (g_cal.flip_x) px = 319 - px;
    if (g_cal.flip_y) py = 239 - py;
    *x = px;
    *y = py;
    return true;
}

void touch_raw(int* x, int* y) {
    *x = g_raw_x;
    *y = g_raw_y;
}

}  // namespace cyd
