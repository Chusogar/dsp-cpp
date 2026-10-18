#pragma once

// Wii Nunchuk on I2C, wired as galagino does on the CYD: the CN1 connector,
// SDA GPIO22, SCL GPIO27, 3.3 V and ground.
namespace cyd {

struct NunchukState {
    bool connected = false;
    bool up = false, down = false, left = false, right = false;
    bool z = false, c = false;
};

void nunchuk_begin(int sda, int scl, int threshold);
// Latest state (one I2C read; reconnects once a second while unplugged).
NunchukState nunchuk_read();

}  // namespace cyd
