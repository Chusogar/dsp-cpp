#include "drivers/arcade/balsente.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"
#include "drivers/arcade/balsente_roms.h"

namespace dsp {
namespace {

constexpr int kUartDelayMainCycles = 625;  // ~500 µs at 1.25 MHz

uint8_t expand4(uint8_t nibble) {
    nibble &= 0x0f;
    return uint8_t((nibble << 4) | nibble);
}

const char* game_title(Balsente::Game g) {
    switch (g) {
        case Balsente::Game::Sentetst: return "Sente Diagnostic Cartridge";
        case Balsente::Game::Cshift: return "Chicken Shift";
        case Balsente::Game::Hattrick: return "Hat Trick";
        case Balsente::Game::Gghost: return "Goalie Ghost";
        case Balsente::Game::Otwalls: return "Off the Wall";
        case Balsente::Game::Snakepit: return "Snake Pit";
        case Balsente::Game::Triviag1: return "Trivial Pursuit (Genus Edition)";
        case Balsente::Game::Snakjack: return "Snacks'n Jaxson";
        case Balsente::Game::Stocker: return "Stocker";
        case Balsente::Game::Triviabb: return "Trivial Pursuit (Baby Boomer)";
        case Balsente::Game::Triviag2: return "Trivial Pursuit (Genus II)";
        case Balsente::Game::Triviayp: return "Trivial Pursuit (Young Players)";
        case Balsente::Game::Triviasp: return "Trivial Pursuit (All Star Sports)";
        case Balsente::Game::Gimeabrk: return "Gimme A Break";
        case Balsente::Game::Minigolf: return "Mini Golf";
        case Balsente::Game::Teamht: return "Team Hat Trick";
        case Balsente::Game::Grudge: return "Grudge Match";
        case Balsente::Game::Triviaes: return "Trivial Pursuit (Spanish)";
        case Balsente::Game::Toggle: return "Toggle";
        case Balsente::Game::Nstocker: return "Night Stocker";
        case Balsente::Game::Sfootbal: return "Street Football";
        case Balsente::Game::Spiker: return "Spiker";
        case Balsente::Game::Stompin: return "Stompin'";
        case Balsente::Game::Nametune: return "Name That Tune";
        case Balsente::Game::Rescraid: return "Rescue Raider";
    }
    return "Bally/Sente";
}

bool needs_rombank2(Balsente::Game g) {
    switch (g) {
        case Balsente::Game::Nstocker:
        case Balsente::Game::Sfootbal:
        case Balsente::Game::Spiker:
        case Balsente::Game::Stompin:
        case Balsente::Game::Nametune:
            return true;
        default:
            return false;
    }
}

}  // namespace

Balsente::Balsente(Game game) : game_(game) {}

const char* Balsente::title() const { return game_title(game_); }

bool Balsente::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    poly17_init();
    apply_game_init();

    main_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                              [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sound_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                               [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                           [this](uint16_t p, uint8_t v) { sound_out(p, v); });

    reset();
    return true;
}

bool Balsente::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    const std::vector<RomEntry>* main_entries = nullptr;
    const std::vector<RomEntry>* sprite_entries = nullptr;

    switch (game_) {
        case Game::Sentetst:
            main_entries = &balsente_roms::kSentetstMain;
            sprite_entries = &balsente_roms::kSentetstSprites;
            break;
        case Game::Cshift:
            main_entries = &balsente_roms::kCshiftMain;
            sprite_entries = &balsente_roms::kCshiftSprites;
            break;
        case Game::Hattrick:
            main_entries = &balsente_roms::kHattrickMain;
            sprite_entries = &balsente_roms::kHattrickSprites;
            break;
        case Game::Gghost:
            main_entries = &balsente_roms::kGghostMain;
            sprite_entries = &balsente_roms::kGghostSprites;
            break;
        case Game::Otwalls:
            main_entries = &balsente_roms::kOtwallsMain;
            sprite_entries = &balsente_roms::kOtwallsSprites;
            break;
        case Game::Snakepit:
            main_entries = &balsente_roms::kSnakepitMain;
            sprite_entries = &balsente_roms::kSnakepitSprites;
            break;
        case Game::Triviag1:
            main_entries = &balsente_roms::kTriviag1Main;
            sprite_entries = &balsente_roms::kTriviag1Sprites;
            break;
        case Game::Snakjack:
            main_entries = &balsente_roms::kSnakjackMain;
            sprite_entries = &balsente_roms::kSnakjackSprites;
            break;
        case Game::Stocker:
            main_entries = &balsente_roms::kStockerMain;
            sprite_entries = &balsente_roms::kStockerSprites;
            break;
        case Game::Triviabb:
            main_entries = &balsente_roms::kTriviabbMain;
            sprite_entries = &balsente_roms::kTriviabbSprites;
            break;
        case Game::Triviag2:
            main_entries = &balsente_roms::kTriviag2Main;
            sprite_entries = &balsente_roms::kTriviag2Sprites;
            break;
        case Game::Triviayp:
            main_entries = &balsente_roms::kTriviaypMain;
            sprite_entries = &balsente_roms::kTriviaypSprites;
            break;
        case Game::Triviasp:
            main_entries = &balsente_roms::kTriviaspMain;
            sprite_entries = &balsente_roms::kTriviaspSprites;
            break;
        case Game::Gimeabrk:
            main_entries = &balsente_roms::kGimeabrkMain;
            sprite_entries = &balsente_roms::kGimeabrkSprites;
            break;
        case Game::Minigolf:
            main_entries = &balsente_roms::kMinigolfMain;
            sprite_entries = &balsente_roms::kMinigolfSprites;
            break;
        case Game::Teamht:
            main_entries = &balsente_roms::kTeamhtMain;
            sprite_entries = &balsente_roms::kTeamhtSprites;
            break;
        case Game::Grudge:
            main_entries = &balsente_roms::kGrudgeMain;
            sprite_entries = &balsente_roms::kGrudgeSprites;
            break;
        case Game::Triviaes:
            main_entries = &balsente_roms::kTriviaesMain;
            sprite_entries = &balsente_roms::kTriviaesSprites;
            break;
        case Game::Toggle:
            main_entries = &balsente_roms::kToggleMain;
            sprite_entries = &balsente_roms::kToggleSprites;
            break;
        case Game::Nstocker:
            main_entries = &balsente_roms::kNstockerMain;
            sprite_entries = &balsente_roms::kNstockerSprites;
            break;
        case Game::Sfootbal:
            main_entries = &balsente_roms::kSfootbalMain;
            sprite_entries = &balsente_roms::kSfootbalSprites;
            break;
        case Game::Spiker:
            main_entries = &balsente_roms::kSpikerMain;
            sprite_entries = &balsente_roms::kSpikerSprites;
            break;
        case Game::Stompin:
            main_entries = &balsente_roms::kStompinMain;
            sprite_entries = &balsente_roms::kStompinSprites;
            break;
        case Game::Nametune:
            main_entries = &balsente_roms::kNametuneMain;
            sprite_entries = &balsente_roms::kNametuneSprites;
            break;
        case Game::Rescraid:
            main_entries = &balsente_roms::kRescraidMain;
            sprite_entries = &balsente_roms::kRescraidSprites;
            break;
    }

    if (!main_entries || !sprite_entries) {
        if (error) *error = "Unknown balsente game";
        return false;
    }

    const size_t rom_size = (game_ == Game::Nametune) ? 0x40000u : 0x20000u;
    if (!loader.load(*main_entries, main_rom_, error)) return false;
    if (main_rom_.size() < rom_size) main_rom_.resize(rom_size, 0xff);

    if (!loader.load(*sprite_entries, sprite_rom_, error)) return false;
    if (sprite_rom_.empty()) {
        if (error) *error = "Missing sprite ROM";
        return false;
    }
    sprite_mask_ = uint32_t(sprite_rom_.size() - 1);

    std::vector<uint8_t> sound;
    if (!loader.load(balsente_roms::kSoundRom, sound, error)) return false;
    sound_rom_.fill(0xff);
    const size_t copy = std::min(sound.size(), sound_rom_.size());
    std::memcpy(sound_rom_.data(), sound.data(), copy);
    return true;
}

void Balsente::apply_game_init() {
    shooter_ = false;
    adc_shift_ = 0;
    switch (game_) {
        case Game::Sentetst:
        case Game::Cshift:
        case Game::Hattrick:
        case Game::Teamht:
        case Game::Toggle:
        case Game::Triviag1:
            expand_roms(kExpandAll);
            break;
        case Game::Gghost:
        case Game::Snakepit:
        case Game::Snakjack:
        case Game::Gimeabrk:
            expand_roms(kExpandAll);
            adc_shift_ = 1;
            break;
        case Game::Otwalls:
        case Game::Stocker:
            expand_roms(kExpandAll);
            adc_shift_ = 0;
            break;
        case Game::Triviabb:
        case Game::Triviag2:
        case Game::Triviayp:
        case Game::Triviasp:
            expand_roms(kExpandNone);
            break;
        case Game::Triviaes:
            expand_roms(kExpandNone | kSwapHalves);
            break;
        case Game::Minigolf:
            expand_roms(kExpandNone);
            adc_shift_ = 2;
            break;
        case Game::Grudge:
        case Game::Rescraid:
            expand_roms(kExpandNone);
            break;
        case Game::Nametune:
            expand_roms(kExpandNone | kSwapHalves);
            break;
        case Game::Nstocker:
            expand_roms(kExpandNone | kSwapHalves);
            shooter_ = true;
            adc_shift_ = 1;
            break;
        case Game::Sfootbal:
            expand_roms(kExpandAll | kSwapHalves);
            break;
        case Game::Spiker:
            expand_roms(kExpandAll | kSwapHalves);
            adc_shift_ = 1;
            break;
        case Game::Stompin:
            expand_roms(0x0c | kSwapHalves);
            adc_shift_ = 32;
            break;
    }
}

void Balsente::expand_roms(uint8_t cd_rom_mask) {
    bankab_.fill(nullptr);
    bankcd_.fill(nullptr);
    bankef_.fill(nullptr);

    const uint32_t len = uint32_t(main_rom_.size());
    num_banks_ = (len > 0x20000) ? 16 : 8;
    const uint32_t bxor = (cd_rom_mask & kSwapHalves) ? 0x02000u : 0;

    uint8_t* rom = main_rom_.data();
    for (int b = 0; b < num_banks_; b += 8) {
        const uint32_t base = 0x00000u + 0x4000u * uint32_t(b);
        uint8_t* ab_base = &rom[base + 0x00000];
        uint8_t* cd_base = &rom[base + 0x10000];
        uint8_t* cd_common = &rom[base + (0x1c000u ^ bxor)];
        uint8_t* ef_common = &rom[base + (0x1e000u ^ bxor)];

        bankef_[size_t(b / 8)] = ef_common;

        bankcd_[size_t(b + 7)] = cd_common;
        bankab_[size_t(b + 7)] = &ab_base[0xe000u ^ bxor];

        bankcd_[size_t(b + 6)] = cd_common;
        bankab_[size_t(b + 6)] = &ab_base[0xc000u ^ bxor];

        bankcd_[size_t(b + 5)] = (cd_rom_mask & (1u << 5)) ? &cd_base[0xa000u ^ bxor] : cd_common;
        bankab_[size_t(b + 5)] = &ab_base[0xa000u ^ bxor];

        bankcd_[size_t(b + 4)] = (cd_rom_mask & (1u << 4)) ? &cd_base[0x8000u ^ bxor] : cd_common;
        bankab_[size_t(b + 4)] = &ab_base[0x8000u ^ bxor];

        bankcd_[size_t(b + 3)] = (cd_rom_mask & (1u << 3)) ? &cd_base[0x6000u ^ bxor] : cd_common;
        bankab_[size_t(b + 3)] = &ab_base[0x6000u ^ bxor];

        bankcd_[size_t(b + 2)] = (cd_rom_mask & (1u << 2)) ? &cd_base[0x4000u ^ bxor] : cd_common;
        bankab_[size_t(b + 2)] = &ab_base[0x4000u ^ bxor];

        bankcd_[size_t(b + 1)] = (cd_rom_mask & (1u << 1)) ? &cd_base[0x2000u ^ bxor] : cd_common;
        bankab_[size_t(b + 1)] = &ab_base[0x2000u ^ bxor];

        bankcd_[size_t(b + 0)] = (cd_rom_mask & (1u << 0)) ? &cd_base[0x0000u ^ bxor] : cd_common;
        bankab_[size_t(b + 0)] = &ab_base[0x0000u ^ bxor];
    }

    bank_ab_ = 0;
    bank_cd_ = 0;
    bank_ef_ = 0;
}

void Balsente::poly17_init() {
    uint32_t x = 0;
    for (size_t i = 0; i <= kPoly17Size; ++i) {
        rand17_[i] = uint8_t(x >> 3);
        x = ((x << kPoly17Shl) + (x >> kPoly17Shr) + kPoly17Add) & kPoly17Size;
    }
}

void Balsente::reset() {
    main_.reset();
    sound_.reset();
    for (auto& c : cem_) c.reset();

    spriteram_.fill(0);
    videoram_.fill(0);
    expanded_videoram_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    novram_.fill(0);
    sound_ram_.fill(0);
    framebuffer_.fill(0);
    audio_.clear();
    audio_error_ = 0;

    palettebank_vis_ = 0;
    bank_ab_ = bank_cd_ = bank_ef_ = 0;
    adc_value_ = 0x80;
    main_total_cycles_ = 0;
    vblank_ = false;

    counter_.fill(Counter8253{});
    counter_[2].gate = 1;
    counter_control_ = 0;
    counter_0_ff_ = 0;
    counter_0_timer_active_ = false;
    counter_0_period_cycles_ = 0;
    counter_0_phase_ = 0;

    dac_value_ = 0;
    dac_register_ = 0;
    chip_select_ = 0x3f;

    m6850_status_ = 0x02;
    m6850_control_ = 3;
    m6850_input_ = 0;
    m6850_output_ = 0;
    m6850_data_ready_ = false;
    m6850_tx_pending_ = false;
    m6850_tx_delay_ = 0;

    m6850_sound_status_ = 0x02;
    m6850_sound_control_ = 3;
    m6850_sound_input_ = 0;
    m6850_sound_output_ = 0;
    m6850_update_io();

    spiker_expand_color_ = 0;
    spiker_expand_bgcolor_ = 0;
    spiker_expand_bits_ = 0;
}

// ---------------------------------------------------------------------------
// Main CPU memory
// ---------------------------------------------------------------------------

uint8_t Balsente::main_read(uint16_t address) {
    if (address < 0x0800) return spriteram_[address];
    if (address < 0x8000) return videoram_[address - 0x0800];
    if (address < 0x9000) return palette_ram_[address - 0x8000];
    if (address >= 0x9400 && address <= 0x9401) return adc_data_r();
    if (address == 0x9900) return dsw_h_;
    if (address == 0x9901) return dsw_g_;
    if (address == 0x9902) return in0_;
    if (address == 0x9903) {
        uint8_t v = in1_ & 0x7f;
        if (vblank_) v |= 0x80;
        return v;
    }
    if (address >= 0x9a00 && address <= 0x9a03) return random_num_r();
    if (address >= 0x9a04 && address <= 0x9a05) return m6850_r(address - 0x9a04);
    if (address >= 0x9b00 && address <= 0x9cff) return novram_[address - 0x9b00];
    if (game_ == Game::Grudge && address == 0x9400) return 0xff;  // steering stub
    if (game_ == Game::Spiker && address >= 0x9f80 && address <= 0x9f8f) {
        return spiker_expand_r();
    }
    if (address >= 0xa000 && address <= 0xbfff) {
        const uint8_t* bank = bankab_[size_t(bank_ab_ & (num_banks_ - 1))];
        return bank ? bank[address - 0xa000] : 0xff;
    }
    if (address >= 0xc000 && address <= 0xdfff) {
        const uint8_t* bank = bankcd_[size_t(bank_cd_ & (num_banks_ - 1))];
        return bank ? bank[address - 0xc000] : 0xff;
    }
    if (address >= 0xe000) {
        const uint8_t* bank = bankef_[size_t(bank_ef_ & 1)];
        return bank ? bank[address - 0xe000] : 0xff;
    }
    return 0xff;
}

void Balsente::main_write(uint16_t address, uint8_t value) {
    if (address < 0x0800) {
        spriteram_[address] = value;
        return;
    }
    if (address < 0x8000) {
        videoram_w(uint16_t(address - 0x0800), value);
        return;
    }
    if (address < 0x9000) {
        palette_ram_w(uint16_t(address - 0x8000), value);
        return;
    }
    if (address >= 0x9000 && address <= 0x9007) {
        adc_select_w(uint8_t(address & 7));
        return;
    }
    if (address >= 0x9800 && address <= 0x987f) {
        // misc outputs / NVRAM recall — ignored
        return;
    }
    if (address >= 0x9880 && address <= 0x989f) {
        // random reset
        return;
    }
    if (address >= 0x98a0 && address <= 0x98bf) {
        rombank_select_w(value);
        return;
    }
    if (address >= 0x98c0 && address <= 0x98df) {
        palette_select_w(value);
        return;
    }
    if (address >= 0x98e0 && address <= 0x98ff) {
        // watchdog
        return;
    }
    if (address >= 0x9a04 && address <= 0x9a05) {
        m6850_w(address - 0x9a04, value);
        return;
    }
    if (address >= 0x9b00 && address <= 0x9cff) {
        novram_[address - 0x9b00] = value;
        return;
    }
    if (needs_rombank2(game_) && address == 0x9f00) {
        rombank2_select_w(value);
        return;
    }
    if (game_ == Game::Spiker && address >= 0x9f80 && address <= 0x9f8f) {
        spiker_expand_w(uint16_t(address - 0x9f80), value);
        return;
    }
}

void Balsente::videoram_w(uint16_t offset, uint8_t data) {
    if (offset >= videoram_.size()) return;
    videoram_[offset] = data;
    const size_t px = size_t(offset) * 2;
    if (px + 1 < expanded_videoram_.size()) {
        expanded_videoram_[px] = uint8_t(data >> 4);
        expanded_videoram_[px + 1] = uint8_t(data & 0x0f);
    }
}

void Balsente::palette_ram_w(uint16_t offset, uint8_t data) {
    if (offset >= palette_ram_.size()) return;
    palette_ram_[offset] = data;
    update_palette_entry(int(offset >> 2));
}

void Balsente::update_palette_entry(int index) {
    if (index < 0 || index >= 1024) return;
    const size_t base = size_t(index) * 4;
    const uint8_t r = expand4(palette_ram_[base + 0]);
    const uint8_t g = expand4(palette_ram_[base + 1]);
    const uint8_t b = expand4(palette_ram_[base + 2]);
    palette_[size_t(index)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void Balsente::palette_select_w(uint8_t data) { palettebank_vis_ = data & 3; }

void Balsente::rombank_select_w(uint8_t data) {
    const int bank = (data >> 4) & 7;
    bank_ab_ = bank;
    bank_cd_ = bank;
}

void Balsente::rombank2_select_w(uint8_t data) {
    int bank = data & 7;
    if (main_rom_.size() > 0x20000) bank |= (data >> 4) & 8;
    if (data & 0x20) {
        bank_ab_ = bank;
        bank_cd_ = 6;
    } else {
        bank_ab_ = bank;
        bank_cd_ = bank;
    }
}

uint8_t Balsente::random_num_r() {
    // CPU @ 1.25 MHz, noise @ 100 kHz → ×12.5
    uint64_t cc = main_total_cycles_;
    cc = (cc << 3) + (cc << 2) + (cc >> 1);
    return rand17_[cc & kPoly17Size];
}

uint8_t Balsente::adc_data_r() { return adc_value_; }

void Balsente::adc_select_w(uint8_t /*offset*/) {
    // Stub: centered stick / trackball.
    adc_value_ = 0x80;
}

// ---------------------------------------------------------------------------
// Soft m6850 UART
// ---------------------------------------------------------------------------

void Balsente::m6850_update_io() {
    // sound → main
    if (!(m6850_sound_status_ & 0x02)) {
        if (m6850_status_ & 0x01) m6850_status_ |= 0x20;
        m6850_input_ = m6850_sound_output_;
        m6850_status_ |= 0x01;
        m6850_sound_status_ |= 0x02;
    }

    // main → sound
    if (m6850_data_ready_) {
        if (m6850_sound_status_ & 0x01) m6850_sound_status_ |= 0x20;
        m6850_sound_input_ = m6850_output_;
        m6850_sound_status_ |= 0x01;
        m6850_status_ |= 0x02;
        m6850_data_ready_ = false;
    }

    if ((m6850_control_ & 3) == 3) {
        m6850_status_ = 0x02;
        m6850_data_ready_ = false;
        m6850_tx_pending_ = false;
    }
    if ((m6850_sound_control_ & 3) == 3) m6850_sound_status_ = 0x02;

    uint8_t new_state = 0;
    if ((m6850_control_ & 0x80) && (m6850_status_ & 0x21)) new_state = 1;
    if ((m6850_control_ & 0x60) == 0x20 && (m6850_status_ & 0x02)) new_state = 1;
    if (new_state && !(m6850_status_ & 0x80)) {
        main_.set_firq(IrqLine::Assert);
        m6850_status_ |= 0x80;
    } else if (!new_state && (m6850_status_ & 0x80)) {
        main_.set_firq(IrqLine::Clear);
        m6850_status_ &= ~0x80;
    }

    new_state = 0;
    if ((m6850_sound_control_ & 0x80) && (m6850_sound_status_ & 0x21)) new_state = 1;
    if ((m6850_sound_control_ & 0x60) == 0x20 && (m6850_sound_status_ & 0x02)) new_state = 1;
    if (!(counter_control_ & 0x20)) new_state = 0;
    if (new_state && !(m6850_sound_status_ & 0x80)) {
        sound_.set_nmi(IrqLine::Assert);
        m6850_sound_status_ |= 0x80;
    } else if (!new_state && (m6850_sound_status_ & 0x80)) {
        sound_.set_nmi(IrqLine::Clear);
        m6850_sound_status_ &= ~0x80;
    }
}

uint8_t Balsente::m6850_r(uint16_t offset) {
    if (offset == 0) return m6850_status_;
    const uint8_t result = m6850_input_;
    m6850_status_ &= ~0x21;
    m6850_update_io();
    return result;
}

void Balsente::m6850_queue_transmit(uint8_t data) {
    m6850_status_ &= ~0x02;
    m6850_update_io();
    m6850_tx_byte_ = data;
    m6850_tx_pending_ = true;
    m6850_tx_delay_ = kUartDelayMainCycles;
}

void Balsente::m6850_w(uint16_t offset, uint8_t data) {
    if (offset == 0) {
        m6850_control_ = data;
        m6850_update_io();
    } else {
        m6850_queue_transmit(data);
    }
}

uint8_t Balsente::m6850_sound_r(uint16_t offset) {
    if (offset == 0) return m6850_sound_status_;
    const uint8_t result = m6850_sound_input_;
    m6850_sound_status_ &= ~0x21;
    m6850_update_io();
    return result;
}

void Balsente::m6850_sound_w(uint16_t offset, uint8_t data) {
    if (offset == 0) {
        m6850_sound_control_ = data;
    } else {
        m6850_sound_output_ = data;
        m6850_sound_status_ &= ~0x02;
    }
    m6850_update_io();
}

void Balsente::advance_uart(int main_cycles) {
    if (!m6850_tx_pending_) return;
    m6850_tx_delay_ -= main_cycles;
    if (m6850_tx_delay_ <= 0) {
        m6850_tx_pending_ = false;
        m6850_output_ = m6850_tx_byte_;
        m6850_data_ready_ = true;
        m6850_update_io();
    }
}

// ---------------------------------------------------------------------------
// Sound CPU memory / IO
// ---------------------------------------------------------------------------

uint8_t Balsente::sound_read(uint16_t address) {
    if (address < 0x2000) return sound_rom_[address];
    if (address < 0x6000) return sound_ram_[address];
    if (address >= 0xe000) return m6850_sound_r(address & 1);
    return 0xff;
}

void Balsente::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x2000 && address < 0x6000) {
        sound_ram_[address] = value;
        return;
    }
    if (address >= 0x6000 && address < 0x8000) {
        m6850_sound_w(address & 1, value);
        return;
    }
}

uint8_t Balsente::sound_in(uint16_t port) {
    port &= 0xff;
    if ((port & 0xf8) == 0x00) return counter_8253_r(port & 3);
    if ((port & 0xf8) == 0x08) return counter_state_r();
    return 0xff;
}

void Balsente::sound_out(uint16_t port, uint8_t value) {
    port &= 0xff;
    if ((port & 0xfc) == 0x00) {
        counter_8253_w(port & 3, value);
        return;
    }
    if ((port & 0xfe) == 0x08) {
        counter_control_w(value);
        return;
    }
    if ((port & 0xfe) == 0x0a) {
        dac_data_w(port & 1, value);
        return;
    }
    if ((port & 0xfe) == 0x0c) {
        register_addr_w(value);
        return;
    }
    if ((port & 0xfe) == 0x0e) {
        chip_select_w(value);
        return;
    }
}

// ---------------------------------------------------------------------------
// 8253 counters (incomplete, as in classic MAME)
// ---------------------------------------------------------------------------

void Balsente::counter_start(int which) {
    if (which == 0) return;
    if (counter_[size_t(which)].gate && !counter_[size_t(which)].timer_active) {
        counter_[size_t(which)].timer_active = true;
        counter_[size_t(which)].remaining_2mhz = counter_[size_t(which)].count;
    }
}

void Balsente::counter_stop(int which) {
    counter_[size_t(which)].timer_active = false;
    counter_[size_t(which)].remaining_2mhz = 0;
}

void Balsente::counter_update_count(int which) {
    if (counter_[size_t(which)].timer_active) {
        int64_t rem = counter_[size_t(which)].remaining_2mhz;
        counter_[size_t(which)].count = (rem < 0) ? 0 : int32_t(rem);
    }
}

void Balsente::counter_set_out(int which, int out) {
    if (which == 2) {
        sound_.set_irq(out ? IrqLine::Assert : IrqLine::Clear);
    } else if (which == 0) {
        counter_set_gate(1, !out);
    }
    counter_[size_t(which)].out = uint8_t(out);
}

void Balsente::counter_set_gate(int which, int gate) {
    const int oldgate = counter_[size_t(which)].gate;
    counter_[size_t(which)].gate = uint8_t(gate);
    if (!gate && oldgate) {
        counter_update_count(which);
        counter_stop(which);
    } else if (gate && !oldgate) {
        if (counter_[size_t(which)].mode == 1) {
            counter_set_out(which, 0);
            counter_[size_t(which)].count = counter_[size_t(which)].initial + 1;
        }
        counter_start(which);
    }
}

void Balsente::counter_callback(int which) {
    counter_[size_t(which)].timer_active = false;
    counter_[size_t(which)].count = 0;
    counter_[size_t(which)].remaining_2mhz = 0;
    if (counter_[size_t(which)].mode == 0 || counter_[size_t(which)].mode == 1) {
        counter_set_out(which, 1);
    }
}

uint8_t Balsente::counter_8253_r(uint16_t offset) {
    switch (offset & 3) {
        case 0:
        case 1:
        case 2: {
            const int which = offset & 3;
            counter_update_count(which);
            if (counter_[size_t(which)].readbyte == 0) {
                counter_[size_t(which)].readbyte = 1;
                return uint8_t(counter_[size_t(which)].count & 0xff);
            }
            counter_[size_t(which)].readbyte = 0;
            return uint8_t((counter_[size_t(which)].count >> 8) & 0xff);
        }
        default:
            return 0;
    }
}

void Balsente::counter_8253_w(uint16_t offset, uint8_t data) {
    switch (offset & 3) {
        case 0:
        case 1:
        case 2: {
            const int which = offset & 3;
            if (counter_[size_t(which)].mode == 0) counter_set_out(which, 0);
            if (counter_[size_t(which)].writebyte == 0) {
                counter_[size_t(which)].count =
                    (counter_[size_t(which)].count & 0xff00) | (data & 0x00ff);
                counter_[size_t(which)].initial =
                    (counter_[size_t(which)].initial & 0xff00) | (data & 0x00ff);
                counter_[size_t(which)].writebyte = 1;
            } else {
                counter_[size_t(which)].count =
                    (counter_[size_t(which)].count & 0x00ff) | ((data << 8) & 0xff00);
                counter_[size_t(which)].initial =
                    (counter_[size_t(which)].initial & 0x00ff) | ((data << 8) & 0xff00);
                counter_[size_t(which)].writebyte = 0;
                if (counter_[size_t(which)].count == 0) {
                    counter_[size_t(which)].count = counter_[size_t(which)].initial = 0x10000;
                }
                counter_stop(which);
                if (counter_[size_t(which)].mode == 0) counter_start(which);
                if (counter_[size_t(which)].mode == 1) counter_set_out(which, 1);
            }
            break;
        }
        case 3: {
            const int which = data >> 6;
            if (which == 3) break;
            if (((counter_[size_t(which)].mode >> 1) & 7) == 0) counter_set_out(which, 0);
            counter_[size_t(which)].mode = (data >> 1) & 7;
            if (counter_[size_t(which)].mode == 0) counter_set_out(which, 0);
            break;
        }
    }
}

void Balsente::set_counter_0_ff(int newstate) {
    if (counter_0_ff_ && !newstate) {
        if (counter_[0].count > 0 && counter_[0].gate) {
            counter_[0].count--;
            if (counter_[0].count == 0) counter_callback(0);
        }
    }
    counter_0_ff_ = uint8_t(newstate);
}

void Balsente::update_counter_0_timer() {
    counter_0_timer_active_ = false;
    counter_0_period_cycles_ = 0;
    double maxfreq = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (cem_[size_t(i)].get_parameter(Cem3394::FINAL_GAIN) < 10.0) {
            double tempfreq;
            if (cem_[size_t(i)].get_parameter(Cem3394::FILTER_RESONANCE) > 0.9) {
                tempfreq = cem_[size_t(i)].get_parameter(Cem3394::FILTER_FREQUENCY);
            } else {
                tempfreq = cem_[size_t(i)].get_parameter(Cem3394::VCO_FREQUENCY);
            }
            if (tempfreq > maxfreq) maxfreq = tempfreq;
        }
    }
    if (maxfreq > 0.0) {
        counter_0_timer_active_ = true;
        // Period in sound-CPU cycles (4 MHz).
        counter_0_period_cycles_ = double(kSoundClock) / maxfreq;
        if (counter_0_period_cycles_ < 1.0) counter_0_period_cycles_ = 1.0;
    }
}

uint8_t Balsente::counter_state_r() {
    int result = !counter_0_ff_;
    if (counter_[0].out) result |= 0x02;
    return uint8_t(result);
}

void Balsente::counter_control_w(uint8_t data) {
    const uint8_t diff = uint8_t(counter_control_ ^ data);
    counter_control_ = data;

    if (!counter_[0].gate && (data & 0x02) && !counter_0_timer_active_) {
        update_counter_0_timer();
    } else if (counter_[0].gate && !(data & 0x02) && counter_0_timer_active_) {
        counter_0_timer_active_ = false;
        counter_0_period_cycles_ = 0;
    }

    counter_set_gate(0, (data >> 1) & 1);

    if (!(data & 0x04)) set_counter_0_ff(1);
    if (!(data & 0x10)) set_counter_0_ff(0);

    if (diff & 0x20) m6850_update_io();
    (void)diff;
}

void Balsente::advance_counters(int sound_cycles) {
    // Counters 1/2 clocked at 2 MHz; sound CPU is 4 MHz → half the cycles.
    const int clocks = sound_cycles / 2;
    if (clocks > 0) {
        for (int which = 1; which <= 2; ++which) {
            if (!counter_[size_t(which)].timer_active) continue;
            counter_[size_t(which)].remaining_2mhz -= clocks;
            if (counter_[size_t(which)].remaining_2mhz <= 0) counter_callback(which);
        }
    }

    if (counter_0_timer_active_ && counter_0_period_cycles_ > 0) {
        counter_0_phase_ += double(sound_cycles);
        while (counter_0_phase_ >= counter_0_period_cycles_) {
            counter_0_phase_ -= counter_0_period_cycles_;
            set_counter_0_ff((counter_control_ >> 3) & 1);
        }
    }
}

void Balsente::chip_select_w(uint8_t data) {
    static const int kRegisterMap[8] = {
        Cem3394::VCO_FREQUENCY,     Cem3394::FINAL_GAIN,        Cem3394::FILTER_RESONANCE,
        Cem3394::FILTER_FREQUENCY,  Cem3394::MIXER_BALANCE,     Cem3394::MODULATION_AMOUNT,
        Cem3394::PULSE_WIDTH,       Cem3394::WAVE_SELECT,
    };
    const double voltage = double(dac_value_) * (8.0 / 4096.0) - 4.0;
    const int diffchip = data ^ chip_select_;
    const int reg = kRegisterMap[dac_register_ & 7];
    chip_select_ = data;
    for (int i = 0; i < 6; ++i) {
        if ((diffchip & (1 << i)) && (data & (1 << i))) {
            cem_[size_t(i)].set_voltage(reg, voltage);
        }
    }
    if (counter_0_timer_active_) update_counter_0_timer();
}

void Balsente::dac_data_w(uint16_t offset, uint8_t data) {
    if (offset & 1) {
        dac_value_ = uint16_t((dac_value_ & 0xfc0) | ((data >> 2) & 0x03f));
    } else {
        dac_value_ = uint16_t((dac_value_ & 0x03f) | ((data << 6) & 0xfc0));
    }
    if ((chip_select_ & 0x3f) != 0x3f) {
        const uint8_t temp = chip_select_;
        chip_select_w(0x3f);
        chip_select_w(temp);
    }
}

void Balsente::register_addr_w(uint8_t data) { dac_register_ = data & 7; }

uint8_t Balsente::spiker_expand_r() {
    spiker_expand_bits_ =
        uint8_t(((spiker_expand_bits_ << 1) & 0xee) | ((spiker_expand_bits_ >> 3) & 0x11));
    const uint8_t left =
        (spiker_expand_bits_ & 0x10) ? spiker_expand_color_ : spiker_expand_bgcolor_;
    const uint8_t right =
        (spiker_expand_bits_ & 0x01) ? spiker_expand_color_ : spiker_expand_bgcolor_;
    spiker_expand_bgcolor_ = 0;
    return uint8_t((left & 0xf0) | (right & 0x0f));
}

void Balsente::spiker_expand_w(uint16_t offset, uint8_t data) {
    if (offset == 0) spiker_expand_bits_ = data;
    else if (offset == 1) spiker_expand_bgcolor_ = data;
    else if (offset == 2) spiker_expand_color_ = data;
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

void Balsente::draw_one_sprite(const uint8_t* sprite) {
    const int flags = sprite[0];
    const int image = sprite[1] | ((flags & 7) << 8);
    int ypos = sprite[2] + 17;  // visible-line coords (VBEND already removed)
    const int xpos = sprite[3];

    const uint8_t* src = &sprite_rom_[(64 * image) & sprite_mask_];
    if (flags & 0x80) src += 4 * 15;

    const uint32_t* pens = &palette_[size_t(palettebank_vis_) * 256];

    for (int y = 0; y < 16; ++y, ypos = (ypos + 1) & 255) {
        if (ypos >= 16 && ypos < kScreenHeight) {
            const uint8_t* old = &expanded_videoram_[size_t(ypos) * 256 + size_t(xpos)];
            int currx = xpos;
            uint32_t* dest = &framebuffer_[size_t(ypos) * kScreenWidth];

            if (!(flags & 0x40)) {
                for (int x = 0; x < 4; ++x, old += 2) {
                    const int ipixel = *src++;
                    const int left = ipixel & 0xf0;
                    const int right = (ipixel << 4) & 0xf0;
                    if (left && currx >= 0 && currx < 256) dest[currx] = pens[left | old[0]];
                    ++currx;
                    if (right && currx >= 0 && currx < 256) dest[currx] = pens[right | old[1]];
                    ++currx;
                }
            } else {
                src += 4;
                for (int x = 0; x < 4; ++x, old += 2) {
                    const int ipixel = *--src;
                    const int left = (ipixel << 4) & 0xf0;
                    const int right = ipixel & 0xf0;
                    if (left && currx >= 0 && currx < 256) dest[currx] = pens[left | old[0]];
                    ++currx;
                    if (right && currx >= 0 && currx < 256) dest[currx] = pens[right | old[1]];
                    ++currx;
                }
                src += 4;
            }
        } else {
            src += 4;
        }
        if (flags & 0x80) src -= 2 * 4;
    }
}

void Balsente::render_frame() {
    const uint32_t* pens = &palette_[size_t(palettebank_vis_) * 256];
    for (int y = 0; y < kScreenHeight; ++y) {
        const uint8_t* src = &expanded_videoram_[size_t(y) * 256];
        uint32_t* dst = &framebuffer_[size_t(y) * kScreenWidth];
        for (int x = 0; x < kScreenWidth; ++x) dst[x] = pens[src[x]];
    }
    for (int i = 0; i < 40; ++i) {
        draw_one_sprite(&spriteram_[(0xe0 + i * 4) & 0xff]);
    }
}

void Balsente::generate_audio(int main_cycles) {
    const double samples_per_cycle =
        double(kSampleRate) / double(kMainClock);
    audio_error_ += double(main_cycles) * samples_per_cycle;
    const int n = int(audio_error_);
    audio_error_ -= n;
    const bool enabled = (counter_control_ & 0x01) != 0;
    for (int i = 0; i < n; ++i) {
        int32_t mix = 0;
        if (enabled) {
            for (int c = 0; c < 6; ++c) mix += cem_[size_t(c)].update();
            mix /= 3;  // soft clip headroom
        }
        audio_.push_back(int16_t(std::clamp(mix, int32_t(-32768), int32_t(32767))));
    }
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

void Balsente::run_frame() {
    const double main_per_line = double(kMainClock) / kFramesPerSecond / double(kScanlines);
    const double sound_per_line = double(kSoundClock) / kFramesPerSecond / double(kScanlines);
    double main_carry = 0;
    double sound_carry = 0;

    for (int scanline = 0; scanline < kScanlines; ++scanline) {
        // Full-screen VBLANK: lines < VBEND or >= VBSTART.
        vblank_ = (scanline < kVbEnd) || (scanline >= kVbStart);

        const bool irq_line = (scanline % 64) == 0;
        main_carry += main_per_line;
        sound_carry += sound_per_line;
        int main_budget = std::max(1, int(main_carry));
        int sound_budget = std::max(1, int(sound_carry));
        main_carry -= main_budget;
        sound_carry -= sound_budget;

        const int hblank_main = std::max(1, main_budget * kHbStart / kHTotal);
        const int after_main = std::max(0, main_budget - hblank_main);
        const int hblank_sound = std::max(1, sound_budget * kHbStart / kHTotal);
        const int after_sound = std::max(0, sound_budget - hblank_sound);

        if (irq_line) main_.set_irq(IrqLine::Assert);

        int ran_m = main_.run(hblank_main);
        main_total_cycles_ += uint64_t(ran_m);
        advance_uart(ran_m);
        generate_audio(ran_m);

        int ran_s = sound_.run(hblank_sound);
        advance_counters(ran_s);

        if (irq_line) main_.set_irq(IrqLine::Clear);

        if (after_main > 0) {
            ran_m = main_.run(after_main);
            main_total_cycles_ += uint64_t(ran_m);
            advance_uart(ran_m);
            generate_audio(ran_m);
        }
        if (after_sound > 0) {
            ran_s = sound_.run(after_sound);
            advance_counters(ran_s);
        }

        // Draw once when entering VBLANK (visible region just finished).
        if (scanline == kVbStart) render_frame();
    }

    // Safety: if we somehow skipped, render at end.
    if (!vblank_) render_frame();
}

void Balsente::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

void Balsente::set_inputs(const MachineInputs& in) {
    // Active-low ports. Start from all-high (released).
    in0_ = 0xff;
    in1_ = 0x7f;  // VBLANK overlaid separately

    // Generic Hat Trick–style mapping (works for many early titles).
    if (in.player1.up) in0_ &= ~0x01;
    if (in.player1.down) in0_ &= ~0x02;
    if (in.player1.right) in0_ &= ~0x04;
    if (in.player1.left) in0_ &= ~0x08;
    if (in.player1.button1) in0_ &= ~0x10;
    if (in.player2.button1) in0_ &= ~0x20;
    if (in.coin1) in0_ &= ~0x40;
    if (in.service) in0_ &= ~0x80;

    if (in.player1.start) in1_ &= ~0x01;
    if (in.player2.start) in1_ &= ~0x02;
    if (in.player2.up) in1_ &= ~0x04;
    if (in.player2.down) in1_ &= ~0x08;
    if (in.player2.right) in1_ &= ~0x10;
    if (in.player2.left) in1_ &= ~0x20;
    if (in.coin2) in1_ &= ~0x40;

    // Chicken Shift: buttons on IN0 bits 0/1.
    if (game_ == Game::Cshift) {
        in0_ = 0xff;
        if (in.player1.button1) in0_ &= ~0x01;
        if (in.player1.button2) in0_ &= ~0x02;
        if (in.coin1) in0_ &= ~0x40;
        if (in.service) in0_ &= ~0x80;
        in1_ = 0x7f;
        if (in.player1.start) in1_ &= ~0x01;
        if (in.player2.start) in1_ &= ~0x02;
        if (in.coin2) in1_ &= ~0x40;
    }
}

void Balsente::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_h_ = value;
    else if (bank == 1) dsw_g_ = value;
}

}  // namespace dsp
