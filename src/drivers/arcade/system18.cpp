#include "drivers/arcade/system18.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "drivers/arcade/system18_roms.h"

namespace dsp {
namespace {

uint8_t joy_bits(const InputState& p) {
    uint8_t v = 0xff;
    if (p.button1) v &= ~0x01;
    if (p.button2) v &= ~0x02;
    if (p.button3) v &= ~0x04;
    if (p.down) v &= ~0x10;
    if (p.up) v &= ~0x20;
    if (p.right) v &= ~0x40;
    if (p.left) v &= ~0x80;
    return v;
}

}  // namespace

System18::System18(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym1_(8000000, 0.40f),
      ym2_(8000000, 0.40f),
      rf5c68_(10000000, 1.0f),
      vdp_(false),
      framebuffer_(kNativeWidth * kNativeHeight, 0),
      bg_low_(1024 * 512, 0),
      bg_high_(1024 * 512, 0),
      fg_low_(1024 * 512, 0),
      fg_high_(1024 * 512, 0),
      text_low_(512 * 256, 0),
      text_high_(512 * 256, 0),
      vdp_fb_(kNativeWidth * kNativeHeight, 0),
      vdp_pri_(kNativeWidth * kNativeHeight, 0) {
    mcu_ = std::make_unique<Mcs51>(kMcuClock);

    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v, true); });
    main_cpu_.set_cmpild_handler([this](uint8_t reg, uint32_t data) { fd1094_.on_cmpild(reg, data); });
    main_cpu_.set_rte_handler([this]() { fd1094_.on_rte(); });
    main_cpu_.set_irq_taken_handler([this](int) { fd1094_.on_irq(); });

    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    mapper_.set_open_bus([this]() -> uint8_t {
        if (rom_.empty()) return 0xff;
        return uint8_t(rom_[(main_cpu_.pc() >> 1) % rom_.size()] >> 8);
    });
    mapper_.set_bus_handlers([this](uint32_t a) { return main_read(a); },
                             [this](uint32_t a, uint16_t v) { main_write(a, v, false); });
    mapper_.set_reset_handler([this](IrqLine state) {
        if (state != IrqLine::Clear) fd1094_.reset();
        main_cpu_.set_reset_line(state);
    });
    mapper_.set_irq_handler(
        [this](int level, IrqLine state) { main_cpu_.set_irq(level, state); });
    mapper_.set_pbf_handler([this](IrqLine state) { sound_cpu_.set_nmi(state); });
    mapper_.set_mcu_int_handler([this](IrqLine state) {
        if (mcu_) mcu_->set_irq1_line(state);
    });

    mcu_->set_external_handlers(
        [this](uint16_t address) { return mapper_.read_reg(uint8_t(address & 0x1f)); },
        [this](uint16_t address, uint8_t value) {
            const uint32_t old = mapper_.dirs_start(5);
            mapper_.write_reg(uint8_t(address & 0x1f), value);
            if (old != mapper_.dirs_start(5)) {
                for (auto& page : video_.tile_dirty) page.fill(false);
            }
        });

    // 315-5296 port map for System 18 / Moonwalker.
    io_.set_port_read(0, [this]() { return in_p1_; });
    io_.set_port_read(1, [this]() { return in_p2_; });
    io_.set_port_read(2, [this]() { return in_p3_; });
    io_.set_port_read(4, [this]() { return in_service_; });
    io_.set_port_read(5, [this]() { return dsw_coinage_; });
    io_.set_port_read(6, [this]() { return dsw_; });
    io_.set_port_write(3, [this](uint8_t data) {
        grayscale_ = (data & 0x40) == 0;
        // bit 5: flip (ignored for now)
    });
    io_.set_port_write(7, [this](uint8_t data) { apply_tile_bank(data); });
    io_.set_cnt_write([this](int bit, bool state) {
        if (bit == 1) video_.screen_enabled = state;
        if (bit == 2) vdp_enable_ = state;
    });

    ym1_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
    });

    vdp_.set_dma_reader([this](uint32_t addr) { return main_read(addr); });
}

const char* System18::title() const {
    return "Michael Jackson's Moonwalker";
}

bool System18::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    video_.init_palette_luts();
    video_.cram_words = 0x800;
    video_.tile_banks = 0x0f;
    video_.tile_bank[0] = 0;
    video_.tile_bank[1] = 1;
    for (int i = 0; i < 16; i++) video_.sprite_bank[size_t(i)] = uint8_t(i);
    sprite_banks_ = 16;
    reset();
    return true;
}

bool System18::load_roms(const std::string& rom_path, std::string* error) {
    using namespace system18_roms;
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint16_t> encrypted;
    if (!load_roms16w(loader, kMwalkMain, encrypted, error)) return false;
    std::vector<uint8_t> key_bytes;
    if (!load_rom_bytes(loader, kMwalkKey, key_bytes, error)) return false;
    key_bytes.resize(Fd1094::kKeySize, 0);
    fd1094_.set_key(key_bytes.data(), key_bytes.size());
    rom_ = std::move(encrypted);

    if (!load_rom_bytes(loader, kMwalkSound, sound_rom_, error)) return false;
    sound_rom_.resize(0x200000, 0xff);

    std::vector<uint8_t> mcu_bytes;
    if (!load_rom_bytes(loader, kMwalkMcu, mcu_bytes, error)) return false;
    std::fill(mcu_->rom(), mcu_->rom() + Mcs51::kRomSize, 0xff);
    std::copy(mcu_bytes.begin(), mcu_bytes.end(), mcu_->rom());

    if (!load_roms16w(loader, kMwalkSprites, sprite_rom_, error)) return false;

    std::vector<uint8_t> tile_bytes;
    if (!load_rom_bytes(loader, kMwalkTiles, tile_bytes, error)) return false;
    // 3 planes, enough tiles for the 0xc0000 ROM (32768 tiles).
    decode_s16_tiles(video_.tiles, tile_bytes, 8);
    return true;
}

void System18::reset() {
    mapper_.reset();
    fd1094_.reset();
    main_cpu_.reset();
    sound_cpu_.reset();
    if (mcu_) mcu_->reset();
    ym1_.reset();
    ym2_.reset();
    rf5c68_.reset();
    vdp_.reset();
    io_.reset();
    video_.reset();
    // CNT1 starts low; the game enables the System 16 display through the I/O chip.
    video_.screen_enabled = false;
    work_ram_.fill(0);
    sound_ram_.fill(0);
    sound_bank_ = 0;
    tile_bank_latch_ = 0;
    vdp_mixing_ = 0;
    vdp_enable_ = false;
    grayscale_ = false;
    main_debt_ = sound_debt_ = mcu_debt_ = 0;
    audio_acc_ = 0;
    audio_.clear();
    in_p1_ = in_p2_ = in_p3_ = in_service_ = 0xff;
}

void System18::set_inputs(const MachineInputs& inputs) {
    in_p1_ = joy_bits(inputs.player1);
    in_p2_ = joy_bits(inputs.player2);
    // Player 3 is unused by the two-player front end; keep idle high.
    in_p3_ = 0xff;

    in_service_ = 0xff;
    if (inputs.coin1) in_service_ &= ~0x01;
    if (inputs.coin2) in_service_ &= ~0x02;
    if (inputs.service) in_service_ &= ~0x04;
    if (inputs.player1.start) in_service_ &= ~0x10;
    if (inputs.player2.start) in_service_ &= ~0x20;
}

void System18::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_coinage_ = value;
    else if (bank == 1) dsw_ = value;
}

void System18::apply_tile_bank(uint8_t data) {
    tile_bank_latch_ = data;
    // 171-5874 ROM board: low nibble banks pages 0-3, high nibble pages 4-7.
    // Our Sega16Video collapses that to two bank slots of 0x1000 tiles.
    const uint8_t lo = uint8_t(data & 0x0f);
    const uint8_t hi = uint8_t((data >> 4) & 0x0f);
    if (video_.tile_bank[0] != lo || video_.tile_bank[1] != hi) {
        video_.tile_bank[0] = lo;
        video_.tile_bank[1] = hi;
        for (auto& page : video_.tile_dirty) page.fill(true);
        video_.text_dirty.fill(true);
    }
}

void System18::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

uint16_t System18::read_region0(uint32_t address) {
    const size_t index = (address >> 1) % std::max<size_t>(rom_.size(), 1);
    if (main_cpu_.opcode()) {
        const uint16_t* dec = fd1094_.decrypted_opcodes(rom_.data(), uint32_t(rom_.size() * 2));
        return dec[index];
    }
    return rom_[index];
}

uint16_t System18::misc_io_r(uint16_t word_offset) {
    const uint16_t offset = uint16_t(word_offset & 0x1fff);
    switch (offset & (0x3000 / 2)) {
        case 0x0000 / 2:
        case 0x1000 / 2:
            return uint16_t(io_.read(uint8_t(offset)) | 0xff00);
        case 0x2000 / 2:
            return (offset & 1) ? dsw_coinage_ : in_service_;
        default:
            break;
    }
    return 0xffff;
}

void System18::misc_io_w(uint16_t word_offset, uint16_t value) {
    const uint16_t offset = uint16_t(word_offset & 0x1fff);
    switch (offset & (0x3000 / 2)) {
        case 0x0000 / 2:
        case 0x1000 / 2:
            io_.write(uint8_t(offset), uint8_t(value));
            break;
        case 0x2000 / 2:
            vdp_mixing_ = uint8_t(value);
            break;
        default:
            break;
    }
}

uint16_t System18::main_read(uint32_t address) {
    address &= 0xffffff;
    if (mapper_.contains(0, address)) return read_region0(address);

    uint16_t result = 0xffff;
    bool mapped = false;
    if (mapper_.contains(1, address)) {
        // 171-5874: region 1 is extra ROM window (mwalk has 512 KB only in region 0).
        const size_t index = ((address & 0x7ffff) >> 1) % std::max<size_t>(rom_.size(), 1);
        result = rom_[index];
        mapped = true;
    }
    if (mapper_.contains(2, address)) {
        result = vdp_.read(uint8_t((address >> 1) & 0x1f));
        mapped = true;
    }
    if (mapper_.contains(3, address)) {
        result = work_ram_[(address >> 1) & 0x1fff];
        mapped = true;
    }
    if (mapper_.contains(4, address)) {
        result = video_.sprite_ram[(address >> 1) & 0x7ff];
        mapped = true;
    }
    if (mapper_.contains(5, address)) {
        if ((address & 0x1ffff) <= 0xffff) result = video_.tile_ram[(address & 0xffff) >> 1];
        else result = video_.char_ram[(address & 0xfff) >> 1];
        mapped = true;
    }
    if (mapper_.contains(6, address)) {
        result = video_.pal_ram[(address & 0xfff) >> 1];
        mapped = true;
    }
    if (mapper_.contains(7, address)) {
        result = misc_io_r(uint16_t((address >> 1) & 0x1fff));
        mapped = true;
    }
    if (!mapped) result = mapper_.read_reg(uint8_t((address >> 1) & 0x1f));
    return result;
}

void System18::main_write(uint32_t address, uint16_t value, bool allow_mapper) {
    address &= 0xffffff;
    bool mapped = false;
    if (mapper_.contains(0, address)) mapped = true;
    if (mapper_.contains(1, address)) mapped = true;
    if (mapper_.contains(2, address)) {
        vdp_.write(uint8_t((address >> 1) & 0x1f), value);
        mapped = true;
    }
    if (mapper_.contains(3, address)) {
        work_ram_[(address >> 1) & 0x1fff] = value;
        mapped = true;
    }
    if (mapper_.contains(4, address)) {
        video_.sprite_ram[(address >> 1) & 0x7ff] = value;
        mapped = true;
    }
    if (mapper_.contains(5, address)) {
        if ((address & 0x1ffff) <= 0xffff) {
            const uint16_t offset = uint16_t((address & 0xffff) >> 1);
            if (video_.tile_ram[offset] != value) {
                video_.tile_ram[offset] = value;
                video_.mark_tile(offset);
            }
        } else {
            const uint16_t offset = uint16_t((address & 0xfff) >> 1);
            if (video_.char_ram[offset] != value) {
                video_.char_ram[offset] = value;
                video_.text_dirty[offset] = true;
            }
            video_.apply_screen_select_16b(offset);
        }
        mapped = true;
    }
    if (mapper_.contains(6, address)) {
        video_.set_palette_entry(int((address & 0xfff) >> 1), value, true);
        mapped = true;
    }
    if (mapper_.contains(7, address)) {
        misc_io_w(uint16_t((address >> 1) & 0x1fff), value);
        mapped = true;
    }
    if (!mapped && allow_mapper) {
        const uint32_t old = mapper_.dirs_start(5);
        mapper_.write_reg(uint8_t((address >> 1) & 0x1f), uint8_t(value));
        if (old != mapper_.dirs_start(5)) {
            for (auto& page : video_.tile_dirty) page.fill(false);
        }
    }
}

uint8_t System18::sound_read(uint16_t address) {
    if (address <= 0x9fff) {
        return sound_rom_[address];
    }
    if (address >= 0xa000 && address <= 0xbfff) {
        const uint32_t base = uint32_t(sound_bank_) * 0x2000u;
        return sound_rom_[(base + (address & 0x1fff)) & 0x1fffff];
    }
    if (address >= 0xc000 && address <= 0xcfff) {
        // Register aliases — reads are unused on RF5C68.
        return 0xff;
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        return rf5c68_.read_mem(uint16_t(address & 0x0fff));
    }
    if (address >= 0xe000) return sound_ram_[address & 0x1fff];
    return 0xff;
}

void System18::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0xc000 && address <= 0xcfff) {
        rf5c68_.write_reg(uint8_t(address & 0x0f), value);
        return;
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        rf5c68_.write_mem(uint16_t(address & 0x0fff), value);
        return;
    }
    if (address >= 0xe000) sound_ram_[address & 0x1fff] = value;
}

uint8_t System18::sound_in(uint16_t port) {
    const uint8_t p = uint8_t(port);
    if ((p & 0xf0) == 0x80) return ym1_.read(p & 3);
    if ((p & 0xf0) == 0x90) return ym2_.read(p & 3);
    if ((p & 0xe0) == 0xc0) return mapper_.pread();
    return 0xff;
}

void System18::sound_out(uint16_t port, uint8_t value) {
    const uint8_t p = uint8_t(port);
    if ((p & 0xf0) == 0x80) {
        ym1_.write(p & 3, value);
        return;
    }
    if ((p & 0xf0) == 0x90) {
        ym2_.write(p & 3, value);
        return;
    }
    if ((p & 0xe0) == 0xa0) {
        sound_bank_ = value;
        return;
    }
    if ((p & 0xe0) == 0xc0) {
        mapper_.pwrite(value);
        return;
    }
}

void System18::on_sound_cycles(int cycles) {
    // YM2612 timers tick at the chip clock; approximate with the Z80 slice.
    (void)cycles;
}

void System18::overlay_vdp(int /*priority_layer*/) {
    if (!vdp_enable_) return;
    for (int y = 0; y < kNativeHeight; y++) {
        for (int x = 0; x < kNativeWidth; x++) {
            const size_t i = size_t(y * kNativeWidth + x);
            if (vdp_pri_[i] == 0) continue;
            framebuffer_[i] = vdp_fb_[i];
        }
    }
}

void System18::update_video() {
    const uint32_t blank = video_.palette[0x1000];
    if (!video_.screen_enabled) {
        std::fill(framebuffer_.begin(), framebuffer_.end(), blank);
        return;
    }

    // Capture the Genesis VDP picture for this frame (scanlines already run).
    // Priority mixing is simplified: non-backdrop VDP pixels replace the
    // current layer when the VDP is enabled (mwalk uses mixing 0x04 / 0x07).
    const int vdplayer = (vdp_mixing_ >> 1) & 3;

    video_.render_tile_pages(bg_low_, bg_high_, 0, false, 5, 0x1fff, 0x8000, true, false);
    video_.render_tile_pages(fg_low_, fg_high_, 4, true, 5, 0x1fff, 0x8000, true, false);
    video_.render_text(text_low_, text_high_, 8, 0xff, 0x8000, true);

    int scroll_x1 = 0, scroll_y1 = video_.char_ram[0x749] & 0x1ff;
    int scroll_x2 = 0, scroll_y2 = video_.char_ram[0x748] & 0x1ff;
    bool row_back = (video_.char_ram[0x74d] & 0x8000) != 0;
    bool row_fore = (video_.char_ram[0x74c] & 0x8000) != 0;
    if (!row_back) scroll_x1 = (704 - (video_.char_ram[0x74d] & 0x3ff)) & 0x3ff;
    if (!row_fore) scroll_x2 = (704 - (video_.char_ram[0x74c] & 0x3ff)) & 0x3ff;

    auto blit_rows = [&](const std::vector<uint32_t>& src, bool row, int sx, int sy,
                         uint16_t table_base) {
        if (!row) {
            video_.blit_scrolled(framebuffer_.data(), src, sx, sy, 1024, 512);
            return;
        }
        for (int y = 0; y < kNativeHeight; y++) {
            const int line = (y + sy) & 0x1ff;
            const int row_i = (line >> 3) & 0x3f;
            const int rx = (704 - (video_.char_ram[table_base + row_i] & 0x3ff)) & 0x3ff;
            for (int x = 0; x < kNativeWidth; x++) {
                const uint32_t pixel = src[size_t(line * 1024 + ((x + rx) & 0x3ff))];
                if (pixel) framebuffer_[size_t(y * kNativeWidth + x)] = pixel;
            }
        }
    };

    auto maybe_vdp = [&](int layer) {
        if (vdp_enable_ && vdplayer == layer) overlay_vdp(layer);
    };

    std::fill(framebuffer_.begin(), framebuffer_.end(), blank);
    blit_rows(bg_low_, row_back, scroll_x1, scroll_y1, 0x7e0);
    maybe_vdp(0);
    draw_sprites_16b(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 0, 0x800);
    blit_rows(bg_high_, row_back, scroll_x1, scroll_y1, 0x7e0);
    maybe_vdp(1);
    draw_sprites_16b(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 1, 0x800);
    blit_rows(fg_low_, row_fore, scroll_x2, scroll_y2, 0x7c0);
    maybe_vdp(2);
    draw_sprites_16b(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 2, 0x800);
    blit_rows(fg_high_, row_fore, scroll_x2, scroll_y2, 0x7c0);
    video_.blit_text(framebuffer_.data(), text_low_);
    maybe_vdp(3);
    draw_sprites_16b(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 3, 0x800);
    video_.blit_text(framebuffer_.data(), text_high_);
}

void System18::run_frame() {
    const double slices = double(kScanlines * kCpuSync);
    const double main_cycles = double(kMainClock) / fps_ / slices;
    const double sound_cycles = double(kSoundClock) / fps_ / slices;
    const double mcu_cycles = mcu_ ? double(mcu_->clock()) / fps_ / slices : 0.0;
    const int samples_per_frame = int(std::lround(double(YM2612::kSampleRate) / fps_));
    int samples_done = 0;

    for (int line = 0; line < kScanlines; line++) {
        vdp_.handle_scanline(line);
        if (line < kNativeHeight) {
            const uint32_t* src = vdp_.line_buffer();
            const uint8_t* bd = vdp_.line_backdrop();
            const int w = std::min(vdp_.screen_width(), kNativeWidth);
            for (int x = 0; x < w; x++) {
                const size_t i = size_t(line * kNativeWidth + x);
                vdp_fb_[i] = src[x];
                vdp_pri_[i] = bd[x] ? 0 : 1;
            }
        }

        if (line == 224) {
            if (mcu_) mcu_->set_irq0_line(IrqLine::Hold);
            else main_cpu_.set_irq(4, IrqLine::Hold);
            update_video();
        }

        for (int slice = 0; slice < kCpuSync; slice++) {
            main_debt_ += main_cycles;
            main_debt_ -= main_cpu_.run(int(main_debt_));
            sound_debt_ += sound_cycles;
            sound_debt_ -= sound_cpu_.run(int(sound_debt_));
            if (mcu_) {
                mcu_debt_ += mcu_cycles;
                mcu_debt_ -= mcu_->run(int(mcu_debt_));
            }
        }

        // Spread audio samples evenly across the frame.
        const int target = (line + 1) * samples_per_frame / kScanlines;
        while (samples_done < target) {
            double mix = double(ym1_.update()) + double(ym2_.update());
            mix += double(rf5c68_.update());
            mix += double(vdp_.psg().update()) * 0.15;
            const int32_t sample = int32_t(std::lround(mix * kMixGain));
            audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
            samples_done++;
        }
    }
    vdp_.handle_eof();
}

}  // namespace dsp
