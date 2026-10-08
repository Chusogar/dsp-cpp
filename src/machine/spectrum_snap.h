#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Decoded ZX Spectrum snapshot (SNA / Z80 / SZX), ready to apply to a machine.
struct SpectrumSnap {
    uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    uint8_t a2 = 0, f2 = 0, b2 = 0, c2 = 0, d2 = 0, e2 = 0, h2 = 0, l2 = 0;
    uint16_t ix = 0, iy = 0, sp = 0, pc = 0;
    uint8_t i = 0, r = 0, im = 0;
    bool iff1 = false, iff2 = false;
    uint8_t border = 7;
    uint8_t port_7ffd = 0;
    uint8_t port_1ffd = 0;
    bool trdos_paged = false;  // 128K SNA tail byte; Beta 128 / TR-DOS ROM
    bool ay_used = false;
    uint8_t ay_latch = 0;
    std::array<uint8_t, 16> ay_regs{};

    // 48K: bytes at $4000..$FFFF. 128K: eight 16K banks (0..7).
    bool is_128 = false;
    std::array<uint8_t, 0xc000> ram48{};
    std::array<std::array<uint8_t, 0x4000>, 8> banks{};
};

bool spectrum_snap_from_sna(const uint8_t* data, size_t size, SpectrumSnap& out, std::string* error);
bool spectrum_snap_from_z80(const uint8_t* data, size_t size, SpectrumSnap& out, std::string* error);
bool spectrum_snap_from_szx(const uint8_t* data, size_t size, SpectrumSnap& out, std::string* error);
bool spectrum_snap_from_bytes(const uint8_t* data, size_t size, const char* ext_hint, SpectrumSnap& out,
                              std::string* error);

}  // namespace dsp
