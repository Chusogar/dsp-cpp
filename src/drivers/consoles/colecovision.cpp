#include "drivers/consoles/colecovision.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// coleco_bios in coleco.pas.
const std::vector<RomEntry> kBiosRom = {
    {"coleco.rom", 0x2000, 0x0000, 0x3aa93ef3},
};

// CRCs of the two Super Game Module cartridges that ship with a battery
// backed EEPROM, from abrir_cartucho.
constexpr uint32_t kCrcBoxxle = 0x62dacf07;      // 24C256
constexpr uint32_t kCrcBlackOnix = 0xdddd1396;   // 24C08

bool valid_cartridge_header(const uint8_t* data) {
    // Standard ColecoVision cartridge signature, checked by abrir_cartucho.
    return (data[0] == 0x55 && data[1] == 0xaa) || (data[0] == 0xaa && data[1] == 0x55) ||
           (data[0] == 0x66 && data[1] == 0x99);
}

}  // namespace

ColecoVision::ColecoVision()
    : z80_(kMainClock),
      vdp_(1, [this](bool asserted) { on_vdp_interrupt(asserted); }),
      sn76489_(kMainClock),
      ay8910_(kMainClock / 2, 2.0f) {
    z80_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    z80_.set_cycle_handler([this](int cycles) { on_main_cycles(cycles); });
}

ColecoVision::~ColecoVision() { save_eeprom(); }

bool ColecoVision::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> bios(0x2000, 0);
    if (!loader.load(kBiosRom, bios, error)) return false;
    std::copy(bios.begin(), bios.end(), bios_.begin());
    warnings_ = loader.warnings();
    reset();
    return true;
}

bool ColecoVision::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        // Sniff the local zip signature so plain cartridge files (.rom/.col),
        // which have no fixed name RomLoader could look up, are read directly.
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
            if (!probe) {
                if (error) *error = "cannot read " + path;
                return false;
            }
        }
    }
    if (data.empty()) {
        RomLoader loader;
        if (!loader.open(path, error)) return false;
        data.reserve(kMaxCartridge);
        if (!loader.load_first_file(data, error)) return false;
    }

    save_eeprom();  // flush whatever cartridge was previously inserted
    delete i2cmem_;
    i2cmem_ = nullptr;
    eeprom_type_ = EepromType::None;
    eeprom_save_name_.clear();
    cartridge_path_ = path;

    if (!open_cartridge(data, error)) return false;
    reset();
    return true;
}

bool ColecoVision::open_cartridge(const std::vector<uint8_t>& data, std::string* error) {
    mega_cart_ = false;
    mega_cart_rom_.clear();

    if (data.size() <= 32768) {
        if (data.size() < 2 || !valid_cartridge_header(data.data())) {
            if (error) *error = "not a valid ColecoVision cartridge";
            return false;
        }
        std::fill(memory_.begin() + 0x8000, memory_.end(), 0);
        std::copy(data.begin(), data.end(), memory_.begin() + 0x8000);
        return true;
    }

    // Cartridges over 32 KiB are either a MegaCart (bank switched by reads to
    // $ffc0-$ffff) or a Super Game Module title with a battery backed EEPROM
    // (Black Onix, Boxxle), bank switched by writes to $ff90/$ffa0/$ffb0.
    size_t length = std::min(data.size(), size_t(kMaxCartridge));
    int bank_count = int(length / 0x4000);
    if (bank_count < 1) {
        if (error) *error = "cartridge too small";
        return false;
    }
    mega_cart_size_ = bank_count - 1;

    mega_cart_rom_.assign(size_t(bank_count), {});
    for (int bank = 0; bank < bank_count; bank++) {
        size_t offset = size_t(bank) * 0x4000;
        size_t chunk = std::min<size_t>(0x4000, length - offset);
        std::copy(data.begin() + long(offset), data.begin() + long(offset + chunk),
                  mega_cart_rom_[size_t(bank)].begin());
    }

    uint32_t crc = crc32_of(data.data(), data.size());
    if (crc == kCrcBoxxle || crc == kCrcBlackOnix) {
        if (crc != kCrcBoxxle) {
            eeprom_type_ = EepromType::C08;
            eeprom_save_name_ = "black_onix";
            i2cmem_ = new I2CMem(I2CMem::Type::C08);
        } else {
            eeprom_type_ = EepromType::C256;
            eeprom_save_name_ = "boxxle";
            i2cmem_ = new I2CMem(I2CMem::Type::C256);
        }
        load_eeprom_save();
        std::copy(mega_cart_rom_[0].begin(), mega_cart_rom_[0].end(), memory_.begin() + 0x8000);
        return true;
    }

    mega_cart_ = true;
    const auto& last = mega_cart_rom_[size_t(mega_cart_size_)];
    if (!valid_cartridge_header(last.data())) {
        if (error) *error = "not a valid ColecoVision MegaCart";
        return false;
    }
    std::copy(last.begin(), last.end(), memory_.begin() + 0x8000);
    return true;
}

void ColecoVision::load_eeprom_save() {
    if (i2cmem_ == nullptr || eeprom_save_name_.empty()) return;
    namespace fs = std::filesystem;
    fs::path save_path = fs::path(cartridge_path_).parent_path() / (eeprom_save_name_ + ".nv");
    std::ifstream stream(save_path, std::ios::binary);
    if (!stream) return;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    i2cmem_->load_data(data);
}

void ColecoVision::save_eeprom() {
    if (i2cmem_ == nullptr || eeprom_save_name_.empty()) return;
    namespace fs = std::filesystem;
    fs::path save_path = fs::path(cartridge_path_).parent_path() / (eeprom_save_name_ + ".nv");
    std::vector<uint8_t> data;
    i2cmem_->write_data(data);
    std::ofstream stream(save_path, std::ios::binary);
    if (stream) stream.write(reinterpret_cast<const char*>(data.data()), long(data.size()));
}

void ColecoVision::reset() {
    z80_.reset();
    sn76489_.reset();
    ay8910_.reset();
    vdp_.reset();
    if (i2cmem_ != nullptr) i2cmem_->reset();

    // "Importante o el juego 'The Yolk's on You' se para": the 1 KiB RAM
    // must come up with random contents, not zeroed.
    for (int i = 0; i < 0x400; i++) memory_[size_t(0x6000 + i)] = uint8_t(std::rand() & 0xff);

    joymode_ = false;
    rom_enabled_ = true;
    sgm_ram_ = false;
    last_nmi_ = false;
    joystick_ = {0xff, 0xff};
    keypad_ = {0xffff, 0xffff};
    audio_accumulator_ = 0;
    audio_.clear();
}

uint8_t ColecoVision::read_byte(uint16_t address) {
    if (address <= 0x1fff) return rom_enabled_ ? bios_[address] : memory_[address];
    if (address <= 0x5fff) return sgm_ram_ ? memory_[address] : 0xff;
    if (address <= 0x7fff) {
        return sgm_ram_ ? memory_[address] : memory_[0x6000 + (address & 0x3ff)];
    }
    if (address == 0xff80) {
        return eeprom_type_ != EepromType::None ? uint8_t(i2cmem_->read_sda() ? 1 : 0)
                                                 : memory_[address];
    }
    if (address >= 0xffc0) {
        if (mega_cart_) {
            int bank = mega_cart_size_ - ((0xffff - address) & mega_cart_size_);
            std::copy(mega_cart_rom_[size_t(bank)].begin(), mega_cart_rom_[size_t(bank)].end(),
                      memory_.begin() + 0xc000);
        }
        return memory_[address];
    }
    return memory_[address];  // $8000-$ff7f, $ff81-$ffbf
}

void ColecoVision::write_byte(uint16_t address, uint8_t value) {
    // The real ColecoVision has only 1 KiB of RAM, mirrored across $6000-$7fff;
    // the Super Game Module adds RAM at $2000-$7fff.
    if (address <= 0x1fff) {
        if (!rom_enabled_) memory_[address] = value;
        return;
    }
    if (address <= 0x5fff) {
        if (sgm_ram_) memory_[address] = value;
        return;
    }
    if (address <= 0x7fff) {
        if (sgm_ram_) memory_[address] = value;
        else memory_[0x6000 + (address & 0x3ff)] = value;
        return;
    }
    if (eeprom_type_ != EepromType::None) {
        switch (address) {
            case 0xff90:
            case 0xffa0:
            case 0xffb0: {
                int bank = ((address >> 4) & 3) & mega_cart_size_;
                std::copy(mega_cart_rom_[size_t(bank)].begin(), mega_cart_rom_[size_t(bank)].end(),
                          memory_.begin() + 0xc000);
                return;
            }
            case 0xffc0: i2cmem_->write_scl(false); return;
            case 0xffd0: i2cmem_->write_scl(true); return;
            case 0xffe0: i2cmem_->write_sda(false); return;
            case 0xfff0: i2cmem_->write_sda(true); return;
            default: break;
        }
    }
}

uint8_t ColecoVision::read_port(uint16_t port) {
    port &= 0xff;
    switch (port & 0xe0) {
        case 0x40:
            if (port == 0x52) return ay8910_.read();
            break;
        case 0xa0:
            return (port & 1) != 0 ? vdp_.register_read() : vdp_.vram_read();
        case 0xe0: {
            int player = (port >> 1) & 1;
            if (joymode_) {
                return uint8_t(joystick_[size_t(player)] & 0x7f);
            }
            uint8_t data = 0x0f;
            uint16_t input = keypad_[size_t(player)];
            if ((input & 0x0001) == 0) data &= 0x0a;  // 0
            if ((input & 0x0002) == 0) data &= 0x0d;  // 1
            if ((input & 0x0004) == 0) data &= 0x07;  // 2
            if ((input & 0x0008) == 0) data &= 0x0c;  // 3
            if ((input & 0x0010) == 0) data &= 0x02;  // 4
            if ((input & 0x0020) == 0) data &= 0x03;  // 5
            if ((input & 0x0040) == 0) data &= 0x0e;  // 6
            if ((input & 0x0080) == 0) data &= 0x05;  // 7
            if ((input & 0x0100) == 0) data &= 0x01;  // 8
            if ((input & 0x0200) == 0) data &= 0x0b;  // 9
            if ((input & 0x0400) == 0) data &= 0x06;  // #
            if ((input & 0x0800) == 0) data &= 0x09;  // *
            return uint8_t(((input & 0x4000) >> 8) | 0x30 | data);
        }
        default: break;
    }
    return 0xff;
}

void ColecoVision::write_port(uint16_t port, uint8_t value) {
    port &= 0xff;
    switch (port & 0xe0) {
        case 0x40:  // Super Game Module
            switch (port) {
                case 0x50: ay8910_.control(value); break;
                case 0x51: ay8910_.write(value); break;
                case 0x53: sgm_ram_ = (value & 1) != 0; break;
                default: break;
            }
            break;
        case 0x60: rom_enabled_ = (value & 2) != 0; break;  // Super Game Module
        case 0x80:
        case 0xc0: joymode_ = (port & 0x40) != 0; break;
        case 0xa0:
            if ((port & 1) != 0) vdp_.register_write(value); else vdp_.vram_write(value);
            break;
        case 0xe0: sn76489_.write(value); break;
        default: break;
    }
}

void ColecoVision::on_vdp_interrupt(bool asserted) {
    if (asserted && !last_nmi_) z80_.set_nmi(IrqLine::Pulse);
    last_nmi_ = asserted;
}

void ColecoVision::on_main_cycles(int cycles) {
    audio_accumulator_ += uint64_t(cycles) * uint64_t(SN76496::kSampleRate);
    while (audio_accumulator_ >= kMainClock) {
        audio_accumulator_ -= kMainClock;
        int32_t sample = sn76489_.update() + ay8910_.update();
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void ColecoVision::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        z80_.run(kCyclesPerLine);
        vdp_.refresh_ntsc(line);
    }
}

void ColecoVision::set_inputs(const MachineInputs& inputs) {
    const InputState* players[2] = {&inputs.player1, &inputs.player2};
    for (int p = 0; p < 2; p++) {
        const InputState& player = *players[p];
        uint8_t joy = 0xff;
        if (player.up) joy &= 0xfe;
        if (player.right) joy &= 0xfd;
        if (player.down) joy &= 0xfb;
        if (player.left) joy &= 0xf7;
        if (player.button1) joy &= 0xbf;
        joystick_[size_t(p)] = joy;

        uint16_t keys = 0xffff;
        if (player.button2) keys &= 0xbfff;
        keypad_[size_t(p)] = keys;
    }

    // Numeric keypad (0-9, * and #), player 1 only: the Machine interface has
    // a single shared host keyboard, unlike the two independent keypads a
    // real ColecoVision has one per controller.
    static constexpr Key kDigitKeys[10] = {Key::Num0, Key::Num1, Key::Num2, Key::Num3, Key::Num4,
                                           Key::Num5, Key::Num6, Key::Num7, Key::Num8, Key::Num9};
    for (int digit = 0; digit < 10; digit++) {
        if (inputs.key(kDigitKeys[digit])) keypad_[0] &= uint16_t(~(1u << digit));
    }
    if (inputs.key(Key::Q)) keypad_[0] &= uint16_t(~(1u << 10));  // *
    if (inputs.key(Key::W)) keypad_[0] &= uint16_t(~(1u << 11));  // #
}

void ColecoVision::set_dip_switch(int, uint8_t) {
    // The ColecoVision is a cartridge console, it has no DIP switches.
}

void ColecoVision::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
