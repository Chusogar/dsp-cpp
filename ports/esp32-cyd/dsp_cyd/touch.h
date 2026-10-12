#pragma once

// XPT2046 resistive touch controller of the CYD, on its own pins (CLK 25,
// MOSI 32, MISO 39, CS 33, IRQ 36), read by bit-banging.
namespace cyd {

struct TouchCalibration {
    int x_min = 200, x_max = 3700;   // raw readings at the screen edges
    int y_min = 240, y_max = 3800;
    bool swap_xy = false;
    bool flip_x = false, flip_y = false;
};

void touch_begin(const TouchCalibration& cal);
// True while the screen is pressed; `x`, `y` in screen pixels (320x240).
bool touch_read(int* x, int* y);
// Last raw reading (for calibration, shown in the serial log).
void touch_raw(int* x, int* y);

}  // namespace cyd
