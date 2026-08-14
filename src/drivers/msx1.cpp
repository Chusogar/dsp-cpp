#include "drivers/msx1.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// mpc100_bios in msx1.pas (Panasonic/Mitsubishi 32 KiB MSX1 BIOS). CRC32
// updated to the generic MSX1 BIOS shipped by Abdess/retrobios under
// bios/Other/msx-emu/Machines/Shared Roms/MSX.rom (blueMSX's default MSX1
// machine ROM) — functionally equivalent, any real 32 KiB MSX1 BIOS boots
// the same way, only the splash screen differs.
const std::vector<RomEntry> kBiosRom = {
    {"mpc100bios.rom", 0x8000, 0x0000, 0xa317e6b4},
};

// key_press's run_key_0/1/2 tables: pairs of (keyboard matrix row, value to
// force onto keypad_[row]) that spell out an auto CLOAD/RUN sequence, poked
// once every ~250 ms, terminated by the 0xffff sentinel.
const std::array<uint16_t, 35> kRunKey0 = {
    0x027f, 0x02ff, 0x04fd, 0x04ff, 0x04ef, 0x04ff, 0x02bf, 0x02ff, 0x03fd, 0x03ff,
    0x06fe, 0x02fe, 0x06ff, 0x02ff, 0x03fe, 0x03ff, 0x02bf, 0x02ff, 0x05fe, 0x05ff,
    0x06fe, 0x017f, 0x06ff, 0x01ff, 0x06fe, 0x02fe, 0x06ff, 0x02ff, 0x02fb, 0x02ff,
    0x047f, 0x04ff, 0x077f, 0x07ff, 0xffff,
};
const std::array<uint16_t, 13> kRunKey1 = {
    0x03fe, 0x03ff, 0x04fd, 0x04ff, 0x04ef, 0x04ff, 0x02bf, 0x02ff, 0x03fd, 0x03ff,
    0x077f, 0x07ff, 0xffff,
};
const std::array<uint16_t, 23> kRunKey2 = {
    0x047f, 0x04ff, 0x05fb, 0x05ff, 0x04f7, 0x04ff, 0x06fe, 0x02fe, 0x06ff, 0x02ff,
    0x03fe, 0x03ff, 0x02bf, 0x02ff, 0x05fe, 0x05ff, 0x06fe, 0x017f, 0x06ff, 0x01ff,
    0x077f, 0x07ff, 0xffff,
};

}  // namespace

Msx1::Msx1()
    : z80_(kMainClock),
      vdp_(1, [this](bool asserted) { on_vdp_interrupt(asserted); }),
      ay8910_(kMainClock / 2, 0.8f) {
    z80_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    z80_.set_cycle_handler([this](int cycles) { on_main_cycles(cycles); });

    ay8910_.set_port_handlers([this] { return ay_port_a_read(); }, [this] { return ay_port_b_read(); },
                              nullptr, [this](uint8_t v) { ay_port_b_write(v); });

    ppi_.set_port_handlers([this] { return port_a_read(); }, [this] { return port_b_read(); }, nullptr,
                           [this](uint8_t v) { port_a_write(v); }, nullptr,
                           [this](uint8_t v) { port_c_write(v); });
}

bool Msx1::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> bios(0x8000, 0);
    if (!loader.load(kBiosRom, bios, error)) return false;

    std::copy(bios.begin(), bios.begin() + 0x4000, slot_[0][0].mem.begin());
    std::copy(bios.begin() + 0x4000, bios.end(), slot_[0][1].mem.begin());
    slot_[0][0].rom = true;
    slot_[0][1].rom = true;
    slot_[0][0].ena = true;
    slot_[0][1].ena = true;

    // Slot 3 is plain 64 KiB system RAM (no expansion/disk slots in this
    // minimal MSX1 machine).
    for (int page = 0; page < 4; page++) {
        slot_[3][page].rom = false;
        slot_[3][page].ena = true;
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

namespace {
bool read_plain_or_zip_file(const std::string& path, std::vector<uint8_t>& data, size_t max_size,
                            std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' &&
                     magic[2] == 0x03 && magic[3] == 0x04;
        if (!is_zip) {
            probe.clear();
            probe.seekg(0, std::ios::end);
            std::streamoff size = probe.tellg();
            probe.seekg(0, std::ios::beg);
            if (size <= 0) {
                if (error) *error = "cannot read " + path;
                return false;
            }
            data.resize(size_t(size));
            probe.read(reinterpret_cast<char*>(data.data()), size);
            return bool(probe);
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    //data.reserve(max_size);
    //return loader.load_first_file(data, error);
	return false;
}
}  // namespace

bool Msx1::load_media(const std::string& path, std::string* error) {
    std::string lower = path;
    for (char& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    const auto ends = [&](const char* ext) {
        size_t n = std::strlen(ext);
        return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
    };
    if (ends(".tzx") || ends(".tsx") || ends(".cas") || ends(".wav")) {
        return load_tape(path, error);
    }

    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge, error)) return false;
    if (data.size() > 0x4000 * 2) data.resize(0x4000 * 2);  // 32 KiB cartridge window

    reset();
    std::fill(slot_[1][1].mem.begin(), slot_[1][1].mem.end(), 0);
    std::fill(slot_[1][2].mem.begin(), slot_[1][2].mem.end(), 0);
    size_t first_half = std::min<size_t>(0x4000, data.size());
    std::copy(data.begin(), data.begin() + long(first_half), slot_[1][1].mem.begin());
    if (data.size() > 0x4000) {
        size_t second_half = std::min<size_t>(0x4000, data.size() - 0x4000);
        std::copy(data.begin() + 0x4000, data.begin() + long(0x4000 + second_half),
                  slot_[1][2].mem.begin());
    }
    slot_[1][1].rom = true;
    slot_[1][2].rom = true;
    slot_[1][1].ena = true;
    slot_[1][2].ena = true;
    return true;
}

bool Msx1::load_tape(const std::string& path, std::string* error) {
    if (!tape_.load_file(path, error)) return false;
    tape_.stop();
    start_auto_type(auto_type_key_type_);
    return true;
}

void Msx1::tape_play() {
    if (!tape_.is_loaded()) return;
    if (tape_.is_paused()) tape_.play(false);
    else tape_.play(true);
}

void Msx1::tape_stop() { tape_.stop(); }

void Msx1::start_auto_type(int key_type) {
    auto_type_active_ = true;
    auto_type_pos_ = 0;
    auto_type_key_type_ = key_type;
    auto_type_frame_counter_ = 0;
}

void Msx1::auto_type_step() {
    const uint16_t* table = nullptr;
    int size = 0;
    switch (auto_type_key_type_) {
        case 0: table = kRunKey0.data(); size = int(kRunKey0.size()); break;
        case 1: table = kRunKey1.data(); size = int(kRunKey1.size()); break;
        default: table = kRunKey2.data(); size = int(kRunKey2.size()); break;
    }
    if (auto_type_pos_ >= size) {
        auto_type_active_ = false;
        return;
    }
    uint16_t entry = table[auto_type_pos_];
    if (entry == 0xffff) {
        auto_type_active_ = false;
        auto_type_pos_ = 0;
        return;
    }
    keypad_[entry >> 8] = uint8_t(entry & 0xff);
    auto_type_pos_++;
}

void Msx1::reset() {
    z80_.reset();
    ay8910_.reset();
    ppi_.reset();
    vdp_.reset();

    keypad_.fill(0xff);
    joystick_ = {0x3f, 0x3f};
    joy_select_ = 0;
    port_a_ = 0;
    port_c_ = 0x7f;
    last_irq_ = false;
    audio_accumulator_ = 0;
    audio_.clear();

    page_slot_ = {0, 0, 0, 0};
    pag_rom_[0] = true;
    pag_rom_[1] = true;
    pag_ena_[0] = true;
    pag_ena_[1] = true;
    // pag_ena_[2]/[3] are intentionally left as-is (matches reset_msx1: the
    // BIOS itself configures pages 2/3 via port A right after reset).

    // Real cartridge ROM cannot be erased by a reset; unlike reset_msx1 this
    // clears slot 3 (system RAM) only, not slot 1 (the cartridge), so F3/soft
    // reset does not wipe the inserted game.
    for (int page = 0; page < 4; page++) {
        slot_[2][page].mem.fill(0);
        slot_[3][page].mem.fill(0);
    }
}

uint8_t Msx1::read_byte(uint16_t address) {
    int page = address >> 14;
    if (!pag_ena_[page]) return 0xff;  // important: unmapped pages read $ff
    return slot_[page_slot_[page]][page].mem[address & 0x3fff];
}

void Msx1::write_byte(uint16_t address, uint8_t value) {
    int page = address >> 14;
    if (pag_rom_[page] || !pag_ena_[page]) return;
    slot_[page_slot_[page]][page].mem[address & 0x3fff] = value;
}

uint8_t Msx1::read_port(uint16_t port) {
    port &= 0xff;
    switch (port) {
        case 0x98: return vdp_.vram_read();
        case 0x99: return vdp_.register_read();
        case 0xa2: return ay8910_.read();
        case 0xa8: case 0xa9: case 0xaa: case 0xab: return ppi_.read(port & 3);
        default: return 0xff;
    }
}

void Msx1::write_port(uint16_t port, uint8_t value) {
    port &= 0xff;
    switch (port) {
        case 0x98: vdp_.vram_write(value); break;
        case 0x99: vdp_.register_write(value); break;
        case 0xa0: ay8910_.control(value); break;
        case 0xa1: ay8910_.write(value); break;
        case 0xa8: case 0xa9: case 0xaa: case 0xab: ppi_.write(port & 3, value); break;
        default: break;
    }
}

void Msx1::on_vdp_interrupt(bool asserted) {
    if (asserted && !last_irq_) z80_.set_irq(IrqLine::Hold);
    last_irq_ = asserted;
}

uint8_t Msx1::ay_port_a_read() {
    return uint8_t(joystick_[joy_select_] | uint8_t(tape_.level() << 1));
}

void Msx1::ay_port_b_write(uint8_t value) {
    joy_select_ = (value & 0x40) >> 6;
    port_b_ay_ = value;
}

uint8_t Msx1::port_b_read() { return teclado_ < 10 ? keypad_[teclado_] : 0xff; }

void Msx1::port_a_write(uint8_t value) {
    for (int page = 0; page < 4; page++) {
        int selected = (value >> (page * 2)) & 3;
        page_slot_[page] = selected;
        pag_rom_[page] = slot_[selected][page].rom;
        pag_ena_[page] = slot_[selected][page].ena;
    }
    port_a_ = value;
}

void Msx1::port_c_write(uint8_t value) {
    teclado_ = value & 0x0f;
    // Cassette motor relay, bit 4: 0 = motor on.
    if (((port_c_ ^ value) & 0x10) != 0 && tape_.is_loaded()) {
        bool motor_on = (value & 0x10) == 0;
        if (motor_on && !tape_.is_playing()) tape_.play(false);
        if (!motor_on && tape_.is_playing()) tape_.pause();
    }
    port_c_ = value;
}

void Msx1::on_main_cycles(int cycles) {
    if (tape_.is_playing()) {
        // The tape engine assumes a nominal 3.5 MHz clock; the MSX Z80 runs
        // at 3.579545 MHz, hence the same ratio as msx_despues_instruccion's
        // 0.9777 scale factor.
        int tape_cycles = int(double(cycles) * 3500000.0 / double(kMainClock));
        tape_.advance(tape_cycles);
    }

    audio_accumulator_ += uint64_t(cycles) * uint64_t(AY8910::kSampleRate);
    while (audio_accumulator_ >= kMainClock) {
        audio_accumulator_ -= kMainClock;
        int32_t sample = ay8910_.update();
        if ((port_c_ & 0x80) != 0) sample += 3000;               // cassette monitor bit
        if (tape_.is_playing()) sample += int32_t(tape_.level()) * 96;  // tape playback level
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void Msx1::run_frame() {
    if (auto_type_active_) {
        auto_type_frame_counter_++;
        if (auto_type_frame_counter_ >= int(kFramesPerSecond * 0.25 + 0.5)) {
            auto_type_frame_counter_ = 0;
            auto_type_step();
        }
    }
    for (int line = 0; line < kScanlines; line++) {
        z80_.run(kCyclesPerLine);
        vdp_.refresh_ntsc(line);
    }
}

void Msx1::set_inputs(const MachineInputs& inputs) {
    keypad_.fill(0xff);

    bool rshift = inputs.key(Key::RightShift);
    // P0
    if (inputs.key(Key::Num0)) keypad_[0] &= 0xfe;
    if (inputs.key(Key::Num1) && !rshift) keypad_[0] &= 0xfd;
    if (inputs.key(Key::Num2) && !rshift) keypad_[0] &= 0xfb;
    if (inputs.key(Key::Num3) && !rshift) keypad_[0] &= 0xf7;
    if (inputs.key(Key::Num4) && !rshift) keypad_[0] &= 0xef;
    if (inputs.key(Key::Num5) && !rshift) keypad_[0] &= 0xdf;
    if (inputs.key(Key::Num6)) keypad_[0] &= 0xbf;
    if (inputs.key(Key::Num7)) keypad_[0] &= 0x7f;
    // P1 (Colon/Comma/Period/Slash/Minus approximate the original's spare
    // "FILA" host-key bindings, which had no direct A-Z0-9 equivalent).
    if (inputs.key(Key::Num8)) keypad_[1] &= 0xfe;
    if (inputs.key(Key::Num9)) keypad_[1] &= 0xfd;
    if (inputs.key(Key::Semicolon)) keypad_[1] &= 0xfb;
    if (inputs.key(Key::Comma)) keypad_[1] &= 0xef;
    if (inputs.key(Key::Period)) keypad_[1] &= 0xdf;
    if (inputs.key(Key::Slash)) keypad_[1] &= 0xbf;
    if (inputs.key(Key::Minus)) keypad_[1] &= 0x7f;
    // P2
    if (inputs.key(Key::Quote)) keypad_[2] &= 0xfe;
    // bits 1, 4, 5 (]/, /, *-) have no equivalent free host key, left unbound
    // just like the original left them commented out.
    if (inputs.key(Key::A)) keypad_[2] &= 0xbf;
    if (inputs.key(Key::B)) keypad_[2] &= 0x7f;
    // P3
    if (inputs.key(Key::C)) keypad_[3] &= 0xfe;
    if (inputs.key(Key::D)) keypad_[3] &= 0xfd;
    if (inputs.key(Key::E)) keypad_[3] &= 0xfb;
    if (inputs.key(Key::F)) keypad_[3] &= 0xf7;
    if (inputs.key(Key::G)) keypad_[3] &= 0xef;
    if (inputs.key(Key::H)) keypad_[3] &= 0xdf;
    if (inputs.key(Key::I)) keypad_[3] &= 0xbf;
    if (inputs.key(Key::J)) keypad_[3] &= 0x7f;
    // P4
    if (inputs.key(Key::K)) keypad_[4] &= 0xfe;
    if (inputs.key(Key::L)) keypad_[4] &= 0xfd;
    if (inputs.key(Key::M)) keypad_[4] &= 0xfb;
    if (inputs.key(Key::N)) keypad_[4] &= 0xf7;
    if (inputs.key(Key::O)) keypad_[4] &= 0xef;
    if (inputs.key(Key::P)) keypad_[4] &= 0xdf;
    if (inputs.key(Key::Q)) keypad_[4] &= 0xbf;
    if (inputs.key(Key::R)) keypad_[4] &= 0x7f;
    // P5
    if (inputs.key(Key::S)) keypad_[5] &= 0xfe;
    if (inputs.key(Key::T)) keypad_[5] &= 0xfd;
    if (inputs.key(Key::U)) keypad_[5] &= 0xfb;
    if (inputs.key(Key::V)) keypad_[5] &= 0xf7;
    if (inputs.key(Key::W)) keypad_[5] &= 0xef;
    if (inputs.key(Key::X)) keypad_[5] &= 0xdf;
    if (inputs.key(Key::Y)) keypad_[5] &= 0xbf;
    if (inputs.key(Key::Z)) keypad_[5] &= 0x7f;
    // P6
    if (inputs.key(Key::LeftShift)) keypad_[6] &= 0xfe;
    if (inputs.key(Key::LeftCtrl)) keypad_[6] &= 0xfd;
    if (inputs.key(Key::CapsLock)) keypad_[6] &= 0xf7;
    if (inputs.key(Key::Num1) && rshift) keypad_[6] &= 0xdf;
    if (inputs.key(Key::Num2) && rshift) keypad_[6] &= 0xbf;
    if (inputs.key(Key::Num3) && rshift) keypad_[6] &= 0x7f;
    // P7
    if (inputs.key(Key::Num4) && rshift) keypad_[7] &= 0xfe;
    if (inputs.key(Key::Num5) && rshift) keypad_[7] &= 0xfd;
    if (inputs.key(Key::Escape)) keypad_[7] &= 0xfb;
    if (inputs.key(Key::Tab)) keypad_[7] &= 0xf7;
    if (inputs.key(Key::Backspace)) keypad_[7] &= 0xdf;
    if (inputs.key(Key::Enter)) keypad_[7] &= 0x7f;
    // P8
    if (inputs.key(Key::Space)) keypad_[8] &= 0xfe;
    if (inputs.key(Key::Left)) keypad_[8] &= 0xef;
    if (inputs.key(Key::Up)) keypad_[8] &= 0xdf;
    if (inputs.key(Key::Down)) keypad_[8] &= 0xbf;
    if (inputs.key(Key::Right)) keypad_[8] &= 0x7f;

    joystick_ = {0x3f, 0x3f};
    const InputState* players[2] = {&inputs.player1, &inputs.player2};
    for (int p = 0; p < 2; p++) {
        const InputState& player = *players[p];
        uint8_t joy = 0x3f;
        if (player.up) joy &= 0xfe;
        if (player.down) joy &= 0xfd;
        if (player.left) joy &= 0xfb;
        if (player.right) joy &= 0xf7;
        if (player.button1) joy &= 0xef;
        if (player.button2) joy &= 0xdf;
        joystick_[size_t(p)] = joy;
    }
}

void Msx1::set_dip_switch(int, uint8_t) {
    // No DIP switches on an MSX1.
}

void Msx1::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
