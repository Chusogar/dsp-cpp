#include "drivers/starwars.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"136021.214.1f|136021.214|136021-214.1f", 0x4000, 0x6000, 0x04f1876e},
    {"136021.102.1hj|136021.102|136021-102.1hj", 0x2000, 0x8000, 0xf725e344},
    {"136021.203.1jk|136021.203|136021-203.1jk", 0x2000, 0xa000, 0xf6da0a00},
    {"136021.104.1kl|136021.104|136021-104.1kl", 0x2000, 0xc000, 0x7e406703},
    {"136021.206.1m|136021.206|136021-206.1m", 0x2000, 0xe000, 0xc7e51237},
};

const std::vector<RomEntry> kVectorRom = {
    {"136021-105.1l|136021.105|136021-105", 0x1000, 0, 0x538e7d2f},
};

const std::vector<RomEntry> kSoundRoms = {
    {"136021-107.1jk|136021.107|136021-107", 0x2000, 0x4000, 0xdbf3aea2},
    {"136021-208.1h|136021.208|136021-208", 0x2000, 0x6000, 0xe38070a8},
};

const std::vector<RomEntry> kAvgProm = {
    {"136021-109.4b|136021.109|136021-109", 0x100, 0, 0x82fc3eb2},
};

const std::vector<RomEntry> kMathProms = {
    {"136021-110.7h|136021.110|136021-110", 0x400, 0x000, 0x810e040e},
    {"136021-111.7j|136021.111|136021-111", 0x400, 0x400, 0xae69881c},
    {"136021-112.7k|136021.112|136021-112", 0x400, 0x800, 0xecf22628},
    {"136021-113.7l|136021.113|136021-113", 0x400, 0xc00, 0x83febfde},
};

constexpr int kIrqCycles = int(StarWars::kCpuClock / (StarWars::kClock3k / 12.0) + 0.5);

void blend_pixel(uint32_t& dest, uint32_t color, int intensity) {
    if (intensity <= 0) return;
    if (intensity > 255) intensity = 255;
    const int r = int((color >> 16) & 0xff) * intensity / 255;
    const int g = int((color >> 8) & 0xff) * intensity / 255;
    const int b = int(color & 0xff) * intensity / 255;
    const int dr = std::max(int((dest >> 16) & 0xff), r);
    const int dg = std::max(int((dest >> 8) & 0xff), g);
    const int db = std::max(int(dest & 0xff), b);
    dest = 0xff000000u | (uint32_t(dr) << 16) | (uint32_t(dg) << 8) | uint32_t(db);
}

}  // namespace

const char* StarWars::title() const {
    return game_ == Game::Empire ? "The Empire Strikes Back" : "Star Wars";
}

StarWars::StarWars(Game game)
    : game_(game),
      main_cpu_(kCpuClock),
      sound_cpu_(kCpuClock),
      pokey0_(kCpuClock, 0.20f),
      pokey1_(kCpuClock, 0.20f),
      pokey2_(kCpuClock, 0.20f),
      pokey3_(kCpuClock, 0.20f),
      tms_(kMasterClock / 2 / 9),
      slapstic_(101, nullptr) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_cycle_handler([this](int cycles) { on_main_cycles(cycles); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    riot_.set_irq_callback([this](IrqLine line) { sound_cpu_.set_irq(line); });
    riot_.set_pa(
        [this]() {
            uint8_t value = 0x10;  // PA4 = not self-test
            // MAME wires TMS /READY directly to PA2 (1 = busy).
            if (tms_.readyq()) value |= 0x04;
            if (main_pending_) value |= 0x40;
            if (sound_pending_) value |= 0x80;
            return value;
        },
        [this](uint8_t value) {
            riot_pa_out_ = value;
            // MAME: PA0=/WS, PA1=/RS. Data is latched on PB; /WS commits it.
            tms_.strobe_ws_rs(uint8_t(value & 0x03));
        });
    riot_.set_pb([this]() { return tms_.status(); },
                 [this](uint8_t value) { tms_.set_data_latch(value); });

    avg_.set_memory([this](uint16_t address) { return avg_read(address); });
}

bool StarWars::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    const bool ok = (game_ == Game::Empire) ? load_esb(loader, error) : load_starwars(loader, error);
    if (!ok) return false;
    warnings_ = loader.warnings();
    reset();
    return true;
}

bool StarWars::load_starwars(RomLoader& loader, std::string* error) {
    auto load_at = [&](const RomEntry& entry, std::vector<uint8_t>& dest) -> bool {
        dest.assign(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        return loader.load({single}, dest, error);
    };

    std::fill(main_rom_.begin(), main_rom_.end(), 0);
    std::vector<uint8_t> rom0;
    if (!load_at(kMainRoms[0], rom0)) return false;
    std::memcpy(main_rom_.data() + 0x6000, rom0.data(), 0x2000);
    std::memcpy(main_rom_.data() + 0x10000, rom0.data() + 0x2000, 0x2000);
    for (size_t i = 1; i < kMainRoms.size(); i++) {
        std::vector<uint8_t> chunk;
        if (!load_at(kMainRoms[i], chunk)) return false;
        std::memcpy(main_rom_.data() + kMainRoms[i].offset, chunk.data(), chunk.size());
    }

    std::vector<uint8_t> vec;
    if (!load_at(kVectorRom[0], vec)) return false;
    std::copy(vec.begin(), vec.end(), vector_rom_.begin());

    std::vector<uint8_t> sound(0x10000, 0);
    if (!loader.load(kSoundRoms, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());
    std::memcpy(sound_rom_.data() + 0xc000, sound_rom_.data() + 0x4000, 0x2000);
    std::memcpy(sound_rom_.data() + 0xe000, sound_rom_.data() + 0x6000, 0x2000);

    std::vector<uint8_t> avg_prom;
    if (!load_at(kAvgProm[0], avg_prom)) return false;
    avg_.set_prom(avg_prom.data(), avg_prom.size());

    std::vector<uint8_t> math_prom(0x1000, 0);
    if (!loader.load(kMathProms, math_prom, error)) return false;
    math_.init(math_prom.data());
    return true;
}

bool StarWars::load_esb(RomLoader& loader, std::string* error) {
    auto load_file = [&](const char* name, uint32_t length, uint32_t crc,
                         std::vector<uint8_t>& dest) -> bool {
        dest.assign(length, 0);
        RomEntry single{name, length, 0, crc};
        return loader.load({single}, dest, error);
    };

    std::fill(main_rom_.begin(), main_rom_.end(), 0);

    std::vector<uint8_t> rom;
    if (!load_file("136031-101.1f|136031.101|136031-101", 0x4000, 0xef1e3ae5, rom)) return false;
    std::memcpy(main_rom_.data() + 0x6000, rom.data(), 0x2000);
    std::memcpy(main_rom_.data() + 0x10000, rom.data() + 0x2000, 0x2000);

    if (!load_file("136031-102.1jk|136031.102|136031-102", 0x4000, 0x62ce5c12, rom)) return false;
    std::memcpy(main_rom_.data() + 0xa000, rom.data(), 0x2000);
    std::memcpy(main_rom_.data() + 0x1c000, rom.data() + 0x2000, 0x2000);

    if (!load_file("136031-203.1kl|136031.203|136031-203", 0x4000, 0x27b0889b, rom)) return false;
    std::memcpy(main_rom_.data() + 0xc000, rom.data(), 0x2000);
    std::memcpy(main_rom_.data() + 0x1e000, rom.data() + 0x2000, 0x2000);

    if (!load_file("136031-104.1m|136031.104|136031-104", 0x4000, 0xfd5c725e, rom)) return false;
    std::memcpy(main_rom_.data() + 0xe000, rom.data(), 0x2000);
    std::memcpy(main_rom_.data() + 0x20000, rom.data() + 0x2000, 0x2000);

    if (!load_file("136031-105.3u|136031.105|136031-105", 0x4000, 0xea9e4dce, rom)) return false;
    std::memcpy(main_rom_.data() + 0x14000, rom.data(), 0x4000);

    if (!load_file("136031-106.2u|136031.106|136031-106", 0x4000, 0x76d07f59, rom)) return false;
    std::memcpy(main_rom_.data() + 0x18000, rom.data(), 0x4000);

    if (!load_file("136031-111.1l|136031.111|136031-111", 0x1000, 0xb1f9bd12, rom)) return false;
    std::copy(rom.begin(), rom.end(), vector_rom_.begin());

    std::fill(sound_rom_.begin(), sound_rom_.end(), 0);
    if (!load_file("136031-113.1jk|136031.113|136031-113", 0x4000, 0x24ae3815, rom)) return false;
    std::memcpy(sound_rom_.data() + 0x4000, rom.data(), 0x2000);
    std::memcpy(sound_rom_.data() + 0xc000, rom.data() + 0x2000, 0x2000);

    if (!load_file("136031-112.1h|136031.112|136031-112", 0x4000, 0xca72d341, rom)) return false;
    std::memcpy(sound_rom_.data() + 0x6000, rom.data(), 0x2000);
    std::memcpy(sound_rom_.data() + 0xe000, rom.data() + 0x2000, 0x2000);

    if (!load_file("136021-109.4b|136021.109|136021-109", 0x100, 0x82fc3eb2, rom)) return false;
    avg_.set_prom(rom.data(), rom.size());

    const std::vector<RomEntry> math = {
        {"136031-110.7h|136031.110|136031-110", 0x400, 0x000, 0xb8d0f69d},
        {"136031-109.7j|136031.109|136031-109", 0x400, 0x400, 0x6a2a4d98},
        {"136031-108.7k|136031.108|136031-108", 0x400, 0x800, 0x6a76138f},
        {"136031-107.7l|136031.107|136031-107", 0x400, 0xc00, 0xafbf6e01},
    };
    std::vector<uint8_t> math_prom(0x1000, 0);
    if (!loader.load(math, math_prom, error)) return false;
    math_.init(math_prom.data());
    return true;
}

void StarWars::reset() {
    vector_ram_.fill(0);
    work_ram_.fill(0);
    sound_ram_.fill(0);
    math_.ram().fill(0);
    math_.reset();
    // Xicor X2212: 256×4 SRAM powers up 0xFF (MAME x2212_device).
    nvram_.fill(0xff);
    nvram_eeprom_.fill(0xff);
    nvram_store_ = false;
    nvram_recall_ = false;
    main_cpu_.reset();
    sound_cpu_.reset();
    pokey0_.reset();
    pokey1_.reset();
    pokey2_.reset();
    pokey3_.reset();
    tms_.reset();
    riot_.reset();
    avg_.reset();
    bank_ = 0;
    bank2_ = 0;
    slapstic_.reset();
    outlatch_ = 0;
    sound_latch_ = main_latch_ = 0;
    sound_pending_ = main_pending_ = false;
    sound_writes_ = 0;
    main_writes_ = 0;
    sound_resets_ = 0;
    analog_x_ = analog_y_ = 0x80;
    adc_value_ = 0x80;
    adc_channel_ = 0;
    prng_ = 0x1;
    audio_accumulator_ = 0;
    audio_.clear();
    in0_ = 0xff;
    in1_ = 0x3f;
    // DSW0. Attract music ($622D) only latches $4814 when the starting-
    // shields nibble in NVRAM is 0 (6 shields). MAME 0.260 defaulted to
    // 8 shields; the operator manual / current MAME use 6.
    // ESB inverts Demo Sounds (bit 6 = 1 → ON) and remaps shields/Jedi.
    dsw0_ = (game_ == Game::Empire) ? uint8_t(0xf3) : uint8_t(0x94);
    riot_pa_out_ = 0xff;
    tms_.strobe_ws_rs(0x03);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
    // Sound init posts $5A to the main latch. The 6809 handshake at $EFD1
    // is a single attempt — if main_pending is still clear it resets the
    // sound CPU and never retries.
    catch_up_sound();
}

uint8_t StarWars::avg_read(uint16_t address) const {
    if (address < 0x3000) return vector_ram_[address];
    if (address < 0x4000) return vector_rom_[address - 0x3000];
    return 0;
}

uint8_t StarWars::main_read(uint16_t address) {
    if (address < 0x3000) return vector_ram_[address];
    if (address < 0x4000) return vector_rom_[address - 0x3000];
    if (address >= 0x4300 && address <= 0x431f) return in0_;
    if (address >= 0x4320 && address <= 0x433f) {
        uint8_t value = uint8_t(in1_ & 0x3f);
        if (avg_.done()) value |= 0x40;
        if (math_.running()) value |= 0x80;
        return value;
    }
    if (address >= 0x4340 && address <= 0x435f) return dsw0_;
    if (address >= 0x4360 && address <= 0x437f) return dsw1_;
    if (address >= 0x4380 && address <= 0x439f) return adc_value_;
    if (address == 0x4400) {
        main_pending_ = false;
        return main_latch_;
    }
    if (address == 0x4401) {
        // Attract send ($BCE9) polls bit 7 until the sound CPU ACKs. A
        // 14-iteration wait is only ~100 main cycles, so the sound CPU
        // must run here even after $5A has already been posted.
        catch_up_sound(8192);
        return uint8_t((sound_pending_ ? 0x80 : 0) | (main_pending_ ? 0x40 : 0));
    }
    if (address >= 0x4500 && address <= 0x45ff) {
        // Unmapped high nibble is 0xF0, matching MAME's space.unmap().
        return uint8_t((nvram_[address & 0xff] & 0x0f) | 0xf0);
    }
    if (address == 0x4700) return math_.div_reh();
    if (address == 0x4701) return math_.div_rel();
    if (address == 0x4703) return uint8_t((prng_ >> 8) & 0xff);
    if (address >= 0x4800 && address <= 0x4fff) return work_ram_[address & 0x7ff];
    if (address >= 0x5000 && address <= 0x5fff) return math_.ram()[address & 0xfff];
    if (address >= 0x6000 && address <= 0x7fff) {
        const uint32_t base = bank_ ? 0x10000u : 0x6000u;
        return main_rom_[base + (address & 0x1fff)];
    }
    if (game_ == Game::Empire) {
        if (address >= 0x8000 && address <= 0x9fff) {
            const uint8_t bank = slapstic_.tweak(uint16_t(address & 0x1fff));
            return main_rom_[0x14000 + uint32_t(bank & 3) * 0x2000 + (address & 0x1fff)];
        }
        if (address >= 0xa000) {
            const uint32_t base = bank2_ ? 0x1c000u : 0xa000u;
            return main_rom_[base + (address - 0xa000)];
        }
    }
    if (address >= 0x8000) return main_rom_[address];
    return 0xff;
}

void StarWars::main_write(uint16_t address, uint8_t value) {
    if (address < 0x3000) {
        vector_ram_[address] = value;
        return;
    }
    if (address == 0x4400) {
        sound_latch_ = value;
        sound_pending_ = true;
        ++sound_writes_;
        catch_up_sound(8192);
        return;
    }
    if (address >= 0x4500 && address <= 0x45ff) {
        nvram_[address & 0xff] = uint8_t(value & 0x0f);
        return;
    }
    if (address >= 0x4640 && address <= 0x465f) {
        return;  // watchdog
    }
    if (address >= 0x46a0 && address <= 0x46bf) {
        if (!nvram_store_) nvram_eeprom_ = nvram_;
        nvram_store_ = true;
        return;
    }
    if (address >= 0x4600 && address <= 0x461f) {
        avg_.go();
        return;
    }
    if (address >= 0x4620 && address <= 0x463f) {
        avg_.vg_reset();
        return;
    }
    if (address >= 0x4660 && address <= 0x467f) {
        main_cpu_.set_irq(IrqLine::Clear);
        return;
    }
    if (address >= 0x4680 && address <= 0x469f) {
        outlatch_w(address & 7, (value & 0x80) != 0);
        return;
    }
    if (address >= 0x46c0 && address <= 0x46c3) {
        adc_channel_ = address & 3;
        adc_value_ = adc_channel(adc_channel_);
        return;
    }
    if (address == 0x46e0) {
        sound_pending_ = false;
        main_pending_ = false;
        ++sound_resets_;
        sound_cpu_.reset();
        catch_up_sound();
        return;
    }
    if (address >= 0x4700 && address <= 0x4707) {
        math_.write(uint8_t(address & 7), value);
        return;
    }
    if (address >= 0x4800 && address <= 0x4fff) {
        work_ram_[address & 0x7ff] = value;
        return;
    }
    if (address >= 0x5000 && address <= 0x5fff) {
        math_.ram()[address & 0xfff] = value;
        return;
    }
}

void StarWars::catch_up_sound(int cycles) {
    // Init clears $2000-$27FF and RIOT RAM before posting $5A; a short
    // burst leaves the handshake seeing an empty latch. Latch polls use
    // a smaller burst so $BCE9 can see the sound CPU ACK within 14 reads.
    if (cycles > 0) sound_cpu_.run(cycles);
}

void StarWars::outlatch_w(int bit, bool value) {
    if (value) outlatch_ |= uint8_t(1u << bit);
    else outlatch_ = uint8_t(outlatch_ & ~(1u << bit));
    if (bit == 4) {
        bank_ = value ? 1 : 0;
        bank2_ = bank_;
    }
    if (bit == 7) {
        // LS259 Q7 → X2212 /RECALL (MAME treats a 0→1 edge as recall).
        if (value && !nvram_recall_) nvram_ = nvram_eeprom_;
        nvram_recall_ = value;
    }
}

uint8_t StarWars::sound_read(uint16_t address) {
    if (address >= 0x0800 && address <= 0x0fff) {
        sound_pending_ = false;
        return sound_latch_;
    }
    if (address >= 0x1000 && address <= 0x107f) return riot_.ram_read(uint8_t(address));
    if (address >= 0x1080 && address <= 0x109f) return riot_.io_read(uint8_t(address));
    if (address >= 0x2000 && address <= 0x27ff) return sound_ram_[address & 0x7ff];
    if (address >= 0x4000) return sound_rom_[address];
    return 0xff;
}

void StarWars::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x07ff) {
        main_latch_ = value;
        main_pending_ = true;
        ++main_writes_;
        return;
    }
    if (address >= 0x1000 && address <= 0x107f) {
        riot_.ram_write(uint8_t(address), value);
        return;
    }
    if (address >= 0x1080 && address <= 0x109f) {
        riot_.io_write(uint8_t(address), value);
        return;
    }
    if (address >= 0x1800 && address <= 0x183f) {
        quad_pokey_w(address, value);
        return;
    }
    if (address >= 0x2000 && address <= 0x27ff) sound_ram_[address & 0x7ff] = value;
}

void StarWars::quad_pokey_w(uint16_t offset, uint8_t data) {
    const int off = int(offset) & 0x3f;
    const int pokey_num = (off >> 3) & ~0x04;
    const int control = (off & 0x20) >> 2;
    const int pokey_reg = (off % 8) | control;
    switch (pokey_num & 3) {
        case 0: pokey0_.write(uint16_t(pokey_reg), data); break;
        case 1: pokey1_.write(uint16_t(pokey_reg), data); break;
        case 2: pokey2_.write(uint16_t(pokey_reg), data); break;
        default: pokey3_.write(uint16_t(pokey_reg), data); break;
    }
}

void StarWars::on_main_cycles(int cycles) {
    math_.tick(cycles);
    prng_ = ((prng_ << 1) | (1u ^ (((prng_ >> 22) ^ (prng_ >> 4)) & 1u))) & 0x7fffffu;
}

void StarWars::on_sound_cycles(int cycles) {
    pokey0_.run(cycles);
    pokey1_.run(cycles);
    pokey2_.run(cycles);
    pokey3_.run(cycles);
    riot_.tick(cycles);
    tms_.tick(int((int64_t(cycles) * int64_t(tms_.clock()) + (kCpuClock / 2)) / kCpuClock));
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= kCpuClock) {
        audio_accumulator_ -= kCpuClock;
        int32_t sample = pokey0_.update() + pokey1_.update() + pokey2_.update() + pokey3_.update();
        sample += int32_t(tms_.last_sample());
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void StarWars::run_frame() {
    for (int irq = 0; irq < kIrqsPerFrame; irq++) {
        main_cpu_.set_irq(IrqLine::Assert);
        int remain = kIrqCycles;
        // MAME boosts to a 100 µs quantum on every latch write. A long
        // timeslice lets the main CPU spin on $4401 before the sound CPU
        // can ACK, so the handshake times out after a single command.
        while (remain > 0) {
            const int slice = std::min(remain, 64);
            sound_cpu_.run(slice);
            main_cpu_.run(slice);
            remain -= slice;
        }
    }
    update_video();
}

void StarWars::update_video() {
    // Phosphor decay: dim the previous frame instead of wiping to black.
    for (uint32_t& pixel : framebuffer_) {
        const int r = int((pixel >> 16) & 0xff) * 6 / 10;
        const int g = int((pixel >> 8) & 0xff) * 6 / 10;
        const int b = int(pixel & 0xff) * 6 / 10;
        pixel = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }

    const int xoff = (kScreenWidth - AvgStarwars::kVisWidth) / 2;
    const int yoff = (kScreenHeight - AvgStarwars::kVisHeight) / 2;
    for (const AvgStarwars::Line& line : avg_.lines()) {
        const int x0 = (line.x0 >> 16) + xoff;
        const int y0 = (line.y0 >> 16) + yoff;
        const int x1 = (line.x1 >> 16) + xoff;
        const int y1 = (line.y1 >> 16) + yoff;
        draw_line(x0, y0, x1, y1, line.color, line.intensity);
    }
}

void StarWars::draw_line(int x0, int y0, int x1, int y1, uint32_t color, int intensity) {
    auto plot = [&](int x, int y, int i) {
        if (x >= 0 && x < kScreenWidth && y >= 0 && y < kScreenHeight) {
            blend_pixel(framebuffer_[size_t(y * kScreenWidth + x)], color, i);
        }
    };
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0;
    int y = y0;
    const int glow = intensity / 3;
    while (true) {
        plot(x, y, intensity);
        if (glow > 0) {
            plot(x - 1, y, glow);
            plot(x + 1, y, glow);
            plot(x, y - 1, glow);
            plot(x, y + 1, glow);
        }
        if (x == x1 && y == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

void StarWars::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    in1_ = 0x3f;
    if (inputs.coin2) in0_ &= uint8_t(~0x01);
    if (inputs.coin1) in0_ &= uint8_t(~0x02);
    if (inputs.player1.button2) in0_ &= uint8_t(~0x40);
    if (inputs.player1.button1) in0_ &= uint8_t(~0x80);
    if (inputs.player1.button3) in1_ &= uint8_t(~0x10);
    if (inputs.player1.start) in1_ &= uint8_t(~0x20);

    analog_y_ = 0x80;
    analog_x_ = 0x80;
    if (inputs.player1.up) analog_y_ = 0x10;
    if (inputs.player1.down) analog_y_ = 0xf0;
    if (inputs.player1.left) analog_x_ = 0x10;
    if (inputs.player1.right) analog_x_ = 0xf0;
}

uint8_t StarWars::adc_channel(int channel) const {
    if (channel == 0) return analog_y_;
    if (channel == 1) return analog_x_;
    return 0;  // thrust, unused
}

void StarWars::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw0_ = value;
    if (bank == 1) dsw1_ = value;
}

void StarWars::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
