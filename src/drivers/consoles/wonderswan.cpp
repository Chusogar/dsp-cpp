#include "drivers/consoles/wonderswan.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

// Mednafen/Cygne post-boot SoC I/O image (start.inc). Applied when no BIOS is
// present so FFFF:0000 lands on the cartridge footer JMP with banks set.
constexpr uint8_t kStartIo[0xc8] = {
    0x00, 0x00, 0x9d, 0xbb, 0x00, 0x00, 0x00, 0x26, 0xfe, 0xde, 0xf9, 0xfb, 0xdb, 0xd7, 0x7f, 0xf5,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x9e, 0x9b, 0x00, 0x00, 0x00, 0x00, 0x99, 0xfd, 0xb7, 0xdf,
    0x30, 0x57, 0x75, 0x76, 0x15, 0x73, 0x77, 0x77, 0x20, 0x75, 0x50, 0x36, 0x70, 0x67, 0x50, 0x77,
    0x57, 0x54, 0x75, 0x77, 0x75, 0x17, 0x37, 0x73, 0x50, 0x57, 0x60, 0x77, 0x70, 0x77, 0x10, 0x73,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
    0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xdb, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x00, 0x83, 0x00,
    0x2f, 0x3f, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
};

enum Irq : int {
    kIrqSerialSend = 0,
    kIrqInput = 1,
    kIrqCartridge = 2,
    kIrqSerialRecv = 3,
    kIrqLineCompare = 4,
    kIrqVblankTimer = 5,
    kIrqVblank = 6,
    kIrqHblankTimer = 7,
};

std::string lower_copy(std::string value) {
    for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

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
            if (size_t(size) > max_size) {
                if (error) *error = "file too large: " + path;
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

uint32_t next_pow2(uint32_t value) {
    if (value <= 1) return 1;
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

}  // namespace

bool WonderSwan::Timer::step() {
    uint16_t next = uint16_t(counter - 1);
    if (enable && counter) {
        counter = next;
        if (repeat && counter == 0) counter = frequency;
    }
    return next == 0;
}

bool WonderSwan::Window::inside(uint8_t x, uint8_t y) const {
    bool xin = (x >= x0 && x <= x1) || (x >= x1 && x <= x0);
    bool yin = (y >= y0 && y <= y1) || (y >= y1 && y <= y0);
    return xin && yin;
}

bool WonderSwan::Window::outside(uint8_t x, uint8_t y) const {
    return x < x0 || x > x1 || y < y0 || y > y1;
}

WonderSwan::WonderSwan(Model model)
    : cpu_(kClock, NecV30::Type::V30), model_(model), color_(model != Model::WonderSwan) {
    cpu_.set_memory_handlers([this](uint32_t a) { return read_mem(a); },
                             [this](uint32_t a, uint8_t v) { write_mem(a, v); });
    cpu_.set_io_handlers([this](uint16_t p) { return read_io(p); },
                         [this](uint16_t p, uint8_t v) { write_io(p, v); });
    apu_.set_memory_reader([this](uint32_t a) { return read_mem(a); });
    internal_eeprom_.assign(color_ ? 2048 : 128, 0);
}

bool WonderSwan::init(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    search_bios(rom_path);

    if (!rom_path.empty() && fs::is_directory(rom_path, ec)) {
        // Prefer a cartridge sitting next to the BIOS directory contents.
        for (const auto& entry : fs::directory_iterator(rom_path, ec)) {
            if (!entry.is_regular_file()) continue;
            auto ext = lower_copy(entry.path().extension().string());
            if (ext == ".ws" || ext == ".wsc" || ext == ".pc2") {
                std::string cart_error;
                if (load_media(entry.path().string(), &cart_error)) return true;
                if (!cart_error.empty()) warnings_.push_back(cart_error);
            }
        }
        reset();
        return true;
    }

    if (!rom_path.empty() && fs::is_regular_file(rom_path, ec)) {
        auto name = lower_copy(fs::path(rom_path).filename().string());
        if (name == "boot.rom" || name == "wswan.zip" || name == "wscolor.zip" ||
            name.find("bios") != std::string::npos) {
            if (!load_bios(rom_path, error)) return false;
            reset();
            return true;
        }
        auto ext = lower_copy(fs::path(rom_path).extension().string());
        if (ext == ".ws" || ext == ".wsc" || ext == ".pc2") {
            if (!load_media(rom_path, error)) return false;
            return true;
        }
        // Unknown regular file: try BIOS first, then cartridge.
        std::string bios_error;
        if (load_bios(rom_path, &bios_error)) {
            reset();
            return true;
        }
        if (!load_media(rom_path, error)) return false;
        return true;
    }

    reset();
    (void)error;
    return true;
}

void WonderSwan::search_bios(const std::string& rom_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> candidates;

    auto add_dir = [&](const fs::path& dir) {
        if (!fs::is_directory(dir, ec)) return;
        candidates.push_back(dir / "boot.rom");
        if (color_) {
            candidates.push_back(dir / "boot_color.rom");
            candidates.push_back(dir / "wscolor" / "boot.rom");
        } else {
            candidates.push_back(dir / "wswan" / "boot.rom");
        }
    };

    if (!rom_path.empty()) {
        fs::path path(rom_path);
        if (fs::is_directory(path, ec)) add_dir(path);
        else add_dir(path.parent_path());
    }
    add_dir("/tmp/roms/consoles/wscolor");
    add_dir("/tmp/roms/consoles/wswan");
    add_dir("roms/consoles/wscolor");
    add_dir("roms/consoles/wswan");

    for (const auto& candidate : candidates) {
        if (!fs::is_regular_file(candidate, ec)) continue;
        std::string ignored;
        if (load_bios(candidate.string(), &ignored)) return;
    }

    // MAME zip sets.
    const char* zips[] = {
        color_ ? "/tmp/roms/consoles/wscolor/wscolor.zip" : "/tmp/roms/consoles/wswan/wswan.zip",
        color_ ? "roms/consoles/wscolor.zip" : "roms/consoles/wswan.zip",
        "/tmp/roms/consoles/wswan/wswan.zip",
    };
    for (const char* zip : zips) {
        if (!fs::is_regular_file(zip, ec)) continue;
        std::string ignored;
        if (load_bios(zip, &ignored)) return;
    }
}

bool WonderSwan::load_bios(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, 16384, error)) return false;

    // Prefer boot.rom from a multi-file zip via RomLoader if the plain read
    // returned a random member; re-open zips looking for boot.rom.
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K';
        if (is_zip) {
            RomLoader loader;
            if (loader.open(path, error)) {
                std::vector<uint8_t> boot;
                if (loader.try_read("boot.rom", boot) || loader.try_read("BOOT.ROM", boot)) {
                    data = std::move(boot);
                }
                std::vector<uint8_t> eep;
                if (loader.try_read("internal_eeprom.wsc", eep) ||
                    loader.try_read("internal_eeprom.ws", eep)) {
                    if (!eep.empty()) internal_eeprom_ = std::move(eep);
                }
            }
        }
    }

    if (data.size() != 4096 && data.size() != 8192) {
        if (error) *error = "WonderSwan BIOS must be 4 KiB or 8 KiB";
        return false;
    }
    // Color hardware expects the 8 KiB SPHINX boot ROM when available.
    if (color_ && data.size() == 4096 && boot_rom_.size() == 8192) return true;
    boot_rom_ = std::move(data);
    return true;
}

bool WonderSwan::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge, error)) return false;
    auto ext = lower_copy(std::filesystem::path(path).extension().string());
    if (ext == ".wsc") color_ = true;
    else if (ext == ".ws" && model_ == Model::WonderSwan) color_ = false;
    return load_cart_bytes(std::move(data), error);
}

bool WonderSwan::load_cart_bytes(std::vector<uint8_t> data, std::string* error) {
    if (data.size() < 16) {
        if (error) *error = "WonderSwan cartridge too small";
        return false;
    }

    // Right-align into a power-of-two ROM image (Mednafen / ares convention).
    uint32_t real_size = uint32_t((data.size() + 0xffff) & ~size_t(0xffff));
    if (real_size == 0) real_size = 0x10000;
    uint32_t rom_size = next_pow2(real_size);
    cart_rom_.assign(rom_size, 0xff);
    std::memcpy(cart_rom_.data() + (rom_size - data.size()), data.data(), data.size());

    // Save type from footer (last 10 bytes); values match Mednafen's table.
    const uint8_t* header = cart_rom_.data() + cart_rom_.size() - 10;
    uint8_t save = header[5];
    cart_ram_.clear();
    cart_eeprom_.clear();
    switch (save) {
        case 0x01: cart_ram_.assign(8 * 1024, 0); break;
        case 0x02: cart_ram_.assign(32 * 1024, 0); break;
        case 0x03: cart_ram_.assign(128 * 1024, 0); break;
        case 0x04: cart_ram_.assign(256 * 1024, 0); break;
        case 0x05: cart_ram_.assign(512 * 1024, 0); break;
        case 0x10: cart_eeprom_.assign(128, 0xff); break;
        case 0x20: cart_eeprom_.assign(2048, 0xff); break;
        case 0x50: cart_eeprom_.assign(1024, 0xff); break;
        default: break;
    }

    if (color_) {
        internal_eeprom_.resize(2048, 0);
    } else {
        internal_eeprom_.resize(128, 0);
    }

    reset();
    return true;
}

void WonderSwan::reset() {
    cpu_.reset();
    iram_.fill(0);
    // Mednafen puts a small signature in IRAM after reset.
    if (iram_.size() > 0x75b3) {
        iram_[0x75ac] = 0x41;
        iram_[0x75ad] = 0x5f;
        iram_[0x75ae] = 0x43;
        iram_[0x75af] = 0x31;
        iram_[0x75b0] = 0x6e;
        iram_[0x75b1] = 0x5f;
        iram_[0x75b2] = 0x63;
        iram_[0x75b3] = 0x31;
    }

    rom_bank2_ = 0xff;
    sram_bank_ = 0xff;
    rom_bank0_ = 0xff;
    rom_bank1_ = 0xff;
    cartridge_enable_ = false;
    cartridge_rom_width_ = true;
    cartridge_rom_wait_ = false;
    disp_mode_ = 0;
    irq_base_ = irq_enable_ = irq_status_ = 0;
    nmi_control_ = 0;
    keypad_matrix_ = 0;
    screen1_ = screen2_ = {};
    sprite_ = {};
    screen2_window_ = {};
    lcd_enable_ = true;
    lcd_contrast_ = false;
    backdrop_ = 0;
    lcd_icons_ = 0;
    vtotal_ = kDefaultVtotal;
    vsync_line_ = 155;
    vcompare_ = 0xbb;
    vcounter_ = 0;
    field_ = false;
    std::memset(mono_pool_, 0, sizeof(mono_pool_));
    std::memset(mono_pal_, 0, sizeof(mono_pal_));
    htimer_ = vtimer_ = {};
    dma_source_ = dma_dest_ = dma_length_ = 0;
    dma_control_ = 0;
    eep_data_[0] = eep_data_[1] = 0;
    eep_cmd_[0] = eep_cmd_[1] = 0;
    eep_ctrl_[0] = eep_ctrl_[1] = 0;
    eep_ready_[0] = eep_ready_[1] = true;
    eep_protect_ = false;
    apu_.reset(color_);
    io_shadow_.fill(0);
    framebuffer_.fill(0);
    audio_.clear();
    audio_accumulator_ = 0;
    cpu_.set_irq(IrqLine::Clear);

    // Cartridge present: boot like Mednafen (post-boot I/O + footer at FFFF:0000).
    // The real SPHINX/ASWAN boot ROM is still loaded for inspection, but the
    // NecV30 core is not a full V30MZ, so the IPLROM path often stalls.
    if (!cart_rom_.empty()) {
        cartridge_enable_ = true;
        apply_startio();
    } else if (boot_rom_.empty()) {
        cartridge_enable_ = true;
        apply_startio();
    }
}

void WonderSwan::apply_startio() {
    for (uint16_t port = 0; port < 0xc8; ++port) {
        if (port == 0xc4 || port == 0xc5 || port == 0xba || port == 0xbb) continue;
        write_io(port, kStartIo[port]);
    }
}

void WonderSwan::set_inputs(const MachineInputs& inputs) {
    // WonderSwan keypad: X1-X4 (horizontal), Y1-Y4 (vertical), A/B/Start.
    // Packed the way Mednafen expects in WSButtonStatus.
    uint16_t buttons = 0;
    const auto& p = inputs.player1;
    if (p.right) buttons |= 0x1;
    if (p.left) buttons |= 0x2;
    if (p.up) buttons |= 0x4;
    if (p.down) buttons |= 0x8;
    // Y cursors: reuse player2 d-pad / extra keys when present.
    if (inputs.player2.right || inputs.key(Key::D)) buttons |= 0x10;
    if (inputs.player2.left || inputs.key(Key::A)) buttons |= 0x20;
    if (inputs.player2.up || inputs.key(Key::W)) buttons |= 0x40;
    if (inputs.player2.down || inputs.key(Key::S)) buttons |= 0x80;
    if (p.button1) buttons |= 0x200;   // A
    if (p.button2) buttons |= 0x400;   // B
    if (p.start) buttons |= 0x100;     // Start
    buttons_ = buttons;
}

void WonderSwan::set_dip_switch(int, uint8_t) {}

uint8_t WonderSwan::read_iram(uint16_t address) const {
    uint32_t size = color_ ? 0x10000u : 0x4000u;
    if (address >= size) return color_ ? 0x00 : 0x90;
    return iram_[address];
}

void WonderSwan::write_iram(uint16_t address, uint8_t value) {
    uint32_t size = color_ ? 0x10000u : 0x4000u;
    if (address >= size) return;
    iram_[address] = value;
}

uint16_t WonderSwan::read_iram16(uint16_t address) const {
    return uint16_t(read_iram(address) | (uint16_t(read_iram(uint16_t(address + 1))) << 8));
}

uint32_t WonderSwan::read_iram32(uint16_t address) const {
    return uint32_t(read_iram16(address)) | (uint32_t(read_iram16(uint16_t(address + 2))) << 16);
}

uint8_t WonderSwan::read_cart_rom(uint32_t address) const {
    if (cart_rom_.empty()) return 0xff;
    uint32_t bank = (address >> 16) & 0xf;
    uint32_t offset = address & 0xffff;
    uint32_t rom_mask = uint32_t(cart_rom_.size() >> 16) - 1;
    uint32_t linear;

    switch (bank) {
        case 2:
            linear = (uint32_t(rom_bank0_) & rom_mask) << 16 | offset;
            break;
        case 3:
            linear = (uint32_t(rom_bank1_) & rom_mask) << 16 | offset;
            break;
        default: {
            // Linear window: (rom_bank2 low nibble << 4 | bank) << 16 | offset
            uint32_t bank_num = (uint32_t(rom_bank2_ & 0x0f) << 4) | bank;
            bank_num &= rom_mask;
            linear = (bank_num << 16) | offset;
            break;
        }
    }
    return cart_rom_[linear & (cart_rom_.size() - 1)];
}

uint8_t WonderSwan::read_cart_ram(uint32_t address) const {
    if (cart_ram_.empty()) return 0xff;
    uint32_t linear = (uint32_t(sram_bank_) << 16) | (address & 0xffff);
    return cart_ram_[linear & (cart_ram_.size() - 1)];
}

void WonderSwan::write_cart_ram(uint32_t address, uint8_t value) {
    if (cart_ram_.empty()) return;
    uint32_t linear = (uint32_t(sram_bank_) << 16) | (address & 0xffff);
    cart_ram_[linear & (cart_ram_.size() - 1)] = value;
}

uint8_t WonderSwan::read_mem(uint32_t address) {
    address &= 0xfffff;

    if (!cartridge_enable_ && !boot_rom_.empty()) {
        uint32_t boot_base = 0x100000 - uint32_t(boot_rom_.size());
        if (address >= boot_base) return boot_rom_[address - boot_base];
    }

    uint32_t bank = address >> 16;
    if (bank == 0) {
        if (!color_ && (address & 0xc000)) return 0x90;
        return read_iram(uint16_t(address));
    }
    if (bank == 1) return read_cart_ram(address);
    return read_cart_rom(address);
}

void WonderSwan::write_mem(uint32_t address, uint8_t value) {
    address &= 0xfffff;
    if (!cartridge_enable_ && !boot_rom_.empty()) {
        uint32_t boot_base = 0x100000 - uint32_t(boot_rom_.size());
        if (address >= boot_base) return;  // boot ROM is not writable
    }

    uint32_t bank = address >> 16;
    if (bank == 0) {
        if (!color_ && (address & 0xc000)) return;
        write_iram(uint16_t(address), value);
        return;
    }
    if (bank == 1) {
        write_cart_ram(address, value);
        return;
    }
    // Cartridge ROM region is read-only on the SoC bus.
}

uint8_t WonderSwan::read_keypad() const {
    uint8_t latch = 0;
    if (keypad_matrix_ & 0x4) {  // buttons
        latch |= uint8_t(((buttons_ >> 8) << 1) & 0x0f);
    }
    if (keypad_matrix_ & 0x2) {  // X cursors
        latch |= uint8_t(buttons_ & 0x0f);
    }
    if (keypad_matrix_ & 0x1) {  // Y cursors
        latch |= uint8_t((buttons_ >> 4) & 0x0f);
    }
    return latch;
}

void WonderSwan::raise_irq(int irq) {
    if (!(irq_enable_ & (1u << irq))) return;
    irq_status_ |= uint8_t(1u << irq);
    poll_irq();
}

void WonderSwan::lower_irq(int irq) {
    irq_status_ &= uint8_t(~(1u << irq));
    poll_irq();
}

void WonderSwan::acknowledge_irq(uint8_t mask) {
    irq_status_ &= uint8_t(~mask);
    poll_irq();
}

void WonderSwan::poll_irq() {
    uint8_t pending = irq_status_ & irq_enable_;
    if (!pending) {
        cpu_.set_irq(IrqLine::Clear);
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (pending & (1 << i)) {
            uint8_t vector = uint8_t((irq_base_ & ~7) | i);
            cpu_.set_irq(IrqLine::Hold, vector);
            return;
        }
    }
}

void WonderSwan::run_gdma() {
    if (!color_) return;
    while (dma_length_) {
        uint8_t lo = read_mem(dma_source_);
        uint8_t hi = read_mem(dma_source_ + 1);
        write_mem(dma_dest_, lo);
        write_mem(dma_dest_ + 1, hi);
        if (dma_control_ & 0x40) {
            dma_source_ -= 2;
            dma_dest_ -= 2;
        } else {
            dma_source_ += 2;
            dma_dest_ += 2;
        }
        dma_source_ &= 0xfffff;
        dma_dest_ &= 0xffff;
        dma_length_ -= 2;
    }
    dma_control_ &= 0x7f;
}

uint8_t WonderSwan::eeprom_read_status(bool internal_eep) const {
    int idx = internal_eep ? 0 : 1;
    uint8_t data = 0;
    data |= 0x02;  // ready
    data |= uint8_t((eep_ctrl_[idx] & 0xf) << 4);
    if (internal_eep && eep_protect_) data |= 0x80;
    return data;
}

void WonderSwan::eeprom_write_ctrl(bool internal_eep, uint8_t value) {
    int idx = internal_eep ? 0 : 1;
    auto& store = internal_eep ? internal_eeprom_ : cart_eeprom_;
    if (store.empty()) return;

    uint8_t ctrl = uint8_t((value >> 4) & 0xf);
    eep_ctrl_[idx] = ctrl;
    uint16_t command = eep_cmd_[idx];
    uint32_t words = uint32_t(store.size() / 2);
    uint32_t addr_mask = words - 1;

    if (ctrl == 1) {  // read
        uint32_t addr = command & addr_mask;
        // Internal protect: only low addresses when locked (approximate).
        if (internal_eep && eep_protect_ && addr >= 0x30) {
            eep_data_[idx] = 0xffff;
        } else {
            eep_data_[idx] = uint16_t(store[addr * 2] | (uint16_t(store[addr * 2 + 1]) << 8));
        }
    } else if (ctrl == 2) {  // write
        uint32_t addr = command & addr_mask;
        if (!(internal_eep && eep_protect_ && addr >= 0x30)) {
            store[addr * 2] = uint8_t(eep_data_[idx]);
            store[addr * 2 + 1] = uint8_t(eep_data_[idx] >> 8);
        }
    } else if (ctrl == 8 && internal_eep) {
        eep_protect_ = true;
    }
    eep_ctrl_[idx] = 0;
    eep_ready_[idx] = true;
}

uint8_t WonderSwan::read_io(uint16_t port) {
    port &= 0xffff;
    // SoC ports mirror in low 256 when address bits match the decode.
    if (!(port & 0x100) && ((port < 0x100) || ((port & 0xff) < 0xb8))) {
        port &= 0xff;
    } else if (port >= 0xc0 && port <= 0xff) {
        port &= 0xff;
    } else {
        return color_ ? 0x00 : 0x90;
    }

    if (port <= 0x3f || port == 0x60 || (port >= 0xa0 && port <= 0xab)) {
        switch (port) {
            case 0x00:
                return uint8_t((screen1_.enable ? 1 : 0) | (screen2_.enable ? 2 : 0) |
                               (sprite_.enable ? 4 : 0) | (sprite_.window.enable ? 8 : 0) |
                               (screen2_window_.invert ? 0x10 : 0) |
                               (screen2_window_.enable ? 0x20 : 0));
            case 0x01: return backdrop_;
            case 0x02: return uint8_t(vcounter_);
            case 0x03: return vcompare_;
            case 0x04: return sprite_.oam_base & (color_ ? 0x3f : 0x1f);
            case 0x05: return sprite_.first;
            case 0x06: return sprite_.count;
            case 0x07:
                return uint8_t((screen1_.map_base & (color_ ? 0xf : 0x7)) |
                               ((screen2_.map_base & (color_ ? 0xf : 0x7)) << 4));
            case 0x08: return screen2_window_.x0;
            case 0x09: return screen2_window_.y0;
            case 0x0a: return screen2_window_.x1;
            case 0x0b: return screen2_window_.y1;
            case 0x0c: return sprite_.window.x0;
            case 0x0d: return sprite_.window.y0;
            case 0x0e: return sprite_.window.x1;
            case 0x0f: return sprite_.window.y1;
            case 0x10: return screen1_.hscroll;
            case 0x11: return screen1_.vscroll;
            case 0x12: return screen2_.hscroll;
            case 0x13: return screen2_.vscroll;
            case 0x14:
                return uint8_t((lcd_enable_ ? 1 : 0) | (lcd_contrast_ ? 2 : 0));
            case 0x15: return lcd_icons_;
            case 0x16: return vtotal_;
            case 0x17: return vsync_line_;
            case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
                int i = (port - 0x1c) * 2;
                return uint8_t((mono_pool_[i] & 0xf) | ((mono_pool_[i + 1] & 0xf) << 4));
            }
            case 0x60:
                return uint8_t((disp_mode_ & 0xe0));
            case 0xa0:
                return uint8_t((cartridge_enable_ ? 1 : 0) | (color_ ? 2 : 0) |
                               (cartridge_rom_width_ ? 4 : 0) | (cartridge_rom_wait_ ? 8 : 0) |
                               0x80);
            case 0xa2:
                return uint8_t((htimer_.enable ? 1 : 0) | (htimer_.repeat ? 2 : 0) |
                               (vtimer_.enable ? 4 : 0) | (vtimer_.repeat ? 8 : 0));
            case 0xa4: return uint8_t(htimer_.frequency);
            case 0xa5: return uint8_t(htimer_.frequency >> 8);
            case 0xa6: return uint8_t(vtimer_.frequency);
            case 0xa7: return uint8_t(vtimer_.frequency >> 8);
            case 0xa8: return uint8_t(htimer_.counter);
            case 0xa9: return uint8_t(htimer_.counter >> 8);
            case 0xaa: return uint8_t(vtimer_.counter);
            case 0xab: return uint8_t(vtimer_.counter >> 8);
            default:
                if (port >= 0x20 && port <= 0x3f) {
                    int pal = (port - 0x20) >> 1;
                    int pair = (port & 1) << 1;
                    return uint8_t((mono_pal_[pal][pair] & 7) |
                                   ((mono_pal_[pal][pair + 1] & 7) << 4));
                }
                break;
        }
    }

    if (color_ && port >= 0x40 && port <= 0x48) {
        switch (port) {
            case 0x40: return uint8_t(dma_source_);
            case 0x41: return uint8_t(dma_source_ >> 8);
            case 0x42: return uint8_t(dma_source_ >> 16);
            case 0x44: return uint8_t(dma_dest_);
            case 0x45: return uint8_t(dma_dest_ >> 8);
            case 0x46: return uint8_t(dma_length_);
            case 0x47: return uint8_t(dma_length_ >> 8);
            case 0x48: return dma_control_;
            default: break;
        }
    }

    if (port >= 0x80 && port <= 0x9f) return apu_.read(port);
    if (port == 0x4a || port == 0x4b || port == 0x4c || port == 0x4e || port == 0x4f ||
        port == 0x50 || port == 0x52 || port == 0x6a || port == 0x6b) {
        return apu_.read(port);
    }

    switch (port) {
        case 0xb0: return irq_base_;
        case 0xb2: return irq_enable_;
        case 0xb4: return irq_status_;
        case 0xb5: return uint8_t((keypad_matrix_ << 4) | read_keypad());
        case 0xb6: {
            uint8_t pending = irq_status_ & irq_enable_;
            for (int i = 0; i < 8; ++i) {
                if (pending & (1 << i)) return uint8_t(1 << i);
            }
            return 0;
        }
        case 0xb7: return nmi_control_;
        case 0xba: return uint8_t(eep_data_[0]);
        case 0xbb: return uint8_t(eep_data_[0] >> 8);
        case 0xbc: return uint8_t(eep_cmd_[0]);
        case 0xbd: return uint8_t(eep_cmd_[0] >> 8);
        case 0xbe: return eeprom_read_status(true);
        case 0xc0: return uint8_t(rom_bank2_ | 0x20);
        case 0xc1: return sram_bank_;
        case 0xc2: return rom_bank0_;
        case 0xc3: return rom_bank1_;
        case 0xc4: return uint8_t(eep_data_[1]);
        case 0xc5: return uint8_t(eep_data_[1] >> 8);
        case 0xc6: return uint8_t(eep_cmd_[1]);
        case 0xc7: return uint8_t(eep_cmd_[1] >> 8);
        case 0xc8: return eeprom_read_status(false);
        default: break;
    }

    return color_ ? 0x00 : 0x90;
}

void WonderSwan::write_io(uint16_t port, uint8_t value) {
    port &= 0xffff;
    if (!(port & 0x100) && ((port < 0x100) || ((port & 0xff) < 0xb8))) {
        port &= 0xff;
    } else if (port >= 0xc0 && port <= 0xff) {
        port &= 0xff;
    } else {
        return;
    }
    io_shadow_[port] = value;

    if (port <= 0x3f || port == 0x60 || (port >= 0xa0 && port <= 0xab)) {
        switch (port) {
            case 0x00:
                screen1_.enable = value & 1;
                screen2_.enable = value & 2;
                sprite_.enable = value & 4;
                sprite_.window.enable = value & 8;
                screen2_window_.invert = value & 0x10;
                screen2_window_.enable = value & 0x20;
                return;
            case 0x01: backdrop_ = value; return;
            case 0x03: vcompare_ = value; return;
            case 0x04: sprite_.oam_base = value & 0x3f; return;
            case 0x05: sprite_.first = value & 0x7f; return;
            case 0x06: sprite_.count = value; return;
            case 0x07:
                screen1_.map_base = value & 0x0f;
                screen2_.map_base = (value >> 4) & 0x0f;
                return;
            case 0x08: screen2_window_.x0 = value; return;
            case 0x09: screen2_window_.y0 = value; return;
            case 0x0a: screen2_window_.x1 = value; return;
            case 0x0b: screen2_window_.y1 = value; return;
            case 0x0c: sprite_.window.x0 = value; return;
            case 0x0d: sprite_.window.y0 = value; return;
            case 0x0e: sprite_.window.x1 = value; return;
            case 0x0f: sprite_.window.y1 = value; return;
            case 0x10: screen1_.hscroll = value; return;
            case 0x11: screen1_.vscroll = value; return;
            case 0x12: screen2_.hscroll = value; return;
            case 0x13: screen2_.vscroll = value; return;
            case 0x14:
                lcd_enable_ = value & 1;
                lcd_contrast_ = value & 2;
                return;
            case 0x15: lcd_icons_ = value; return;
            case 0x16: vtotal_ = value; return;
            case 0x17: vsync_line_ = value; return;
            case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
                int i = (port - 0x1c) * 2;
                mono_pool_[i] = value & 0xf;
                mono_pool_[i + 1] = (value >> 4) & 0xf;
                return;
            }
            case 0x60:
                disp_mode_ = value & 0xe0;
                return;
            case 0xa0:
                cartridge_enable_ = cartridge_enable_ || (value & 1);
                cartridge_rom_width_ = value & 4;
                cartridge_rom_wait_ = value & 8;
                return;
            case 0xa2:
                htimer_.enable = value & 1;
                htimer_.repeat = value & 2;
                vtimer_.enable = value & 4;
                vtimer_.repeat = value & 8;
                return;
            case 0xa4:
                htimer_.frequency = uint16_t((htimer_.frequency & 0xff00) | value);
                htimer_.counter = htimer_.frequency;
                return;
            case 0xa5:
                htimer_.frequency = uint16_t((htimer_.frequency & 0x00ff) | (uint16_t(value) << 8));
                htimer_.counter = htimer_.frequency;
                return;
            case 0xa6:
                vtimer_.frequency = uint16_t((vtimer_.frequency & 0xff00) | value);
                vtimer_.counter = vtimer_.frequency;
                return;
            case 0xa7:
                vtimer_.frequency = uint16_t((vtimer_.frequency & 0x00ff) | (uint16_t(value) << 8));
                vtimer_.counter = vtimer_.frequency;
                return;
            default:
                if (port >= 0x20 && port <= 0x3f) {
                    int pal = (port - 0x20) >> 1;
                    int pair = (port & 1) << 1;
                    if ((port & 0x9) != 0x8) mono_pal_[pal][pair] = value & 7;
                    mono_pal_[pal][pair + 1] = (value >> 4) & 7;
                    return;
                }
                break;
        }
    }

    if (color_ && port >= 0x40 && port <= 0x48) {
        switch (port) {
            case 0x40: dma_source_ = (dma_source_ & 0xffff00) | (value & ~1); return;
            case 0x41: dma_source_ = (dma_source_ & 0xff00ff) | (uint32_t(value) << 8); return;
            case 0x42: dma_source_ = (dma_source_ & 0x00ffff) | ((uint32_t(value) & 0x0f) << 16); return;
            case 0x44: dma_dest_ = uint16_t((dma_dest_ & 0xff00) | (value & ~1)); return;
            case 0x45: dma_dest_ = uint16_t((dma_dest_ & 0x00ff) | (uint16_t(value) << 8)); return;
            case 0x46: dma_length_ = uint16_t((dma_length_ & 0xff00) | (value & ~1)); return;
            case 0x47: dma_length_ = uint16_t((dma_length_ & 0x00ff) | (uint16_t(value) << 8)); return;
            case 0x48:
                dma_control_ = value & ~0x3f;
                if (dma_control_ & 0x80) run_gdma();
                return;
            default: break;
        }
    }

    if (port >= 0x80 && port <= 0x9f) {
        apu_.write(port, value);
        return;
    }
    if (port == 0x4a || port == 0x4b || port == 0x4c || port == 0x4e || port == 0x4f ||
        port == 0x50 || port == 0x52 || port == 0x6a || port == 0x6b) {
        apu_.write(port, value);
        return;
    }

    switch (port) {
        case 0xb0: irq_base_ = value & ~7; poll_irq(); return;
        case 0xb2: irq_enable_ = value; poll_irq(); return;
        case 0xb5: keypad_matrix_ = (value >> 4) & 7; return;
        case 0xb6: acknowledge_irq(value); return;
        case 0xb7: nmi_control_ = value; return;
        case 0xba: eep_data_[0] = uint16_t((eep_data_[0] & 0xff00) | value); return;
        case 0xbb: eep_data_[0] = uint16_t((eep_data_[0] & 0x00ff) | (uint16_t(value) << 8)); return;
        case 0xbc: eep_cmd_[0] = uint16_t((eep_cmd_[0] & 0xff00) | value); return;
        case 0xbd: eep_cmd_[0] = uint16_t((eep_cmd_[0] & 0x00ff) | (uint16_t(value) << 8)); return;
        case 0xbe: eeprom_write_ctrl(true, value); return;
        case 0xc0: rom_bank2_ = value; return;
        case 0xc1: sram_bank_ = value; return;
        case 0xc2: rom_bank0_ = value; return;
        case 0xc3: rom_bank1_ = value; return;
        case 0xc4: eep_data_[1] = uint16_t((eep_data_[1] & 0xff00) | value); return;
        case 0xc5: eep_data_[1] = uint16_t((eep_data_[1] & 0x00ff) | (uint16_t(value) << 8)); return;
        case 0xc6: eep_cmd_[1] = uint16_t((eep_cmd_[1] & 0xff00) | value); return;
        case 0xc7: eep_cmd_[1] = uint16_t((eep_cmd_[1] & 0x00ff) | (uint16_t(value) << 8)); return;
        case 0xc8: eeprom_write_ctrl(false, value); return;
        default: break;
    }
}

uint8_t WonderSwan::fetch_tile(uint16_t tile, uint8_t x, uint8_t y) const {
    tile &= grayscale() ? 0x1ff : 0x3ff;
    x &= 7;
    y &= 7;
    uint8_t color = 0;

    if (planar() && depth() == 2) {
        uint16_t data = read_iram16(uint16_t(0x2000 + (tile << 4) + (y << 1)));
        if (data & (0x80 >> x)) color |= 1;
        if (data & (0x8000 >> x)) color |= 2;
    } else if (planar() && depth() == 4) {
        uint32_t data = read_iram32(uint16_t(0x4000 + (tile << 5) + (y << 2)));
        if (data & (0x80u >> x)) color |= 1;
        if (data & (0x8000u >> x)) color |= 2;
        if (data & (0x800000u >> x)) color |= 4;
        if (data & (0x80000000u >> x)) color |= 8;
    } else if (packed() && depth() == 2) {
        uint8_t data = read_iram(uint16_t(0x2000 + (tile << 4) + (y << 1) + (x >> 2)));
        color = uint8_t((data >> (6 - ((x & 3) << 1))) & 3);
    } else if (packed() && depth() == 4) {
        uint8_t data = read_iram(uint16_t(0x4000 + (tile << 5) + (y << 2) + (x >> 1)));
        color = uint8_t((data >> (4 - ((x & 1) << 2))) & 0xf);
    }
    return color;
}

bool WonderSwan::opaque(uint8_t palette, uint8_t color) const {
    if (color) return true;
    if (depth() == 2 && !(palette & 4)) return true;
    return false;
}

uint16_t WonderSwan::backdrop_color(uint8_t color) const {
    if (grayscale()) {
        uint8_t luma = uint8_t(15 - mono_pool_[color & 7]);
        return uint16_t(luma | (luma << 4) | (luma << 8));
    }
    return uint16_t(read_iram16(uint16_t(0xfe00 + (color << 1))) & 0x0fff);
}

uint16_t WonderSwan::palette_color(uint8_t palette, uint8_t color) const {
    if (grayscale()) {
        uint8_t pool = mono_pal_[palette & 0xf][color & 3] & 7;
        uint8_t luma = uint8_t(15 - mono_pool_[pool]);
        return uint16_t(luma | (luma << 4) | (luma << 8));
    }
    return uint16_t(read_iram16(uint16_t(0xfe00 + (palette << 5) + (color << 1))) & 0x0fff);
}

uint32_t WonderSwan::rgb12_to_argb(uint16_t color) const {
    uint32_t b = color & 0xf;
    uint32_t g = (color >> 4) & 0xf;
    uint32_t r = (color >> 8) & 0xf;
    if (color_ && !grayscale()) {
        // Approximate WSC LCD mix (ares/Mednafen-style).
        uint32_t R = std::min(480u, r * 26 + g * 4 + b * 2);
        uint32_t G = std::min(480u, g * 24 + b * 8);
        uint32_t B = std::min(480u, r * 6 + g * 4 + b * 22);
        r = (R * 255) / 480;
        g = (G * 255) / 480;
        b = (B * 255) / 480;
    } else {
        r = (r * 255) / 15;
        g = (g * 255) / 15;
        b = (b * 255) / 15;
    }
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

void WonderSwan::render_scanline(int y) {
    screen1_latched_ = screen1_;
    screen2_latched_ = screen2_;
    sprite_latched_ = sprite_;
    screen2_window_latched_ = screen2_window_;
    lcd_enable_latched_ = lcd_enable_;
    lcd_contrast_latched_ = lcd_contrast_;
    backdrop_latched_ = backdrop_;

    // Build sprite list for this line from the previous field's OAM cache.
    struct Obj {
        uint32_t attr;
    };
    Obj objects[32];
    int object_count = 0;
    if (sprite_latched_.enable) {
        for (int i = 0; i < oam_count_ && object_count < 32; ++i) {
            uint32_t attr = oam_cache_[i];
            uint8_t voffset = uint8_t(attr >> 16);
            if (uint8_t(y - voffset) > 7) continue;
            objects[object_count++].attr = attr;
        }
    }

    auto layer_pixel = [&](const Layer& layer, uint8_t x, uint8_t yy, bool* valid,
                           uint16_t* out_color) {
        *valid = false;
        if (!layer.enable) return;
        uint8_t lx = uint8_t(x + layer.hscroll);
        uint8_t ly = uint8_t(yy + layer.vscroll);
        uint16_t address = uint16_t(((lx >> 3) << 1) | ((ly >> 3) << 6) | (layer.map_base << 11));
        uint16_t attributes = read_iram16(address);
        uint16_t tile = uint16_t((attributes & 0x1ff) | ((attributes >> 13) & 1) << 9);
        uint8_t palette = uint8_t((attributes >> 9) & 0xf);
        uint8_t hflip = (attributes & 0x4000) ? 7 : 0;
        uint8_t vflip = (attributes & 0x8000) ? 7 : 0;
        uint8_t color = fetch_tile(tile, uint8_t((lx & 7) ^ hflip), uint8_t((ly & 7) ^ vflip));
        if (opaque(palette, color)) {
            *valid = true;
            *out_color = palette_color(palette, color);
        }
    };

    uint32_t* out = framebuffer_.data() + y * kScreenWidth;
    for (int x = 0; x < kScreenWidth; ++x) {
        uint16_t color = backdrop_color(backdrop_latched_);
        if (lcd_enable_latched_) {
            bool v1 = false, v2 = false, vs = false;
            uint16_t c1 = 0, c2 = 0, cs = 0;
            layer_pixel(screen1_latched_, uint8_t(x), uint8_t(y), &v1, &c1);

            layer_pixel(screen2_latched_, uint8_t(x), uint8_t(y), &v2, &c2);
            if (screen2_window_latched_.enable) {
                bool hide = screen2_window_latched_.invert
                                ? screen2_window_latched_.inside(uint8_t(x), uint8_t(y))
                                : screen2_window_latched_.outside(uint8_t(x), uint8_t(y));
                if (hide) v2 = false;
            }

            if (sprite_latched_.enable) {
                bool outside = sprite_latched_.window.outside(uint8_t(x), uint8_t(y));
                for (int i = 0; i < object_count; ++i) {
                    uint32_t attr = objects[i].attr;
                    uint16_t tile = uint16_t(attr & 0x1ff);
                    uint8_t palette = uint8_t(((attr >> 9) & 7) | 8);
                    bool region = (attr >> 12) & 1;
                    bool priority = (attr >> 13) & 1;
                    uint8_t hflip = (attr & 0x4000) ? 7 : 0;
                    uint8_t vflip = (attr & 0x8000) ? 7 : 0;
                    uint8_t voffset = uint8_t(attr >> 16);
                    uint8_t hoffset = uint8_t(attr >> 24);
                    if (sprite_latched_.window.enable && region != outside) continue;
                    if (uint8_t(x - hoffset) > 7) continue;
                    uint8_t px = fetch_tile(tile, uint8_t((x - hoffset) ^ hflip),
                                           uint8_t((y - voffset) ^ vflip));
                    if (!opaque(palette, px)) continue;
                    if (!priority && v2) continue;
                    vs = true;
                    cs = palette_color(palette, px);
                    break;
                }
            }

            if (v1) color = c1;
            if (v2) color = c2;
            if (vs) color = cs;

            if (color_ && lcd_contrast_latched_) {
                uint32_t b = color & 0xf;
                uint32_t g = (color >> 4) & 0xf;
                uint32_t r = (color >> 8) & 0xf;
                b = std::min(15u, (b * 3) / 2);
                g = std::min(15u, (g * 3) / 2);
                r = std::min(15u, (r * 3) / 2);
                color = uint16_t(b | (g << 4) | (r << 8));
            }
        }
        out[x] = rgb12_to_argb(color);
    }
}

void WonderSwan::run_frame() {
    const int lines = std::max(144, int(vtotal_) + 1);
    audio_accumulator_ = 0;
    audio_.clear();

    for (vcounter_ = 0; vcounter_ < lines; ++vcounter_) {
        if (vcounter_ == vcompare_) raise_irq(kIrqLineCompare);
        if (htimer_.step()) raise_irq(kIrqHblankTimer);

        if (vcounter_ < kScreenHeight) {
            render_scanline(vcounter_);
            cpu_.run(kCyclesPerLine);
        } else if (vcounter_ == kScreenHeight) {
            raise_irq(kIrqVblank);
            if (vtimer_.step()) raise_irq(kIrqVblankTimer);

            // Sync OAM for the next frame (ares does this on line 144).
            oam_count_ = 0;
            if (sprite_.enable) {
                uint16_t base = uint16_t((sprite_.oam_base & (grayscale() ? 0x1f : 0x3f)) << 9);
                uint8_t index = sprite_.first;
                int count = std::min<int>(128, sprite_.count);
                for (int i = 0; i < count; ++i) {
                    uint16_t addr = uint16_t(base + index * 4);
                    oam_cache_[oam_count_++] =
                        uint32_t(read_iram16(addr)) | (uint32_t(read_iram16(uint16_t(addr + 2))) << 16);
                    index = uint8_t((index + 1) & 0x7f);
                }
            }
            field_ = !field_;
            cpu_.run(kCyclesPerLine);
        } else {
            cpu_.run(kCyclesPerLine);
        }

        // Rough audio clocking paced to the frame.
        audio_accumulator_ += uint64_t(WswanApu::kSampleRate) * kCyclesPerLine;
        while (audio_accumulator_ >= kClock) {
            audio_accumulator_ -= kClock;
            audio_.push_back(apu_.update());
        }
    }
}

void WonderSwan::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
