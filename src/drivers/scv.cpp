#include "drivers/scv.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kBiosRom = {
    {"upd7801g.s01", 0x1000, 0x0000, 0x7ac06182},
    {"epochtv.chr", 0x0400, 0x1000, 0xdb521533},
};

bool is_bios_name(const std::string& name) {
    std::string lower;
    for (char c : name) lower += char(std::tolower(static_cast<unsigned char>(c)));
    return lower == "upd7801g.s01" || lower == "epochtv.chr" || lower == "scv.bin" ||
           lower.find("upd7801") != std::string::npos;
}

std::string lower_copy(std::string value) {
    for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool read_plain_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(n));
    f.read(reinterpret_cast<char*>(out.data()), n);
    return bool(f);
}

bool load_bytes(const std::string& path, std::vector<uint8_t>& data, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        const bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' &&
                            magic[2] == 0x03 && magic[3] == 0x04;
        if (!is_zip) {
            if (!read_plain_file(path, data)) {
                if (error) *error = "cannot read " + path;
                return false;
            }
            return true;
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    data.reserve(Scv::kMaxCartridge);
    return loader.load_first_file(data, error);
}

}  // namespace

Scv::Scv()
    : cpu_(kCrystal),
      sound_(6000000, 10.0f),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0) {
    cpu_.set_memory_handlers([this](uint16_t a) { return read_mem(a); },
                             [this](uint16_t a, uint8_t v) { write_mem(a, v); });
    cpu_.set_port_in([this](uint8_t m) { return port_b_in(m); },
                     [this](uint8_t m) { return port_c_in(m); });
    cpu_.set_port_out([this](uint8_t v) { port_a_out(v); },
                      [this](uint8_t v) { port_c_out(v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });
    sound_.set_ack_handler([this](bool asserted) {
        cpu_.set_input_line(Upd7801::kIntf1, asserted ? IrqLine::Assert : IrqLine::Clear);
    });
}

bool Scv::install_bios(const std::vector<uint8_t>& bios, const std::vector<uint8_t>& chr,
                       std::string* error) {
    if (bios.size() < 0x1000) {
        if (error) *error = "SCV BIOS upd7801g.s01 must be 4 KiB";
        return false;
    }
    if (chr.size() < 0x400) {
        if (error) *error = "SCV character ROM epochtv.chr must be 1 KiB";
        return false;
    }
    std::memcpy(mem_.data(), bios.data(), 0x1000);
    std::memcpy(chars_.data(), chr.data(), 0x400);
    bios_loaded_ = true;
    return true;
}

bool Scv::search_bios(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto try_loader = [this, error](const std::string& path) {
        RomLoader loader;
        std::string ignored;
        if (!loader.open(path, &ignored)) return false;
        std::vector<uint8_t> dest(0x1400, 0);
        if (!loader.load(kBiosRom, dest, &ignored)) return false;
        std::vector<uint8_t> bios(dest.begin(), dest.begin() + 0x1000);
        std::vector<uint8_t> chr(dest.begin() + 0x1000, dest.begin() + 0x1400);
        return install_bios(bios, chr, error);
    };

    if (rom_path.empty()) return false;
    const fs::path input(rom_path);
    if (try_loader(rom_path)) return true;
    if (fs::is_regular_file(input, ec)) {
        if (try_loader(input.parent_path().string())) return true;
        const fs::path dir = input.parent_path();
        std::vector<uint8_t> bios, chr;
        if (read_plain_file((dir / "upd7801g.s01").string(), bios) &&
            read_plain_file((dir / "epochtv.chr").string(), chr)) {
            return install_bios(bios, chr, error);
        }
    }
    if (fs::is_directory(input, ec)) {
        std::vector<uint8_t> bios, chr;
        if (read_plain_file((input / "upd7801g.s01").string(), bios) &&
            read_plain_file((input / "epochtv.chr").string(), chr)) {
            return install_bios(bios, chr, error);
        }
    }
    return false;
}

bool Scv::init(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    search_bios(rom_path, error);
    if (!bios_loaded_) {
        if (error && error->empty()) *error = "cannot load SCV BIOS (upd7801g.s01 + epochtv.chr)";
        return false;
    }

    if (!rom_path.empty() && fs::is_directory(rom_path, ec)) {
        for (const auto& item : fs::directory_iterator(rom_path, ec)) {
            if (!item.is_regular_file(ec)) continue;
            const std::string name = item.path().filename().string();
            if (is_bios_name(name)) continue;
            const std::string ext = lower_copy(item.path().extension().string());
            if (ext == ".bin" || ext == ".0" || ext == ".rom") {
                std::string cart_error;
                if (load_media(item.path().string(), &cart_error) && cart_error.empty()) break;
            }
        }
        reset();
        return true;
    }

    if (!rom_path.empty() && fs::is_regular_file(rom_path, ec)) {
        const std::string name = fs::path(rom_path).filename().string();
        if (!is_bios_name(name)) {
            std::string cart_error;
            if (!load_media(rom_path, &cart_error) && cart_error.size()) {
                warnings_.push_back(cart_error);
            }
        }
    }
    reset();
    return true;
}

void Scv::apply_cart_crc(uint32_t crc) {
    ram_bank_ = false;
    ram_bank2_ = false;
    mapper_ = Mapper::Standard;
    switch (crc) {
        case 0x5971940f:
        case 0x84005c4c:
        case 0xca965c2b:
            ram_bank2_ = true;
            break;
        case 0xcc4fb04d:
            ram_bank_ = true;
            break;
        case 0xcb69903d:
        case 0x5b3a04e0:
            mapper_ = Mapper::PolePosition2;
            break;
        default:
            break;
    }
}

bool Scv::load_split_companion(const std::string& path, std::vector<uint8_t>& data,
                               std::string* error) {
    namespace fs = std::filesystem;
    const std::string ext = lower_copy(fs::path(path).extension().string());
    if (ext != ".0") return true;

    std::vector<uint8_t> second;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        fs::path companion = fs::path(path);
        companion.replace_extension(".1");
        if (fs::is_regular_file(companion, ec)) {
            if (!read_plain_file(companion.string(), second)) {
                if (error) *error = "cannot read " + companion.string();
                return false;
            }
        }
    }

    if (second.empty()) {
        RomLoader loader;
        std::string ignored;
        if (loader.open(path, &ignored) || loader.open(fs::path(path).parent_path().string(), &ignored)) {
            loader.load_first_file(second, &ignored);
        }
    }
    if (second.empty()) return true;

    const uint32_t crc = crc32_of(second.data(), second.size());
    std::vector<uint8_t> combined(0x10000, 0);
    if (crc == 0xd2de91a6) {
        // Doraemon: first 32K then second 32K.
        std::memcpy(combined.data(), data.data(), std::min(data.size(), size_t(0x8000)));
        std::memcpy(combined.data() + 0x8000, second.data(),
                    std::min(second.size(), size_t(0x8000)));
        data = std::move(combined);
    } else if (crc == 0xa895375a) {
        // Kung-Fu Road
        combined.assign(0x10000, 0);
        std::memcpy(combined.data(), data.data(), std::min(data.size(), size_t(0x8000)));
        std::memcpy(combined.data() + 0x8000, data.data(), std::min(data.size(), size_t(0x6000)));
        std::memcpy(combined.data() + 0xE000, second.data(),
                    std::min(second.size(), size_t(0x2000)));
        data = std::move(combined);
    } else if (crc == 0x7978c4a6) {
        // Star Speeder
        combined.assign(0x10000, 0);
        std::memcpy(combined.data() + 0x8000, data.data(), std::min(data.size(), size_t(0x8000)));
        for (int i = 0; i < 4; i++) {
            std::memcpy(combined.data() + i * 0x2000, second.data(),
                        std::min(second.size(), size_t(0x2000)));
        }
        data = std::move(combined);
    }
    return true;
}

bool Scv::load_cart_bytes(std::vector<uint8_t> data, std::string* error) {
    if (data.empty()) {
        if (error) *error = "empty SCV cartridge";
        return false;
    }
    std::fill(mem_.begin() + 0x1000, mem_.end(), 0);
    for (auto& bank : rom_) bank.fill(0);

    rom_bank_type_ = 0;
    rom_window_ = 0;
    const size_t n = std::min(data.size(), kMaxCartridge);
    if (n <= 0x2000) {
        for (int mirror = 0; mirror < 4; mirror++) {
            std::memcpy(rom_[0].data() + mirror * 0x2000, data.data(), n);
        }
    } else if (n <= 0x4000) {
        std::memcpy(rom_[0].data(), data.data(), n);
        std::memcpy(rom_[0].data() + 0x4000, data.data(), std::min(n, size_t(0x4000)));
    } else if (n <= 0x8000) {
        std::memcpy(rom_[0].data(), data.data(), n);
    } else if (n <= 0x10000) {
        std::memcpy(rom_[0].data(), data.data(), 0x8000);
        std::memcpy(rom_[1].data(), data.data() + 0x8000, n - 0x8000);
        rom_bank_type_ = 1;
    } else {
        const size_t banks = std::min(size_t(4), (n + 0x7FFF) / 0x8000);
        for (size_t i = 0; i < banks; i++) {
            const size_t off = i * 0x8000;
            std::memcpy(rom_[i].data(), data.data() + off, std::min(size_t(0x8000), n - off));
        }
        rom_bank_type_ = 2;
    }
    reset();
    return true;
}

bool Scv::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!load_bytes(path, data, error)) return false;
    apply_cart_crc(crc32_of(data.data(), data.size()));
    if (!load_split_companion(path, data, error)) return false;
    return load_cart_bytes(std::move(data), error);
}

void Scv::reset() {
    cpu_.reset();
    sound_.reset();
    porta_val_ = 0xFF;
    portc_val_ = 0xFF;
    keys_.fill(0xFF);
    rom_window_ = 0;
    plane_.fill(kPalette[1]);
    std::fill(framebuffer_.begin(), framebuffer_.end(), kPalette[1]);
    audio_.clear();
}

void Scv::set_inputs(const MachineInputs& inputs) {
    keys_.fill(0xFF);
    auto clr = [](uint8_t& v, uint8_t mask) { v = uint8_t(v & ~mask); };

    if (inputs.key(Key::Num0)) clr(keys_[2], 0x40);
    if (inputs.key(Key::Num1)) clr(keys_[2], 0x80);
    if (inputs.key(Key::Num2)) clr(keys_[3], 0x40);
    if (inputs.key(Key::Num3)) clr(keys_[3], 0x80);
    if (inputs.key(Key::Num4)) clr(keys_[4], 0x40);
    if (inputs.key(Key::Num5)) clr(keys_[4], 0x80);
    if (inputs.key(Key::Num6)) clr(keys_[5], 0x40);
    if (inputs.key(Key::Num7)) clr(keys_[5], 0x80);
    if (inputs.key(Key::Num8)) clr(keys_[6], 0x40);
    if (inputs.key(Key::Num9)) clr(keys_[6], 0x80);
    if (inputs.key(Key::Q)) clr(keys_[7], 0x40);
    if (inputs.key(Key::W)) clr(keys_[7], 0x80);
    if (inputs.key(Key::P) || inputs.player1.start) clr(keys_[8], 0x01);

    if (inputs.player1.left) clr(keys_[0], 0x01);
    if (inputs.player1.up) clr(keys_[0], 0x02);
    if (inputs.player1.button1) clr(keys_[0], 0x04);
    if (inputs.player2.left) clr(keys_[0], 0x08);
    if (inputs.player2.up) clr(keys_[0], 0x10);
    if (inputs.player2.button1) clr(keys_[0], 0x20);

    if (inputs.player1.down) clr(keys_[1], 0x01);
    if (inputs.player1.right) clr(keys_[1], 0x02);
    if (inputs.player1.button2) clr(keys_[1], 0x04);
    if (inputs.player2.down) clr(keys_[1], 0x08);
    if (inputs.player2.right) clr(keys_[1], 0x10);
    if (inputs.player2.button2) clr(keys_[1], 0x20);
}

void Scv::set_dip_switch(int, uint8_t) {}

void Scv::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

uint8_t Scv::read_mem(uint16_t addr) {
    if (mapper_ == Mapper::PolePosition2) {
        if (addr <= 0x0FFF || (addr >= 0x2000 && addr <= 0x3403)) return mem_[addr];
        if (addr >= 0x8000 && addr <= 0xEFFF) return rom_[rom_window_ & 3][addr & 0x7FFF];
        if (addr >= 0xF000 && addr <= 0xFF7F) return mem_[addr];
        if (addr >= 0xFF80) return cpu_.iram[addr & 0x7F];
        return 0xFF;
    }
    if (addr <= 0x0FFF || (addr >= 0x2000 && addr <= 0x3403)) return mem_[addr];
    if (addr >= 0x6000 && addr <= 0x7FFF) return ram_bank_ ? mem_[addr] : 0xFF;
    if (addr >= 0x8000 && addr <= 0xDFFF) return rom_[rom_window_ & 3][addr & 0x7FFF];
    if (addr >= 0xE000 && addr <= 0xFF7F) {
        if (ram_bank2_) return mem_[addr];
        return rom_[rom_window_ & 3][addr & 0x7FFF];
    }
    if (addr >= 0xFF80) return cpu_.iram[addr & 0x7F];
    return 0xFF;
}

void Scv::write_mem(uint16_t addr, uint8_t value) {
    if (mapper_ == Mapper::PolePosition2) {
        if (addr <= 0x0FFF || (addr >= 0x8000 && addr <= 0xEFFF)) return;
        if (addr >= 0x2000 && addr <= 0x3403) {
            mem_[addr] = value;
            return;
        }
        if (addr == 0x3600) {
            sound_.write(value);
            return;
        }
        if (addr >= 0xF000 && addr <= 0xFF7F) {
            mem_[addr] = value;
            return;
        }
        if (addr >= 0xFF80) cpu_.iram[addr & 0x7F] = value;
        return;
    }
    if (addr <= 0x0FFF || (addr >= 0x8000 && addr <= 0xDFFF)) return;
    if (addr >= 0x2000 && addr <= 0x3403) {
        mem_[addr] = value;
        return;
    }
    if (addr == 0x3600) {
        sound_.write(value);
        return;
    }
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (ram_bank_) mem_[addr] = value;
        return;
    }
    if (addr >= 0xE000 && addr <= 0xFF7F) {
        if (ram_bank2_) mem_[addr] = value;
        return;
    }
    if (addr >= 0xFF80) cpu_.iram[addr & 0x7F] = value;
}

uint8_t Scv::port_b_in(uint8_t) {
    uint8_t data = 0xFF;
    for (int f = 0; f < 8; f++) {
        if ((porta_val_ & (1 << f)) == 0) data = uint8_t(data & keys_[f]);
    }
    return data;
}

uint8_t Scv::port_c_in(uint8_t) {
    return uint8_t((portc_val_ & 0xFE) | (keys_[8] & 1));
}

void Scv::port_a_out(uint8_t value) { porta_val_ = value; }

void Scv::port_c_out(uint8_t value) {
    portc_val_ = value;
    sound_.pcm_write(value & 0x08);
    switch (rom_bank_type_) {
        case 1:
            rom_window_ = uint8_t((value & 0x20) >> 5);
            break;
        case 2:
            rom_window_ = uint8_t((value >> 5) & 3);
            break;
        default:
            break;
    }
}

void Scv::on_cycles(int cycles) { sound_.run_cycles(cycles, kCpuClock); }

void Scv::put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= 256) return;
    plane_[size_t((y & 0xFF) * 256 + x)] = color;
}

void Scv::draw_text(int x, int y, uint16_t char_data, uint8_t fg, uint8_t bg) {
    for (int f = 0; f < 8; f++) {
        uint8_t d = chars_[(char_data + f) & 0x3FF];
        for (int h = 0; h < 8; h++) {
            put_pixel(x + h, y + f, kPalette[(d & 0x80) ? fg : bg]);
            d = uint8_t(d << 1);
        }
    }
    for (int f = 8; f < 16; f++)
        for (int h = 0; h < 8; h++) put_pixel(x + h, y + f, kPalette[bg]);
}

void Scv::draw_semi_graph(int x, int y, uint8_t data, uint8_t fg) {
    if (data == 0) return;
    for (int f = 0; f < 4; f++)
        for (int h = 0; h < 4; h++) put_pixel(x + h, y + f, kPalette[fg]);
}

void Scv::draw_block_graph(int x, int y, uint8_t col) {
    for (int f = 0; f < 8; f++)
        for (int h = 0; h < 8; h++) put_pixel(x + h, y + f, kPalette[col & 15]);
}

void Scv::plot_sprite_part(int x, int y, uint8_t pat, uint8_t col, int start_line) {
    if (x < 4 || (y + 2) < start_line) return;
    x -= 4;
    const uint32_t c = kPalette[col & 15];
    if (pat & 8) put_pixel(x, y + 2, c);
    if ((pat & 4) && x < 255) put_pixel(x + 1, y + 2, c);
    if ((pat & 2) && x < 254) put_pixel(x + 2, y + 2, c);
    if ((pat & 1) && x < 253) put_pixel(x + 3, y + 2, c);
}

void Scv::draw_sprite(int x, int y, uint8_t tile_idx, uint8_t col, bool left, bool right,
                      bool top, bool bottom, uint8_t clip_y, int start_line) {
    y += int(clip_y) * 2;
    for (int f = clip_y; f < 8; f++) {
        const int base = 0x2000 + tile_idx * 32 + f * 4;
        const uint8_t pat0 = mem_[base & 0xFFFF];
        const uint8_t pat1 = mem_[(base + 1) & 0xFFFF];
        const uint8_t pat2 = mem_[(base + 2) & 0xFFFF];
        const uint8_t pat3 = mem_[(base + 3) & 0xFFFF];
        if ((top && (f * 4) < 16) || (bottom && (f * 4) >= 16)) {
            if (left) {
                plot_sprite_part(x, y, uint8_t(pat0 >> 4), col, start_line);
                plot_sprite_part(x + 4, y, uint8_t(pat1 >> 4), col, start_line);
            }
            if (right) {
                plot_sprite_part(x + 8, y, uint8_t(pat2 >> 4), col, start_line);
                plot_sprite_part(x + 12, y, uint8_t(pat3 >> 4), col, start_line);
            }
            if (left) {
                plot_sprite_part(x, y + 1, pat0 & 0x0F, col, start_line);
                plot_sprite_part(x + 4, y + 1, pat1 & 0x0F, col, start_line);
            }
            if (right) {
                plot_sprite_part(x + 8, y + 1, pat2 & 0x0F, col, start_line);
                plot_sprite_part(x + 12, y + 1, pat3 & 0x0F, col, start_line);
            }
        }
        y += 2;
    }
}

void Scv::update_video() {
    const uint8_t fg = uint8_t(mem_[0x3403] >> 4);
    const uint8_t bg = uint8_t(mem_[0x3403] & 0x0F);
    const uint8_t gr_fg = uint8_t(mem_[0x3401] >> 4);
    const uint8_t gr_bg = uint8_t(mem_[0x3401] & 0x0F);
    const int clip_x = (mem_[0x3402] & 0x0F) * 2;
    const int clip_y = mem_[0x3402] >> 4;
    plane_.fill(kPalette[gr_bg]);

    for (int y = 0; y < 16; y++) {
        const bool text_y =
            (y < clip_y) ? ((mem_[0x3400] & 0x80) == 0) : ((mem_[0x3400] & 0x80) != 0);
        for (int x = 0; x < 32; x++) {
            const uint8_t d = mem_[0x3000 + y * 32 + x];
            const bool text_x =
                (x < clip_x) ? ((mem_[0x3400] & 0x40) == 0) : ((mem_[0x3400] & 0x40) != 0);
            if (text_x && text_y) {
                draw_text(x * 8, y * 16, uint16_t((d & 0x7F) * 8), fg, bg);
            } else {
                switch (mem_[0x3400] & 3) {
                    case 1:
                        draw_semi_graph(x * 8, y * 16, d & 0x80, gr_fg);
                        draw_semi_graph(x * 8 + 4, y * 16, d & 0x40, gr_fg);
                        draw_semi_graph(x * 8, y * 16 + 4, d & 0x20, gr_fg);
                        draw_semi_graph(x * 8 + 4, y * 16 + 4, d & 0x10, gr_fg);
                        draw_semi_graph(x * 8, y * 16 + 8, d & 0x08, gr_fg);
                        draw_semi_graph(x * 8 + 4, y * 16 + 8, d & 0x04, gr_fg);
                        draw_semi_graph(x * 8, y * 16 + 12, d & 0x02, gr_fg);
                        draw_semi_graph(x * 8 + 4, y * 16 + 12, d & 0x01, gr_fg);
                        break;
                    case 3:
                        draw_block_graph(x * 8, y * 16, uint8_t(d >> 4));
                        draw_block_graph(x * 8, y * 16 + 8, d & 0x0F);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (mem_[0x3400] & 0x10) {
        int screen_start_sprite_line = 0;
        if ((mem_[0x3400] & 0xF7) == 0x17 && (mem_[0x3402] & 0xEF) == 0x4F)
            screen_start_sprite_line = 21 + 32;

        for (int f = 0; f < 128; f++) {
            int spr_y = mem_[0x3200 + f * 4] & 0xFE;
            bool y_32 = (mem_[0x3200 + f * 4] & 1) != 0;
            uint8_t clip = uint8_t(mem_[0x3201 + f * 4] >> 4);
            uint8_t col = mem_[0x3201 + f * 4] & 0x0F;
            int spr_x = mem_[0x3202 + f * 4] & 0xFE;
            bool x_32 = (mem_[0x3202 + f * 4] & 1) != 0;
            uint8_t tile_idx = mem_[0x3203 + f * 4] & 0x7F;
            bool half = (mem_[0x3203 + f * 4] & 0x80) != 0;
            bool left = true, right = true, top = true, bottom = true;
            if (col == 0 || spr_y == 0) continue;

            if (half) {
                if (tile_idx & 0x40) {
                    if (y_32) {
                        spr_y -= 8;
                        top = false;
                        bottom = true;
                        y_32 = false;
                    } else {
                        top = true;
                        bottom = false;
                    }
                }
                if (x_32) {
                    spr_x -= 8;
                    left = false;
                    right = true;
                    x_32 = false;
                } else {
                    left = true;
                    right = false;
                }
            }

            if ((mem_[0x3400] & 0x20) && (f & 0x20)) {
                draw_sprite(spr_x, spr_y, tile_idx, col, left, right, top, bottom, clip,
                            screen_start_sprite_line);
                if (x_32 || y_32) {
                    const uint8_t spr_col =
                        (f & 0x40) ? kSpr2ColLut1[col] : kSpr2ColLut0[col];
                    draw_sprite(spr_x, spr_y,
                                uint8_t(tile_idx ^ (8 * int(x_32) + int(y_32))), spr_col,
                                left, right, top, bottom, clip, screen_start_sprite_line);
                }
            } else {
                draw_sprite(spr_x, spr_y, tile_idx, col, left, right, top, bottom, clip,
                            screen_start_sprite_line);
                if (x_32)
                    draw_sprite(spr_x + 16, spr_y, uint8_t(tile_idx | 8), col, true, true,
                                top, bottom, clip, screen_start_sprite_line);
                if (y_32) {
                    if (clip & 8) clip = uint8_t(clip & 7);
                    else clip = 0;
                    draw_sprite(spr_x, spr_y + 16, uint8_t(tile_idx | 1), col, left, right,
                                true, true, clip, screen_start_sprite_line);
                    if (x_32)
                        draw_sprite(spr_x + 16, spr_y + 16, uint8_t(tile_idx | 9), col, true,
                                    true, true, true, clip, screen_start_sprite_line);
                }
            }
        }
    }

    for (int y = 0; y < kScreenHeight; y++) {
        const uint32_t* src = plane_.data() + size_t(y + 23) * 256 + 24;
        uint32_t* dst = framebuffer_.data() + size_t(y) * kScreenWidth;
        std::memcpy(dst, src, size_t(kScreenWidth) * sizeof(uint32_t));
    }
}

void Scv::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        if (line == 7) cpu_.set_input_line(Upd7801::kIntf2, IrqLine::Clear);
        if (line == 240) {
            update_video();
            cpu_.set_input_line(Upd7801::kIntf2, IrqLine::Assert);
        }
        cpu_.run(kCyclesPerLine);
    }
    sound_.take_samples(audio_);
}

}  // namespace dsp
