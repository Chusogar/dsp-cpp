#include "drivers/computers/c128.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

bool ends_ci(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = s[s.size() - n + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out->resize(size_t(n));
    f.read(reinterpret_cast<char*>(out->data()), n);
    return bool(f);
}

struct RomCandidate {
    const char* name;
    uint32_t crc;
    size_t size;
};

bool load_first(RomLoader& loader, const RomCandidate* cands, size_t n,
                uint8_t* dst, size_t dst_size, std::string* error,
                const char* label) {
    for (size_t i = 0; i < n; i++) {
        std::vector<uint8_t> data;
        if (!loader.try_read(cands[i].name, data)) continue;
        if (data.size() < cands[i].size) continue;
        const uint32_t crc = crc32_of(data.data(), cands[i].size);
        if (crc != cands[i].crc) {
            // Allowed: still accept (revision / redump), but prefer matching.
            // Fall through and use it only if no later candidate matches; we
            // take the first readable candidate of the expected size.
        }
        std::memcpy(dst, data.data(), std::min(dst_size, cands[i].size));
        if (dst_size > cands[i].size) {
            std::memset(dst + cands[i].size, 0xff, dst_size - cands[i].size);
        }
        (void)crc;
        return true;
    }
    // Retry without CRC preference: any matching filename.
    for (size_t i = 0; i < n; i++) {
        std::vector<uint8_t> data;
        if (!loader.try_read(cands[i].name, data)) continue;
        if (data.size() < cands[i].size) continue;
        std::memcpy(dst, data.data(), std::min(dst_size, cands[i].size));
        return true;
    }
    // Any file with the CRC of one of the candidates, whatever it is called.
    for (size_t i = 0; i < n; i++) {
        std::vector<uint8_t> data;
        if (!loader.find_by_crc(cands[i].crc, cands[i].size, data)) continue;
        std::memcpy(dst, data.data(), std::min(dst_size, cands[i].size));
        return true;
    }
    if (error) *error = std::string("missing C128 ROM: ") + label;
    return false;
}

}  // namespace

C128::C128()
    : cpu_(kCpuClock),
      vic_(kCpuClock),
      sid_(kCpuClock),
      cia1_(kCpuClock),
      cia2_(kCpuClock),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    vic_.set_irq_handler([this](IrqLine s) {
        vic_irq_ = (s != IrqLine::Clear);
        update_irq();
    });
    vic_.set_color_ram(color_ram_.data());
    vic_.set_mem_read([this](uint16_t a) -> uint8_t {
        // Character ROM shadows RAM in banks 0/2 at $1000-$1FFF / $9000-$9FFF.
        // C128 mode uses the upper 4K of the 8K CHAROM (ms3=1); lower 4K is
        // the C64 charset and is close enough that either works for READY.
        if ((a & 0x7000) == 0x1000) {
            return char_rom_[0x1000 + (a & 0x0FFF)];
        }
        return ram_[mmu_.translate_ram(a) & 0x1ffff];
    });

    cia1_.set_irq_handler([this](IrqLine s) {
        cia_irq_ = (s != IrqLine::Clear);
        update_irq();
    });
    cia1_.set_port_b([this]() { return cia1_portb_r(); }, {});

    cia2_.set_irq_handler([this](IrqLine s) {
        cia_nmi_ = (s != IrqLine::Clear);
        update_nmi();
    });
    cia2_.set_port_a(
        [this]() { return 0xFF; },
        [this](uint8_t v) { vic_.changed_va(uint16_t(~v & 3)); });
}

bool C128::init(const std::string& rom_path, std::string* error) {
    return load_roms(rom_path, error);
}

bool C128::load_roms(const std::string& path, std::string* error) {
    RomLoader loader;
    std::string open_err;
    bool opened = loader.open(path, &open_err);

    // Also accept a directory that holds c128.zip / c128p.zip.
    if (!opened || (!ends_ci(path, ".zip") && loader.filenames().empty())) {
        static const char* kZips[] = {"c128.zip", "c128p.zip", "c128", "c128p"};
        for (const char* z : kZips) {
            std::string candidate = path;
            if (!candidate.empty() && candidate.back() != '/' && candidate.back() != '\\')
                candidate += '/';
            candidate += z;
            if (loader.open(candidate, &open_err)) {
                opened = true;
                break;
            }
        }
    }
    if (!opened) {
        // Plain directory of loose ROM files.
        if (!loader.open(path, error)) return false;
    }

    // Prefer revision 4/5; fall back to revision 2/3.
    static const RomCandidate kBasicLo[] = {
        {"318018-04.u33", 0x9f9c355b, 0x4000},
        {"318018-02.u33", 0x2ee6e2fa, 0x4000},
    };
    static const RomCandidate kBasicHi[] = {
        {"318019-04.u34", 0x6e2c91a7, 0x4000},
        {"318019-02.u34", 0xd551fce0, 0x4000},
    };
    static const RomCandidate kKernal[] = {
        {"318020-05.u35", 0xba456b8e, 0x4000},
        {"318020-03.u35", 0x1e94bb02, 0x4000},
    };
    // 251913-01: C64 BASIC+KERNAL (16K). Lower half is not CHAROM despite some
    // set notes; CHAROM is 390059-01 (see MAME c128).
    static const RomCandidate kC64Rom[] = {
        {"251913-01.u32", 0x0010ec31, 0x4000},
    };
    // MAME charom. User notes sometimes list this as Z80 BIOS; content/CRC is
    // the 8K character generator. Z80 BIOS lives in the middle of U35.
    static const RomCandidate kChar[] = {
        {"390059-01.u18", 0x6aaaafe6, 0x2000},
    };

    if (!load_first(loader, kBasicLo, 2, basic_lo_.data(), basic_lo_.size(), error,
                    "BASIC LO (318018-04/02.u33)"))
        return false;
    if (!load_first(loader, kBasicHi, 2, basic_hi_.data(), basic_hi_.size(), error,
                    "BASIC HI (318019-04/02.u34)"))
        return false;
    if (!load_first(loader, kKernal, 2, kernal_rom_.data(), kernal_rom_.size(), error,
                    "KERNAL (318020-05/03.u35)"))
        return false;
    if (!load_first(loader, kC64Rom, 1, c64_rom_.data(), c64_rom_.size(), error,
                    "C64 ROM (251913-01.u32)"))
        return false;
    if (!load_first(loader, kChar, 1, char_rom_.data(), char_rom_.size(), error,
                    "CHAROM (390059-01.u18)"))
        return false;

    // Z80 BIOS lives in the middle 4K of U35; keep a copy (Z80 stays halted).
    std::memcpy(z80_bios_.data(), kernal_rom_.data() + 0x1000, 0x1000);

    reset();
    return true;
}

void C128::update_irq() {
    cpu_.set_irq((cia_irq_ || vic_irq_) ? IrqLine::Assert : IrqLine::Clear);
}

void C128::update_nmi() {
    cpu_.set_nmi(cia_nmi_ ? IrqLine::Assert : IrqLine::Clear);
}

void C128::reset() {
    port_bits_ = 0x2F;
    port_val_ = 0x37;
    mmu_.reset();
    vic_.reset();
    sid_.reset();
    cia1_.reset();
    cia2_.reset();
    cia_irq_ = false;
    vic_irq_ = false;
    cia_nmi_ = false;
    cpu_.set_irq(IrqLine::Clear);
    cpu_.set_nmi(IrqLine::Clear);
    cpu_cycle_debt_ = 0;
    keyboard_.fill(0xFF);
    color_ram_.fill(0);
    pending_prg_.clear();
    boot_frames_ = 0;
    audio_.clear();
    audio_acc_ = 0;
    std::fill(framebuffer_.begin(), framebuffer_.end(), Mos6566::kPalette[0]);
    // MMU must already map KERNAL before the CPU reads the reset vector.
    cpu_.reset();
}

uint8_t* C128::color_bank() {
    // Simplified: always the low 1K bank (PLA CLRBANK=0 at power-on).
    return color_ram_.data();
}

uint8_t C128::cia1_portb_r() {
    uint8_t ret = 0xFF;
    const uint8_t pa = cia1_.pa();
    for (int i = 0; i < 8; i++) {
        if ((pa & (1 << i)) == 0) ret = uint8_t(ret & keyboard_[size_t(i)]);
    }
    return ret;
}

uint8_t C128::read_io(uint16_t addr) {
    const uint16_t a = uint16_t(addr & 0x0fff);
    if (a < 0x400) return vic_.read(addr & 0x3F);
    if (a < 0x500) return sid_.read(addr & 0x1F);
    if (a < 0x50c) return mmu_.read(addr, 0xFF);
    if (a >= 0x600 && a < 0x700) return 0xFF;  // VDC 8563 stub (open bus)
    if (a >= 0x800 && a < 0xc00) {
        return uint8_t(color_bank()[addr & 0x3FF] | 0xF0);
    }
    if (a >= 0xc00 && a < 0xd00) return cia1_.read(addr & 0x0F);
    if (a >= 0xd00 && a < 0xe00) return cia2_.read(addr & 0x0F);
    return 0xFF;
}

void C128::write_io(uint16_t addr, uint8_t value) {
    const uint16_t a = uint16_t(addr & 0x0fff);
    if (a < 0x400) {
        vic_.write(addr & 0x3F, value);
        return;
    }
    if (a < 0x500) {
        sid_.write(addr & 0x1F, value);
        return;
    }
    if (a < 0x50c) {
        mmu_.write(addr, value);
        return;
    }
    if (a >= 0x600 && a < 0x700) return;  // VDC stub
    if (a >= 0x800 && a < 0xc00) {
        color_bank()[addr & 0x3FF] = value & 0x0F;
        return;
    }
    if (a >= 0xc00 && a < 0xd00) {
        cia1_.write(addr & 0x0F, value);
        return;
    }
    if (a >= 0xd00 && a < 0xe00) {
        cia2_.write(addr & 0x0F, value);
        return;
    }
}

uint8_t C128::read_byte(uint16_t addr) {
    // 8502 port
    if (addr == 0) return port_bits_;
    if (addr == 1) {
        uint8_t ext = 0xFF;  // cassette sense high
        return uint8_t((port_val_ & port_bits_) | (ext & uint8_t(~port_bits_)));
    }

    // MMU $FF00-$FF04 always visible.
    if (addr >= 0xff00 && addr < 0xff05) {
        return mmu_.read(addr, kernal_rom_[0x2000 + (addr & 0x1FFF)]);
    }

    if (mmu_.c64_mode()) {
        // Minimal C64-mode fallback: map like a stock C64 with all ROMs in.
        if (addr >= 0xa000 && addr <= 0xbfff) return c64_rom_[addr & 0x1FFF];
        if (addr >= 0xd000 && addr <= 0xdfff) return read_io(addr);
        if (addr >= 0xe000) return c64_rom_[0x2000 + (addr & 0x1FFF)];
        return ram_[mmu_.translate_ram(addr)];
    }

    // ---- C128 mode ----
    const uint8_t hi = mmu_.rom_hi();
    const uint8_t mid = mmu_.rom_mid();

    if (addr >= 0x4000 && addr <= 0x7fff) {
        if (mmu_.rom_lo()) return basic_lo_[addr & 0x3FFF];
        return ram_[mmu_.translate_ram(addr)];
    }
    if (addr >= 0x8000 && addr <= 0xbfff) {
        if (mid == 0) return basic_hi_[addr & 0x3FFF];
        // 01/10 function ROMs → open bus / RAM; 11 = RAM
        return ram_[mmu_.translate_ram(addr)];
    }
    if (addr >= 0xc000 && addr <= 0xcfff) {
        // System ROM: Editor (first 4K of U35).
        if (hi == 0) return kernal_rom_[addr & 0x0FFF];
        return ram_[mmu_.translate_ram(addr)];
    }
    if (addr >= 0xd000 && addr <= 0xdfff) {
        if (mmu_.io_enabled()) {
            uint8_t data = read_io(addr);
            return mmu_.read(addr, data);
        }
        // Chargen: 8K CHAROM, C128 half at +0x1000 when ms3=1.
        return char_rom_[0x1000 + (addr & 0x0FFF)];
    }
    if (addr >= 0xe000) {
        if (hi == 0) return kernal_rom_[0x2000 + (addr & 0x1FFF)];
        return ram_[mmu_.translate_ram(addr)];
    }

    return ram_[mmu_.translate_ram(addr)];
}

void C128::write_byte(uint16_t addr, uint8_t value) {
    if (addr == 0) {
        port_bits_ = value;
        return;
    }
    if (addr == 1) {
        port_val_ = value;
        return;
    }

    // MMU registers always writable at $FF00-$FF04.
    if (addr >= 0xff00 && addr < 0xff05) {
        mmu_.write(addr, value);
        // Also write-through to underlying RAM (KERNAL window).
        ram_[mmu_.translate_ram(addr)] = value;
        return;
    }

    if (!mmu_.c64_mode() && addr >= 0xd000 && addr <= 0xdfff && mmu_.io_enabled()) {
        write_io(addr, value);
        // Color RAM / I/O — no RAM write-through for I/O selects except where
        // real hardware dual-maps; RAM under I/O is still written on C64, and
        // C128 does the same for the $D000 page when I/O is mapped.
        ram_[mmu_.translate_ram(addr)] = value;
        return;
    }

    if (mmu_.c64_mode() && addr >= 0xd000 && addr <= 0xdfff) {
        write_io(addr, value);
        ram_[mmu_.translate_ram(addr)] = value;
        return;
    }

    // Writes always hit RAM (ROM is read-only overlay).
    ram_[mmu_.translate_ram(addr)] = value;
}

void C128::on_cycles(int cycles) {
    if (cycles <= 0) return;
    cia1_.tick(cycles);
    cia2_.tick(cycles);
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kCpuClock)) {
        audio_acc_ -= int64_t(kCpuClock);
        int32_t s = sid_.update();
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        audio_.push_back(int16_t(s));
    }
}

void C128::run_frame() {
    update_pending_prg();
    for (int line = 0; line < kScanlines; ++line) {
        int cpu_cycles = kCyclesPerLine;
        const int vis_y = line - 16;
        uint32_t* row = (vis_y >= 0 && vis_y < kScreenHeight)
                            ? framebuffer_.data() + size_t(vis_y) * kScreenWidth
                            : nullptr;
        const int stolen = vic_.update_line(line, row);
        cpu_cycles -= stolen;
        if (cpu_cycles < 0) cpu_cycles = 0;

        if (stolen > 0) on_cycles(stolen);

        cpu_cycle_debt_ += cpu_cycles;
        if (cpu_cycle_debt_ > 0) {
            const int ran = cpu_.run(cpu_cycle_debt_);
            cpu_cycle_debt_ -= ran;
        }
    }
}

void C128::set_inputs(const MachineInputs& inputs) {
    keyboard_.fill(0xFF);

    auto press = [this](int column, uint8_t row_mask) {
        keyboard_[static_cast<size_t>(column)] =
            uint8_t(keyboard_[static_cast<size_t>(column)] & ~row_mask);
    };

    const auto& keys = inputs.keys;
    auto key = [&keys](Key id) { return keys[static_cast<size_t>(id)]; };

    // Same CIA1 matrix as the C64 (C128 extras are out of scope).
    if (key(Key::Backspace)) press(0, 0x01);
    if (key(Key::Enter)) press(0, 0x02);
    if (key(Key::Right) || key(Key::Left)) press(0, 0x04);
    if (key(Key::F7)) press(0, 0x08);
    if (key(Key::F1)) press(0, 0x10);
    if (key(Key::F3)) press(0, 0x20);
    if (key(Key::F5)) press(0, 0x40);
    if (key(Key::Down) || key(Key::Up)) press(0, 0x80);
    if (key(Key::Left) || key(Key::Up)) press(1, 0x80);

    if (key(Key::Num3)) press(1, 0x01);
    if (key(Key::W)) press(1, 0x02);
    if (key(Key::A)) press(1, 0x04);
    if (key(Key::Num4)) press(1, 0x08);
    if (key(Key::Z)) press(1, 0x10);
    if (key(Key::S)) press(1, 0x20);
    if (key(Key::E)) press(1, 0x40);
    if (key(Key::LeftShift)) press(1, 0x80);

    if (key(Key::Num5)) press(2, 0x01);
    if (key(Key::R)) press(2, 0x02);
    if (key(Key::D)) press(2, 0x04);
    if (key(Key::Num6)) press(2, 0x08);
    if (key(Key::C)) press(2, 0x10);
    if (key(Key::F)) press(2, 0x20);
    if (key(Key::T)) press(2, 0x40);
    if (key(Key::X)) press(2, 0x80);

    if (key(Key::Num7)) press(3, 0x01);
    if (key(Key::Y)) press(3, 0x02);
    if (key(Key::G)) press(3, 0x04);
    if (key(Key::Num8)) press(3, 0x08);
    if (key(Key::B)) press(3, 0x10);
    if (key(Key::H)) press(3, 0x20);
    if (key(Key::U)) press(3, 0x40);
    if (key(Key::V)) press(3, 0x80);

    if (key(Key::Num9)) press(4, 0x01);
    if (key(Key::I)) press(4, 0x02);
    if (key(Key::J)) press(4, 0x04);
    if (key(Key::Num0)) press(4, 0x08);
    if (key(Key::M)) press(4, 0x10);
    if (key(Key::K)) press(4, 0x20);
    if (key(Key::O)) press(4, 0x40);
    if (key(Key::N)) press(4, 0x80);

    if (key(Key::Equals)) press(5, 0x01);
    if (key(Key::P)) press(5, 0x02);
    if (key(Key::L)) press(5, 0x04);
    if (key(Key::Minus)) press(5, 0x08);
    if (key(Key::Period)) press(5, 0x10);
    if (key(Key::Comma)) press(5, 0x80);

    if (key(Key::Asterisk)) press(6, 0x02);
    if (key(Key::Semicolon)) press(6, 0x04);
    if (key(Key::Home)) press(6, 0x08);
    if (key(Key::RightShift)) press(6, 0x10);
    if (key(Key::Equals)) press(6, 0x20);
    if (key(Key::Tab)) press(6, 0x40);
    if (key(Key::Slash)) press(6, 0x80);

    if (key(Key::Num1)) press(7, 0x01);
    if (key(Key::LeftCtrl)) press(7, 0x04);
    if (key(Key::Num2)) press(7, 0x08);
    if (key(Key::Space)) press(7, 0x10);
    if (key(Key::Q)) press(7, 0x40);
    if (key(Key::Escape)) press(7, 0x80);

    auto joy = [](const InputState& p) {
        uint8_t v = 0xFF;
        if (p.up) v &= uint8_t(~0x01);
        if (p.down) v &= uint8_t(~0x02);
        if (p.left) v &= uint8_t(~0x04);
        if (p.right) v &= uint8_t(~0x08);
        if (p.button1) v &= uint8_t(~0x10);
        return v;
    };
    cia1_.joystick1 = joy(inputs.player1);
    cia1_.joystick2 = joy(inputs.player2);
}

void C128::set_dip_switch(int /*bank*/, uint8_t /*value*/) {}

bool C128::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open: " + path;
        return false;
    }
    // .prg or raw PRG without extension (same fallback as C64).
    return queue_prg(data, error);
}

bool C128::queue_prg(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 3) {
        if (error) *error = "PRG too small";
        return false;
    }
    pending_prg_ = data;
    boot_frames_ = 0;
    return true;
}

void C128::update_pending_prg() {
    if (pending_prg_.empty()) return;
    if (++boot_frames_ < kPrgInjectFrames) return;
    // BASIC 7.0 TXTTAB at $2D/$2E must point at $1C01 after cold start.
    if (ram_[0x2D] != 0x01 || ram_[0x2E] != 0x1C) return;

    const std::vector<uint8_t> data = std::move(pending_prg_);
    pending_prg_.clear();
    inject_prg(data);
}

void C128::inject_prg(const std::vector<uint8_t>& data) {
    const uint16_t addr = uint16_t(data[0] | (data[1] << 8));
    const size_t n = data.size() - 2;
    for (size_t i = 0; i < n; i++) {
        ram_[uint16_t(addr + i) & 0xffff] = data[i + 2];
    }
    const uint16_t end = uint16_t(addr + n);
    if (addr != 0x1C01) return;

    // C128 BASIC 7.0 pointers (C64 offsets + 2): TXTTAB $2D, VARTAB $2F,
    // ARYTAB $31, STREND $33. EAL remains at $AE/$AF.
    ram_[0x2F] = uint8_t(end & 0xFF); ram_[0x30] = uint8_t(end >> 8);
    ram_[0x31] = uint8_t(end & 0xFF); ram_[0x32] = uint8_t(end >> 8);
    ram_[0x33] = uint8_t(end & 0xFF); ram_[0x34] = uint8_t(end >> 8);
    ram_[0xAE] = uint8_t(end & 0xFF); ram_[0xAF] = uint8_t(end >> 8);

    static constexpr uint8_t kRun[] = {'R', 'U', 'N', 0x0D};
    // C128 mode editor: KEYD at $034A, NDX at $D0 (not the C64 $0277/$C6).
    for (size_t i = 0; i < sizeof(kRun); i++) ram_[0x034A + i] = kRun[i];
    ram_[0x00D0] = uint8_t(sizeof(kRun));
}

void C128::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
