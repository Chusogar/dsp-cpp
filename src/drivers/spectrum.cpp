#include "drivers/spectrum.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// spec_paleta of spectrum_misc.pas, in Delphi's $00BBGGRR order.
const uint32_t kSpectrumColours[16] = {
    0x000000, 0xC00000, 0x0000C0, 0xC000C0, 0x00C000, 0xC0C000, 0x00C0C0, 0xC0C0C0,
    0x000000, 0xFF0000, 0x0000FF, 0xFF00FF, 0x00FF00, 0xFFFF00, 0x00FFFF, 0xFFFFFF,
};

const std::vector<RomEntry> kRoms = {
    {"spectrum.rom|48.rom|48k.rom|zx48.rom", 0x4000, 0x0000, 0xddee531f},
};

// LD-BYTES, the ROM tape loader: the tape only runs while the CPU is in it.
constexpr uint16_t kLoaderStart = 0x0556;
constexpr uint16_t kLoaderEnd = 0x0605;

// Keyboard matrix of the ULA: one half row per address line A8..A15.
constexpr Key kMatrix[8][5] = {
    {Key::LeftShift, Key::Z, Key::X, Key::C, Key::V},
    {Key::A, Key::S, Key::D, Key::F, Key::G},
    {Key::Q, Key::W, Key::E, Key::R, Key::T},
    {Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5},
    {Key::Num0, Key::Num9, Key::Num8, Key::Num7, Key::Num6},
    {Key::P, Key::O, Key::I, Key::U, Key::Y},
    {Key::Enter, Key::L, Key::K, Key::J, Key::H},
    {Key::Space, Key::RightCtrl, Key::M, Key::N, Key::B},
};

uint32_t to_argb(uint32_t bgr) {
    const uint32_t blue = (bgr >> 16) & 0xff;
    const uint32_t green = (bgr >> 8) & 0xff;
    const uint32_t red = bgr & 0xff;
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

// Address of the eight pixel row `line` of the display file.
uint16_t screen_address(int line) {
    return uint16_t(0x4000 + ((line & 0xc0) << 5) + ((line & 0x07) << 8) + ((line & 0x38) << 2));
}

bool ends_with(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                      [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

}  // namespace

Spectrum48::Spectrum48() : cpu_(kCpuClock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    for (size_t index = 0; index < palette_.size(); index++) {
        palette_[index] = to_argb(kSpectrumColours[index]);
    }
    keys_.fill(0xff);

    cpu_.set_memory_handlers([this](uint16_t address) { return read_byte(address); },
                             [this](uint16_t address, uint8_t value) { write_byte(address, value); });
    cpu_.set_io_handlers([this](uint16_t port) { return read_port(port); },
                         [this](uint16_t port, uint8_t value) { write_port(port, value); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });
}

bool Spectrum48::init(const std::string& rom_path, std::string* error) {
    if (ends_with(rom_path, ".rom")) {
        std::FILE* file = std::fopen(rom_path.c_str(), "rb");
        if (file == nullptr) {
            if (error != nullptr) *error = "cannot open " + rom_path;
            return false;
        }
        const size_t read = std::fread(rom_.data(), 1, rom_.size(), file);
        std::fclose(file);
        if (read != rom_.size()) {
            if (error != nullptr) *error = rom_path + " is not a 16K ROM image";
            return false;
        }
    } else {
        RomLoader loader;
        if (!loader.open(rom_path, error)) return false;
        std::vector<uint8_t> rom;
        if (!loader.load(kRoms, rom, error)) return false;
        std::copy(rom.begin(), rom.end(), rom_.begin());
        for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);
    }
    reset();
    return true;
}

bool Spectrum48::load_media(const std::string& path, std::string* error) {
    if (!tape_.load(path, error)) return false;
    return true;
}

void Spectrum48::reset() {
    memory_.fill(0);
    std::copy(rom_.begin(), rom_.end(), memory_.begin());
    keys_.fill(0xff);
    border_ = 0;
    speaker_ = 0;
    mic_ = 0;
    joystick_ = 0;
    flash_counter_ = 0;
    flash_ = false;
    audio_.clear();
    audio_accumulator_ = 0;
    audio_level_ = 0;
    tape_.rewind();
    cpu_.reset();
}

void Spectrum48::write_byte(uint16_t address, uint8_t value) {
    if (address < 0x4000) return;  // ROM
    memory_[address] = value;
}

uint8_t Spectrum48::read_port(uint16_t port) {
    if ((port & 1) == 0) {  // ULA
        uint8_t value = 0xff;
        for (int row = 0; row < 8; row++) {
            if ((port & (0x100 << row)) == 0) value &= keys_[row];
        }
        value &= 0xbf;
        const bool ear = tape_.playing() ? tape_.ear()
                                         : (speaker_ != 0 || (issue2_ && mic_ != 0));
        if (ear) value |= 0x40;
        return value;
    }
    // Kempston joystick, decoded on A5 low.
    if ((port & 0x20) == 0) return joystick_;
    // Everything else is the floating bus; the idle value is enough here.
    return 0xff;
}

void Spectrum48::write_port(uint16_t port, uint8_t value) {
    if ((port & 1) != 0) return;
    border_ = uint8_t(value & 7);
    speaker_ = uint8_t(value & 0x10);
    mic_ = uint8_t(value & 0x08);
}

void Spectrum48::on_cycles(int cycles) {
    tape_.advance(cycles);

    const int level = (speaker_ != 0 ? 1 : 0) + ((tape_.playing() && tape_.ear()) ? 1 : 0);
    audio_level_ += int64_t(cycles) * level;
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        const int64_t cycles_per_sample = int64_t(kCpuClock) / kSampleRate;
        int64_t average = audio_level_ * 8000 / (cycles_per_sample * 2);
        audio_level_ = 0;
        audio_.push_back(int16_t(std::min<int64_t>(average, 16000)));
    }
}

void Spectrum48::update_tape() {
    if (!tape_.loaded() || tape_.finished()) return;
    const uint16_t pc = cpu_.pc();
    tape_.set_playing(pc >= kLoaderStart && pc <= kLoaderEnd);
}

void Spectrum48::render_line(int line) {
    const int y = line - (64 - kBorderTop);
    if (y < 0 || y >= kScreenHeight) return;
    uint32_t* row = framebuffer_.data() + size_t(y) * kScreenWidth;
    const uint32_t border = palette_[border_];

    const int display_line = line - 64;
    if (display_line < 0 || display_line >= 192) {
        std::fill(row, row + kScreenWidth, border);
        return;
    }

    std::fill(row, row + kBorderLeft, border);
    std::fill(row + kBorderLeft + 256, row + kScreenWidth, border);

    const uint16_t pixels = screen_address(display_line);
    const uint16_t attributes = uint16_t(0x5800 + (display_line >> 3) * 32);
    uint32_t* pixel = row + kBorderLeft;
    for (int column = 0; column < 32; column++) {
        uint8_t bits = memory_[pixels + column];
        const uint8_t attribute = memory_[attributes + column];
        const uint8_t bright = uint8_t((attribute & 0x40) != 0 ? 8 : 0);
        uint32_t ink = palette_[(attribute & 7) | bright];
        uint32_t paper = palette_[((attribute >> 3) & 7) | bright];
        if ((attribute & 0x80) != 0 && flash_) std::swap(ink, paper);
        for (int bit = 0; bit < 8; bit++) {
            *pixel++ = (bits & 0x80) != 0 ? ink : paper;
            bits = uint8_t(bits << 1);
        }
    }
}

void Spectrum48::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        update_tape();
        if (line == 0) {
            cpu_.set_irq(IrqLine::Assert);
            cpu_.run(kIrqCycles);
            cpu_.set_irq(IrqLine::Clear);
            cpu_.run(kCyclesPerLine - kIrqCycles);
        } else {
            cpu_.run(kCyclesPerLine);
        }
        render_line(line);
    }
    flash_counter_ = uint8_t((flash_counter_ + 1) & 0x0f);
    if (flash_counter_ == 0) flash_ = !flash_;
}

void Spectrum48::set_inputs(const MachineInputs& inputs) {
    keys_.fill(0xff);
    for (int row = 0; row < 8; row++) {
        for (int bit = 0; bit < 5; bit++) {
            if (inputs.key(kMatrix[row][bit])) keys_[row] &= uint8_t(~(1 << bit));
        }
    }
    // Left control doubles as symbol shift and the cursor keys as caps shift
    // plus 5/6/7/8, the combinations the ROM expects.
    if (inputs.key(Key::LeftCtrl) || inputs.key(Key::RightShift)) keys_[7] &= 0xfd;
    auto caps_shift_with = [this](int row, int bit) {
        keys_[0] &= 0xfe;
        keys_[row] &= uint8_t(~(1 << bit));
    };
    if (inputs.key(Key::Left)) caps_shift_with(3, 4);   // 5
    if (inputs.key(Key::Down)) caps_shift_with(4, 4);   // 6
    if (inputs.key(Key::Up)) caps_shift_with(4, 3);     // 7
    if (inputs.key(Key::Right)) caps_shift_with(4, 2);  // 8
    if (inputs.key(Key::Backspace)) caps_shift_with(4, 0);

    joystick_ = 0;
    if (inputs.player1.right) joystick_ |= 0x01;
    if (inputs.player1.left) joystick_ |= 0x02;
    if (inputs.player1.down) joystick_ |= 0x04;
    if (inputs.player1.up) joystick_ |= 0x08;
    if (inputs.player1.button1) joystick_ |= 0x10;
}

void Spectrum48::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) issue2_ = value != 0;
}

void Spectrum48::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
