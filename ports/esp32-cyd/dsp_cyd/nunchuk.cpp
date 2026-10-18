// Wii Nunchuk (and clones) over I2C at address 0x52 and 100 kHz, initialised
// the "unencrypted" way (0xF0=0x55, 0xFB=0x00) every clone answers, with the
// same pauses as the NintendoExtensionCtrl library galagino uses: each read
// writes the data pointer (0), waits 200 us for the conversion and reads six
// bytes. The serial log shows the raw bytes for the first half minute.
//   byte 0: stick X (0 left .. 255 right), byte 1: stick Y (0 down .. 255 up)
//   byte 5: bit 0 = Z released, bit 1 = C released
// As galagino: a direction counts once the stick is `threshold` away from
// its centre (taken from the first reading, so a worn stick still works).
#include "nunchuk.h"

#include <Arduino.h>
#include <Wire.h>

namespace cyd {
namespace {

constexpr uint8_t kAddress = 0x52;
int g_threshold = 30;
bool g_started = false;
bool g_connected = false;
int g_centre_x = 128, g_centre_y = 128;
bool g_have_centre = false;
uint32_t g_retry_ms = 0;
NunchukState g_last;

bool write_reg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kAddress);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool start_conversion() {
    Wire.beginTransmission(kAddress);
    Wire.write(uint8_t(0x00));
    return Wire.endTransmission() == 0;
}

bool connect() {
    if (!write_reg(0xF0, 0x55)) return false;
    delay(10);
    if (!write_reg(0xFB, 0x00)) return false;
    delay(20);
    g_have_centre = false;
    return true;
}

}  // namespace

void nunchuk_begin(int sda, int scl, int threshold) {
    g_threshold = threshold;
    Wire.begin(sda, scl, 100000);
    Wire.setTimeOut(5);
    g_started = true;
    g_connected = connect();
    g_retry_ms = millis();
    Serial.printf("nunchuk on SDA %d / SCL %d: %s\n", sda, scl, g_connected ? "found" : "not found yet");
}

NunchukState nunchuk_read() {
    if (!g_started) return NunchukState();
    if (!g_connected) {
        if (millis() - g_retry_ms < 1000) return NunchukState();
        g_retry_ms = millis();
        g_connected = connect();
        if (g_connected) Serial.println("nunchuk connected");
        return NunchukState();
    }
    uint8_t b[6];
    bool ok = start_conversion();
    int got = 0;
    if (ok) {
        delayMicroseconds(200);
        got = Wire.requestFrom(kAddress, uint8_t(6));
    }
    for (int i = 0; i < 6; ++i) b[i] = got == 6 ? uint8_t(Wire.read()) : 0xff;
    ok = ok && got == 6;
    static uint32_t last_log = 0;
    if (millis() < 30000 && millis() - last_log >= 2000) {
        last_log = millis();
        Serial.printf("nunchuk %s: %02x %02x %02x %02x %02x %02x\n", ok ? "data" : "no answer", b[0], b[1], b[2],
                      b[3], b[4], b[5]);
    }
    // All 0xFF: no answer (unplugged); all zero: not initialised.
    if (ok) {
        bool all_ff = true, all_00 = true;
        for (uint8_t v : b) all_ff = all_ff && v == 0xff, all_00 = all_00 && v == 0;
        ok = !all_ff && !all_00;
    }
    if (!ok) {
        if (g_connected) Serial.println("nunchuk lost");
        g_connected = false;
        g_retry_ms = millis();
        return NunchukState();
    }
    if (!g_have_centre) {
        // The stick at rest when it was plugged in; a stick already pushed
        // (or far off) falls back to the nominal centre.
        g_centre_x = (b[0] > 88 && b[0] < 168) ? b[0] : 128;
        g_centre_y = (b[1] > 88 && b[1] < 168) ? b[1] : 128;
        g_have_centre = true;
    }
    NunchukState s;
    s.connected = true;
    s.left = b[0] < g_centre_x - g_threshold;
    s.right = b[0] > g_centre_x + g_threshold;
    s.up = b[1] > g_centre_y + g_threshold;
    s.down = b[1] < g_centre_y - g_threshold;
    s.z = (b[5] & 1) == 0;
    s.c = (b[5] & 2) == 0;
    g_last = s;
    return s;
}

}  // namespace cyd
