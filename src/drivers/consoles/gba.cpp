#include "drivers/consoles/gba.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

constexpr size_t kBiosSize = 0x4000;
constexpr size_t kMaxRom = 0x2000000;  // 32 MiB
const int kNWait[4] = {4, 3, 2, 8};
const uint32_t kTimerPrescale[4] = {1, 64, 256, 1024};

enum Irq {
    kIrqVBlank = 0, kIrqHBlank = 1, kIrqVCount = 2,
    kIrqTimer0 = 3, kIrqDma0 = 8, kIrqKeypad = 12,
};

bool ends_with_ci(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != ext[i]) return false;
    }
    return true;
}

// A plain file, or the first file of a zip.
bool read_plain_or_zip(const std::string& path, std::vector<uint8_t>& data, size_t max_size, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream in(path, std::ios::binary);
        char magic[4] = {};
        in.read(magic, 4);
        const bool zip = in.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4;
        if (!zip) {
            in.clear();
            in.seekg(0, std::ios::end);
            const std::streamoff size = in.tellg();
            in.seekg(0, std::ios::beg);
            if (size <= 0) {
                if (error) *error = "cannot read " + path;
                return false;
            }
            data.resize(std::min(size_t(size), max_size));
            in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
            return true;
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    if (!loader.load_first_file(data, error)) return false;
    if (data.size() > max_size) data.resize(max_size);
    return true;
}

bool find_bytes(const std::vector<uint8_t>& hay, const char* needle) {
    const size_t n = std::strlen(needle);
    return std::search(hay.begin(), hay.end(), needle, needle + n) != hay.end();
}

}  // namespace

Gba::Gba() : cpu_(*this), ppu_(io_.data(), pal_.data(), nullptr, oam_.data()) {
    bios_.assign(kBiosSize, 0);
    ewram_.assign(0x40000, 0);
    iwram_.assign(0x8000, 0);
    vram_.assign(0x18000, 0);
    ppu_ = GbaPpu(io_.data(), pal_.data(), vram_.data(), oam_.data());
    apu_.request_fifo = [this](int ch) {
        // Sound DMA: DMA1/DMA2 in "special" timing aimed at FIFO A/B.
        for (int n = 1; n <= 2; n++) {
            Dma& d = dma_[size_t(n)];
            if (!(d.control & 0x8000) || ((d.control >> 12) & 3) != 3) continue;
            if ((d.dst & 0x0FFFFFFF) != 0x040000A0u + uint32_t(ch) * 4) continue;
            run_dma(n);
        }
    };
}

Gba::~Gba() { flush_save(); }

bool Gba::init(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string bios_source = rom_path;
    std::string cart;
    if (fs::is_regular_file(rom_path, ec) && (ends_with_ci(rom_path, ".gba") || ends_with_ci(rom_path, ".agb"))) {
        // `--game gba game.gba`: the BIOS sits next to the cartridge.
        cart = rom_path;
        bios_source = fs::path(rom_path).parent_path().string();
        if (bios_source.empty()) bios_source = ".";
    }
    std::vector<uint8_t> bios;
    std::string bios_error;
    bool ok = false;
    if (fs::is_directory(bios_source, ec)) {
        for (const char* name : {"gba.bin", "gba_bios.bin", "gba.zip"}) {
            const fs::path p = fs::path(bios_source) / name;
            if (fs::exists(p, ec) && read_plain_or_zip(p.string(), bios, kBiosSize, &bios_error) &&
                bios.size() == kBiosSize) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            RomLoader loader;
            if (loader.open(bios_source, &bios_error)) {
                const std::vector<RomEntry> entry = {{"gba.bin|gba_bios.bin", 0x4000, 0, 0x81977335}};
                ok = loader.load(entry, bios, &bios_error);
            }
        }
    } else {
        RomLoader loader;
        if (loader.open(bios_source, &bios_error)) {
            const std::vector<RomEntry> entry = {{"gba.bin|gba_bios.bin", 0x4000, 0, 0x81977335}};
            ok = loader.load(entry, bios, &bios_error);
            if (ok) warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
        }
        if (!ok && read_plain_or_zip(bios_source, bios, kBiosSize, &bios_error) && bios.size() == kBiosSize) ok = true;
    }
    if (!ok || bios.size() < kBiosSize) {
        if (error) *error = "Game Boy Advance BIOS (gba.bin, 16 KiB) not found in " + bios_source;
        return false;
    }
    std::memcpy(bios_.data(), bios.data(), kBiosSize);
    if (!cart.empty() && !load_media(cart, error)) return false;
    reset();
    return true;
}

bool Gba::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip(path, data, kMaxRom, error)) return false;
    if (data.size() < 0xC0) {
        if (error) *error = "not a Game Boy Advance cartridge (too small)";
        return false;
    }
    flush_save();
    rom_ = std::move(data);
    game_title_.assign(reinterpret_cast<const char*>(rom_.data() + 0xA0), 12);
    game_title_.erase(std::find(game_title_.begin(), game_title_.end(), '\0'), game_title_.end());
    namespace fs = std::filesystem;
    fs::path sav = fs::path(path);
    sav.replace_extension(".sav");
    save_path_ = sav.string();
    detect_save_type();
    load_save();
    reset();
    return true;
}

void Gba::detect_save_type() {
    save_type_ = SaveType::None;
    save_.clear();
    if (find_bytes(rom_, "EEPROM_V")) {
        save_type_ = SaveType::Eeprom;
        save_.assign(0x2000, 0xFF);
    } else if (find_bytes(rom_, "FLASH1M_V")) {
        save_type_ = SaveType::Flash128;
        save_.assign(0x20000, 0xFF);
    } else if (find_bytes(rom_, "FLASH_V") || find_bytes(rom_, "FLASH512_V")) {
        save_type_ = SaveType::Flash64;
        save_.assign(0x10000, 0xFF);
    } else if (find_bytes(rom_, "SRAM_V") || find_bytes(rom_, "SRAM_F_V")) {
        save_type_ = SaveType::Sram;
        save_.assign(0x8000, 0xFF);
    }
}

void Gba::load_save() {
    if (save_.empty() || save_path_.empty()) return;
    std::ifstream in(save_path_, std::ios::binary);
    if (!in) return;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.empty()) return;
    if (save_type_ == SaveType::Eeprom && data.size() > 0x200) eeprom_addr_bits_ = 14;
    std::memcpy(save_.data(), data.data(), std::min(data.size(), save_.size()));
}

void Gba::flush_save() {
    if (!save_dirty_ || save_.empty() || save_path_.empty()) return;
    std::ofstream out(save_path_, std::ios::binary);
    size_t size = save_.size();
    if (save_type_ == SaveType::Eeprom) size = eeprom_addr_bits_ == 6 ? 0x200 : 0x2000;
    out.write(reinterpret_cast<const char*>(save_.data()), std::streamsize(size));
    save_dirty_ = false;
}

void Gba::reset() {
    std::fill(ewram_.begin(), ewram_.end(), 0);
    std::fill(iwram_.begin(), iwram_.end(), 0);
    std::fill(vram_.begin(), vram_.end(), 0);
    io_.fill(0);
    pal_.fill(0);
    oam_.fill(0);
    framebuffer_.fill(0xFF000000u);
    cycles_ = 0;
    line_ = 0;
    halted_ = false;
    ie_ = if_ = ime_ = 0;
    dispstat_ = 0;
    keyinput_ = 0x3FF;
    bios_latch_ = 0;
    last_fetch_ = 0;
    last_rom_address_ = 0xFFFFFFFF;
    timers_ = {};
    dma_ = {};
    dma_running_ = false;
    audio_acc_ = 0;
    audio_.clear();
    flash_state_ = 0;
    flash_id_ = flash_erase_ = flash_write_ = flash_bank_cmd_ = false;
    flash_bank_ = 0;
    eeprom_bits_.clear();
    eeprom_reading_ = false;
    set_io16(0x20, 0x100);  // BG2PA
    set_io16(0x26, 0x100);  // BG2PD
    set_io16(0x30, 0x100);
    set_io16(0x36, 0x100);
    set_io16(0x88, 0x200);  // SOUNDBIAS
    ppu_.reset();
    apu_.reset();
    cpu_.reset();
}

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------

int Gba::rom_wait(uint32_t address, int width, bool code) {
    const uint16_t waitcnt = io16(0x204);
    const int region = int((address >> 25) & 3);  // 0: WS0, 1: WS1, 2: WS2
    int n, s;
    switch (region) {
        case 0: n = kNWait[(waitcnt >> 2) & 3]; s = (waitcnt & 0x10) ? 1 : 2; break;
        case 1: n = kNWait[(waitcnt >> 5) & 3]; s = (waitcnt & 0x80) ? 1 : 4; break;
        default: n = kNWait[(waitcnt >> 8) & 3]; s = (waitcnt & 0x400) ? 1 : 8; break;
    }
    const bool seq = address == last_rom_address_;
    last_rom_address_ = address + uint32_t(width);
    if (code && seq && (waitcnt & 0x4000)) return width == 4 ? 2 : 1;  // prefetch buffer
    int cost = 1 + (seq ? s : n);
    if (width == 4) cost += 1 + s;
    return cost;
}

uint32_t Gba::open_bus(uint32_t address) const {
    // Unmapped reads return the last prefetched opcode.
    const uint32_t sh = (address & 3) * 8;
    return sh ? (last_fetch_ >> sh) | (last_fetch_ << (32 - sh)) : last_fetch_;
}

bool Gba::is_eeprom(uint32_t address) const {
    if (save_type_ != SaveType::Eeprom) return false;
    if ((address >> 24) != 0x0D) return false;
    return rom_.size() <= 0x1000000 || address >= 0x0DFFFF00u;
}

uint8_t Gba::read8(uint32_t address) {
    switch (address >> 24) {
        case 0x00:
            cycles_ += 1;
            if (address < kBiosSize) {
                if (cpu_.pc() >= kBiosSize) return uint8_t(bios_latch_ >> ((address & 3) * 8));
                return bios_[address];
            }
            return uint8_t(open_bus(address));
        case 0x02: cycles_ += 3; return ewram_[address & 0x3FFFF];
        case 0x03: cycles_ += 1; return iwram_[address & 0x7FFF];
        case 0x04: {
            cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off >= 0x400) return uint8_t(open_bus(address));
            return uint8_t(io_read16(off & ~1u) >> ((off & 1) * 8));
        }
        case 0x05: cycles_ += 1; return pal_[address & 0x3FF];
        case 0x06: {
            cycles_ += 1;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            return vram_[a];
        }
        case 0x07: cycles_ += 1; return oam_[address & 0x3FF];
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
            cycles_ += uint64_t(rom_wait(address & ~1u, 2, false));
            if (is_eeprom(address)) return uint8_t(eeprom_read());
            const uint32_t a = address & 0x1FFFFFF;
            if (a < rom_.size()) return rom_[a];
            return uint8_t(((address >> 1) & 0xFFFF) >> ((address & 1) * 8));
        }
        case 0x0E: case 0x0F:
            cycles_ += uint64_t(1 + kNWait[io16(0x204) & 3]);
            return save_read8(address);
        default:
            cycles_ += 1;
            return uint8_t(open_bus(address));
    }
}

uint16_t Gba::read16(uint32_t address, bool code) {
    const uint32_t unaligned = address;
    address &= ~1u;
    uint16_t value;
    switch (address >> 24) {
        case 0x00:
            cycles_ += 1;
            if (address < kBiosSize) {
                if (cpu_.pc() >= kBiosSize && !code) {
                    value = uint16_t(bios_latch_ >> ((address & 2) * 8));
                } else {
                    value = uint16_t(bios_[address] | (bios_[address + 1] << 8));
                    // The protected-BIOS latch holds the prefetched opcode
                    // (executing address + 4 in Thumb, + 8 in ARM).
                    if (code) std::memcpy(&bios_latch_, &bios_[(address + 4) & 0x3FFC], 4);
                }
            } else {
                value = uint16_t(open_bus(address));
            }
            break;
        case 0x02: cycles_ += 3; value = uint16_t(ewram_[address & 0x3FFFF] | (ewram_[(address & 0x3FFFF) + 1] << 8)); break;
        case 0x03: cycles_ += 1; value = uint16_t(iwram_[address & 0x7FFF] | (iwram_[(address & 0x7FFF) + 1] << 8)); break;
        case 0x04: {
            cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            value = off < 0x400 ? io_read16(off) : uint16_t(open_bus(address));
            break;
        }
        case 0x05: cycles_ += 1; value = uint16_t(pal_[address & 0x3FF] | (pal_[(address & 0x3FF) + 1] << 8)); break;
        case 0x06: {
            cycles_ += 1;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            value = uint16_t(vram_[a] | (vram_[a + 1] << 8));
            break;
        }
        case 0x07: cycles_ += 1; value = uint16_t(oam_[address & 0x3FF] | (oam_[(address & 0x3FF) + 1] << 8)); break;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
            cycles_ += uint64_t(rom_wait(address, 2, code));
            if (is_eeprom(address)) {
                value = eeprom_read();
                break;
            }
            const uint32_t a = address & 0x1FFFFFF;
            if (a + 1 < rom_.size()) value = uint16_t(rom_[a] | (rom_[a + 1] << 8));
            else value = uint16_t(address >> 1);
            break;
        }
        case 0x0E: case 0x0F:
            cycles_ += uint64_t(1 + kNWait[io16(0x204) & 3]);
            value = uint16_t(save_read8(unaligned) * 0x0101u);  // 8-bit bus
            break;
        default:
            cycles_ += 1;
            value = uint16_t(open_bus(address));
            break;
    }
    if (code) last_fetch_ = uint32_t(value) | (uint32_t(value) << 16);
    return value;
}

uint32_t Gba::read32(uint32_t address, bool code) {
    const uint32_t unaligned = address;
    address &= ~3u;
    uint32_t value;
    switch (address >> 24) {
        case 0x00:
            cycles_ += 1;
            if (address < kBiosSize) {
                if (cpu_.pc() >= kBiosSize && !code) {
                    value = bios_latch_;
                } else {
                    std::memcpy(&value, &bios_[address], 4);
                    if (code) std::memcpy(&bios_latch_, &bios_[(address + 8) & 0x3FFC], 4);
                }
            } else {
                value = open_bus(address);
            }
            break;
        case 0x02: cycles_ += 6; std::memcpy(&value, &ewram_[address & 0x3FFFF], 4); break;
        case 0x03: cycles_ += 1; std::memcpy(&value, &iwram_[address & 0x7FFF], 4); break;
        case 0x04: {
            cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off < 0x400) value = uint32_t(io_read16(off)) | (uint32_t(io_read16(off + 2)) << 16);
            else value = open_bus(address);
            break;
        }
        case 0x05: cycles_ += 2; std::memcpy(&value, &pal_[address & 0x3FF], 4); break;
        case 0x06: {
            cycles_ += 2;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            std::memcpy(&value, &vram_[a], 4);
            break;
        }
        case 0x07: cycles_ += 1; std::memcpy(&value, &oam_[address & 0x3FF], 4); break;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
            cycles_ += uint64_t(rom_wait(address, 4, code));
            const uint32_t a = address & 0x1FFFFFF;
            if (a + 3 < rom_.size()) {
                std::memcpy(&value, &rom_[a], 4);
            } else {
                value = ((address >> 1) & 0xFFFF) | ((((address >> 1) + 1) & 0xFFFF) << 16);
            }
            break;
        }
        case 0x0E: case 0x0F:
            cycles_ += uint64_t(1 + kNWait[io16(0x204) & 3]);
            value = save_read8(unaligned) * 0x01010101u;
            break;
        default:
            cycles_ += 1;
            value = open_bus(address);
            break;
    }
    if (code) last_fetch_ = value;
    return value;
}

void Gba::write8(uint32_t address, uint8_t value) {
    switch (address >> 24) {
        case 0x02: cycles_ += 3; ewram_[address & 0x3FFFF] = value; return;
        case 0x03: cycles_ += 1; iwram_[address & 0x7FFF] = value; return;
        case 0x04: {
            cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off >= 0x400) return;
            const uint16_t v = uint16_t(value) * 0x0101u;
            io_write16(off & ~1u, v, (off & 1) ? 0xFF00 : 0x00FF);
            return;
        }
        case 0x05:  // byte writes to palette RAM store the byte in both halves
            cycles_ += 1;
            pal_[address & 0x3FE] = value;
            pal_[(address & 0x3FE) + 1] = value;
            return;
        case 0x06: {
            cycles_ += 1;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            // BG VRAM: the byte lands in both halves; OBJ VRAM ignores bytes.
            const uint32_t limit = (io16(0) & 7) >= 3 ? 0x14000u : 0x10000u;
            if (a < limit) {
                vram_[a & ~1u] = value;
                vram_[(a & ~1u) + 1] = value;
            }
            return;
        }
        case 0x07: cycles_ += 1; return;  // OAM ignores byte writes
        case 0x0E: case 0x0F:
            cycles_ += uint64_t(1 + kNWait[io16(0x204) & 3]);
            save_write8(address, value);
            return;
        default:
            cycles_ += 1;
            return;
    }
}

void Gba::write16(uint32_t address, uint16_t value) {
    const uint32_t unaligned = address;
    address &= ~1u;
    switch (address >> 24) {
        case 0x02: cycles_ += 3; ewram_[address & 0x3FFFF] = uint8_t(value); ewram_[(address & 0x3FFFF) + 1] = uint8_t(value >> 8); return;
        case 0x03: cycles_ += 1; iwram_[address & 0x7FFF] = uint8_t(value); iwram_[(address & 0x7FFF) + 1] = uint8_t(value >> 8); return;
        case 0x04: {
            cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off < 0x400) io_write16(off, value, 0xFFFF);
            return;
        }
        case 0x05: cycles_ += 1; pal_[address & 0x3FF] = uint8_t(value); pal_[(address & 0x3FF) + 1] = uint8_t(value >> 8); return;
        case 0x06: {
            cycles_ += 1;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            vram_[a] = uint8_t(value);
            vram_[a + 1] = uint8_t(value >> 8);
            return;
        }
        case 0x07: cycles_ += 1; oam_[address & 0x3FF] = uint8_t(value); oam_[(address & 0x3FF) + 1] = uint8_t(value >> 8); return;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            cycles_ += uint64_t(rom_wait(address, 2, false));
            if (is_eeprom(address)) eeprom_write(value);
            return;
        case 0x0E: case 0x0F:
            cycles_ += uint64_t(1 + kNWait[io16(0x204) & 3]);
            save_write8(unaligned, uint8_t(value >> ((unaligned & 1) * 8)));
            return;
        default:
            cycles_ += 1;
            return;
    }
}

void Gba::write32(uint32_t address, uint32_t value) {
    const uint32_t unaligned = address;
    address &= ~3u;
    switch (address >> 24) {
        case 0x02: cycles_ += 6; std::memcpy(&ewram_[address & 0x3FFFF], &value, 4); return;
        case 0x03: cycles_ += 1; std::memcpy(&iwram_[address & 0x7FFF], &value, 4); return;
        case 0x04: {
            cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off < 0x400) {
                io_write16(off, uint16_t(value), 0xFFFF);
                io_write16(off + 2, uint16_t(value >> 16), 0xFFFF);
            }
            return;
        }
        case 0x05: cycles_ += 2; std::memcpy(&pal_[address & 0x3FF], &value, 4); return;
        case 0x06: {
            cycles_ += 2;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            std::memcpy(&vram_[a], &value, 4);
            return;
        }
        case 0x07: cycles_ += 1; std::memcpy(&oam_[address & 0x3FF], &value, 4); return;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            cycles_ += uint64_t(rom_wait(address, 4, false));
            if (is_eeprom(address)) eeprom_write(uint16_t(value));
            return;
        case 0x0E: case 0x0F:
            cycles_ += uint64_t(1 + kNWait[io16(0x204) & 3]);
            save_write8(unaligned, uint8_t(value >> ((unaligned & 3) * 8)));
            return;
        default:
            cycles_ += 1;
            return;
    }
}

// ---------------------------------------------------------------------------
// I/O registers
// ---------------------------------------------------------------------------

uint16_t Gba::io_read16(uint32_t off) {
    switch (off) {
        case 0x004: return dispstat_;
        case 0x006: return uint16_t(line_);
        case 0x100: case 0x104: case 0x108: case 0x10C:
            return uint16_t(timers_[(off - 0x100) / 4].counter);
        case 0x102: case 0x106: case 0x10A: case 0x10E:
            return timers_[(off - 0x102) / 4].control;
        case 0x130: return keyinput_;
        case 0x200: return ie_;
        case 0x202: return if_;
        case 0x208: return ime_;
        case 0x0BA: case 0x0C6: case 0x0D2: case 0x0DE:
            return dma_[(off - 0xBA) / 12].control;
        case 0x0B8: case 0x0C4: case 0x0D0: case 0x0DC:
            return 0;  // DMA word count is write-only
        default:
            break;
    }
    if (off >= 0x060 && off < 0x0B0) {
        return uint16_t(apu_.read8(off - 0x60) | (apu_.read8(off - 0x60 + 1) << 8));
    }
    if (off >= 0x0B0 && off < 0x0E0) return 0;  // DMA addresses: write-only
    return io16(off);
}

void Gba::io_write16(uint32_t off, uint16_t value, uint16_t mask) {
    const uint16_t merged = uint16_t((io16(off) & ~mask) | (value & mask));
    if (off >= 0x060 && off < 0x0B0) {
        if (mask & 0x00FF) apu_.write8(off - 0x60, uint8_t(value));
        if (mask & 0xFF00) apu_.write8(off - 0x60 + 1, uint8_t(value >> 8));
        set_io16(off, merged);
        return;
    }
    switch (off) {
        case 0x004:
            dispstat_ = uint16_t((dispstat_ & 0x0007) | (merged & 0xFF38));
            return;
        case 0x006:
            return;  // VCOUNT is read-only
        case 0x028: case 0x02A: case 0x02C: case 0x02E:
            set_io16(off, merged);
            ppu_.latch_reference(0);
            return;
        case 0x038: case 0x03A: case 0x03C: case 0x03E:
            set_io16(off, merged);
            ppu_.latch_reference(1);
            return;
        case 0x100: case 0x104: case 0x108: case 0x10C:
            timers_[(off - 0x100) / 4].reload = merged;
            set_io16(off, merged);
            return;
        case 0x102: case 0x106: case 0x10A: case 0x10E: {
            Timer& t = timers_[(off - 0x102) / 4];
            const uint16_t old = t.control;
            t.control = merged & 0x00C7;
            if (!(old & 0x80) && (t.control & 0x80)) {
                t.counter = t.reload;
                t.sub = 0;
            }
            return;
        }
        case 0x0BA: case 0x0C6: case 0x0D2: case 0x0DE:
            dma_write_control(int((off - 0xBA) / 12), merged);
            return;
        case 0x130:
            return;  // KEYINPUT is read-only
        case 0x132:
            set_io16(off, merged);
            check_keypad_irq();
            return;
        case 0x200:
            ie_ = merged & 0x3FFF;
            update_irq();
            return;
        case 0x202:
            if_ = uint16_t(if_ & ~(value & mask));  // write 1 to acknowledge
            update_irq();
            return;
        case 0x208:
            ime_ = merged & 1;
            update_irq();
            return;
        case 0x300:
            set_io16(off, uint16_t(merged & 0x00FF));
            if (mask & 0xFF00) halted_ = true;  // HALTCNT (bit 7 = stop, treated as halt)
            return;
        default:
            break;
    }
    if (off >= 0x0B0 && off < 0x0E0) {
        set_io16(off, merged);
        const int n = int((off - 0xB0) / 12);
        const uint32_t base = 0xB0 + uint32_t(n) * 12;
        Dma& d = dma_[size_t(n)];
        d.src = uint32_t(io16(base)) | (uint32_t(io16(base + 2)) << 16);
        d.dst = uint32_t(io16(base + 4)) | (uint32_t(io16(base + 6)) << 16);
        d.count = io16(base + 8);
        return;
    }
    set_io16(off, merged);
}

void Gba::raise_irq(int bit) {
    if_ = uint16_t(if_ | (1u << bit));
    update_irq();
}

void Gba::update_irq() {
    cpu_.set_irq((ime_ & 1) && (ie_ & if_ & 0x3FFF));
}

void Gba::check_keypad_irq() {
    const uint16_t cnt = io16(0x132);
    if (!(cnt & 0x4000)) return;
    const uint16_t pressed = uint16_t(~keyinput_ & 0x3FF);
    const uint16_t select = cnt & 0x3FF;
    const bool hit = (cnt & 0x8000) ? (pressed & select) == select && select : (pressed & select) != 0;
    if (hit) raise_irq(kIrqKeypad);
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------

void Gba::timer_overflow(int t) {
    Timer& tm = timers_[size_t(t)];
    tm.counter = tm.reload;
    if (tm.control & 0x40) raise_irq(kIrqTimer0 + t);
    if (t < 2) apu_.timer_overflow(t);
    if (t < 3) {
        Timer& next = timers_[size_t(t + 1)];
        if ((next.control & 0x80) && (next.control & 0x04)) {
            if (++next.counter > 0xFFFF) timer_overflow(t + 1);
        }
    }
}

void Gba::tick_timers(uint64_t cycles) {
    for (int t = 0; t < 4; t++) {
        Timer& tm = timers_[size_t(t)];
        if (!(tm.control & 0x80)) continue;
        if (t > 0 && (tm.control & 0x04)) continue;  // count-up: driven by the previous timer
        const uint32_t prescale = kTimerPrescale[tm.control & 3];
        uint64_t total = tm.sub + cycles;
        uint64_t inc = total / prescale;
        tm.sub = uint32_t(total % prescale);
        while (inc > 0) {
            const uint64_t room = 0x10000u - tm.counter;
            if (inc >= room) {
                inc -= room;
                timer_overflow(t);
            } else {
                tm.counter += uint32_t(inc);
                inc = 0;
            }
        }
    }
}

uint64_t Gba::cycles_to_timer_event() const {
    uint64_t best = UINT64_MAX;
    for (int t = 0; t < 4; t++) {
        const Timer& tm = timers_[size_t(t)];
        if (!(tm.control & 0x80)) continue;
        if (t > 0 && (tm.control & 0x04)) continue;
        const uint64_t prescale = kTimerPrescale[tm.control & 3];
        const uint64_t c = (0x10000u - tm.counter) * prescale - tm.sub;
        best = std::min(best, std::max<uint64_t>(c, 1));
    }
    return best;
}

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------

void Gba::dma_write_control(int n, uint16_t value) {
    Dma& d = dma_[size_t(n)];
    const uint16_t old = d.control;
    d.control = value & (n == 3 ? 0xFFE0 : 0xF7E0);
    set_io16(0xBA + uint32_t(n) * 12, d.control);
    if ((old & 0x8000) || !(d.control & 0x8000)) return;
    const uint32_t src_mask = n == 0 ? 0x07FFFFFFu : 0x0FFFFFFFu;
    const uint32_t dst_mask = n == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu;
    d.isrc = d.src & src_mask;
    d.idst = d.dst & dst_mask;
    const uint32_t count = n == 3 ? d.count : (d.count & 0x3FFFu);
    d.icount = count ? count : (n == 3 ? 0x10000u : 0x4000u);
    if (((d.control >> 12) & 3) == 0) run_dma(n);
}

void Gba::dma_trigger(int timing) {
    for (int n = 0; n < 4; n++) {
        const Dma& d = dma_[size_t(n)];
        if ((d.control & 0x8000) && ((d.control >> 12) & 3) == timing) run_dma(n);
    }
}

void Gba::run_dma(int n) {
    Dma& d = dma_[size_t(n)];
    const int timing = (d.control >> 12) & 3;
    const bool fifo = timing == 3 && (n == 1 || n == 2);
    const bool word = fifo || (d.control & 0x0400);
    const int dst_ctl = (d.control >> 5) & 3;
    const int src_ctl = (d.control >> 7) & 3;
    const int size = word ? 4 : 2;
    const int32_t src_step = src_ctl == 0 ? size : src_ctl == 1 ? -size : 0;
    const int32_t dst_step = fifo ? 0 : (dst_ctl == 0 || dst_ctl == 3) ? size : dst_ctl == 1 ? -size : 0;
    const uint32_t count = fifo ? 4 : d.icount;
    if (n == 3 && save_type_ == SaveType::Eeprom && (is_eeprom(d.idst) || is_eeprom(d.isrc))) {
        // 6-bit (512 B) or 14-bit (8 KiB) EEPROM, from the bit-stream length.
        if (count == 9 || count == 73) eeprom_addr_bits_ = 6;
        else if (count == 17 || count == 81) eeprom_addr_bits_ = 14;
    }
    cycles_ += 2;
    uint32_t src = d.isrc, dst = d.idst;
    for (uint32_t i = 0; i < count; i++) {
        if (word) write32(dst & ~3u, read32(src & ~3u, false));
        else write16(dst & ~1u, read16(src & ~1u, false));
        src += uint32_t(src_step);
        dst += uint32_t(dst_step);
    }
    d.isrc = src;
    d.idst = dst;
    if (d.control & 0x4000) raise_irq(kIrqDma0 + n);
    if ((d.control & 0x0200) && timing != 0) {
        if (!fifo) {
            const uint32_t reload = n == 3 ? d.count : (d.count & 0x3FFFu);
            d.icount = reload ? reload : (n == 3 ? 0x10000u : 0x4000u);
        }
        if (dst_ctl == 3) d.idst = d.dst & (n == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu);
    } else {
        d.control &= 0x7FFF;
        set_io16(0xBA + uint32_t(n) * 12, d.control);
    }
}

// ---------------------------------------------------------------------------
// Cartridge saves
// ---------------------------------------------------------------------------

uint8_t Gba::save_read8(uint32_t address) {
    const uint32_t a = address & 0xFFFF;
    switch (save_type_) {
        case SaveType::Sram: return save_[a & 0x7FFF];
        case SaveType::Flash64:
        case SaveType::Flash128:
            if (flash_id_ && a < 2) {
                if (save_type_ == SaveType::Flash128) return a == 0 ? 0x62 : 0x13;  // Sanyo
                return a == 0 ? 0x32 : 0x1B;                                       // Panasonic
            }
            return save_[size_t(flash_bank_) * 0x10000 + a];
        default:
            return 0xFF;
    }
}

void Gba::save_write8(uint32_t address, uint8_t value) {
    const uint32_t a = address & 0xFFFF;
    if (save_type_ == SaveType::Sram) {
        save_[a & 0x7FFF] = value;
        save_dirty_ = true;
        return;
    }
    if (save_type_ != SaveType::Flash64 && save_type_ != SaveType::Flash128) return;
    if (flash_write_) {
        save_[size_t(flash_bank_) * 0x10000 + a] = value;
        flash_write_ = false;
        save_dirty_ = true;
        return;
    }
    if (flash_bank_cmd_) {
        if (a == 0) flash_bank_ = save_type_ == SaveType::Flash128 ? (value & 1) : 0;
        flash_bank_cmd_ = false;
        return;
    }
    if (flash_state_ == 0 && a == 0x5555 && value == 0xAA) {
        flash_state_ = 1;
    } else if (flash_state_ == 1 && a == 0x2AAA && value == 0x55) {
        flash_state_ = 2;
    } else if (flash_state_ == 2 && a == 0x5555) {
        switch (value) {
            case 0x90: flash_id_ = true; break;
            case 0xF0: flash_id_ = false; break;
            case 0x80: flash_erase_ = true; break;
            case 0x10:
                if (flash_erase_) {
                    std::fill(save_.begin(), save_.end(), 0xFF);
                    save_dirty_ = true;
                }
                flash_erase_ = false;
                break;
            case 0xA0: flash_write_ = true; break;
            case 0xB0: flash_bank_cmd_ = true; break;
            default: break;
        }
        flash_state_ = 0;
    } else if (flash_state_ == 2 && value == 0x30 && flash_erase_) {
        const size_t base = size_t(flash_bank_) * 0x10000 + (a & 0xF000);
        std::fill(save_.begin() + long(base), save_.begin() + long(base + 0x1000), 0xFF);
        save_dirty_ = true;
        flash_erase_ = false;
        flash_state_ = 0;
    } else {
        if (value == 0xF0) flash_id_ = false;
        flash_state_ = 0;
    }
}

uint16_t Gba::eeprom_read() {
    if (!eeprom_reading_) return 1;  // ready
    const int pos = eeprom_read_pos_++;
    if (pos < 4) return 0;
    const int bit = pos - 4;
    if (bit >= 63) eeprom_reading_ = false;
    const uint32_t byte = (eeprom_read_addr_ * 8 + uint32_t(bit / 8)) & (uint32_t(save_.size()) - 1);
    return (save_[byte] >> (7 - (bit & 7))) & 1;
}

void Gba::eeprom_write(uint16_t value) {
    eeprom_bits_.push_back(uint8_t(value & 1));
    if (eeprom_bits_.size() < 2) return;
    const size_t ab = size_t(eeprom_addr_bits_);
    const bool read_request = eeprom_bits_[0] == 1 && eeprom_bits_[1] == 1;
    const bool write_request = eeprom_bits_[0] == 1 && eeprom_bits_[1] == 0;
    if (!read_request && !write_request) {
        eeprom_bits_.clear();
        return;
    }
    const size_t need = read_request ? 2 + ab + 1 : 2 + ab + 64 + 1;
    if (eeprom_bits_.size() < need) return;
    uint32_t address = 0;
    for (size_t i = 0; i < ab; i++) address = (address << 1) | eeprom_bits_[2 + i];
    if (ab == 14) address &= 0x3FF;
    if (read_request) {
        eeprom_read_addr_ = address;
        eeprom_reading_ = true;
        eeprom_read_pos_ = 0;
    } else {
        for (int byte = 0; byte < 8; byte++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++) v = uint8_t((v << 1) | eeprom_bits_[2 + ab + size_t(byte * 8 + b)]);
            save_[(address * 8 + uint32_t(byte)) & (uint32_t(save_.size()) - 1)] = v;
        }
        save_dirty_ = true;
        eeprom_reading_ = false;
    }
    eeprom_bits_.clear();
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Gba::tick(uint64_t cycles) {
    tick_timers(cycles);
    audio_acc_ += cycles;
    while (audio_acc_ >= uint64_t(GbaApu::kCyclesPerSample)) {
        audio_acc_ -= GbaApu::kCyclesPerSample;
        audio_.push_back(apu_.sample());
    }
}

void Gba::run_until(uint64_t target) {
    while (cycles_ < target) {
        if (halted_) {
            if (ie_ & if_ & 0x3FFF) {
                halted_ = false;
                continue;
            }
            uint64_t step = target - cycles_;
            step = std::min(step, cycles_to_timer_event());
            step = std::min<uint64_t>(step, GbaApu::kCyclesPerSample);
            cycles_ += step;
            tick(step);
            continue;
        }
        const uint64_t before = cycles_;
        cpu_.step();
        if (cycles_ == before) cycles_++;
        tick(cycles_ - before);
    }
}

void Gba::run_frame() {
    uint64_t line_start = cycles_;
    for (int line = 0; line < kLines; line++) {
        line_ = line;
        // V-count match
        const bool match = (dispstat_ >> 8) == line;
        dispstat_ = uint16_t((dispstat_ & ~0x4) | (match ? 0x4 : 0));
        if (match && (dispstat_ & 0x20)) raise_irq(kIrqVCount);
        if (line == 0) ppu_.start_frame();
        if (line == kVisibleLines) {
            dispstat_ |= 1;
            if (dispstat_ & 0x08) raise_irq(kIrqVBlank);
            dma_trigger(1);
        }
        if (line == kLines - 1) dispstat_ &= ~1;
        run_until(line_start + kHDrawCycles);
        dispstat_ |= 2;
        if (dispstat_ & 0x10) raise_irq(kIrqHBlank);
        if (line < kVisibleLines) {
            ppu_.render_line(line, framebuffer_.data() + line * GbaPpu::kWidth);
            ppu_.end_line();
            dma_trigger(2);
        }
        run_until(line_start + kCyclesPerLine);
        dispstat_ &= ~2;
        line_start += kCyclesPerLine;
    }
    if (save_dirty_ && ++save_timer_ >= 300) {
        save_timer_ = 0;
        flush_save();
    }
}

void Gba::set_inputs(const MachineInputs& inputs) {
    const InputState& p = inputs.player1;
    uint16_t k = 0x3FF;
    if (p.button1) k &= ~0x001;  // A
    if (p.button2) k &= ~0x002;  // B
    if (p.select) k &= ~0x004;
    if (p.start) k &= ~0x008;
    if (p.right) k &= ~0x010;
    if (p.left) k &= ~0x020;
    if (p.up) k &= ~0x040;
    if (p.down) k &= ~0x080;
    if (p.button4) k &= ~0x100;  // R
    if (p.button3) k &= ~0x200;  // L
    keyinput_ = k;
    check_keypad_irq();
}

void Gba::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
