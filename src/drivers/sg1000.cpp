#include "drivers/sg1000.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
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
    data.reserve(max_size);
    return loader.load_first_file(data, error);
}

// CRCs from abrir_sg's cartridge case statement: some real SG-1000
// cartridges physically carried extra RAM chips mapped into windows that
// are ordinarily ROM-only.
bool has_extra_ram_2000(uint32_t crc) {
    // BomberMan Special (2), King's Valley, Knightmare, Legend of Kage,
    // Rally X, Road Fighter, Tank Battalion, Twinbee, Yie Ar Kung-Fu II.
    switch (crc) {
        case 0x69fc1494: case 0xce5648c3: case 0x223397a1: case 0x281d2888:
        case 0x2e7166d5: case 0x306d5f78: case 0x29e047cc: case 0x5cbd1163:
        case 0xc550b4f0: case 0xfc87463c:
            return true;
        default:
            return false;
    }
}

bool has_extra_ram_8000(uint32_t crc) {
    // Castle, Othello (x2).
    return crc == 0x092f29d6 || crc == 0xaf4f14bc || crc == 0x1d1a0ca3;
}

}  // namespace

Sg1000::Sg1000()
    : z80_(kMainClock), vdp_(1, [this](bool asserted) { on_vdp_interrupt(asserted); }),
      sn76489_(kMainClock) {
    z80_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    z80_.set_cycle_handler([this](int cycles) { on_main_cycles(cycles); });
}

bool Sg1000::init(const std::string& rom_path, std::string*) {
    // No BIOS: the positional argument is treated as the cartridge itself,
    // for convenience (`--game sg1000 game.sg`). Falling back to `--cart`
    // still works too; a missing/invalid path here just boots with no
    // cartridge inserted instead of failing outright.
    reset();
    if (!rom_path.empty()) {
        std::string cart_error;
        if (!load_media(rom_path, &cart_error)) warnings_.push_back(cart_error);
    }
    return true;
}

bool Sg1000::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge, error)) return false;
    if (data.size() > size_t(kMaxCartridge)) data.resize(size_t(kMaxCartridge));

    ram_8k_ = false;
    mid_8k_ram_ = false;
    memory_.fill(0);
    std::copy(data.begin(), data.end(), memory_.begin());
    reset();

    uint32_t crc = crc32_of(data.data(), data.size());
    if (has_extra_ram_2000(crc)) ram_8k_ = true;
    if (has_extra_ram_8000(crc)) mid_8k_ram_ = true;
    if (crc == 0x49e9718b) {  // Safari Hunting copy protection mirror
        std::copy(memory_.begin(), memory_.begin() + 0x4000, memory_.begin() + 0x4000);
    }
    return true;
}

void Sg1000::reset() {
    z80_.reset();
    sn76489_.reset();
    vdp_.reset();
    keys_ = {0xff, 0xff};
    push_pause_ = false;
    audio_accumulator_ = 0;
    audio_.clear();
}

uint8_t Sg1000::read_byte(uint16_t address) {
    if (address <= 0xbfff) return memory_[address];
    return memory_[0xc000 + (address & 0x1fff)];  // 8 KiB RAM, mirrored
}

void Sg1000::write_byte(uint16_t address, uint8_t value) {
    if (address <= 0x1fff || (address >= 0x4000 && address <= 0x7fff) ||
        (address >= 0xa000 && address <= 0xbfff)) {
        return;  // ROM
    }
    if (address <= 0x3fff) {  // $2000-$3fff
        if (ram_8k_) memory_[address] = value;
        return;
    }
    if (address <= 0x9fff) {  // $8000-$9fff
        if (mid_8k_ram_) memory_[address] = value;
        return;
    }
    memory_[0xc000 + (address & 0x1fff)] = value;
}

uint8_t Sg1000::read_port(uint16_t port) {
    port &= 0xff;
    if (port >= 0x80 && port <= 0xbf) {
        return (port & 1) != 0 ? vdp_.register_read() : vdp_.vram_read();
    }
    if (port >= 0xc0) {
        return (port & 1) != 0 ? keys_[1] : keys_[0];
    }
    return 0xff;
}

void Sg1000::write_port(uint16_t port, uint8_t value) {
    port &= 0xff;
    if (port >= 0x40 && port <= 0x7f) {
        sn76489_.write(value);
    } else if (port >= 0x80 && port <= 0xbf) {
        if ((port & 1) != 0) vdp_.register_write(value); else vdp_.vram_write(value);
    }
    // $c0-$ff: controller ports, read-only.
}

void Sg1000::on_vdp_interrupt(bool asserted) {
    z80_.set_irq(asserted ? IrqLine::Assert : IrqLine::Clear);
}

void Sg1000::on_main_cycles(int cycles) {
    audio_accumulator_ += uint64_t(cycles) * uint64_t(SN76496::kSampleRate);
    while (audio_accumulator_ >= kMainClock) {
        audio_accumulator_ -= kMainClock;
        audio_.push_back(int16_t(sn76489_.update()));
    }
}

void Sg1000::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        z80_.run(kCyclesPerLine);
        vdp_.refresh_ntsc(line);
    }
}

void Sg1000::set_inputs(const MachineInputs& inputs) {
    uint8_t k0 = 0xff, k1 = 0xff;
    const InputState& p1 = inputs.player1;
    const InputState& p2 = inputs.player2;

    if (p1.up) k0 &= 0xfe;
    if (p1.down) k0 &= 0xfd;
    if (p1.left) k0 &= 0xfb;
    if (p1.right) k0 &= 0xf7;
    if (p1.button1) k0 &= 0xef;
    if (p1.button2) k0 &= 0xdf;
    if (p2.up) k0 &= 0xbf;
    if (p2.down) k0 &= 0x7f;
    if (p2.left) k1 &= 0xfe;
    if (p2.right) k1 &= 0xfd;
    if (p2.button1) k1 &= 0xfb;
    if (p2.button2) k1 &= 0xf7;

    keys_[0] = k0;
    keys_[1] = k1;

    // The console's PAUSE button fires an NMI directly (it is wired outside
    // the joystick ports entirely); player 1's start button stands in for it.
    if (p1.start || inputs.coin1) {
        push_pause_ = true;
    } else {
        if (push_pause_) z80_.set_nmi(IrqLine::Pulse);
        push_pause_ = false;
    }
}

void Sg1000::set_dip_switch(int, uint8_t) {
    // The SG-1000 is a cartridge console, it has no DIP switches.
}

void Sg1000::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
