#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// High-level Namco 51xx I/O (coin/credit/joystick), matching the software
// model in leniad/dsp-emulator namcoio_06xx_5Xxx.pas (namcoio_51XX_read/write).
// Galaga HW uses this instead of the full MB8843 MCU emulation.
class Namco51xxSw {
public:
    using PortRead = std::function<uint8_t()>;

    void set_input(int port, PortRead handler) {
        if (port >= 0 && port < 2) in_[size_t(port)] = std::move(handler);
    }

    void reset(bool kludge = false) {
        kludge_ = kludge;
        credits_ = 0;
        coins_[0] = coins_[1] = 0;
        coins_per_cred_[0] = coins_per_cred_[1] = 1;
        creds_per_coin_[0] = creds_per_coin_[1] = 1;
        in_count_ = 0;
        mode_ = 0;
        coincred_mode_ = 0;
        lastcoins_ = 0;
        lastbuttons_ = 0xff;
        remap_joy_ = false;
    }

    // Chip-select / R/W lines are ignored in the software model (instant response).
    void set_chip_select(bool) {}
    void set_rw(bool) {}
    void set_reset(bool) {}
    void vblank(bool) {}
    void run(int) {}

    void write(uint8_t data) {
        data = uint8_t(data & 0x07);
        if (coincred_mode_ != 0) {
            switch (coincred_mode_) {
                case 4: coins_per_cred_[0] = data; break;
                case 3: creds_per_coin_[0] = data; break;
                case 2: coins_per_cred_[1] = data; break;
                case 1: creds_per_coin_[1] = data; break;
                default: break;
            }
            coincred_mode_--;
            return;
        }
        switch (data) {
            case 0: break;  // nop
            case 1:
                if (kludge_) {
                    coincred_mode_ = 6;
                    remap_joy_ = true;
                } else {
                    coincred_mode_ = 4;
                    credits_ = 0;
                }
                break;
            case 2:  // credits mode
                mode_ = 1;
                in_count_ = 0;
                break;
            case 3: remap_joy_ = false; break;
            case 4: remap_joy_ = true; break;
            case 5:  // switch mode
                mode_ = 0;
                in_count_ = 0;
                break;
            default: break;
        }
    }

    uint8_t read() {
        static constexpr uint8_t kJoyMap[16] = {
            0xf, 0xe, 0xd, 0x5, 0xc, 0x9, 0x7, 0x6,
            0xb, 0x3, 0xa, 0x4, 0x1, 0x2, 0x0, 0x8};
        const uint8_t in0 = in_[0] ? in_[0]() : uint8_t(0xff);
        const uint8_t in1 = in_[1] ? in_[1]() : uint8_t(0xff);
        uint8_t res = 0;

        if (mode_ == 0) {
            // switch mode
            switch (in_count_ & 3) {
                case 0: res = in0; break;
                case 1: res = in1; break;
                default: res = 0; break;
            }
            in_count_ = uint8_t((in_count_ + 1) & 3);
            return res;
        }

        // credits mode
        switch (in_count_ % 3) {
            case 0: {
                const uint8_t inverted = uint8_t(~in0);
                const uint8_t toggle = uint8_t(inverted ^ lastcoins_);
                lastcoins_ = inverted;
                if (coins_per_cred_[0] > 0) {
                    if (credits_ < 99) {
                        if ((toggle & inverted & 0x10) != 0) {
                            coins_[0]++;
                            if (coins_[0] >= coins_per_cred_[0]) {
                                credits_ = uint8_t(credits_ + creds_per_coin_[0]);
                                coins_[0] = 0;
                                if (credits_ > 99) credits_ = 99;
                            }
                        }
                        if ((toggle & inverted & 0x20) != 0) {
                            coins_[1]++;
                            if (coins_[1] >= coins_per_cred_[1]) {
                                credits_ = uint8_t(credits_ + creds_per_coin_[1]);
                                coins_[1] = 0;
                                if (credits_ > 99) credits_ = 99;
                            }
                        }
                        if ((toggle & inverted & 0x04) != 0 && credits_ >= 1) {
                            credits_--;
                            mode_ = 2;
                        }
                        if ((toggle & inverted & 0x08) != 0 && credits_ >= 2) {
                            credits_ = uint8_t(credits_ - 2);
                            mode_ = 2;
                        }
                    }
                }
                // test mode switch on in0 bit 7 (active low in raw port)
                if (((~in0) & 0x80) != 0) return 0xbb;
                res = uint8_t(((credits_ / 10) << 4) | (credits_ % 10));
                break;
            }
            case 1: {
                uint8_t joy = uint8_t(in1 & 0x0f);
                const uint8_t inverted = uint8_t(~(in0 & 0x0f));
                const uint8_t toggle = uint8_t(inverted ^ lastbuttons_);
                lastbuttons_ = uint8_t((lastbuttons_ & 2) | (inverted & 1));
                if (remap_joy_) joy = kJoyMap[joy & 0x0f];
                joy = uint8_t(joy | ((((toggle & inverted & 0x01) ^ 1) & 1) << 4));
                joy = uint8_t(joy | ((((inverted & 0x01) ^ 1) & 1) << 5));
                res = joy;
                break;
            }
            case 2: {
                uint8_t joy = uint8_t(in1 >> 4);
                const uint8_t inverted = uint8_t(~(in0 & 0x0f));
                const uint8_t toggle = uint8_t(inverted ^ lastbuttons_);
                lastbuttons_ = uint8_t((lastbuttons_ & 1) | (inverted & 2));
                if (remap_joy_) joy = kJoyMap[joy & 0x0f];
                joy = uint8_t(joy | ((((toggle & inverted & 0x02) ^ 2) & 2) << 3));
                joy = uint8_t(joy | ((((inverted & 0x02) ^ 2) & 2) << 4));
                res = joy;
                break;
            }
            default: break;
        }
        in_count_ = uint8_t((in_count_ + 1) % 3);
        return res;
    }

    uint16_t debug_pc() const { return 0; }

private:
    PortRead in_[2];
    bool kludge_ = false;
    bool remap_joy_ = false;
    uint8_t mode_ = 0;
    uint8_t coincred_mode_ = 0;
    uint8_t credits_ = 0;
    uint8_t in_count_ = 0;
    uint8_t lastcoins_ = 0;
    uint8_t lastbuttons_ = 0xff;
    std::array<uint8_t, 2> coins_{};
    std::array<uint8_t, 2> coins_per_cred_{{1, 1}};
    std::array<uint8_t, 2> creds_per_coin_{{1, 1}};
};

}  // namespace dsp
