#include "drivers/consoles/snes.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace dsp {
namespace {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return !out.empty();
}

// Scores a candidate header the way the hardware documentation suggests: a
// printable title and a checksum that agrees with its own complement.
int score_header(const std::vector<uint8_t>& rom, size_t base) {
    if (base + 32 > rom.size()) return -1;
    int score = 0;
    for (size_t i = 0; i < 21; i++) {
        const uint8_t c = rom[base + i];
        if (c >= 0x20 && c < 0x7f) score++;
    }
    const uint16_t chk = uint16_t(rom[base + 28] | (rom[base + 29] << 8));
    const uint16_t inv = uint16_t(rom[base + 30] | (rom[base + 31] << 8));
    if ((chk ^ inv) == 0xffff) score += 16;
    return score;
}

}  // namespace

Snes::Snes() : cpu_(kCpuClock) {
    cpu_.set_memory_handlers([this](uint32_t a) { return cpu_read(a); },
                             [this](uint32_t a, uint8_t v) { cpu_write(a, v); });
    apu_.set_memory_handlers([this](uint16_t a) { return aram_read(a); },
                             [this](uint16_t a, uint8_t v) { aram_write(a, v); });
}

bool Snes::init(const std::string& rom_path, std::string* error) {
    // The 64-byte SPC700 boot ROM: looked for next to the path given, since
    // the console itself has no BIOS the user would otherwise supply.
    std::vector<uint8_t> ipl;
    const std::filesystem::path base(rom_path);
    for (const std::filesystem::path& dir : {base.parent_path(), base}) {
        if (read_file((dir / "spc700.rom").string(), ipl) && ipl.size() >= ipl_.size()) break;
        ipl.clear();
    }
    static const uint8_t kDefaultIpl[64] = {
        0xcd,0xef,0xbd,0xe8,0x00,0xc6,0x1d,0xd0,0xfc,0x8f,0xaa,0xf4,0x8f,0xbb,0xf5,0x78,
        0xcc,0xf4,0xd0,0xfb,0x2f,0x19,0xeb,0xf4,0xd0,0xfc,0x7e,0xf4,0xd0,0x0b,0xe4,0xf5,
        0xcb,0xf4,0xd7,0x00,0xfc,0xd0,0xf3,0xab,0x01,0x10,0xef,0x7e,0xf4,0x10,0xeb,0xba,
        0xf6,0xda,0x00,0xba,0xf4,0xc4,0xf4,0xdd,0x5d,0xd0,0xdb,0x1f,0x00,0x00,0xc0,0xff
    };
    if (ipl.size() >= ipl_.size())
        std::copy(ipl.begin(), ipl.begin() + long(ipl_.size()), ipl_.begin());
    else
        std::copy(std::begin(kDefaultIpl), std::end(kDefaultIpl), ipl_.begin());

    // The console has no BIOS of its own, so a path here is taken as the
    // cartridge if it names one; otherwise the driver waits for load_media.
    std::vector<uint8_t> rom;
    if (read_file(rom_path, rom) && rom.size() >= 0x8000) {
        return load_media(rom_path, error);
    }
    reset();
    return true;
}

bool Snes::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> rom;
    if (!read_file(path, rom)) {
        if (error) *error = "cannot open cartridge: " + path;
        return false;
    }
    // A copier header is 512 bytes that do not belong to the ROM itself.
    if ((rom.size() % 0x8000) == 512) rom.erase(rom.begin(), rom.begin() + 512);
    if (rom.size() < 0x8000) {
        if (error) *error = "cartridge too small: " + path;
        return false;
    }
    hirom_ = score_header(rom, 0xffc0) > score_header(rom, 0x7fc0);
    const size_t hdr = hirom_ ? 0xffc0u : 0x7fc0u;
    size_t sram_bytes = 0;
    if (hdr + 24 < rom.size()) {
        const uint8_t sh = rom[hdr + 24];
        if (sh > 0 && sh < 16) sram_bytes = size_t(1) << (sh + 10);
    }
    if (hdr + 22 < rom.size() && (rom[hdr + 22] & 0x0f) >= 2 && sram_bytes == 0)
        sram_bytes = 2048;
    sram_.assign(std::max(sram_bytes, size_t(0x800)), 0);
    rom_ = std::move(rom);
    std::fprintf(stderr, "SNES: %zu bytes %s SRAM=%zu\n",
                 rom_.size(), hirom_ ? "HiROM" : "LoROM", sram_.size());
    reset();
    return true;
}

void Snes::reset() {
    wram_.fill(0);
    ppu_.reset();
    dma_ = {};
    nmitimen_ = 0;
    rdnmi_ = 0;
    hdmaen_ = 0;
    memsel_ = 0;
    wrio_ = 0xff;
    htime_ = vtime_ = 0x1ff;
    in_vblank_ = false;
    irq_pending_ = false;
    nmi_pending_ = false;
    line_ = 0;
    line_cycle_ = 0;
    wrmpya_ = 0xff;
    wrdiv_ = 0xffff;
    rddiv_ = rdmpy_ = 0;
    ophct_ = opvct_ = 0;
    ophct_high_ = opvct_high_ = false;
    counter_latched_ = false;
    wram_addr_ = 0;
    extra_clocks_ = 0;
    in_dma_ = false;
    pad1_ = pad1_shift_ = 0;
    joy_latch_ = 0;
    aram_.fill(0);
    apu_out_ = {};
    apu_in_ = {};
    apu_control_ = 0;
    timer_target_ = {};
    timer_stage_ = {};
    timer_out_ = {};
    timer_div_ = {};
    dsp_.fill(0);
    dsp_addr_ = 0;
    apu_cycles_ = 0;
    ipl_visible_ = true;
    apu_.reset();
    framebuffer_.fill(0xff000000u);
    open_bus_ = 0;
    cpu_.reset();
}

uint32_t Snes::map_rom(uint32_t addr) const {
    const uint8_t bank = uint8_t(addr >> 16);
    const uint16_t off = uint16_t(addr);
    if (rom_.empty()) return 0xffffffffu;
    if (hirom_) {
        // HiROM: banks $C0-$FF map linearly; $00-$3F mirror the upper half.
        const uint32_t a = ((bank & 0x3f) << 16) | off;
        return a % rom_.size();
    }
    // LoROM: the top 32 KB of every bank, packed back to back.
    const uint32_t a = (uint32_t(bank & 0x7f) << 15) | (off & 0x7fff);
    return a % rom_.size();
}

void Snes::account_access(uint32_t addr) {
    if (in_dma_) return;
    const uint8_t bank = uint8_t(addr >> 16);
    const uint16_t off = uint16_t(addr);
    const bool fast_rom = (bank & 0x80) && (memsel_ & 1);
    int clocks;
    if (!(bank & 0x40)) {
        if (off < 0x2000) clocks = 8;
        else if (off < 0x4000) clocks = 6;
        else if (off < 0x4200) clocks = 12;
        else if (off < 0x6000) clocks = 6;
        else if (off < 0x8000) clocks = 8;
        else clocks = fast_rom ? 6 : 8;
    } else {
        clocks = (bank == 0x7e || bank == 0x7f) ? 8 : (fast_rom ? 6 : 8);
    }
    extra_clocks_ += clocks - 6;
}

uint8_t Snes::cpu_read(uint32_t addr) {
    account_access(addr);
    const uint8_t bank = uint8_t(addr >> 16);
    const uint16_t off = uint16_t(addr);
    auto force_7000 = [](uint16_t o) { return (o & 0xfffe) == 0x7000; };

    if (bank == 0x7e || bank == 0x7f) {
        if (force_7000(off)) return 0x80;
        return wram_[((bank - 0x7e) << 16) | off];
    }
    if ((bank <= 0x3f) || (bank >= 0x80 && bank <= 0xbf)) {
        if (off < 0x2000) {
            if (force_7000(off)) return 0x80;
            return wram_[off];
        }
        if (off < 0x6000) return read_io(off);
        if (off < 0x8000) {
            if (force_7000(off)) return 0x80;
            if (!sram_.empty()) {
                const uint32_t s = (uint32_t(bank & 0x0f) << 13) | (off & 0x1fff);
                return sram_[s % sram_.size()];
            }
            return open_bus_;
        }
        const uint32_t a = map_rom(addr);
        return a < rom_.size() ? rom_[a] : open_bus_;
    }
    if (!sram_.empty() && bank >= 0x70 && bank <= 0x77) {
        if (force_7000(off)) return 0x80;
        const uint32_t s = (uint32_t(bank - 0x70) << 15) | off;
        return sram_[s % sram_.size()];
    }
    const uint32_t a = map_rom(addr);
    return a < rom_.size() ? rom_[a] : open_bus_;
}

void Snes::cpu_write(uint32_t addr, uint8_t value) {
    account_access(addr);
    const uint8_t bank = uint8_t(addr >> 16);
    const uint16_t off = uint16_t(addr);
    open_bus_ = value;

    if (bank == 0x7e || bank == 0x7f) {
        wram_[((bank - 0x7e) << 16) | off] = value;
        return;
    }
    if ((bank <= 0x3f) || (bank >= 0x80 && bank <= 0xbf)) {
        if (off < 0x2000) { wram_[off] = value; return; }
        if (off < 0x6000) { write_io(off, value); return; }
        if (off < 0x8000 && !sram_.empty()) {
            const uint32_t s = (uint32_t(bank & 0x0f) << 13) | (off & 0x1fff);
            sram_[s % sram_.size()] = value;
            return;
        }
    }
    if (!sram_.empty() && bank >= 0x70 && bank <= 0x77) {
        const uint32_t s = (uint32_t(bank - 0x70) << 15) | off;
        sram_[s % sram_.size()] = value;
        return;
    }
}

uint8_t Snes::apu_read(int port) { return apu_out_[size_t(port & 3)]; }

void Snes::apu_write(int port, uint8_t value) { apu_in_[size_t(port & 3)] = value; }

uint8_t Snes::aram_read(uint16_t addr) {
    if (addr >= 0xffc0 && ipl_visible_) return ipl_[addr - 0xffc0];
    if (addr >= 0x00f0 && addr <= 0x00ff) {
        switch (addr) {
            case 0xf2: return dsp_addr_;
            case 0xf3: return dsp_[dsp_addr_ & 0x7f];
            case 0xf4: case 0xf5: case 0xf6: case 0xf7:
                return apu_in_[addr - 0xf4];
            case 0xfd: case 0xfe: case 0xff: {
                // Reading a counter returns it and clears it.
                const size_t t = addr - 0xfd;
                const uint8_t v = timer_out_[t];
                timer_out_[t] = 0;
                return v;
            }
            default: return aram_[addr];
        }
    }
    return aram_[addr];
}

void Snes::aram_write(uint16_t addr, uint8_t value) {
    if (addr >= 0x00f0 && addr <= 0x00ff) {
        switch (addr) {
            case 0xf1:
                apu_control_ = value;
                // Bits 4 and 5 clear the mailbox in each direction; bit 7
                // keeps the boot ROM mapped.
                if (value & 0x10) { apu_in_[0] = apu_in_[1] = 0; }
                if (value & 0x20) { apu_in_[2] = apu_in_[3] = 0; }
                ipl_visible_ = (value & 0x80) != 0;
                for (int t = 0; t < 3; t++)
                    if (value & (1 << t)) { timer_stage_[size_t(t)] = 0; timer_out_[size_t(t)] = 0; }
                return;
            case 0xf2: dsp_addr_ = value; return;
            case 0xf3: dsp_[dsp_addr_ & 0x7f] = value; return;
            case 0xf4: case 0xf5: case 0xf6: case 0xf7:
                apu_out_[addr - 0xf4] = value; return;
            case 0xfa: case 0xfb: case 0xfc:
                timer_target_[addr - 0xfa] = value; return;
            default: break;
        }
    }
    aram_[addr] = value;
}

void Snes::tick_apu_timers(int cycles) {
    // Timers 0 and 1 tick at 8 kHz, timer 2 at 64 kHz, derived from the
    // sound CPU's 1.024 MHz clock.
    static const int kDivider[3] = {128, 128, 16};
    for (int t = 0; t < 3; t++) {
        if (!(apu_control_ & (1 << t))) continue;
        timer_div_[size_t(t)] += cycles;
        while (timer_div_[size_t(t)] >= kDivider[t]) {
            timer_div_[size_t(t)] -= kDivider[t];
            const uint8_t target = timer_target_[size_t(t)];
            if (++timer_stage_[size_t(t)] == (target ? target : 0)) {
                timer_stage_[size_t(t)] = 0;
                timer_out_[size_t(t)] = uint8_t((timer_out_[size_t(t)] + 1) & 0x0f);
            }
        }
    }
}

void Snes::run_apu(int main_cycles) {
    // The sound CPU runs at 1.024 MHz against the main CPU's 3.58 MHz.
    apu_cycles_ += main_cycles * 1024;
    while (apu_cycles_ >= 3580) {
        const int used = apu_.step();
        apu_cycles_ -= used * 3580;
        tick_apu_timers(used);
    }
}

int Snes::current_hdot() const {
    // 1364 master clocks per line, 4 per dot; the CPU cycle count assumes
    // 6 master clocks per cycle.
    return std::min(339, line_cycle_ * 6 / 4);
}

uint8_t Snes::read_io(uint16_t addr) {
    switch (addr) {
        case 0x2137:   // SLHV: latch the H/V counters
            ophct_ = uint16_t(current_hdot());
            opvct_ = uint16_t(line_);
            counter_latched_ = true;
            return open_bus_;
        case 0x213c: {
            const uint8_t v = ophct_high_ ? uint8_t((ophct_ >> 8) & 1) : uint8_t(ophct_);
            ophct_high_ = !ophct_high_;
            return v;
        }
        case 0x213d: {
            const uint8_t v = opvct_high_ ? uint8_t((opvct_ >> 8) & 1) : uint8_t(opvct_);
            opvct_high_ = !opvct_high_;
            return v;
        }
        case 0x213f: {   // STAT78: reading resets the counter flip-flops
            const uint8_t v = uint8_t((ppu_.read(addr) & 0xbf) | (counter_latched_ ? 0x40 : 0));
            ophct_high_ = opvct_high_ = false;
            counter_latched_ = false;
            return v;
        }
        default: break;
    }
    if (addr >= 0x2100 && addr <= 0x213f) return ppu_.read(addr);
    if (addr >= 0x2140 && addr <= 0x217f) return apu_read(addr & 3);
    switch (addr) {
        case 0x2180: {
            const uint8_t v = wram_[wram_addr_ & 0x1ffff];
            wram_addr_ = (wram_addr_ + 1) & 0x1ffff;
            return v;
        }
        case 0x4210: {   // RDNMI: reading the flag clears it
            const uint8_t v = uint8_t(rdnmi_ | 0x02);
            rdnmi_ &= uint8_t(~0x80);
            return v;
        }
        case 0x4211: {   // TIMEUP: reading acknowledges the timer interrupt
            const uint8_t v = uint8_t(irq_pending_ ? 0x80 : 0);
            irq_pending_ = false;
            cpu_.set_irq(IrqLine::Clear);
            return v;
        }
        case 0x4212: {   // HVBJOY: vblank, hblank and the auto-joypad busy bit
            const int dot = current_hdot();
            const bool hblank = dot < 22 || dot >= 274;
            const bool joy_busy = (nmitimen_ & 1) && line_ >= 225 && line_ <= 227;
            return uint8_t((in_vblank_ ? 0x80 : 0) | (hblank ? 0x40 : 0) | (joy_busy ? 0x01 : 0));
        }
        case 0x4214: return uint8_t(rddiv_);
        case 0x4215: return uint8_t(rddiv_ >> 8);
        case 0x4216: return uint8_t(rdmpy_);
        case 0x4217: return uint8_t(rdmpy_ >> 8);
        case 0x4213: return wrio_;                  // RDIO echoes back WRIO
        case 0x4218: return uint8_t(pad1_);         // JOY1L
        case 0x4219: return uint8_t(pad1_ >> 8);    // JOY1H
        case 0x421a: case 0x421b: case 0x421c:
        case 0x421d: case 0x421e: case 0x421f: return 0;
        case 0x4016: case 0x4017: {
            const uint8_t bit = uint8_t(pad1_shift_ & 1);
            pad1_shift_ >>= 1;
            return bit;
        }
        default:
            if (addr >= 0x4300 && addr <= 0x437f) {
                DmaChannel& c = dma_[(addr >> 4) & 7];
                switch (addr & 0x0f) {
                    case 0x0: return c.control;
                    case 0x1: return c.dest;
                    case 0x2: return uint8_t(c.src);
                    case 0x3: return uint8_t(c.src >> 8);
                    case 0x4: return uint8_t(c.src >> 16);
                    case 0x5: return uint8_t(c.count);
                    case 0x6: return uint8_t(c.count >> 8);
                    default: return open_bus_;
                }
            }
            return open_bus_;
    }
}

bool Snes::vram_open_after(int master_clocks) const {
    // Where the beam will be `master_clocks` from now (1364 per line).
    const int clocks = line_cycle_ * 6 + extra_clocks_ + master_clocks;
    const int line = (line_ + clocks / 1364) % kLinesTotal;
    return line > kVisibleLines;
}

void Snes::write_io(uint16_t addr, uint8_t value) {
    if (addr == 0x2118 || addr == 0x2119) ppu_.set_vram_open(in_dma_ ? dma_vram_open_ : in_vblank_);
    if (addr >= 0x2100 && addr <= 0x213f) { ppu_.write(addr, value); return; }
    if (addr >= 0x2140 && addr <= 0x217f) { apu_write(addr & 3, value); return; }
    switch (addr) {
        case 0x2180:
            wram_[wram_addr_ & 0x1ffff] = value;
            wram_addr_ = (wram_addr_ + 1) & 0x1ffff;
            return;
        case 0x2181: wram_addr_ = (wram_addr_ & 0x1ff00) | value; return;
        case 0x2182: wram_addr_ = (wram_addr_ & 0x100ff) | (uint32_t(value) << 8); return;
        case 0x2183: wram_addr_ = (wram_addr_ & 0x0ffff) | (uint32_t(value & 1) << 16); return;
        case 0x4016:
            // Writing bit 0 latches the pad; reads then shift it out.
            if ((joy_latch_ & 1) && !(value & 1)) pad1_shift_ = pad1_;
            joy_latch_ = value;
            return;
        case 0x4200:
            // Disabling both timer sources also drops a pending timer IRQ.
            if ((value & 0x30) == 0) {
                irq_pending_ = false;
                cpu_.set_irq(IrqLine::Clear);
            }
            // Enabling NMI during vblank with the flag still set fires it.
            if (!(nmitimen_ & 0x80) && (value & 0x80) && (rdnmi_ & 0x80)) cpu_.set_nmi(IrqLine::Pulse);
            nmitimen_ = value;
            return;
        case 0x4201:
            // A 1->0 transition on bit 7 latches the H/V counters.
            if ((wrio_ & 0x80) && !(value & 0x80)) {
                ophct_ = uint16_t(current_hdot());
                opvct_ = uint16_t(line_);
                counter_latched_ = true;
            }
            wrio_ = value;
            return;
        case 0x4202: wrmpya_ = value; return;
        case 0x4203:
            rdmpy_ = uint16_t(wrmpya_ * value);
            rddiv_ = value;   // the multiplier also lands in RDDIV
            return;
        case 0x4204: wrdiv_ = uint16_t((wrdiv_ & 0xff00) | value); return;
        case 0x4205: wrdiv_ = uint16_t((wrdiv_ & 0x00ff) | (value << 8)); return;
        case 0x4206:
            if (value == 0) {
                rddiv_ = 0xffff;
                rdmpy_ = wrdiv_;
            } else {
                rddiv_ = uint16_t(wrdiv_ / value);
                rdmpy_ = uint16_t(wrdiv_ % value);
            }
            return;
        case 0x4207: htime_ = uint16_t((htime_ & 0x100) | value); return;
        case 0x4208: htime_ = uint16_t((htime_ & 0x0ff) | ((value & 1) << 8)); return;
        case 0x4209: vtime_ = uint16_t((vtime_ & 0x100) | value); return;
        case 0x420a: vtime_ = uint16_t((vtime_ & 0x0ff) | ((value & 1) << 8)); return;
        case 0x420b: run_dma(value); return;
        case 0x420c:
            // Turning a channel on part-way through a frame starts it on the
            // next line rather than waiting for the next frame.
            if (value != hdmaen_) {
                hdmaen_ = value;
                run_hdma_init();
                return;
            }
            hdmaen_ = value;
            return;
        case 0x420d: memsel_ = value; return;
        default:
            if (addr >= 0x4300 && addr <= 0x437f) {
                DmaChannel& c = dma_[(addr >> 4) & 7];
                switch (addr & 0x0f) {
                    case 0x0: c.control = value; return;
                    case 0x1: c.dest = value; return;
                    case 0x2: c.src = (c.src & 0xffff00) | value; return;
                    case 0x3: c.src = (c.src & 0xff00ff) | (uint32_t(value) << 8); return;
                    case 0x4: c.src = (c.src & 0x00ffff) | (uint32_t(value) << 16); return;
                    case 0x5: c.count = uint16_t((c.count & 0xff00) | value); return;
                    case 0x6: c.count = uint16_t((c.count & 0x00ff) | (value << 8)); return;
                    case 0x8: c.table = uint16_t((c.table & 0xff00) | value); return;
                    case 0x9: c.table = uint16_t((c.table & 0x00ff) | (value << 8)); return;
                    case 0x7: c.indirect_bank = value; return;
                    case 0xa: c.line_count = value; return;
                    default: return;
                }
            }
            return;
    }
}

void Snes::run_dma(uint8_t channels) {
    // The transfer patterns say how many $21xx registers a unit covers and in
    // what order, which is what lets one channel fill VRAM through the pair
    // of data ports.
    static const uint8_t kPattern[8][4] = {
        {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
        {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    };
    static const int kPatternLen[8] = {1, 2, 2, 4, 4, 4, 2, 4};

    // DMA halts the CPU: 8 master clocks a byte plus 8 per channel.
    in_dma_ = true;
    for (int ch = 0; ch < 8; ch++) {
        if (!(channels & (1 << ch))) continue;
        DmaChannel& c = dma_[size_t(ch)];
        const int mode = c.control & 7;
        const bool to_cpu = (c.control & 0x80) != 0;
        const int bytes = int(c.count ? c.count : 0x10000);
        const int step = (c.control & 0x08) ? 0 : ((c.control & 0x10) ? -1 : 1);
        uint32_t count = c.count ? c.count : 0x10000;
        int unit = 0;
        int done = 0;
        while (count--) {
            const uint16_t reg = uint16_t(0x2100 + c.dest + kPattern[mode][unit]);
            // Long transfers run on into the picture, where VRAM drops writes.
            dma_vram_open_ = vram_open_after(8 * done++);
            if (to_cpu) {
                cpu_write(c.src, read_io(reg));
            } else {
                write_io(reg, cpu_read(c.src));
            }
            c.src = (c.src & 0xff0000) | uint16_t(uint16_t(c.src) + step);
            unit = (unit + 1) % kPatternLen[mode];
        }
        extra_clocks_ += 8 + 8 * bytes;
        c.count = 0;
    }
    in_dma_ = false;
}

void Snes::run_hdma_init() {
    for (int ch = 0; ch < 8; ch++) {
        if (!(hdmaen_ & (1 << ch))) continue;
        DmaChannel& c = dma_[size_t(ch)];
        c.table = uint16_t(c.src);
        c.line_count = 0;
        c.done = false;
        c.pending = false;
    }
}

void Snes::run_hdma_line() {
    if (hdmaen_) extra_clocks_ += 18;
    in_dma_ = true;
    struct Guard { bool& f; ~Guard() { f = false; } } guard{in_dma_};
    static const uint8_t kPattern[8][4] = {
        {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
        {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    };
    static const int kPatternLen[8] = {1, 2, 2, 4, 4, 4, 2, 4};

    for (int ch = 0; ch < 8; ch++) {
        if (!(hdmaen_ & (1 << ch))) continue;
        DmaChannel& c = dma_[size_t(ch)];
        if (c.done) continue;
        const uint32_t bank = c.src & 0xff0000;
        const int mode = c.control & 7;
        const bool indirect = (c.control & 0x40) != 0;

        if (c.line_count == 0) {
            // A header of zero ends the channel for this frame; otherwise the
            // low seven bits are a repeat count and bit 7 says whether the
            // transfer happens on every line or only the first.
            const uint8_t hdr = cpu_read(bank | c.table++);
            if (hdr == 0) { c.done = true; continue; }
            c.repeat = (hdr & 0x80) != 0;
            c.line_count = uint8_t(hdr & 0x7f);
            if (indirect) {
                const uint8_t lo = cpu_read(bank | c.table++);
                const uint8_t hi = cpu_read(bank | c.table++);
                c.indirect = uint16_t(lo | (hi << 8));
            }
            c.pending = true;   // always transfer on the first line of a block
        }

        if (c.pending || c.repeat) {
            const uint32_t addr = indirect ? ((uint32_t(c.indirect_bank) << 16) | c.indirect)
                                           : (bank | c.table);
            for (int i = 0; i < kPatternLen[mode]; i++) {
                write_io(uint16_t(0x2100 + c.dest + kPattern[mode][i]),
                         cpu_read(addr + uint32_t(i)));
            }
            extra_clocks_ += 8 + 8 * kPatternLen[mode];
            if (indirect) c.indirect = uint16_t(c.indirect + kPatternLen[mode]);
            else c.table = uint16_t(c.table + kPatternLen[mode]);
        }
        c.pending = false;
        c.line_count--;
    }
}

void Snes::raise_timer_irq() {
    irq_pending_ = true;
    cpu_.set_irq(IrqLine::Assert);   // held until $4211 is read
}

void Snes::run_cpu_line() {
    // H/V timer IRQ: $4200 bits 4-5 select H (every line at HTIME), V (at
    // VTIME, dot 0) or both. The line is split at the trigger point.
    const int mode = (nmitimen_ >> 4) & 3;
    int trigger = -1;
    if (mode == 1) trigger = htime_ <= 339 ? htime_ : -1;
    else if (mode == 2) trigger = line_ == vtime_ ? 0 : -1;
    else if (mode == 3) trigger = (line_ == vtime_ && htime_ <= 339) ? htime_ : -1;
    const int trigger_cycle = trigger < 0 ? -1 : trigger * 4 / 6;

    // line_cycle_ carries the previous line's overshoot.
    bool fired = false;
    while (line_cycle_ < kCyclesPerLine) {
        if (!fired && trigger_cycle >= 0 && line_cycle_ >= trigger_cycle) {
            raise_timer_irq();
            fired = true;
        }
        int slice = kCyclesPerLine - line_cycle_;
        if (!fired && trigger_cycle > line_cycle_) slice = std::min(slice, trigger_cycle - line_cycle_);
        int ran = cpu_.run(std::min(slice, 32));
        if (ran <= 0) break;
        ran += extra_clocks_ / 6;
        extra_clocks_ %= 6;
        // A long DMA can stall the CPU across whole frames; the stall is
        // paid off line by line rather than skipping NMIs.
        line_cycle_ += ran;
        run_apu(ran);
    }
    line_cycle_ -= kCyclesPerLine;
    if (line_cycle_ < 0) line_cycle_ = 0;
}

void Snes::run_frame() {
    // V=0 is not displayed; lines 1-224 are the picture (rows 0-223) and
    // vertical blank starts at V=225. HDMA is set up at the top of the frame
    // and transfers during the horizontal blank of lines 0-224, so what it
    // writes at the end of one line shows on the next.
    for (line_ = 0; line_ < kLinesTotal; line_++) {
        if (line_ == 0) {
            in_vblank_ = false;
            rdnmi_ &= uint8_t(~0x80);
            run_hdma_init();
        }
        if (line_ == kVisibleLines + 1) {
            in_vblank_ = true;
            ppu_.start_vblank();
            rdnmi_ |= 0x80;
            if (nmitimen_ & 0x80) cpu_.set_nmi(IrqLine::Pulse);
            if (nmitimen_ & 0x01) pad1_shift_ = pad1_;   // auto joypad read
        }
        if (line_ >= 1 && line_ <= kVisibleLines) {
            ppu_.render_line(line_, &framebuffer_[size_t(line_ - 1) * kWidth]);
        }
        run_cpu_line();
        if (line_ <= kVisibleLines) run_hdma_line();
    }
}

void Snes::set_inputs(const MachineInputs& in) {
    // Standard pad bit order, MSB first: B Y Select Start Up Down Left Right
    // A X L R.
    uint16_t v = 0;
    if (in.player1.button1) v |= 0x8000;  // B
    if (in.player1.button2) v |= 0x4000;  // Y
    if (in.player1.select)  v |= 0x2000;  // Select
    if (in.player1.start)   v |= 0x1000;  // Start
    if (in.player1.up)      v |= 0x0800;
    if (in.player1.down)    v |= 0x0400;
    if (in.player1.left)    v |= 0x0200;
    if (in.player1.right)   v |= 0x0100;
    if (in.player1.button3) v |= 0x0080;  // A
    if (in.player1.button4) v |= 0x0040;  // X
    pad1_ = v;
}

}  // namespace dsp
