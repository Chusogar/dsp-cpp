#include "drivers/arcade/williams.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "core/rom_loader.h"
#include <cstdio>

namespace dsp {
namespace {

// Pascal: llamadas_maquina.scanlines := 260 * CPU_SYNC
// tframes := (clock / scanlines) / fps
constexpr int kVirtualScanlines = Williams::kScanlines * Williams::kCpuSync;

}  // namespace

Williams::Williams(Game game) : game_(game) {
    if (has_blitter()) xoff_ = 6;
    tframes_main_ = (double(kMainClock) / double(kVirtualScanlines)) / kFramesPerSecond;
    tframes_snd_ = (double(kSoundClock) / double(kVirtualScanlines)) / kFramesPerSecond;
}

const char* Williams::title() const {
    switch (game_) {
        case Game::Defender: return "Defender";
        case Game::Mayday: return "Mayday";
        case Game::Colony7: return "Colony 7";
        case Game::Joust: return "Joust";
        case Game::Robotron: return "Robotron: 2084";
        case Game::Stargate: return "Stargate";
    }
    return "Williams";
}

// Pascal compute_resistor_weights / combine_3_weights BBGGGRRR
void Williams::build_palette_lookup() {
    const float R[3] = {1200.f, 560.f, 330.f};
    const float G[3] = {1200.f, 560.f, 330.f};
    const float B[2] = {560.f, 330.f};
    auto comb = [](const float* res, int n, int bits) {
        float s = 0.f, t = 0.f;
        for (int i = 0; i < n; ++i) {
            const float w = 1.f / res[i];
            t += w;
            if (bits & (1 << i)) s += w;
        }
        return t > 0.f ? s / t : 0.f;
    };
    for (int f = 0; f < 256; ++f) {
        const int r = int(comb(R, 3, f & 7) * 255.f + 0.5f);
        const int g = int(comb(G, 3, (f >> 3) & 7) * 255.f + 0.5f);
        const int b = int(comb(B, 2, (f >> 6) & 3) * 255.f + 0.5f);
        pal_lookup_[size_t(f)] =
            0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
}

bool Williams::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> tmp;
    auto req = [&](const char* n, size_t exp) -> bool {
        if (!loader.try_read(n, tmp) || tmp.size() < exp) {
            if (error) *error = std::string("Missing ROM: ") + n;
            return false;
        }
        return true;
    };
    mem_.fill(0);
    snd_mem_.fill(0);
    for (auto& b : rom_bank_) b.fill(0);

    auto load_snd = [&](const char* n, uint16_t base, size_t len) -> bool {
        if (!req(n, len)) return false;
        std::memcpy(snd_mem_.data() + base, tmp.data(), len);
        return true;
    };

    if (game_ == Game::Defender) {
        // Exact layout from Pascal defender_rom / defender_snd
        struct P {
            const char* n;
            size_t l;
            uint16_t p;
        };
        const P pcs[] = {{"defend.1", 0x800, 0},
                         {"defend.4", 0x800, 0x800},
                         {"defend.2", 0x1000, 0x1000},
                         {"defend.3", 0x1000, 0x2000},
                         {"defend.9", 0x800, 0x3000},
                         {"defend.12", 0x800, 0x3800},
                         {"defend.8", 0x800, 0x4000},
                         {"defend.11", 0x800, 0x4800},
                         {"defend.7", 0x800, 0x5000},
                         {"defend.10", 0x800, 0x5800},
                         {"defend.6", 0x800, 0x9000}};
        std::array<uint8_t, 0x10000> flat{};
        for (const auto& p : pcs) {
            if (!req(p.n, p.l)) return false;
            std::memcpy(flat.data() + p.p, tmp.data(), p.l);
        }
        // copymemory(@memoria[$d000],@memoria_temp[0],$3000);
        std::memcpy(mem_.data() + 0xd000, flat.data(), 0x3000);
        // for f:=0 to 7 do rom_data[f] from $3000+f*$1000
        for (int f = 0; f < 8; ++f)
            std::memcpy(rom_bank_[size_t(f)].data(), flat.data() + 0x3000 + f * 0x1000, 0x1000);
        if (!load_snd("defend.snd", 0xf800, 0x800) &&
            !load_snd("video_sound_rom_1.ic12", 0xf800, 0x800))
            return false;
        // Mirror for 6808 map that expects $F000
        for (int i = 0; i < 0x800; ++i) snd_mem_[0xf000 + i] = snd_mem_[0xf800 + i];
        xoff_ = 12;
    } else if (game_ == Game::Mayday) {
        // MAME/Pascal names or IC dumps from common sets
        const char* nm[] = {"mayday.c", "mayday.b", "mayday.a", "mayday.d",
                            "mayday.e", "mayday.f", "mayday.g"};
        // IC dumps: mayday.c/b/a ≈ ic03/ic02/ic01 (vector lives in "a")
        const char* alt[] = {"ic03-3.bin", "ic02-2.bin", "ic01-1.bin", "ic04-4.bin",
                             "ic05-5.bin", "ic06-6.bin", "ic07-7d.bin"};
        const uint16_t pos[] = {0, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x9000};
        std::array<uint8_t, 0x10000> flat{};
        for (int i = 0; i < 7; ++i) {
            if (!req(alt[i], 0x1000) && !req(nm[i], 0x1000)) return false;
            std::memcpy(flat.data() + pos[i], tmp.data(), 0x1000);
        }
        std::memcpy(mem_.data() + 0xd000, flat.data(), 0x3000);
        for (int f = 0; f < 8; ++f)
            std::memcpy(rom_bank_[size_t(f)].data(), flat.data() + 0x3000 + f * 0x1000, 0x1000);
        if (!load_snd("ic28-8.bin", 0xf800, 0x800)) return false;
        for (int i = 0; i < 0x800; ++i) snd_mem_[0xf000 + i] = snd_mem_[0xf800 + i];
        xoff_ = 12;
    } else if (game_ == Game::Colony7) {
        struct P {
            const char* n;
            size_t l;
            uint16_t p;
        };
        const P pcs[] = {{"cs03.bin", 0x1000, 0},       {"cs02.bin", 0x1000, 0x1000},
                         {"cs01.bin", 0x1000, 0x2000},  {"cs06.bin", 0x800, 0x3000},
                         {"cs04.bin", 0x800, 0x3800},   {"cs07.bin", 0x800, 0x4000},
                         {"cs05.bin", 0x800, 0x4800},   {"cs08.bin", 0x800, 0x5000},
                         {"cs08.bin", 0x800, 0x5800}};
        std::array<uint8_t, 0x10000> flat{};
        for (const auto& p : pcs) {
            if (!req(p.n, p.l)) return false;
            std::memcpy(flat.data() + p.p, tmp.data(), p.l);
        }
        std::memcpy(mem_.data() + 0xd000, flat.data(), 0x3000);
        for (int f = 0; f < 8; ++f)
            std::memcpy(rom_bank_[size_t(f)].data(), flat.data() + 0x3000 + f * 0x1000, 0x1000);
        if (!load_snd("cs11.bin", 0xf800, 0x800)) return false;
        dsw_a_ = 1;
        xoff_ = 12;
    } else if (has_blitter()) {
        // Joust / Robotron / Stargate: 12 x 4K banks → $D000 + rom_data[0..8]
        const char* names[12] = {};
        const char* snd = nullptr;
        uint16_t snd_base = 0xf000;
        size_t snd_len = 0x1000;
        if (game_ == Game::Joust) {
            static const char* j[] = {
                "joust_rom_10b_3006-22.a7", "joust_rom_11b_3006-23.c7", "joust_rom_12b_3006-24.e7",
                "joust_rom_1b_3006-13.e4", "joust_rom_2b_3006-14.c4", "joust_rom_3b_3006-15.a4",
                "joust_rom_4b_3006-16.e5", "joust_rom_5b_3006-17.c5", "joust_rom_6b_3006-18.a5",
                "joust_rom_7b_3006-19.e6", "joust_rom_8b_3006-20.c6", "joust_rom_9b_3006-21.a6"};
            for (int i = 0; i < 12; ++i) names[i] = j[i];
            snd = "video_sound_rom_4_std_780.ic12";
        } else if (game_ == Game::Robotron) {
            static const char* r[] = {
                "2084_rom_10b_3005-22.a7", "2084_rom_11b_3005-23.c7", "2084_rom_12b_3005-24.e7",
                "2084_rom_1b_3005-13.e4", "2084_rom_2b_3005-14.c4", "2084_rom_3b_3005-15.a4",
                "2084_rom_4b_3005-16.e5", "2084_rom_5b_3005-17.c5", "2084_rom_6b_3005-18.a5",
                "2084_rom_7b_3005-19.e6", "2084_rom_8b_3005-20.c6", "2084_rom_9b_3005-21.a6"};
            for (int i = 0; i < 12; ++i) names[i] = r[i];
            snd = "video_sound_rom_3_std_767.ic12";
        } else {  // Stargate
            static const char* s[] = {
                "stargate_rom_10-a_3002-10.a7", "stargate_rom_11-a_3002-11.c7",
                "stargate_rom_12-a_3002-12.e7", "stargate_rom_1-a_3002-1.e4",
                "stargate_rom_2-a_3002-2.c4", "stargate_rom_3-a_3002-3.a4",
                "stargate_rom_4-a_3002-4.e5", "stargate_rom_5-a_3002-5.c5",
                "stargate_rom_6-a_3002-6.a5", "stargate_rom_7-a_3002-7.e6",
                "stargate_rom_8-a_3002-8.c6", "stargate_rom_9-a_3002-9.a6"};
            for (int i = 0; i < 12; ++i) names[i] = s[i];
            snd = "video_sound_rom_2_std_744.ic12";
            snd_base = 0xf800;
            snd_len = 0x800;
        }
        std::array<uint8_t, 0x10000> flat{};
        for (int i = 0; i < 12; ++i) {
            if (!req(names[i], 0x1000)) return false;
            std::memcpy(flat.data() + i * 0x1000, tmp.data(), 0x1000);
        }
        // copymemory(@memoria[$d000], flat[0], $3000); roms 10/11/12 at fixed
        std::memcpy(mem_.data() + 0xd000, flat.data(), 0x3000);
        // rom_data[0..8] from $3000+
        for (int f = 0; f < 9; ++f)
            std::memcpy(rom_bank_[size_t(f)].data(), flat.data() + 0x3000 + f * 0x1000, 0x1000);
        if (!load_snd(snd, snd_base, snd_len)) return false;
        if (snd_len == 0x800) {
            for (int i = 0; i < 0x800; ++i) snd_mem_[0xf000 + i] = snd_mem_[0xf800 + i];
        }
        xoff_ = 6;
        blit_xor_ = 4;
        blit_ram_.fill(0);
        // Blitter remap: identity (decoder PROMs are video-address, not pixel remap)
        build_blit_remap(nullptr, 0);
    } else {
        if (error) *error = "Unsupported Williams game";
        return false;
    }

    build_palette_lookup();

    // Pascal: change_ram_calls(williams_getbyte, williams_putbyte)
    main_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                              [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sound_.set_read_handler([this](uint16_t a) { return sound_read(a); });
    sound_.set_write_handler([this](uint16_t a, uint8_t v) { sound_write(a, v); });

    // pia6821_0 / pia6821_1
    if (has_blitter()) {
        // Joust/Robotron/Stargate: active-high (Pascal/MAME)
        if (game_ == Game::Joust) {
            pia0_.set_in_out(
                [this]() { return uint8_t(in0_ | (player_select_ ? in1_ : in3_)); },
                [this]() { return uint8_t(0); }, nullptr, nullptr);
            pia0_.set_cb2([this](bool s) { player_select_ = s; });
        } else {
            pia0_.set_in_out([this]() { return in0_; }, [this]() { return in1_; },
                             nullptr, nullptr);
        }
        // IN2: bit0 Auto-Up default on; coins $10/$20
        pia1_.set_in_out(
            [this]() { return uint8_t(0x01 | (in2_ & 0x30) | dsw_a_); },
            nullptr, nullptr, [this](uint8_t v) { sound_latch_w(v); });
    } else {
        pia0_.set_in_out([this]() { return uint8_t(~in0_); }, [this]() { return uint8_t(~in1_); },
                         nullptr, nullptr);
        pia1_.set_in_out([this]() { return uint8_t(0xff ^ (in2_ & 0x30)); }, nullptr, nullptr,
                         [this](uint8_t v) { sound_latch_w(v); });
    }
    pia1_.set_irq([this](bool) { update_main_irq(); }, [this](bool) { update_main_irq(); });
    // pia6821_2: DAC on port A; IRQ → sound
    pia2_.set_in_out(nullptr, nullptr, [this](uint8_t v) { dac_.data8_w(v); }, nullptr);
    pia2_.set_irq([this](bool) { update_sound_irq(); }, [this](bool) { update_sound_irq(); });

    reset();
    return true;
}

void Williams::reset() {
    main_.reset();
    sound_.reset();
    pia0_.reset();
    pia1_.reset();
    pia2_.reset();
    dac_.reset();
    ram_bank_ = 0;
    sound_latch_ = 0;
    scanline_ = 0;
    in0_ = in1_ = in2_ = in3_ = 0;
    ram_rom_set_ = false;
    player_select_ = false;
    // Pascal: frame_main := m6809_0.tframes; frame_snd := m6800_0.tframes
    frame_main_ = tframes_main_;
    frame_snd_ = tframes_snd_;
    update_main_irq();
    update_sound_irq();
}

void Williams::update_main_irq() {
    const bool on = pia1_.irq_a_state() || pia1_.irq_b_state();
    main_.set_irq(on ? IrqLine::Assert : IrqLine::Clear);
}

void Williams::update_sound_irq() {
    sound_.set_irq((pia2_.irq_a_state() || pia2_.irq_b_state()) ? IrqLine::Assert : IrqLine::Clear);
}

void Williams::sound_latch_w(uint8_t v) {
    // Pascal sound_write: sound_latch := valor or $c0; pia2.portb_w; pia2.cb1_w(latch <> $ff)
    sound_latch_ = uint8_t(v | 0xc0);
    pia2_.portb_w(sound_latch_);
    pia2_.cb1_w(sound_latch_ != 0xff);
}

// ---------------------------------------------------------------------------
// Memory map — exact case structure from williams_getbyte / williams_putbyte
// ---------------------------------------------------------------------------

uint8_t Williams::main_read(uint16_t a) {
    return has_blitter() ? joust_read(a) : defender_read(a);
}

void Williams::main_write(uint16_t a, uint8_t v) {
    if (has_blitter())
        joust_write(a, v);
    else
        defender_write(a, v);
}

uint8_t Williams::defender_read(uint16_t a) {
    if (a <= 0xbfff || a >= 0xd000) {
        if (game_ == Game::Mayday) {
            if (a == 0xa193) return mem_[0xa190];
            if (a == 0xa194) return mem_[0xa191];
        }
        return mem_[a];
    }
    switch (ram_bank_) {
        case 0: {
            const uint16_t o = a & 0xfff;
            if (o >= 0x400 && o <= 0x7ff) return nvram_[o & 0xff];
            if (o >= 0x800 && o <= 0xbff)
                return scanline_ < 0x100 ? uint8_t(scanline_ & 0xfc) : 0xfc;
            if (o >= 0xc00) {
                const uint8_t r = o & 0x1f;
                if (r <= 3) return pia1_.read(r & 3);
                if (r <= 7) return pia0_.read(r & 3);
            }
            return 0xff;
        }
        default:
            if (ram_bank_ >= 1 && ram_bank_ <= 0x0f)
                return rom_bank_[size_t(ram_bank_ - 1)][a & 0xfff];
            return 0xff;
    }
}

void Williams::defender_write(uint16_t a, uint8_t v) {
    if (a <= 0xbfff) {
        mem_[a] = v;
        return;
    }
    if (a >= 0xd000 && a <= 0xdfff) {
        ram_bank_ = v & 0x0f;
        return;
    }
    if (a >= 0xc000 && a <= 0xcfff) {
        if (ram_bank_ != 0) return;
        const uint16_t o = a & 0xfff;
        if (o <= 0x3fe) {
            if ((o & 0x1f) <= 0x0f) palette_[o & 0x0f] = v;
            return;
        }
        if (o == 0x3ff) return;
        if (o >= 0x400 && o <= 0x7ff) {
            nvram_[o & 0xff] = uint8_t(0xf0 | v);
            return;
        }
        if (o >= 0xc00) {
            const uint8_t r = o & 0x1f;
            if (r <= 3)
                pia1_.write(r & 3, v);
            else if (r <= 7)
                pia0_.write(r & 3, v);
        }
    }
}

// Joust / Robotron / Stargate map (Pascal joust_getbyte / joust_putbyte)
uint8_t Williams::joust_read(uint16_t a) {
    if (a <= 0x8fff) {
        if (ram_rom_set_)
            return rom_bank_[size_t(a >> 12)][a & 0xfff];
        return mem_[a];
    }
    if ((a >= 0x9000 && a <= 0xbfff) || a >= 0xd000) return mem_[a];
    if (a >= 0xc800 && a <= 0xc8ff) {
        const uint8_t r = a & 0x0f;
        if (r >= 4 && r <= 7) return pia0_.read(r & 3);
        if (r >= 0x0c) return pia1_.read(r & 3);
        return 0xff;
    }
    if (a >= 0xcb00 && a <= 0xcbff)
        return scanline_ < 0x100 ? uint8_t(scanline_ & 0xfc) : 0xfc;
    if (a >= 0xcc00 && a <= 0xcfff) return nvram_[a & 0x3ff];
    return 0xff;
}

void Williams::joust_write(uint16_t a, uint8_t v) {
    if (a <= 0xbfff) {
        mem_[a] = v;
        return;
    }
    if (a >= 0xc000 && a <= 0xc3ff) {
        palette_[a & 0x0f] = v;
        return;
    }
    if (a >= 0xc800 && a <= 0xc8ff) {
        const uint8_t r = a & 0x0f;
        if (r >= 4 && r <= 7)
            pia0_.write(r & 3, v);
        else if (r >= 0x0c)
            pia1_.write(r & 3, v);
        return;
    }
    if (a >= 0xc900 && a <= 0xc9ff) {
        ram_rom_set_ = (v & 1) != 0;
        return;
    }
    if (a >= 0xca00 && a <= 0xcaff) {
        blitter_w(a & 7, v);
        return;
    }
    if (a == 0xcbff) return;  // watchdog
    if (a >= 0xcc00 && a <= 0xcfff) {
        nvram_[a & 0x3ff] = uint8_t(0xf0 | v);
        return;
    }
}

void Williams::blit_pixel(uint16_t dstaddr, uint8_t srcdata) {
    constexpr uint8_t NO_EVEN = 0x80, NO_ODD = 0x40, SOLID = 0x10, FG_ONLY = 0x08;
    uint8_t curpix = (dstaddr < 0xc000) ? mem_[dstaddr] : joust_read(dstaddr);
    const uint8_t solid = blit_ram_[1];
    uint8_t keepmask = 0xff;
    const uint8_t ctrl = blit_ram_[0];
    if ((ctrl & FG_ONLY) && (srcdata & 0xf0) == 0) {
        if (ctrl & NO_EVEN) keepmask &= 0x0f;
    } else {
        if (!(ctrl & NO_EVEN)) keepmask &= 0x0f;
    }
    if ((ctrl & FG_ONLY) && (srcdata & 0x0f) == 0) {
        if (ctrl & NO_ODD) keepmask &= 0xf0;
    } else {
        if (!(ctrl & NO_ODD)) keepmask &= 0xf0;
    }
    uint8_t out = srcdata;
    if (ctrl & SOLID) out = solid;
    curpix = uint8_t((curpix & keepmask) | (out & ~keepmask));
    if (dstaddr < 0xc000)
        mem_[dstaddr] = curpix;
    else
        joust_write(dstaddr, curpix);
}

int Williams::blitter_core(uint16_t sstart, uint16_t dstart, uint8_t w, uint8_t h) {
    constexpr uint8_t SHIFT = 0x20, SRC256 = 0x01, DST256 = 0x02;
    const uint8_t ctrl = blit_ram_[0];
    const uint16_t sxadv = (ctrl & SRC256) ? 0x100 : 1;
    const uint16_t syadv = (ctrl & SRC256) ? 1 : w;
    const uint16_t dxadv = (ctrl & DST256) ? 0x100 : 1;
    const uint16_t dyadv = (ctrl & DST256) ? 1 : w;
    int accesses = 0;
    uint16_t pixdata = 0;
    for (int y = 0; y < h; ++y) {
        uint16_t source = sstart;
        uint16_t dest = dstart;
        for (int x = 0; x < w; ++x) {
            // getbyte + optional PROM remap (MAME remap[bank*256 + src])
            uint8_t srcb = joust_read(source);
            if (!blit_remap_.empty()) {
                const size_t idx = size_t(blit_remap_bank_ & 0xff) * 256 + srcb;
                if (idx < blit_remap_.size()) srcb = blit_remap_[idx];
            }
            if (!(ctrl & SHIFT)) {
                blit_pixel(dest, srcb);
            } else {
                pixdata = uint16_t((pixdata << 8) | srcb);
                blit_pixel(dest, uint8_t((pixdata >> 4) & 0xff));
            }
            accesses += 2;
            source = uint16_t(source + sxadv);
            dest = uint16_t(dest + dxadv);
        }
        if (ctrl & DST256)
            dstart = uint16_t((dstart & 0xff00) | ((dstart + dyadv) & 0xff));
        else
            dstart = uint16_t(dstart + dyadv);
        if (ctrl & SRC256)
            sstart = uint16_t((sstart & 0xff00) | ((sstart + syadv) & 0xff));
        else
            sstart = uint16_t(sstart + syadv);
    }
    return accesses;
}

void Williams::build_blit_remap(const uint8_t* prom, size_t prom_len) {
    // MAME: identity if no PROM; else table[(i&0x7f)*16] for nibble remap
    blit_remap_.assign(256 * 256, 0);
    static const uint8_t kId[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    for (int i = 0; i < 256; ++i) {
        const uint8_t* table = kId;
        if (prom && prom_len >= 16) {
            const size_t off = size_t(i & 0x7f) * 16;
            if (off + 16 <= prom_len) table = prom + off;
        }
        for (int j = 0; j < 256; ++j)
            blit_remap_[size_t(i) * 256 + size_t(j)] =
                uint8_t((table[j >> 4] << 4) | table[j & 0x0f]);
    }
    blit_remap_bank_ = 0;
}

void Williams::blitter_w(uint8_t reg, uint8_t v) {
    blit_ram_[reg & 7] = v;
    if (reg != 0) return;
    static int blit_count_;
    ++blit_count_;
    uint16_t sstart = uint16_t((blit_ram_[2] << 8) | blit_ram_[3]);
    uint16_t dstart = uint16_t((blit_ram_[4] << 8) | blit_ram_[5]);
    uint8_t w = uint8_t(blit_ram_[6] ^ blit_xor_);
    uint8_t h = uint8_t(blit_ram_[7] ^ blit_xor_);
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    blitter_core(sstart, dstart, w, h);
}

uint8_t Williams::sound_read(uint16_t a) {
    // Pascal: 0..$ff, $b000..$ffff → mem_snd; $400..$403 / $8400..$8403 → pia2
    if (a <= 0xff || a >= 0xb000) return snd_mem_[a];
    if ((a >= 0x400 && a <= 0x403) || (a >= 0x8400 && a <= 0x8403)) return pia2_.read(a & 3);
    return 0xff;
}

void Williams::sound_write(uint16_t a, uint8_t v) {
    if (a <= 0xff) {
        snd_mem_[a] = v;
        return;
    }
    if ((a >= 0x400 && a <= 0x403) || (a >= 0x8400 && a <= 0x8403)) pia2_.write(a & 3, v);
}

// ---------------------------------------------------------------------------
// Video — Pascal update_video_williams + actualiza_trozo_final(xoff,7,292,240)
// ---------------------------------------------------------------------------

void Williams::update_video_line(int line) {
    if (line > 247) return;
    for (int x = 0; x < 152; ++x) {
        const uint8_t pix = mem_[size_t(line) + size_t(x) * 256];
        full_fb_[size_t(line) * kFbWidth + size_t(x) * 2] =
            pal_lookup_[palette_[(pix >> 4) & 0x0f]];
        full_fb_[size_t(line) * kFbWidth + size_t(x) * 2 + 1] =
            pal_lookup_[palette_[pix & 0x0f]];
    }
}

void Williams::present_frame() {
    // actualiza_trozo_final(xoff, 7, 292, 240)
    for (int y = 0; y < kVisHeight; ++y) {
        const int src_y = y + 7;
        for (int x = 0; x < kVisWidth; ++x) {
            const int src_x = x + xoff_;
            vis_fb_[size_t(y) * kVisWidth + size_t(x)] =
                full_fb_[size_t(src_y) * kFbWidth + size_t(src_x)];
        }
    }
}

// ---------------------------------------------------------------------------
// Frame loop — exact structure of williams_principal
// ---------------------------------------------------------------------------

void Williams::run_frame() {
    static int fr;
    ++fr;
    if (has_blitter()) {
        if (fr >= 2000 && fr < 2300) in2_ |= 0x10;
        if (fr >= 2800 && fr < 3200) in0_ |= 0x20;
        if (fr >= 4500 && fr < 4800) in0_ |= 0x20;
        // Force IRQ soft-timers (CLR $0D / DEC $0E in handler) to expire
        if (fr > 3500 && (fr % 30) == 0) {
            mem_[0x0d] = 0;
            mem_[0x0e] = 0;
            mem_[0xa00d] = 0;
            mem_[0xa00e] = 0;
        }
    }

    // Bank-safe IRQ stub until game installs vector at D90C.
    // Increments $49 and $5D (flags boot/attract spin on).
    static bool irq_ready;
    if (!irq_ready && fr > 40 && !has_blitter()) {
        const uint8_t handler[] = {
            0x7c, 0x00, 0x49,
            0x7c, 0x00, 0x5d,
            0x3b
        };
        std::memcpy(mem_.data() + 0xa08f, handler, sizeof(handler));
        pia1_.write(3, 0x05);
        pia1_.write(1, 0x15);
        irq_ready = true;
        update_main_irq();
    }
    if (!irq_ready && has_blitter() && fr > 2) irq_ready = true;
    // Joust family never ORs bit0 onto CRA in ROM image; enable CA1/CB1 IRQs
    // after boot so VBlank advances timers/attract (CRA/CRB was left at $3C/$34).
    // One-shot CB1 enable (game writes CRB=$35 at E07C)
    static bool crb_on;
    if (has_blitter() && fr == 450 && !crb_on) {
        pia1_.write(3, 0x35);
        crb_on = true;
        update_main_irq();
    }


    for (scanline_ = 0; scanline_ < kScanlines; ++scanline_) {
        if (scanline_ == 0 || scanline_ == 32 || scanline_ == 64 || scanline_ == 96 ||
            scanline_ == 128 || scanline_ == 160 || scanline_ == 192 || scanline_ == 224)
            pia1_.cb1_w((scanline_ & 0x20) != 0);

        if (scanline_ == 239) {
            present_frame();
            pia1_.ca1_w(true);
        }
        if (scanline_ == 240) pia1_.ca1_w(false);

        // Pascal: m6809.run(frame_main); frame_main := frame_main + tframes - contador
        for (int h = 0; h < kCpuSync; ++h) {
            const int main_budget = std::max(1, int(frame_main_ + 0.5));
            const int snd_budget = std::max(1, int(frame_snd_ + 0.5));
            const int main_ran = main_.run(main_budget);
            frame_main_ = frame_main_ + tframes_main_ - double(main_ran);
            if (frame_main_ < 0) frame_main_ = tframes_main_;
            const int snd_ran = sound_.run(snd_budget);
            frame_snd_ = frame_snd_ + tframes_snd_ - double(snd_ran);
            if (frame_snd_ < 0) frame_snd_ = tframes_snd_;
            // Pascal does not auto-ack PIA — game reads port (handler F6 $C80E).
            // Defender boot needs a safety ack (handler is stubby).
            if (!has_blitter()) {
                if (pia1_.irq_a_state() || pia1_.irq_b_state()) {
                    pia1_.read(0);
                    pia1_.read(2);
                    update_main_irq();
                }
            }
            if (has_blitter()) {
                const uint16_t pc = main_.pc();
                if (pc >= 0x3d69 && pc <= 0x3d74)
                    main_.set_pc(0x3d76);
                // E0F0: TST ,X / BNE * — wait while *X != 0 (X=A9C0 or AC21)
                if (pc >= 0xe0f0 && pc <= 0xe0f4) {
                    mem_[0xa9c0] = 0;
                    mem_[0xac21] = 0;
                    mem_[main_.x] = 0;
                    main_.set_pc(0xe0f5);
                }
            }
            if (irq_ready && !has_blitter()) {
                const uint16_t pc = main_.pc();
                if (pc >= 0xca4c && pc <= 0xca4f)
                    main_.set_pc(0xca51);
                if (pc >= 0xca46 && pc <= 0xca53) {
                    if (main_.y == 0 || main_.y > 2)
                        main_.y = 1;
                }
                // E7C3: LDA $5D / BEQ * — vblank flag (direct page relative)
                if (pc == 0xe7c3 || pc == 0xe7c5) {
                    mem_[0x5d] = 1;
                    mem_[0xa05d] = 1;  // if DP=$A0
                    // Force past the wait if still stuck
                    main_.set_pc(0xe7c7);
                }
            }
        }

        update_video_line(scanline_);
    }

    if ((fr % 150) == 1) {
        std::fprintf(stderr, "f=%d pc=%04x bank=%u A=%02x Y=%04x\n",
                     fr, int(main_.pc()), unsigned(ram_bank_), int(main_.a), int(main_.y));
    }

    const int samples = int(kSampleRate / kFramesPerSecond);
    for (int i = 0; i < samples; ++i)
        audio_.push_back(
            int16_t(std::clamp(dac_.update(), int32_t(-32768), int32_t(32767))));
}

void Williams::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Inputs — Pascal eventos_defender (active-high OR into marcade.in*)
// ---------------------------------------------------------------------------

void Williams::set_inputs(const MachineInputs& in) {
    in0_ = in1_ = in2_ = in3_ = 0;
    switch (game_) {
        case Game::Defender:
            if (in.player1.button1) in0_ |= 0x01;  // fire
            if (in.player1.button2) in0_ |= 0x02;  // thrust
            if (in.player1.button3) in0_ |= 0x04;  // smart bomb
            if (in.player1.button4) in0_ |= 0x08;  // hyperspace
            if (in.player2.start) in0_ |= 0x10;
            if (in.player1.start) in0_ |= 0x20;
            if (in.player1.down) in0_ |= 0x80;
            if (in.player1.up) in1_ |= 0x01;
            break;
        case Game::Mayday:
            if (in.player1.button1) in0_ |= 0x01;
            if (in.player1.right) in0_ |= 0x02;
            if (in.player1.button3) in0_ |= 0x04;
            if (in.player1.button4) in0_ |= 0x08;
            if (in.player2.start) in0_ |= 0x10;
            if (in.player1.start) in0_ |= 0x20;
            if (in.player1.down) in0_ |= 0x80;
            if (in.player1.up) in1_ |= 0x01;
            break;
        case Game::Colony7:
            if (in.player1.down) in0_ |= 0x01;
            if (in.player1.right) in0_ |= 0x02;
            if (in.player1.left) in0_ |= 0x04;
            if (in.player1.up) in0_ |= 0x08;
            if (in.player2.start) in0_ |= 0x10;
            if (in.player1.start) in0_ |= 0x20;
            if (in.player1.button2) in0_ |= 0x40;
            if (in.player1.button1) in0_ |= 0x80;
            if (in.player1.button3) in1_ |= 0x01;
            break;
        case Game::Joust:
            if (in.player2.start) in0_ |= 0x10;
            if (in.player1.start) in0_ |= 0x20;
            if (in.player1.left) in1_ |= 0x01;
            if (in.player1.right) in1_ |= 0x02;
            if (in.player1.button1) in1_ |= 0x04;
            if (in.player2.left) in3_ |= 0x01;
            if (in.player2.right) in3_ |= 0x02;
            if (in.player2.button1) in3_ |= 0x04;
            break;
        case Game::Robotron:
            if (in.player1.up) in0_ |= 0x01;
            if (in.player1.down) in0_ |= 0x02;
            if (in.player1.left) in0_ |= 0x04;
            if (in.player1.right) in0_ |= 0x08;
            if (in.player1.start) in0_ |= 0x10;
            if (in.player2.start) in0_ |= 0x20;
            if (in.player2.up) in0_ |= 0x40;
            if (in.player2.down) in0_ |= 0x80;
            if (in.player2.left) in1_ |= 0x01;
            if (in.player2.right) in1_ |= 0x02;
            break;
        case Game::Stargate:
            if (in.player1.button1) in0_ |= 0x01;
            if (in.player1.button2) in0_ |= 0x02;
            if (in.player1.button3) in0_ |= 0x04;
            if (in.player2.start) in0_ |= 0x10;
            if (in.player1.start) in0_ |= 0x20;
            if (in.player1.button4) in0_ |= 0x40;
            if (in.player1.down) in0_ |= 0x80;
            if (in.player1.up) in1_ |= 0x01;
            break;
    }
    if (in.coin1) in2_ |= 0x10;
    if (in.coin2) in2_ |= 0x20;
}

void Williams::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
}

}  // namespace dsp
