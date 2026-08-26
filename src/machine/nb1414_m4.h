#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Nichibutsu NB1414M4 — port of arcade/misc/nb1414_m4.pas
class Nb1414M4 {
public:
    Nb1414M4() = default;

    void set_memory(uint8_t* text_ram) { mem_ = text_ram; }
    void load_rom(const std::vector<uint8_t>& data);
    void reset();

    void exec(uint16_t& scroll_fg_x, uint16_t& scroll_fg_y, uint8_t frame);

    const std::array<uint8_t, 0x4000>& rom() const { return rom_; }
    std::array<uint8_t, 0x4000>& rom() { return rom_; }

private:
    void dma(uint16_t src, uint16_t dst, uint16_t size, uint8_t condition);
    void fill(uint16_t dst, uint8_t tile, uint8_t pal);
    void insert_coin_msg();
    void credit_msg();
    void cmd_0200(uint8_t command);
    void cmd_0600(uint8_t is2p);
    void cmd_0e00(uint8_t command);
    void kozure_score_msg(uint16_t dst, uint8_t src_base);

    uint8_t* mem_ = nullptr;
    std::array<uint8_t, 0x4000> rom_{};
    uint8_t frame_ = 0;
};

}  // namespace dsp
