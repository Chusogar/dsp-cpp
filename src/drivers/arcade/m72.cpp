#include "drivers/arcade/m72.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

uint32_t pal5bit(uint8_t n) {
    n &= 0x1f;
    return uint32_t(n) * 255 / 31;
}

uint32_t argb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

bool load_raw(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    return loader.load(entries, dest, error);
}

bool load_16b(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> temp(entry.length);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, temp, error)) return false;
        size_t need = size_t(entry.offset) + size_t(entry.length) * 2;
        if (dest.size() < need) dest.resize(need, 0);
        for (uint32_t i = 0; i < entry.length; i++) dest[size_t(entry.offset) + i * 2] = temp[i];
    }
    return true;
}

const std::vector<int>& tile_x() {
    static const std::vector<int> offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    return offsets;
}

const std::vector<int>& sprite_x() {
    static const std::vector<int> offsets = {0, 1, 2, 3, 4, 5, 6, 7, 16 * 8 + 0, 16 * 8 + 1,
                                             16 * 8 + 2, 16 * 8 + 3, 16 * 8 + 4, 16 * 8 + 5,
                                             16 * 8 + 6, 16 * 8 + 7};
    return offsets;
}

const std::vector<int>& tile_y() {
    static const std::vector<int> offsets = {0 * 8, 1 * 8, 2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                                             8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};
    return offsets;
}

GfxLayout char_layout(int total, int plane_stride) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {plane_stride * 3, plane_stride * 2, plane_stride, 0};
    layout.x_offsets = tile_x();
    layout.y_offsets.assign(tile_y().begin(), tile_y().begin() + 8);
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 0x1000;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {0x60000 * 8, 0x40000 * 8, 0x20000 * 8, 0};
    layout.x_offsets = sprite_x();
    layout.y_offsets = tile_y();
    return layout;
}

}  // namespace

M72::M72(Game game)
    : game_(game),
      main_cpu_(kMainClock, NecV30::Type::V30),
      sound_cpu_(kSoundClock),
      ym_(kSoundClock) {
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_io16_handlers([this](uint32_t p) { return main_in_word(p); },
                                [this](uint32_t p, uint16_t v) { main_out_word(p, v); });
    main_cpu_.set_io_handlers([this](uint16_t p) { return main_in_byte(p); },
                              [this](uint16_t p, uint8_t v) { main_out_byte(p, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler([this](bool asserted) {
        if (asserted) snd_irq_vector_ &= 0xef;
        else snd_irq_vector_ |= 0x10;
        sound_irq_timer_ = true;
    });

    layer_bg_lo_.assign(kTileMapSize * kTileMapSize, 0);
    layer_bg_hi_.assign(kTileMapSize * kTileMapSize, 0);
    layer_fg_lo_.assign(kTileMapSize * kTileMapSize, 0);
    layer_fg_hi_.assign(kTileMapSize * kTileMapSize, 0);
    composite_.assign(kSpriteMapWidth * kSpriteMapHeight, 0);
    framebuffer_.assign(kScreenWidth * kScreenHeight, 0);
    nmi_period_ = int(kSoundClock / (128 * 55));
}

const char* M72::title() const {
    switch (game_) {
        case Game::Hharry: return "Hammerin' Harry";
        case Game::Rtype2: return "R-Type II";
        default: return "R-Type";
    }
}

bool M72::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> temp;

    if (game_ == Game::Rtype) {
        rom_.assign(0x40000, 0);
        if (!load_16b(loader,
                      {{"rt_r-h0-b.1b", 0x10000, 1, 0x591c7754},
                       {"rt_r-l0-b.3b", 0x10000, 0, 0xa1928df0},
                       {"rt_r-h1-b.1c", 0x10000, 0x20001, 0xa9d71eca},
                       {"rt_r-l1-b.3c", 0x10000, 0x20000, 0x0df3573d}},
                      rom_, error))
            return false;
        temp.assign(0x20000, 0);
        if (!load_raw(loader,
                      {{"rt_b-a0.3c", 0x8000, 0, 0x4e212fb0},
                       {"rt_b-a1.3d", 0x8000, 0x8000, 0x8a65bdff},
                       {"rt_b-a2.3a", 0x8000, 0x10000, 0x5a4ae5b9},
                       {"rt_b-a3.3e", 0x8000, 0x18000, 0x73327606}},
                      temp, error))
            return false;
        chars0_.decode(char_layout(0x1000, 0x8000 * 8), temp);
        temp.assign(0x20000, 0);
        if (!load_raw(loader,
                      {{"rt_b-b0.3j", 0x8000, 0, 0xa7b17491},
                       {"rt_b-b1.3k", 0x8000, 0x8000, 0xb9709686},
                       {"rt_b-b2.3h", 0x8000, 0x10000, 0x433b229a},
                       {"rt_b-b3.3f", 0x8000, 0x18000, 0xad89b072}},
                      temp, error))
            return false;
        chars1_.decode(char_layout(0x1000, 0x8000 * 8), temp);
        temp.assign(0x80000, 0);
        if (!load_raw(loader,
                      {{"rt_r-00.1h", 0x10000, 0, 0xdad53bc0},
                       {"rt_r-01.1j", 0x8000, 0x10000, 0x5e441e7f},
                       {"rt_r-01.1j", 0x8000, 0x18000, 0x5e441e7f},
                       {"rt_r-10.1k", 0x10000, 0x20000, 0xd6a66298},
                       {"rt_r-11.1l", 0x8000, 0x30000, 0x791df4f8},
                       {"rt_r-11.1l", 0x8000, 0x38000, 0x791df4f8},
                       {"rt_r-20.3h", 0x10000, 0x40000, 0xfc247c8a},
                       {"rt_r-21.3j", 0x8000, 0x50000, 0xed793841},
                       {"rt_r-21.3j", 0x8000, 0x58000, 0xed793841},
                       {"rt_r-30.3k", 0x10000, 0x60000, 0xeb02a1cb},
                       {"rt_r-31.3l", 0x8000, 0x70000, 0x8558355d},
                       {"rt_r-31.3l", 0x8000, 0x78000, 0x8558355d}},
                      temp, error))
            return false;
        sprites_.decode(sprite_layout(), temp);
        dsw_ = 0xfdfb;
    } else if (game_ == Game::Hharry) {
        rom_.assign(0x80000, 0);
        if (!load_16b(loader,
                      {{"a-h0-v.rom", 0x20000, 1, 0xc52802a5},
                       {"a-l0-v.rom", 0x20000, 0, 0xf463074c},
                       {"a-h1-0.rom", 0x10000, 0x60001, 0x3ae21335},
                       {"a-l1-0.rom", 0x10000, 0x60000, 0xbc6ac5f9}},
                      rom_, error))
            return false;
        std::vector<uint8_t> snd(0x10000, 0);
        if (!load_raw(loader, {{"a-sp-0.rom", 0x10000, 0, 0x80e210e7}}, snd, error)) return false;
        std::copy(snd.begin(), snd.end(), mem_snd_.begin());
        std::vector<uint8_t> dac(0x20000, 0);
        if (!load_raw(loader, {{"a-v0-0.rom", 0x20000, 0, 0xfaaacaff}}, dac, error)) return false;
        std::copy(dac.begin(), dac.end(), mem_dac_.begin());
        temp.assign(0x80000, 0);
        if (!load_raw(loader,
                      {{"hh_a0.rom", 0x20000, 0, 0xc577ba5f},
                       {"hh_a1.rom", 0x20000, 0x20000, 0x429d12ab},
                       {"hh_a2.rom", 0x20000, 0x40000, 0xb5b163b0},
                       {"hh_a3.rom", 0x20000, 0x60000, 0x8ef566a1}},
                      temp, error))
            return false;
        chars0_.decode(char_layout(0x4000, 0x8000 * 8 * 4), temp);
        temp.assign(0x80000, 0);
        if (!load_raw(loader,
                      {{"hh_00.rom", 0x20000, 0, 0xec5127ef},
                       {"hh_10.rom", 0x20000, 0x20000, 0xdef65294},
                       {"hh_20.rom", 0x20000, 0x40000, 0xbb0d6ad4},
                       {"hh_30.rom", 0x20000, 0x60000, 0x4351044e}},
                      temp, error))
            return false;
        sprites_.decode(sprite_layout(), temp);
        dsw_ = 0xfdbf;
    } else {
        rom_.assign(0x80000, 0);
        if (!load_16b(loader,
                      {{"rt2-a-h0-d.54", 0x20000, 1, 0xd8ece6f4},
                       {"rt2-a-l0-d.60", 0x20000, 0, 0x32cfb2e4},
                       {"rt2-a-h1-d.53", 0x20000, 0x40001, 0x4f6e9b15},
                       {"rt2-a-l1-d.59", 0x20000, 0x40000, 0x0fd123bf}},
                      rom_, error))
            return false;
        std::vector<uint8_t> snd(0x10000, 0);
        if (!load_raw(loader, {{"ic17.4f", 0x10000, 0, 0x73ffecb4}}, snd, error)) return false;
        std::copy(snd.begin(), snd.end(), mem_snd_.begin());
        std::vector<uint8_t> dac(0x20000, 0);
        if (!load_raw(loader, {{"ic14.4c", 0x20000, 0, 0x637172d5}}, dac, error)) return false;
        std::copy(dac.begin(), dac.end(), mem_dac_.begin());
        temp.assign(0x100000, 0);
        if (!load_raw(loader,
                      {{"ic50.7s", 0x20000, 0, 0xf3f8736e},
                       {"ic51.7u", 0x20000, 0x20000, 0xb4c543af},
                       {"ic56.8s", 0x20000, 0x40000, 0x4cb80d66},
                       {"ic57.8u", 0x20000, 0x60000, 0xbee128e0},
                       {"ic65.9r", 0x20000, 0x80000, 0x2dc9c71a},
                       {"ic66.9u", 0x20000, 0xa0000, 0x7533c428},
                       {"ic63.9m", 0x20000, 0xc0000, 0xa6ad67f2},
                       {"ic64.9p", 0x20000, 0xe0000, 0x3686d555}},
                      temp, error))
            return false;
        chars0_.decode(char_layout(0x8000, 0x8000 * 8 * 4 * 2), temp);
        temp.assign(0x80000, 0);
        if (!load_raw(loader,
                      {{"ic31.6l", 0x20000, 0, 0x2cd8f913},
                       {"ic21.4l", 0x20000, 0x20000, 0x5033066d},
                       {"ic32.6m", 0x20000, 0x40000, 0xec3a0450},
                       {"ic22.4m", 0x20000, 0x60000, 0xdb6176fc}},
                      temp, error))
            return false;
        sprites_.decode(sprite_layout(), temp);
        dsw_ = 0xf7ff;
    }

    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void M72::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    in0_ = 0xffff;
    in1_ = 0xffff;
    scroll_x1_ = scroll_x2_ = scroll_y1_ = scroll_y2_ = 0;
    snd_irq_vector_ = 0xff;
    sound_latch_ = 0;
    m72_raster_irq_position_ = 0;
    video_off_ = true;
    sample_addr_ = 0;
    irq_base_.fill(0);
    irq_pos_ = 0;
    sound_irq_timer_ = false;
    sound_reset_held_ = false;
    dac_sample_ = 0;
    nmi_counter_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    dirty_fg_.fill(true);
    dirty_bg_.fill(true);
    dirty_color_.fill(true);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0);
    std::fill(composite_.begin(), composite_.end(), 0);
}

void M72::set_inputs(const MachineInputs& inputs) {
    auto bit = [](uint16_t& port, uint16_t mask, bool pressed) {
        if (pressed) port = uint16_t(port & ~mask);
        else port = uint16_t(port | mask);
    };
    bit(in0_, 0x0001, inputs.player1.right);
    bit(in0_, 0x0002, inputs.player1.left);
    bit(in0_, 0x0004, inputs.player1.down);
    bit(in0_, 0x0008, inputs.player1.up);
    bit(in0_, 0x0020, inputs.player1.button3);
    bit(in0_, 0x0040, inputs.player1.button2);
    bit(in0_, 0x0080, inputs.player1.button1);
    bit(in1_, 0x0001, inputs.player1.start);
    bit(in1_, 0x0002, inputs.player2.start);
    bit(in1_, 0x0004, inputs.coin1);
    bit(in1_, 0x0008, inputs.coin2);
}

void M72::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = uint16_t((dsw_ & 0xff00) | value);
    if (bank == 1) dsw_ = uint16_t((dsw_ & 0x00ff) | (uint16_t(value) << 8));
}

void M72::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

void M72::set_sound_reset(bool held) {
    if (held && !sound_reset_held_) {
        sound_cpu_.reset();
    }
    if (!held && sound_reset_held_) sound_cpu_.reset();
    sound_reset_held_ = held;
}

void M72::sound_irq_ack() {
    if (snd_irq_vector_ == 0xff) sound_cpu_.set_irq(IrqLine::Clear);
    else sound_cpu_.set_irq(IrqLine::Assert, snd_irq_vector_);
    sound_irq_timer_ = false;
}

void M72::on_sound_cycles(int cycles) {
    if (sound_irq_timer_) sound_irq_ack();
    ym_.run_timers(cycles);
    if (has_dac()) {
        nmi_counter_ += cycles;
        while (nmi_counter_ >= nmi_period_) {
            nmi_counter_ -= nmi_period_;
            sound_cpu_.set_nmi(IrqLine::Pulse);
        }
    }
    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= kSoundClock;
        int32_t sample = ym_.update();
        if (has_dac()) sample += int32_t(dac_sample_) / 2;
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

uint8_t M72::sound_read(uint16_t address) { return mem_snd_[address]; }

void M72::sound_write(uint16_t address, uint8_t value) { mem_snd_[address] = value; }

uint8_t M72::sound_in(uint16_t port) {
    port &= 0xff;
    if (game_ == Game::Rtype) {
        if (port == 1) return ym_.status();
        if (port == 2) return sound_latch_;
        return 0xff;
    }
    if (port == 1) return ym_.status();
    if (port == 0x80) return sound_latch_;
    if (port == 0x84) return mem_dac_[sample_addr_ & 0x1ffff];
    return 0xff;
}

void M72::sound_out(uint16_t port, uint8_t value) {
    port &= 0xff;
    if (game_ == Game::Rtype) {
        if (port == 0) ym_.select_register(value);
        else if (port == 1) ym_.write(value);
        else if (port == 6) {
            snd_irq_vector_ |= 0x20;
            sound_irq_timer_ = true;
        }
        return;
    }
    if (port == 0) ym_.select_register(value);
    else if (port == 1) ym_.write(value);
    else if (port == 0x80) {
        sample_addr_ >>= 5;
        sample_addr_ = (sample_addr_ & 0xff00) | value;
        sample_addr_ <<= 5;
    } else if (port == 0x81) {
        sample_addr_ >>= 5;
        sample_addr_ = (sample_addr_ & 0xff) | (uint32_t(value) << 8);
        sample_addr_ <<= 5;
    } else if (port == 0x82) {
        dac_sample_ = int16_t(int8_t(value)) * 128;
        sample_addr_ = (sample_addr_ + 1) & 0x1ffff;
    } else if (port == 0x83) {
        snd_irq_vector_ |= 0x20;
        sound_irq_timer_ = true;
    }
}

uint8_t M72::main_read(uint32_t address) {
    address &= 0xfffff;
    if (game_ == Game::Rtype) {
        if (address <= 0x3ffff) return rom_[address];
        if (address >= 0x40000 && address <= 0x43fff) return ram_[address & 0x3fff];
        if (address >= 0xc0000 && address <= 0xc03ff) return spriteram_[address & 0x3ff];
        if (address >= 0xc8000 && address <= 0xc8bff) {
            if (address & 1) return 0xff;
            return uint8_t(palette1_[(address & 0x1ff) + (address & 0xc00)] + 0xe0);
        }
        if (address >= 0xcc000 && address <= 0xccbff) {
            if (address & 1) return 0xff;
            return uint8_t(palette2_[(address & 0x1ff) + (address & 0xc00)] + 0xe0);
        }
        if (address >= 0xd0000 && address <= 0xd3fff) return videoram1_[address & 0x3fff];
        if (address >= 0xd8000 && address <= 0xdbfff) return videoram2_[address & 0x3fff];
        if (address >= 0xe0000 && address <= 0xeffff) return mem_snd_[address & 0xffff];
        if (address >= 0xffff0) return rom_[address & 0x3ffff];
        return 0xff;
    }
    if (game_ == Game::Hharry) {
        if (address <= 0x7ffff) return rom_[address];
        if (address >= 0xa0000 && address <= 0xa3fff) return ram_[address & 0x3fff];
        if (address >= 0xc0000 && address <= 0xc03ff) return spriteram_[address & 0x3ff];
        if (address >= 0xc8000 && address <= 0xc8bff) {
            if (address & 1) return 0xff;
            return uint8_t(palette1_[(address & 0x1ff) + (address & 0xc00)] + 0xe0);
        }
        if (address >= 0xcc000 && address <= 0xccbff) {
            if (address & 1) return 0xff;
            return uint8_t(palette2_[(address & 0x1ff) + (address & 0xc00)] + 0xe0);
        }
        if (address >= 0xd0000 && address <= 0xd3fff) return videoram1_[address & 0x3fff];
        if (address >= 0xd8000 && address <= 0xdbfff) return videoram2_[address & 0x3fff];
        if (address >= 0xffff0) return rom_[address & 0x7ffff];
        return 0xff;
    }
    if (address <= 0x7ffff) return rom_[address];
    if (address >= 0xc0000 && address <= 0xc03ff) return spriteram_[address & 0x3ff];
    if (address >= 0xc8000 && address <= 0xc8bff) {
        if (address & 1) return 0xff;
        return uint8_t(palette1_[(address & 0x1ff) + (address & 0xc00)] + 0xe0);
    }
    if (address >= 0xd0000 && address <= 0xd3fff) return videoram1_[address & 0x3fff];
    if (address >= 0xd4000 && address <= 0xd7fff) return videoram2_[address & 0x3fff];
    if (address >= 0xd8000 && address <= 0xd8bff) {
        if (address & 1) return 0xff;
        return uint8_t(palette2_[(address & 0x1ff) + (address & 0xc00)] + 0xe0);
    }
    if (address >= 0xe0000 && address <= 0xe3fff) return ram_[address & 0x3fff];
    if (address >= 0xffff0) return rom_[address & 0x7ffff];
    return 0xff;
}

void M72::main_write(uint32_t address, uint8_t value) {
    address &= 0xfffff;
    if (game_ == Game::Rtype2) {
        if (address >= 0xb0000 && address <= 0xb0001) {
            sprite_buffer_ = spriteram_;
            return;
        }
        if (address >= 0xbc000 && address <= 0xbc001) {
            m72_raster_irq_position_ = uint16_t(value + 64);
            return;
        }
        if (address >= 0xc0000 && address <= 0xc03ff) {
            spriteram_[address & 0x3ff] = value;
            return;
        }
        if (address >= 0xc8000 && address <= 0xc8bff) {
            palette1_[(address & 0x1ff) + (address & 0xc00)] = value;
            change_color1(uint16_t(address & 0x1fe));
            return;
        }
        if (address >= 0xd0000 && address <= 0xd3fff) {
            videoram1_[address & 0x3fff] = value;
            dirty_fg_[(address & 0x3fff) >> 2] = true;
            return;
        }
        if (address >= 0xd4000 && address <= 0xd7fff) {
            videoram2_[address & 0x3fff] = value;
            dirty_bg_[(address & 0x3fff) >> 2] = true;
            return;
        }
        if (address >= 0xd8000 && address <= 0xd8bff) {
            palette2_[(address & 0x1ff) + (address & 0xc00)] = value;
            change_color2(uint16_t(address & 0x1fe));
            return;
        }
        if (address >= 0xe0000 && address <= 0xe3fff) ram_[address & 0x3fff] = value;
        return;
    }

    if (address <= rom_mask() || address >= 0xffff0) return;
    uint32_t ram_base = (game_ == Game::Hharry) ? 0xa0000u : 0x40000u;
    if (address >= ram_base && address <= ram_base + 0x3fff) {
        ram_[address & 0x3fff] = value;
        return;
    }
    if (address >= 0xc0000 && address <= 0xc03ff) {
        spriteram_[address & 0x3ff] = value;
        return;
    }
    if (address >= 0xc8000 && address <= 0xc8bff) {
        palette1_[(address & 0x1ff) + (address & 0xc00)] = value;
        change_color1(uint16_t(address & 0x1fe));
        return;
    }
    if (address >= 0xcc000 && address <= 0xccbff) {
        palette2_[(address & 0x1ff) + (address & 0xc00)] = value;
        change_color2(uint16_t(address & 0x1fe));
        return;
    }
    if (address >= 0xd0000 && address <= 0xd3fff) {
        videoram1_[address & 0x3fff] = value;
        dirty_fg_[(address & 0x3fff) >> 2] = true;
        return;
    }
    if (address >= 0xd8000 && address <= 0xdbfff) {
        videoram2_[address & 0x3fff] = value;
        dirty_bg_[(address & 0x3fff) >> 2] = true;
        return;
    }
    if (game_ == Game::Rtype && address >= 0xe0000 && address <= 0xeffff) {
        mem_snd_[address & 0xffff] = value;
    }
}

void M72::main_out_word(uint32_t port, uint16_t value) {
    port &= 0xff;
    if (game_ == Game::Rtype2) {
        switch (port) {
            case 0:
                sound_latch_ = uint8_t(value);
                snd_irq_vector_ &= 0xdf;
                sound_irq_timer_ = true;
                break;
            case 2: video_off_ = (value & 0x08) != 0; break;
            case 0x40:
                irq_base_[0] = uint8_t(value);
                irq_pos_ = 1;
                break;
            case 0x42:
                if (irq_pos_ < irq_base_.size()) irq_base_[irq_pos_] = uint8_t(value);
                irq_pos_++;
                break;
            case 0x80: scroll_y1_ = value; break;
            case 0x82: scroll_x1_ = value; break;
            case 0x84: scroll_y2_ = value; break;
            case 0x86: scroll_x2_ = value; break;
            default: break;
        }
        return;
    }
    switch (port) {
        case 0:
            sound_latch_ = uint8_t(value);
            snd_irq_vector_ &= 0xdf;
            sound_irq_timer_ = true;
            break;
        case 2:
            if (game_ == Game::Rtype) set_sound_reset((value & 0x10) == 0);
            video_off_ = (value & 0x08) != 0;
            break;
        case 4:
            sprite_buffer_ = spriteram_;
            spriteram_.fill(0);
            break;
        case 6: m72_raster_irq_position_ = uint16_t(value - 128); break;
        case 0x40:
            irq_base_[0] = uint8_t(value);
            irq_pos_ = 1;
            break;
        case 0x42:
            if (irq_pos_ < irq_base_.size()) irq_base_[irq_pos_] = uint8_t(value);
            irq_pos_++;
            break;
        case 0x80: scroll_y1_ = value; break;
        case 0x82: scroll_x1_ = value; break;
        case 0x84: scroll_y2_ = value; break;
        case 0x86: scroll_x2_ = value; break;
        default: break;
    }
}

uint16_t M72::main_in_word(uint32_t port) {
    port &= 0xff;
    if (game_ == Game::Rtype2) {
        if (port == 0) return in0_;
        if (port == 2) return in1_;
        if (port == 4) return dsw_;
        return 0xffff;
    }
    if (port == 0) return uint16_t(0xff00 | (in0_ & 0xff));
    if (port == 2) return uint16_t(0xff00 | (in1_ & 0xff));
    if (port == 4) return dsw_;
    return 0xffff;
}

uint8_t M72::main_in_byte(uint16_t port) {
    uint16_t word = main_in_word(port);
    return (port & 1) ? uint8_t(word >> 8) : uint8_t(word);
}

void M72::main_out_byte(uint16_t port, uint8_t value) { main_out_word(port, value); }

void M72::change_color1(uint16_t num) {
    uint8_t r = pal5bit(palette1_[num]);
    uint8_t g = pal5bit(palette1_[num + 0x400]);
    uint8_t b = pal5bit(palette1_[num + 0x800]);
    palette_[num >> 1] = argb(r, g, b);
}

void M72::change_color2(uint16_t num) {
    uint8_t r = pal5bit(palette2_[num]);
    uint8_t g = pal5bit(palette2_[num + 0x400]);
    uint8_t b = pal5bit(palette2_[num + 0x800]);
    uint16_t index = uint16_t((num >> 1) + 0x100);
    palette_[index] = argb(r, g, b);
    dirty_color_[(index >> 4) & 0xf] = true;
}

void M72::draw_tile(std::vector<uint32_t>& dest, const GfxSet& gfx, int x, int y, int code,
                    int color, bool flipx, bool flipy, bool transparent) {
    const uint8_t* pixels = gfx.element(code);
    int width = gfx.width();
    int height = gfx.height();
    for (int row = 0; row < height; row++) {
        int sy = flipy ? height - 1 - row : row;
        int dy = y + row;
        if (dy < 0 || dy >= kTileMapSize) continue;
        for (int col = 0; col < width; col++) {
            int sx = flipx ? width - 1 - col : col;
            uint8_t pen = pixels[sy * width + sx];
            int dx = x + col;
            if (dx < 0 || dx >= kTileMapSize) continue;
            if (transparent && pen == 0) {
                dest[size_t(dy * kTileMapSize + dx)] = kTransparent;
                continue;
            }
            dest[size_t(dy * kTileMapSize + dx)] = palette_[size_t((color + pen) & 0x1ff)];
        }
    }
}

void M72::clear_tile(std::vector<uint32_t>& dest, int x, int y) {
    for (int row = 0; row < 8; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= kTileMapSize) continue;
        for (int col = 0; col < 8; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= kTileMapSize) continue;
            dest[size_t(dy * kTileMapSize + dx)] = kTransparent;
        }
    }
}

void M72::update_video() {
    const GfxSet* fg = &chars0_;
    const GfxSet* bg = (game_ == Game::Rtype) ? &chars1_ : &chars0_;
    int code_mask = (game_ == Game::Rtype) ? 0xfff : (game_ == Game::Hharry) ? 0x3fff : 0x7fff;

    for (int f = 0; f < 0x1000; f++) {
        int x = (f % 64) * 8;
        int y = (f / 64) * 8;

        uint8_t atrib2 = videoram2_[size_t(f * 4 + 2)];
        uint8_t color = atrib2 & 0xf;
        if (dirty_bg_[size_t(f)] || dirty_color_[color]) {
            int nchar;
            bool flipx, flipy, priority;
            if (game_ == Game::Rtype2) {
                uint8_t atrib = videoram2_[size_t(f * 4 + 3)];
                nchar = (videoram2_[size_t(f * 4)] | (videoram2_[size_t(f * 4 + 1)] << 8)) & code_mask;
                flipx = (atrib2 & 0x20) != 0;
                flipy = (atrib2 & 0x40) != 0;
                priority = (atrib & 0x01) != 0;
            } else {
                uint8_t atrib = videoram2_[size_t(f * 4 + 1)];
                nchar = (videoram2_[size_t(f * 4)] | ((atrib & 0x3f) << 8)) & code_mask;
                flipx = (atrib & 0x40) != 0;
                flipy = (atrib & 0x80) != 0;
                priority = (atrib2 & 0x80) != 0;
            }
            int pal = (color << 4) + 256;
            if (!priority) {
                draw_tile(layer_bg_lo_, *bg, x, y, nchar, pal, flipx, flipy, false);
                clear_tile(layer_bg_hi_, x, y);
            } else {
                clear_tile(layer_bg_lo_, x, y);
                draw_tile(layer_bg_hi_, *bg, x, y, nchar, pal, flipx, flipy, true);
            }
            dirty_bg_[size_t(f)] = false;
        }

        atrib2 = videoram1_[size_t(f * 4 + 2)];
        color = atrib2 & 0xf;
        if (dirty_fg_[size_t(f)] || dirty_color_[color]) {
            int nchar;
            bool flipx, flipy, priority;
            if (game_ == Game::Rtype2) {
                uint8_t atrib = videoram1_[size_t(f * 4 + 3)];
                nchar = (videoram1_[size_t(f * 4)] | (videoram1_[size_t(f * 4 + 1)] << 8)) & code_mask;
                flipx = (atrib2 & 0x20) != 0;
                flipy = (atrib2 & 0x40) != 0;
                priority = (atrib & 0x01) != 0;
            } else {
                uint8_t atrib = videoram1_[size_t(f * 4 + 1)];
                nchar = (videoram1_[size_t(f * 4)] | ((atrib & 0x3f) << 8)) & code_mask;
                flipx = (atrib & 0x40) != 0;
                flipy = (atrib & 0x80) != 0;
                priority = (atrib2 & 0x80) != 0;
            }
            int pal = (color << 4) + 256;
            if (!priority) {
                draw_tile(layer_fg_lo_, *fg, x, y, nchar, pal, flipx, flipy, true);
                clear_tile(layer_fg_hi_, x, y);
            } else {
                clear_tile(layer_fg_lo_, x, y);
                draw_tile(layer_fg_hi_, *fg, x, y, nchar, pal, flipx, flipy, true);
            }
            dirty_fg_[size_t(f)] = false;
        }
    }
    dirty_color_.fill(false);
}

void M72::blit_layer(const std::vector<uint32_t>& src, int src_w, int src_h, int scroll_x,
                     int scroll_y, bool opaque) {
    for (int y = 0; y < kSpriteMapHeight; y++) {
        int sy = y + scroll_y;
        sy %= src_h;
        if (sy < 0) sy += src_h;
        for (int x = 0; x < kSpriteMapWidth; x++) {
            int sx = x + scroll_x;
            sx %= src_w;
            if (sx < 0) sx += src_w;
            uint32_t pixel = src[size_t(sy * src_w + sx)];
            if (opaque || pixel != kTransparent) {
                composite_[size_t(y * kSpriteMapWidth + x)] = pixel;
            }
        }
    }
}

void M72::draw_sprites() {
    for (int f = 0; f < 0x80; f++) {
        uint16_t nchar =
            uint16_t((sprite_buffer_[size_t(f * 8 + 2)] | (sprite_buffer_[size_t(f * 8 + 3)] << 8)) &
                     0xfff);
        uint16_t atrib =
            uint16_t(sprite_buffer_[size_t(f * 8 + 4)] | (sprite_buffer_[size_t(f * 8 + 5)] << 8));
        int color = (atrib & 0xf) << 4;
        int x = int(sprite_buffer_[size_t(f * 8 + 6)] | (sprite_buffer_[size_t(f * 8 + 7)] << 8)) - 256;
        int y = 384 - int(sprite_buffer_[size_t(f * 8 + 0)] | (sprite_buffer_[size_t(f * 8 + 1)] << 8));
        bool flipx = (atrib & 0x800) != 0;
        bool flipy = (atrib & 0x400) != 0;
        int w = 1 << ((atrib >> 14) & 3);
        int h = 1 << ((atrib >> 12) & 3);
        y -= 16 * h;
        for (int wx = 0; wx < w; wx++) {
            for (int wy = 0; wy < h; wy++) {
                int c = nchar;
                if (flipx) c += 8 * (w - 1 - wx);
                else c += 8 * wx;
                if (flipy) c += h - 1 - wy;
                else c += wy;
                const uint8_t* pixels = sprites_.element(c & 0xfff);
                int dx = (x + 16 * wx) & 0x3ff;
                int dy = (y + 16 * wy) & 0x1ff;
                for (int row = 0; row < 16; row++) {
                    int sy = flipy ? 15 - row : row;
                    int py = (dy + row) & 0x1ff;
                    for (int col = 0; col < 16; col++) {
                        int sx = flipx ? 15 - col : col;
                        uint8_t pen = pixels[sy * 16 + sx];
                        if (pen == 0) continue;
                        int px = (dx + col) & 0x3ff;
                        composite_[size_t(py * kSpriteMapWidth + px)] =
                            palette_[size_t((color + pen) & 0x1ff)];
                    }
                }
            }
        }
    }
}

void M72::crop_to_screen(int line_from, int line_to) {
    if (line_to < line_from) return;
    int height = line_to - line_from;
    if (height <= 0) return;
    for (int y = 0; y < height; y++) {
        int src_y = line_from + y;
        if (src_y < 0 || src_y >= kScreenHeight) continue;
        const uint32_t* src = composite_.data() + size_t(src_y * kSpriteMapWidth + 64);
        uint32_t* dst = framebuffer_.data() + size_t(src_y * kScreenWidth);
        for (int x = 0; x < kScreenWidth; x++) dst[x] = src[x];
    }
}

void M72::paint_video(int line_from, int line_to) {
    int bg_x = scroll_x2_;
    int bg_y = scroll_y2_ + 128;
    int fg_x = scroll_x1_;
    int fg_y = scroll_y1_ + 129;
    if (game_ != Game::Rtype) {
        bg_x = scroll_x2_ - 6;
        fg_x = scroll_x1_ - 4;
        bg_y = scroll_y2_ + 128;
        fg_y = scroll_y1_ + 128;
    }
    std::fill(composite_.begin(), composite_.end(), 0);
    blit_layer(layer_bg_lo_, kTileMapSize, kTileMapSize, bg_x, bg_y, true);
    blit_layer(layer_fg_lo_, kTileMapSize, kTileMapSize, fg_x, fg_y, false);
    draw_sprites();
    blit_layer(layer_bg_hi_, kTileMapSize, kTileMapSize, bg_x, bg_y, false);
    blit_layer(layer_fg_hi_, kTileMapSize, kTileMapSize, fg_x, fg_y, false);
    crop_to_screen(line_from, line_to + 1);
}

void M72::run_frame() {
    const int main_cycles = int(kMainClock / kFramesPerSecond / kScanlines);
    const int sound_cycles = int(kSoundClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        main_cpu_.run(main_cycles);
        if (!sound_reset_held_) sound_cpu_.run(sound_cycles);
        else if (has_dac()) {
            // Keep DAC/YM timers moving while the Z80 is held.
            on_sound_cycles(sound_cycles);
        }
        if (line < 255 && line == int(m72_raster_irq_position_) - 1) {
            main_cpu_.set_irq(IrqLine::Hold, uint8_t(irq_base_[1] + 2));
            if (!video_off_) paint_video(0, line);
        }
        if (line == 255) {
            main_cpu_.set_irq(IrqLine::Hold, irq_base_[1]);
            if (!video_off_) {
                paint_video(int(m72_raster_irq_position_) & 0xff, line);
                update_video();
            } else {
                std::fill(framebuffer_.begin(), framebuffer_.end(), 0);
            }
        }
    }
}

}  // namespace dsp
