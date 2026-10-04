#include "drivers/arcade/model3.h"

#include <algorithm>
#include <cstring>
#include <ctime>

#include "core/rom_loader.h"

namespace dsp {
namespace {

constexpr uint32_t kRamSize = 0x800000;
constexpr uint32_t kCromSize = 0x800000;
constexpr uint32_t kBankedSize = 0x3000000;
constexpr uint32_t kVromSize = 0x4000000;
constexpr uint32_t kBackupSize = 0x20000;
constexpr uint32_t kSecuritySize = 0x20000;
constexpr uint32_t kSwtrilgyKey = 0x31272A01;

inline uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline void put_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}
inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline void put_be16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}

struct FileSpec {
    const char* name;
    uint32_t size;
    uint32_t crc;
};

// Loads four 16-bit interleaved program ROMs (ROM_LOAD64_WORD_SWAP): in the
// big-endian stream, each 8-byte group holds a byte-swapped word from each
// file in order.
bool load_interleaved64(RomLoader& loader, const FileSpec (&files)[4], uint8_t* dest, uint32_t dest_size,
                        std::string* error, std::vector<std::string>& warnings) {
    for (int f = 0; f < 4; ++f) {
        std::vector<uint8_t> data;
        if (!loader.try_read(files[f].name, data)) {
            if (error) *error = std::string("missing ROM ") + files[f].name;
            return false;
        }
        if (data.size() != files[f].size) {
            if (error) *error = std::string("bad size for ") + files[f].name;
            return false;
        }
        if (crc32_of(data.data(), data.size()) != files[f].crc)
            warnings.push_back(std::string("CRC mismatch: ") + files[f].name);
        for (uint32_t i = 0; i + 1 < data.size(); i += 2) {
            const uint32_t off = (i / 2) * 8 + uint32_t(f) * 2;
            if (off + 1 >= dest_size) break;
            dest[off] = data[i + 1];
            dest[off + 1] = data[i];
        }
    }
    return true;
}

}  // namespace

Model3::Model3() : cpu_(*this, Ppc603::kPvr603r), crypt_(kSwtrilgyKey) {
    ram_.assign(kRamSize, 0);
    crom_.assign(kCromSize, 0xff);
    crom_banked_.assign(kBankedSize, 0xff);
    backup_.assign(kBackupSize, 0);
    security_ram_.assign(kSecuritySize, 0);
    blank_bank_.assign(0x800000, 0xff);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    fb_work_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    frame3d_.assign(size_t(kScreenWidth) * kScreenHeight, 0);
    bottom_snap_.assign(size_t(kScreenWidth) * kScreenHeight, 0);
    top_snap_.assign(size_t(kScreenWidth) * kScreenHeight, 0);

    cpu_.add_fast_region(0x00000000, kRamSize, ram_.data(), true);                 // 0
    cpu_.add_fast_region(0xff800000, kCromSize, crom_.data(), false);              // 1
    cpu_.add_fast_region(0xff000000, 0x800000, crom_banked_.data(), false);        // 2 (banked)
    cpu_.add_fast_region(0xf1000000, 0x100000, tilegen_.vram(), true);             // 3
    cpu_.add_fast_region(0xf00c0000, kBackupSize, backup_.data(), true);           // 4
    cpu_.add_fast_region(0xfe0c0000, kBackupSize, backup_.data(), true);           // 5

    Real3D::Host host;
    host.read32 = [this](uint32_t a) { return read32(a); };
    host.write32 = [this](uint32_t a, uint32_t v) { write32(a, v); };
    host.dma_irq = [this](bool on) {
        if (on) irq_assert(0x100);
        else irq_clear(0x100);
    };
    gpu_.set_host(host);
    gpu_.set_texture_write_hook([this] { finish_render(); });
    {
        const unsigned hw = std::thread::hardware_concurrency();
        gpu_.set_render_threads(hw > 2 ? int(std::min(8u, hw - 1)) : 1);
    }
    crypt_.set_read([this](uint32_t addr) -> uint16_t {
        if (addr < 0x8000) return be16(&security_ram_[size_t(addr) * 4]);
        return 0;
    });
    set_cpu_clock(cpu_hz_);
}

Model3::~Model3() { finish_render(); }

void Model3::set_cpu_clock(uint32_t hz) {
    cpu_hz_ = std::max<uint32_t>(hz, 10000000);
    // Time base / decrementer: one tick every 4 bus clocks (66.67 MHz bus),
    // kept at the real rate whatever the emulated CPU clock.
    cpu_.set_timer_divider(std::max(1, int(double(cpu_hz_) / 16666666.0 + 0.5)));
}

bool Model3::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    gpu_.set_vrom(vrom_.data(), vrom_.size());
    reset();
    return true;
}

bool Model3::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    static const FileSpec kCrom[4] = {{"epr-21382a.20", 0x200000, 0x69BAF117},
                                      {"epr-21381a.19", 0x200000, 0x2DD34E28},
                                      {"epr-21380a.18", 0x200000, 0x780FB4E7},
                                      {"epr-21379a.17", 0x200000, 0x24DC1555}};
    static const FileSpec kCrom0[4] = {{"mpr-21342.04", 0x400000, 0x339525CE},
                                       {"mpr-21341.03", 0x400000, 0xB2A269E4},
                                       {"mpr-21340.02", 0x400000, 0xAD36040E},
                                       {"mpr-21339.01", 0x400000, 0xC0CE5037}};
    static const FileSpec kCrom1[4] = {{"mpr-21346.08", 0x400000, 0xC8733594},
                                       {"mpr-21345.07", 0x400000, 0x6C183A21},
                                       {"mpr-21344.06", 0x400000, 0x87453D76},
                                       {"mpr-21343.05", 0x400000, 0x12552D07}};
    static const FileSpec kCrom2[4] = {{"mpr-21350.12", 0x400000, 0x486195E7},
                                       {"mpr-21349.11", 0x400000, 0x3D39454B},
                                       {"mpr-21348.10", 0x400000, 0x1F7CC5F5},
                                       {"mpr-21347.09", 0x400000, 0xECB6B934}};
    if (!load_interleaved64(loader, kCrom, crom_.data(), kCromSize, error, warnings_)) return false;
    if (!load_interleaved64(loader, kCrom0, crom_banked_.data(), 0x1000000, error, warnings_)) return false;
    if (!load_interleaved64(loader, kCrom1, crom_banked_.data() + 0x1000000, 0x1000000, error, warnings_))
        return false;
    if (!load_interleaved64(loader, kCrom2, crom_banked_.data() + 0x2000000, 0x1000000, error, warnings_))
        return false;

    // VROM: 16 x 4 MB, 16-bit words interleaved over a 32-byte stride.
    static const FileSpec kVrom[16] = {
        {"mpr-21359.26", 0x400000, 0x34EF4122}, {"mpr-21360.27", 0x400000, 0x2882B95E},
        {"mpr-21361.28", 0x400000, 0x9B61C3C1}, {"mpr-21362.29", 0x400000, 0x01A92169},
        {"mpr-21363.30", 0x400000, 0xE7D18FED}, {"mpr-21364.31", 0x400000, 0xCB6A5468},
        {"mpr-21365.32", 0x400000, 0xAD5449D8}, {"mpr-21366.33", 0x400000, 0xDEFB6B95},
        {"mpr-21367.34", 0x400000, 0xDFD51029}, {"mpr-21368.35", 0x400000, 0xAE90FD21},
        {"mpr-21369.36", 0x400000, 0xBF17EEB4}, {"mpr-21370.37", 0x400000, 0x2321592A},
        {"mpr-21371.38", 0x400000, 0xA68782FD}, {"mpr-21372.39", 0x400000, 0xFC3F4E8B},
        {"mpr-21373.40", 0x400000, 0xB76AD261}, {"mpr-21374.41", 0x400000, 0xAE6C4D28}};
    vrom_.assign(kVromSize, 0);
    for (int f = 0; f < 16; ++f) {
        std::vector<uint8_t> data;
        if (!loader.try_read(kVrom[f].name, data) || data.size() != kVrom[f].size) {
            if (error) *error = std::string("missing or bad ROM ") + kVrom[f].name;
            return false;
        }
        if (crc32_of(data.data(), data.size()) != kVrom[f].crc)
            warnings_.push_back(std::string("CRC mismatch: ") + kVrom[f].name);
        for (uint32_t i = 0; i + 1 < data.size(); i += 2) {
            const size_t off = size_t(i / 2) * 32 + size_t(f) * 2;
            vrom_[off] = data[i];
            vrom_[off + 1] = data[i + 1];
        }
    }

    // Sound board and DSB2 (sound is optional: missing ROMs only mute it).
    {
        std::vector<uint8_t> prog, s1, s2, dsb, mpeg;
        loader.try_read("epr-21383.21", prog);
        loader.try_read("mpr-21355.22", s1);
        loader.try_read("mpr-21357.24", s2);
        loader.try_read("epr-21384.2", dsb);
        for (const char* n : {"mpr-21375.18", "mpr-21376.20", "mpr-21377.22", "mpr-21378.24"}) {
            std::vector<uint8_t> part;
            if (!loader.try_read(n, part)) {
                warnings_.push_back(std::string("missing MPEG ROM ") + n);
                part.assign(0x400000, 0);
            }
            part.resize(0x400000, 0);
            mpeg.insert(mpeg.end(), part.begin(), part.end());
        }
        if (prog.empty()) warnings_.push_back("missing sound program epr-21383.21 (no sound)");
        std::vector<uint8_t> samples = s1;
        samples.resize(0x400000, 0);
        samples.insert(samples.end(), s2.begin(), s2.end());
        samples.resize(0x800000, 0);
        sound_.load(prog, samples, dsb, mpeg);
    }

    // The force feedback drive board is not emulated: skip its set-up the
    // way MAME does (program offsets in the fixed CROM).
    put_be32(&crom_[0x043dc], 0x48000090);
    put_be32(&crom_[0xf6e44], 0x60000000);
    for (const auto& w : loader.warnings()) warnings_.push_back(w);
    return true;
}

void Model3::reset() {
    finish_render();
    have_pending_ = false;
    std::fill(ram_.begin(), ram_.end(), 0);
    set_crom_bank(0xff);
    irq_enable_ = 0;
    irq_state_ = 0;
    midi_ctrl_ = 0;
    input_bank_ = 0;
    adc_channel_ = 0;
    security_first_read_ = true;
    crypt_.reset();
    eeprom_.reset();
    tilegen_.reset();
    gpu_.reset();
    gpu_.set_vrom(vrom_.data(), vrom_.size());

    mpc_regs_.fill(0);
    auto put_le32 = [this](int off, uint32_t v) {
        for (int i = 0; i < 4; ++i) mpc_regs_[size_t(off + i)] = uint8_t(v >> (8 * i));
    };
    mpc_regs_[0] = 0x57;
    mpc_regs_[1] = 0x10;  // vendor: Motorola
    mpc_regs_[2] = 0x02;
    mpc_regs_[3] = 0x00;  // device: MPC106
    put_le32(0x04, 0x00800006);
    put_le32(0x08, 0x00060000);
    put_le32(0x0c, 0x00000800);
    put_le32(0x70, 0x00cd0000);
    put_le32(0xa8, 0x0010ff00);
    put_le32(0xac, 0x060c000c);
    put_le32(0xb8, 0x04000000);
    put_le32(0xc0, 0x00000100);
    put_le32(0xe0, 0x00420fff);
    put_le32(0xe8, 0x00200000);
    put_le32(0xf0, 0x0000ff02);
    put_le32(0xf4, 0x00030000);
    put_le32(0xfc, 0x00000010);
    pci_bus_ = pci_device_ = pci_function_ = pci_reg_ = 0;

    cpu_.reset();
    cpu_.set_irq(false);
    sound_.reset();
    audio_.clear();
    audio_frac_ = 0;
    frames_ = 0;
}

void Model3::set_crom_bank(uint8_t value) {
    crom_bank_reg_ = value;
    const uint32_t idx = (~value) & 0xf;
    const size_t off = size_t(idx) * 0x800000;
    cpu_.set_region_data(2, off + 0x800000 <= crom_banked_.size() ? crom_banked_.data() + off : blank_bank_.data());
}

// ---------------------------------------------------------------------------
// IRQ controller

void Model3::update_irq() {
    const bool line = (irq_state_ & irq_enable_ & 0xff) != 0 || (irq_state_ & ~0xffu) != 0;
    cpu_.set_irq(line);
}

void Model3::irq_assert(uint32_t bits) {
    irq_state_ |= bits;
    update_irq();
}

void Model3::irq_clear(uint32_t bits) {
    irq_state_ &= ~bits;
    update_irq();
}

// ---------------------------------------------------------------------------
// I/O devices

uint8_t Model3::read_inputs(unsigned reg) {
    reg &= 0x3f;
    switch (reg) {
        case 0x00: return input_bank_;
        case 0x04: {
            uint8_t d = 0xff;
            if ((input_bank_ & 1) == 0) {
                if (inputs_.coin1) d &= uint8_t(~0x01);
                if (inputs_.coin2) d &= uint8_t(~0x02);
                if (inputs_.service) d &= uint8_t(~0x04);          // Test
                if (inputs_.player1.select) d &= uint8_t(~0x08);   // Service
                if (inputs_.player1.start) d &= uint8_t(~0x10);
                if (inputs_.player2.start) d &= uint8_t(~0x20);
            } else {
                d = uint8_t((d & 0xdf) | (eeprom_.do_read() ? 0x20 : 0));
            }
            return d;
        }
        case 0x08: {
            uint8_t d = 0xff;
            if (inputs_.player1.button1) d &= uint8_t(~0x20);  // trigger 1
            if (inputs_.player1.button2) d &= uint8_t(~0x10);  // trigger 2
            if (inputs_.player1.button3) d &= uint8_t(~0x01);  // event 1
            if (inputs_.player1.button4) d &= uint8_t(~0x02);  // event 2
            return d;
        }
        case 0x18: return 0x7f;
        case 0x34: return 0;
        case 0x3c: {
            uint8_t v = 0;
            const int ch = adc_channel_ & 7;
            if (ch == 0) v = uint8_t(joy_y_);
            else if (ch == 1) v = uint8_t(joy_x_);
            adc_channel_ = uint8_t((adc_channel_ + 1) & 7);
            return v;
        }
        default: return 0xff;
    }
}

void Model3::write_inputs(unsigned reg, uint8_t value) {
    switch (reg & 0x3f) {
        case 0x00:
            eeprom_.di_write((value >> 5) & 1);
            eeprom_.cs_write((value >> 6) & 1);
            eeprom_.clk_write((value >> 7) & 1);
            input_bank_ = value;
            break;
        case 0x3c: adc_channel_ = value & 7; break;
        default: break;
    }
}

uint8_t Model3::read_system(unsigned reg) const {
    switch (reg & 0x3f) {
        case 0x08: return crom_bank_reg_;
        case 0x14: return irq_enable_;
        case 0x18: return uint8_t(irq_state_ & 0xff);
        case 0x10: return uint8_t(gpu_.jtag_tdo() ? 0x20 : 0);
        default: return 0xff;
    }
}

void Model3::write_system(unsigned reg, uint8_t value) {
    switch (reg & 0x3f) {
        case 0x08: set_crom_bank(value); break;
        case 0x14:
            irq_enable_ = value;
            update_irq();
            break;
        case 0x18: irq_clear(value); break;
        case 0x0c:
            gpu_.jtag_write((value >> 6) & 1, (value >> 2) & 1, (value >> 5) & 1, (value >> 7) & 1);
            break;
        default: break;
    }
}

uint8_t Model3::read_rtc(unsigned reg) const {
    const std::time_t now = std::time(nullptr);
    if (now != rtc_time_) {
        rtc_time_ = now;
#if defined(_WIN32)
        localtime_s(&rtc_tm_, &now);
#else
        localtime_r(&now, &rtc_tm_);
#endif
    }
    const std::tm& t = rtc_tm_;
    switch (reg & 0xf) {
        case 0: return uint8_t((t.tm_sec % 10) & 0xf);
        case 1: return uint8_t((t.tm_sec / 10) & 0x7);
        case 2: return uint8_t((t.tm_min % 10) & 0xf);
        case 3: return uint8_t((t.tm_min / 10) & 0x7);
        case 4: return uint8_t((t.tm_hour % 10) & 0xf);
        case 5: return uint8_t((t.tm_hour / 10) & 0x7);
        case 6: return uint8_t((t.tm_mday % 10) & 0xf);
        case 7: return uint8_t((t.tm_mday / 10) & 0x3);
        case 8: return uint8_t(((t.tm_mon + 1) % 10) & 0xf);
        case 9: return uint8_t(((t.tm_mon + 1) / 10) & 0x1);
        case 10: return uint8_t((t.tm_year % 10) & 0xf);
        case 11: return uint8_t(((t.tm_year % 100) / 10) & 0xf);
        case 12: return uint8_t(t.tm_wday & 0x7);
        default: return 0;
    }
}

uint32_t Model3::read_security(unsigned reg) {
    switch (reg) {
        case 0x00: return 0;
        case 0x1c:
            if (security_first_read_) {
                security_first_read_ = false;
                return 0xffff0000u;
            } else {
                uint8_t* base = nullptr;
                return uint32_t(crypt_.do_decrypt(base)) << 16;
            }
        default: return 0xffffffffu;
    }
}

void Model3::write_security(unsigned reg, uint32_t value) {
    switch (reg) {
        case 0x10:
        case 0x14:
            crypt_.set_addr_low(0);
            crypt_.set_addr_high(0);
            security_first_read_ = true;
            break;
        case 0x18: {
            const uint16_t sub = uint16_t(value >> 16);
            crypt_.set_subkey(uint16_t((sub >> 8) | (sub << 8)));
            break;
        }
        default: break;
    }
}

uint32_t Model3::pci_config_data_read() {
    if (pci_device_ == 0) return be32(&mpc_regs_[pci_reg_ & 0xfc]);
    if (pci_device_ == 13) return gpu_.pci_config_read((pci_reg_ >> 2) & 0x3c);
    return 0;
}

// ---------------------------------------------------------------------------
// Bus

uint8_t Model3::read8(uint32_t a) {
    if (a < kRamSize) return ram_[a];
    switch (a >> 24) {
        case 0xff:
            if (a >= 0xff800000) return crom_[a & 0x7fffff];
            return read32(a & ~3u) >> (8 * (3 - (a & 3)));
        case 0xc2: return gpu_.read_dma8(a & 0xff);
        case 0xf0:
        case 0xfe:
            switch ((a >> 16) & 0xff) {
                case 0x04: return read_inputs(a & 0x3f);
                case 0x08:
                    if ((a & 0xf) == 0) return 0x00;
                    if ((a & 0xf) == 4) return 0x83;
                    return 0;
                case 0x0c:
                case 0x0d: return backup_[a & 0x1ffff];
                case 0x10: return read_system(a & 0x3f);
                case 0x14:
                    if ((a & 3) == 1) return 0x03;
                    if ((a & 3) == 0) return read_rtc((a >> 2) & 0xf);
                    return 0;
                default: break;
            }
            break;
        case 0xf1:
            if (a < 0xf1120000) return tilegen_.read8(a & 0x1fffff);
            break;
        default: break;
    }
    return 0xff;
}

uint16_t Model3::read16(uint32_t a) {
    if (a & 1) return uint16_t(read8(a) << 8 | read8(a + 1));
    if (a < kRamSize) return be16(&ram_[a]);
    switch (a >> 24) {
        case 0xff:
            if (a >= 0xff800000) return be16(&crom_[a & 0x7fffff]);
            return uint16_t(read32(a & ~3u) >> (16 * (1 - ((a >> 1) & 1))));
        case 0xf0:
        case 0xfe: {
            const uint32_t sub = (a >> 16) & 0xff;
            if (sub == 0x0c || sub == 0x0d) return be16(&backup_[a & 0x1ffff]);
            if (sub >= 0xe0 && sub <= 0xef) {
                const uint32_t d = pci_config_data_read();
                return uint16_t(d >> (16 * (1 - ((a >> 1) & 1))));
            }
            break;
        }
        case 0xf1:
            if (a < 0xf1120000) return uint16_t(read8(a) << 8 | read8(a + 1));
            break;
        default: break;
    }
    return 0xffff;
}

uint32_t Model3::read32(uint32_t a) {
    if (a & 3) return uint32_t(read16(a)) << 16 | read16(a + 2);
    if (a < kRamSize) return be32(&ram_[a]);
    switch (a >> 24) {
        case 0xff:
            if (a >= 0xff800000) return be32(&crom_[a & 0x7fffff]);
            {
                const uint32_t idx = (~uint32_t(crom_bank_reg_)) & 0xf;
                const size_t off = size_t(idx) * 0x800000 + (a & 0x7fffff);
                return off + 4 <= crom_banked_.size() ? be32(&crom_banked_[off]) : 0xffffffffu;
            }
        case 0x84: return __builtin_bswap32(gpu_.read_register(a & 0x3f));
        case 0xc2: return __builtin_bswap32(gpu_.read_dma32(a & 0xff));
        case 0xf0:
        case 0xfe: {
            const uint32_t sub = (a >> 16) & 0xff;
            switch (sub) {
                case 0x04:
                    return uint32_t(read_inputs((a & 0x3f) + 0)) << 24 | uint32_t(read_inputs((a & 0x3f) + 1)) << 16 |
                           uint32_t(read_inputs((a & 0x3f) + 2)) << 8 | read_inputs((a & 0x3f) + 3);
                case 0x0c:
                case 0x0d: return be32(&backup_[a & 0x1ffff]);
                case 0x10:
                    return uint32_t(read_system((a & 0x3f) + 0)) << 24 | uint32_t(read_system((a & 0x3f) + 1)) << 16 |
                           uint32_t(read_system((a & 0x3f) + 2)) << 8 | read_system((a & 0x3f) + 3);
                case 0x14: return uint32_t(read_rtc((a >> 2) & 0xf)) << 24 | 0x00030000u;
                case 0x18:
                case 0x19: return be32(&security_ram_[a & 0x1ffff]);
                case 0x1a: return read_security(a & 0x3f);
                default:
                    if (sub >= 0xe0 && sub <= 0xef) return pci_config_data_read();
                    break;
            }
            break;
        }
        case 0xf1:
            if (a < 0xf1120000)
                return uint32_t(read8(a)) << 24 | uint32_t(read8(a + 1)) << 16 | uint32_t(read8(a + 2)) << 8 |
                       read8(a + 3);
            if (a >= 0xf1180000 && a < 0xf1180100) return __builtin_bswap32(tilegen_.read_register(a & 0xff));
            break;
        default: break;
    }
    return 0xffffffffu;
}

void Model3::write8(uint32_t a, uint8_t v) {
    if (a < kRamSize) {
        ram_[a] = v;
        return;
    }
    switch (a >> 24) {
        case 0xc2: gpu_.write_dma8(a & 0xff, v); break;
        case 0xf0:
        case 0xfe:
            switch ((a >> 16) & 0xff) {
                case 0x04: write_inputs(a & 0x3f, v); break;
                case 0x08:
                    if ((a & 0xf) == 0) {
                        sound_.midi_write(v);
                        irq_clear(0x40);
                    } else if ((a & 0xf) == 4) {
                        midi_ctrl_ = v;
                        if ((v & 0x20) == 0) irq_clear(0x40);
                    }
                    break;
                case 0x0c:
                case 0x0d: backup_[a & 0x1ffff] = v; break;
                case 0x10: write_system(a & 0x3f, v); break;
                default: break;
            }
            break;
        case 0xf1:
            if (a < 0xf1120000) tilegen_.write8(a & 0x1fffff, v);
            break;
        default: break;
    }
}

void Model3::write16(uint32_t a, uint16_t v) {
    if (a & 1) {
        write8(a, uint8_t(v >> 8));
        write8(a + 1, uint8_t(v));
        return;
    }
    if (a < kRamSize) {
        put_be16(&ram_[a], v);
        return;
    }
    switch (a >> 24) {
        case 0xf0:
        case 0xfe: {
            const uint32_t sub = (a >> 16) & 0xff;
            if (sub == 0x0c || sub == 0x0d) put_be16(&backup_[a & 0x1ffff], v);
            break;
        }
        case 0xf1:
            if (a < 0xf1120000) {
                tilegen_.write8(a & 0x1fffff, uint8_t(v >> 8));
                tilegen_.write8((a + 1) & 0x1fffff, uint8_t(v));
            }
            break;
        default: break;
    }
}

void Model3::write32(uint32_t a, uint32_t v) {
    if (a & 3) {
        write16(a, uint16_t(v >> 16));
        write16(a + 2, uint16_t(v));
        return;
    }
    if (a < kRamSize) {
        put_be32(&ram_[a], v);
        return;
    }
    const uint32_t lv = __builtin_bswap32(v);
    switch (a >> 24) {
        case 0x88: gpu_.flush(); break;
        case 0x8c: gpu_.write_culling_low(a & 0x3fffff, lv); break;
        case 0x8e: gpu_.write_culling_high(a & 0xfffff, lv); break;
        case 0x90: gpu_.write_texture_port(lv); break;
        case 0x94: gpu_.write_texture_fifo(lv); break;
        case 0x98: gpu_.write_polygon_ram(a & 0x3fffff, lv); break;
        case 0x9c: gpu_.write_config(a, lv); break;
        case 0xc2: gpu_.write_dma32(a & 0xff, lv); break;
        case 0xf0:
        case 0xfe: {
            const uint32_t sub = (a >> 16) & 0xff;
            switch (sub) {
                case 0x04:
                    for (int i = 0; i < 4; ++i) write_inputs((a & 0x3f) + unsigned(i), uint8_t(v >> (24 - 8 * i)));
                    break;
                case 0x0c:
                case 0x0d: put_be32(&backup_[a & 0x1ffff], v); break;
                case 0x10:
                    for (int i = 0; i < 4; ++i) write_system((a & 0x3f) + unsigned(i), uint8_t(v >> (24 - 8 * i)));
                    break;
                case 0x18:
                case 0x19: put_be32(&security_ram_[a & 0x1ffff], v); break;
                case 0x1a: write_security(a & 0x3f, v); break;
                default:
                    if (a >= 0xfec00000 && a < 0xfee00000) {
                        // MPC106 CONFIG_ADDR (little-endian register)
                        pci_bus_ = (lv >> 16) & 0xff;
                        pci_device_ = (lv >> 11) & 0x1f;
                        pci_function_ = (lv >> 8) & 7;
                        pci_reg_ = lv & 0xff;
                    } else if (a >= 0xfee00000 && a < 0xfef00000) {
                        if (pci_device_ == 0) put_be32(&mpc_regs_[pci_reg_ & 0xfc], v);
                    }
                    break;
            }
            break;
        }
        case 0xf1:
            if (a < 0xf1120000) {
                for (int i = 0; i < 4; ++i) tilegen_.write8((a + uint32_t(i)) & 0x1fffff, uint8_t(v >> (24 - 8 * i)));
            } else if (a >= 0xf1180000 && a < 0xf1180100) {
                tilegen_.write_register(a & 0xff, lv, [this](uint8_t bits) { irq_clear(bits); });
                if (a == 0xf118000c) gpu_.tilegen_draw_frame();
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Frame

void Model3::run_frame() {
    // Analog joystick from the digital controls.
    auto approach = [](int& v, int target) {
        const int step = 24;
        if (v < target) v = std::min(target, v + step);
        else if (v > target) v = std::max(target, v - step);
    };
    approach(joy_x_, inputs_.player1.left ? 0x00 : inputs_.player1.right ? 0xff : 0x80);
    approach(joy_y_, inputs_.player1.up ? 0x00 : inputs_.player1.down ? 0xff : 0x80);

    const int frame_cycles = int(double(cpu_hz_) / kFramesPerSecond);
    const int line_cycles = frame_cycles / 424;
    int vblank_cycles = line_cycles * 40;

    // VBlank
    tilegen_.begin_frame();
    gpu_.begin_vblank();
    while ((irq_enable_ & 0x02) && (irq_state_ & 0x02) && vblank_cycles > 1000) {
        cpu_.run(1000);
        vblank_cycles -= 1000;
    }
    for (int count = 0; (midi_ctrl_ & 0x20) && (irq_enable_ & 0x40) && count < 128 && vblank_cycles > 1000; ++count) {
        irq_assert(0x40);
        cpu_.run(1000);
        vblank_cycles -= 1000;
    }
    if (vblank_cycles > 0) cpu_.run(vblank_cycles);
    irq_assert(0x0d);

    // Active display.
    const uint32_t flip_line = tilegen_.read_register(0x08);
    for (int line = 0; line < 384; ++line) {
        if (uint32_t(line) == flip_line) gpu_.flip_ping_pong();
        if (line == 383) irq_assert(0x02);
        tilegen_.draw_line(line);
        cpu_.run(line_cycles);
    }

    compose();
    ++frames_;

    // Sound board + DSB2 for this frame's worth of 44.1 kHz samples.
    audio_frac_ += 44100.0 / kFramesPerSecond;
    const int n = int(audio_frac_);
    audio_frac_ -= n;
    stereo_.clear();
    sound_.run(n, stereo_);
    for (int i = 0; i < n; ++i)
        audio_.push_back(int16_t((int32_t(stereo_[size_t(i) * 2]) + stereo_[size_t(i) * 2 + 1]) / 2));
}

void Model3::finish_render() {
    if (worker_.joinable()) worker_.join();
}

void Model3::set_threaded(bool on) {
    finish_render();
    threaded_ = on;
    have_pending_ = false;
    if (!on) gpu_.set_render_threads(1);
}

void Model3::compose() {
    finish_render();
    if (threaded_ && have_pending_) framebuffer_.swap(fb_work_);
    gpu_.prepare();
    std::memcpy(bottom_snap_.data(), tilegen_.bottom(), bottom_snap_.size() * 4);
    std::memcpy(top_snap_.data(), tilegen_.top(), top_snap_.size() * 4);
    if (threaded_) {
        worker_ = std::thread([this] {
            gpu_.rasterize(frame3d_);
            mix_layers(fb_work_);
        });
        have_pending_ = true;
    } else {
        gpu_.rasterize(frame3d_);
        mix_layers(framebuffer_);
    }
}

void Model3::mix_layers(std::vector<uint32_t>& dst) {
    const uint32_t* bottom = bottom_snap_.data();
    const uint32_t* top = top_snap_.data();
    for (size_t i = 0; i < dst.size(); ++i) {
        uint32_t c = bottom[i] ? bottom[i] : 0xff000000u;
        const uint32_t g = frame3d_[i];
        const uint32_t ga = g >> 24;
        if (ga == 0xff) {
            c = g;
        } else if (ga) {
            // 3D colour is premultiplied by its alpha.
            const uint32_t inv = 255 - ga;
            const uint32_t r = ((g >> 16) & 0xff) + ((c >> 16) & 0xff) * inv / 255;
            const uint32_t gg = ((g >> 8) & 0xff) + ((c >> 8) & 0xff) * inv / 255;
            const uint32_t b = (g & 0xff) + (c & 0xff) * inv / 255;
            c = 0xff000000u | std::min(r, 255u) << 16 | std::min(gg, 255u) << 8 | std::min(b, 255u);
        }
        if (top[i]) c = top[i];
        dst[i] = c | 0xff000000u;
    }
}

void Model3::set_inputs(const MachineInputs& inputs) { inputs_ = inputs; }

void Model3::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
