// Sony PlayStation (PSX) machine driver.
// Hardware devices ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "drivers/consoles/psx.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

constexpr uint32_t kRegionMask[8] = {
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,  // KUSEG
    0x7FFFFFFFu,                                         // KSEG0
    0x1FFFFFFFu,                                         // KSEG1
    0xFFFFFFFFu, 0xFFFFFFFFu,                            // KSEG2
};

bool ends_with_ci(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) !=
            std::tolower(static_cast<unsigned char>(ext[i]))) {
            return false;
        }
    }
    return true;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        if (error) *error = "empty " + path;
        return false;
    }
    out.resize(size_t(size));
    in.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

}  // namespace

Psx::Psx() {
    ram_.assign(2 * 1024 * 1024, 0);
    bios_.assign(512 * 1024, 0);
    wire_dma();
    cpu_.set_memory_handlers(
        [this](uint32_t a) { return read8(a); },
        [this](uint32_t a) { return read16(a); },
        [this](uint32_t a) { return read32(a); },
        [this](uint32_t a, uint8_t v) { write8(a, v); },
        [this](uint32_t a, uint16_t v) { write16(a, v); },
        [this](uint32_t a, uint32_t v) { write32(a, v); });
    cpu_.set_irq_pending([this]() { return irq_.pending(); });
}

void Psx::wire_dma() {
    dma_.set_ram_callbacks(
        [this](uint32_t addr) {
            addr &= 0x1FFFFCu;
            return uint32_t(ram_[addr]) | (uint32_t(ram_[addr + 1]) << 8) |
                   (uint32_t(ram_[addr + 2]) << 16) | (uint32_t(ram_[addr + 3]) << 24);
        },
        [this](uint32_t addr, uint32_t value) {
            addr &= 0x1FFFFCu;
            ram_[addr] = uint8_t(value);
            ram_[addr + 1] = uint8_t(value >> 8);
            ram_[addr + 2] = uint8_t(value >> 16);
            ram_[addr + 3] = uint8_t(value >> 24);
        });
    dma_.set_gpu_from_ram([this](const uint32_t* data, int words) { gpu_.process_dma(data, words); });
    dma_.set_gpu_to_ram([this](uint32_t addr, int words) {
        for (int i = 0; i < words; i++) {
            const uint32_t w = gpu_.load_gpuread();
            addr &= 0x1FFFFCu;
            ram_[addr] = uint8_t(w);
            ram_[addr + 1] = uint8_t(w >> 8);
            ram_[addr + 2] = uint8_t(w >> 16);
            ram_[addr + 3] = uint8_t(w >> 24);
            addr += 4;
        }
    });
    dma_.set_cdrom_to_ram([this](uint32_t addr, int words) {
        std::vector<uint32_t> buf;
        buf.resize(static_cast<size_t>(words));
        cdrom_.dma_read(buf.data(), words);
        for (int i = 0; i < words; i++) {
            const uint32_t w = buf[size_t(i)];
            const uint32_t a = (addr + uint32_t(i) * 4) & 0x1FFFFCu;
            ram_[a] = uint8_t(w);
            ram_[a + 1] = uint8_t(w >> 8);
            ram_[a + 2] = uint8_t(w >> 16);
            ram_[a + 3] = uint8_t(w >> 24);
        }
    });
    dma_.set_spu_from_ram([this](const uint32_t* data, int words) { spu_.dma_write(data, words); });
    dma_.set_spu_to_ram([this](uint32_t addr, int words) {
        std::vector<uint32_t> buf;
        buf.resize(static_cast<size_t>(words));
        spu_.dma_read(buf.data(), words);
        for (int i = 0; i < words; i++) {
            const uint32_t w = buf[size_t(i)];
            const uint32_t a = (addr + uint32_t(i) * 4) & 0x1FFFFCu;
            ram_[a] = uint8_t(w);
            ram_[a + 1] = uint8_t(w >> 8);
            ram_[a + 2] = uint8_t(w >> 16);
            ram_[a + 3] = uint8_t(w >> 24);
        }
    });
    // MDEC stubs: ignore / return zeros so games using MDEC do not crash the DMA path.
    dma_.set_mdec_from_ram([](const uint32_t*, int) {});
    dma_.set_mdec_to_ram([this](uint32_t addr, int words) {
        for (int i = 0; i < words; i++) {
            const uint32_t a = (addr + uint32_t(i) * 4) & 0x1FFFFCu;
            ram_[a] = ram_[a + 1] = ram_[a + 2] = ram_[a + 3] = 0;
        }
    });
}

uint32_t Psx::physical_addr(uint32_t address) {
    return address & kRegionMask[address >> 29];
}

bool Psx::load_bios(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<uint8_t> data;

    auto try_names = [&](RomLoader& loader) -> bool {
        static const char* kNames[] = {
            "scph1001.bin", "scph5501.bin", "scph7001.bin", "scph7502.bin",
            "SCPH1001.BIN", "SCPH5501.BIN", "SCPH7001.BIN", "SCPH7502.BIN",
        };
        for (const char* name : kNames) {
            if (loader.try_read(name, data) && data.size() >= 512 * 1024) {
                data.resize(512 * 1024);
                return true;
            }
        }
        // Any scph*.bin
        for (const std::string& name : loader.filenames()) {
            if (name.size() >= 4 && name.find("scph") != std::string::npos &&
                ends_with_ci(name, ".bin") && loader.try_read(name, data) &&
                data.size() >= 512 * 1024) {
                data.resize(512 * 1024);
                return true;
            }
        }
        return false;
    };

    if (fs::is_regular_file(rom_path, ec)) {
        if (ends_with_ci(rom_path, ".bin") || ends_with_ci(rom_path, ".rom")) {
            if (!read_file(rom_path, data, error)) return false;
            if (data.size() != 512 * 1024) {
                // Might be a zip misnamed; fall through.
                if (data.size() >= 4 && data[0] == 'P' && data[1] == 'K') {
                    // zip
                } else if (data.size() >= 512 * 1024) {
                    data.resize(512 * 1024);
                    bios_ = std::move(data);
                    return true;
                } else {
                    if (error) *error = "BIOS must be 512 KiB";
                    return false;
                }
            } else {
                bios_ = std::move(data);
                return true;
            }
        }
        RomLoader loader;
        if (!loader.open(rom_path, error)) return false;
        if (!try_names(loader)) {
            // First 512KiB file in archive.
            if (!loader.load_first_file(data, error)) return false;
            if (data.size() < 512 * 1024) {
                if (error) *error = "no 512 KiB BIOS found in " + rom_path;
                return false;
            }
            data.resize(512 * 1024);
        }
        bios_ = std::move(data);
        return true;
    }

    if (fs::is_directory(rom_path, ec)) {
        RomLoader loader;
        if (!loader.open(rom_path, error)) return false;
        if (!try_names(loader)) {
            if (error) *error = "no scph*.bin BIOS in " + rom_path;
            return false;
        }
        bios_ = std::move(data);
        return true;
    }

    if (error) *error = "BIOS path not found: " + rom_path;
    return false;
}

bool Psx::init(const std::string& rom_path, std::string* error) {
    if (!load_bios(rom_path, error)) return false;
    reset();
    return true;
}

bool Psx::load_media(const std::string& path, std::string* error) {
    if (!cdrom_.load_disc(path, error)) return false;
    return true;
}

void Psx::reset() {
    std::fill(ram_.begin(), ram_.end(), 0);
    scratch_.fill(0);
    mem_ctrl1_.fill(0);
    mem_ctrl2_.fill(0);
    sio_.fill(0);
    cache_control_ = 0;
    irq_.reset();
    dma_.reset();
    timers_.reset();
    gpu_.reset();
    cdrom_.reset();
    joypad_.reset();
    spu_.reset();
    wire_dma();
    cpu_.reset();
    screen_w_ = 320;
    screen_h_ = 240;
    framebuffer_.fill(0xFF000000u);
    audio_pending_.clear();
}

void Psx::set_inputs(const MachineInputs& inputs) {
    uint16_t buttons = 0xFFFF;
    const auto& p = inputs.player1;
    auto press = [&](PsxPadButton bit, bool down) {
        if (down) buttons = uint16_t(buttons & ~uint16_t(bit));
    };
    press(kPsxPadUp, p.up);
    press(kPsxPadDown, p.down);
    press(kPsxPadLeft, p.left);
    press(kPsxPadRight, p.right);
    press(kPsxPadCross, p.button1);     // X
    press(kPsxPadCircle, p.button2);    // O
    press(kPsxPadSquare, p.button3);
    press(kPsxPadTriangle, p.button4);
    press(kPsxPadStart, p.start);
    press(kPsxPadSelect, p.select);
    joypad_.set_buttons(buttons);
}

void Psx::tick_devices(int cycles) {
    if (gpu_.tick(cycles)) irq_.raise(kPsxIrqVblank);
    if (cdrom_.tick(cycles)) irq_.raise(kPsxIrqCdrom);
    if (dma_.tick()) irq_.raise(kPsxIrqDma);
    timers_.sync_gpu(gpu_.blanks_and_dot());
    if (timers_.tick(0, cycles)) irq_.raise(kPsxIrqTmr0);
    if (timers_.tick(1, cycles)) irq_.raise(kPsxIrqTmr1);
    if (timers_.tick(2, cycles)) irq_.raise(kPsxIrqTmr2);
    if (joypad_.tick()) irq_.raise(kPsxIrqController);
    if (spu_.tick(cycles)) irq_.raise(kPsxIrqSpu);
}

void Psx::run_frame() {
    // Match ProjectPSX RunFrame underclock scheme.
    constexpr int kSyncLoops = (kCyclesPerFrame / (kSyncCycles * kMipsUnderclock)) + 1;
    int sync = 0;
    for (int i = 0; i < kSyncLoops; i++) {
        while (sync < kSyncCycles) {
            sync += cpu_.run(1);
        }
        sync -= kSyncCycles;
        tick_devices(kSyncCycles * kMipsUnderclock + 1);
        cpu_.handle_interrupts();
    }

    screen_w_ = gpu_.display_width();
    screen_h_ = gpu_.display_height();
    if (screen_w_ <= 0) screen_w_ = 320;
    if (screen_h_ <= 0) screen_h_ = 240;
    screen_w_ = std::min(screen_w_, kMaxFbW);
    screen_h_ = std::min(screen_h_, kMaxFbH);
    gpu_.blit_display(framebuffer_.data(), screen_w_, screen_h_);

    const int samples = PsxSpu::kSampleRate / 60;
    spu_.drain_silence(audio_pending_, samples);
}

void Psx::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_pending_);
    audio_pending_.clear();
}

uint8_t Psx::read8(uint32_t address) {
    const uint32_t addr = physical_addr(address);
    if (addr < 0x1F000000u) {
        return ram_[addr & 0x1FFFFFu];
    }
    if (addr < 0x1F800000u) {
        return 0xFF;  // expansion
    }
    if (addr < 0x1F800400u) {
        return scratch_[addr & 0x3FFu];
    }
    if (addr >= 0x1F801000u && addr < 0x1F802000u) {
        return io_read8(addr);
    }
    if (addr >= 0x1FC00000u && addr < 0x1FC80000u) {
        return bios_[addr & 0x7FFFFu];
    }
    return 0xFF;
}

uint16_t Psx::read16(uint32_t address) {
    const uint32_t addr = physical_addr(address);
    if (addr < 0x1F000000u) {
        const uint32_t a = addr & 0x1FFFFEu;
        return uint16_t(ram_[a] | (ram_[a + 1] << 8));
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        const uint32_t a = addr & 0x3FEu;
        return uint16_t(scratch_[a] | (scratch_[a + 1] << 8));
    }
    if (addr >= 0x1F801000u && addr < 0x1F802000u) {
        return io_read16(addr);
    }
    if (addr >= 0x1FC00000u && addr < 0x1FC80000u) {
        const uint32_t a = addr & 0x7FFFEu;
        return uint16_t(bios_[a] | (bios_[a + 1] << 8));
    }
    return 0xFFFF;
}

uint32_t Psx::read32(uint32_t address) {
    const uint32_t addr = physical_addr(address);
    if (addr < 0x1F000000u) {
        const uint32_t a = addr & 0x1FFFFCu;
        return uint32_t(ram_[a]) | (uint32_t(ram_[a + 1]) << 8) |
               (uint32_t(ram_[a + 2]) << 16) | (uint32_t(ram_[a + 3]) << 24);
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        const uint32_t a = addr & 0x3FCu;
        return uint32_t(scratch_[a]) | (uint32_t(scratch_[a + 1]) << 8) |
               (uint32_t(scratch_[a + 2]) << 16) | (uint32_t(scratch_[a + 3]) << 24);
    }
    if (addr >= 0x1F801000u && addr < 0x1F804000u) {
        return io_read32(addr);
    }
    if (addr >= 0x1FC00000u && addr < 0x1FC80000u) {
        const uint32_t a = addr & 0x7FFFCu;
        return uint32_t(bios_[a]) | (uint32_t(bios_[a + 1]) << 8) |
               (uint32_t(bios_[a + 2]) << 16) | (uint32_t(bios_[a + 3]) << 24);
    }
    if (addr == 0xFFFE0130u) return cache_control_;
    return 0xFFFFFFFFu;
}

void Psx::write8(uint32_t address, uint8_t value) {
    const uint32_t addr = physical_addr(address);
    if (addr < 0x1F000000u) {
        ram_[addr & 0x1FFFFFu] = value;
        return;
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        scratch_[addr & 0x3FFu] = value;
        return;
    }
    if (addr >= 0x1F801000u && addr < 0x1F802000u) {
        io_write8(addr, value);
        return;
    }
}

void Psx::write16(uint32_t address, uint16_t value) {
    const uint32_t addr = physical_addr(address);
    if (addr < 0x1F000000u) {
        const uint32_t a = addr & 0x1FFFFEu;
        ram_[a] = uint8_t(value);
        ram_[a + 1] = uint8_t(value >> 8);
        return;
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        const uint32_t a = addr & 0x3FEu;
        scratch_[a] = uint8_t(value);
        scratch_[a + 1] = uint8_t(value >> 8);
        return;
    }
    if (addr >= 0x1F801000u && addr < 0x1F802000u) {
        io_write16(addr, value);
        return;
    }
    if (addr == 0xFFFE0130u) {
        cache_control_ = value;
        return;
    }
}

void Psx::write32(uint32_t address, uint32_t value) {
    const uint32_t addr = physical_addr(address);
    if (addr < 0x1F000000u) {
        const uint32_t a = addr & 0x1FFFFCu;
        ram_[a] = uint8_t(value);
        ram_[a + 1] = uint8_t(value >> 8);
        ram_[a + 2] = uint8_t(value >> 16);
        ram_[a + 3] = uint8_t(value >> 24);
        return;
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        const uint32_t a = addr & 0x3FCu;
        scratch_[a] = uint8_t(value);
        scratch_[a + 1] = uint8_t(value >> 8);
        scratch_[a + 2] = uint8_t(value >> 16);
        scratch_[a + 3] = uint8_t(value >> 24);
        return;
    }
    if (addr >= 0x1F801000u && addr < 0x1F804000u) {
        io_write32(addr, value);
        return;
    }
    if (addr == 0xFFFE0130u) {
        cache_control_ = value;
        return;
    }
}

uint32_t Psx::io_read32(uint32_t addr) {
    if (addr < 0x1F801040u) {
        const uint32_t o = addr & 0x3Cu;
        return uint32_t(mem_ctrl1_[o]) | (uint32_t(mem_ctrl1_[o + 1]) << 8) |
               (uint32_t(mem_ctrl1_[o + 2]) << 16) | (uint32_t(mem_ctrl1_[o + 3]) << 24);
    }
    if (addr < 0x1F801050u) return joypad_.load(addr);
    if (addr < 0x1F801060u) {
        if (addr == 0x1F801054u) return 0x00000805u;  // SIO_STAT stub
        const uint32_t o = addr & 0xCu;
        return uint32_t(sio_[o]) | (uint32_t(sio_[o + 1]) << 8) |
               (uint32_t(sio_[o + 2]) << 16) | (uint32_t(sio_[o + 3]) << 24);
    }
    if (addr < 0x1F801070u) {
        const uint32_t o = addr & 0xCu;
        return uint32_t(mem_ctrl2_[o]) | (uint32_t(mem_ctrl2_[o + 1]) << 8) |
               (uint32_t(mem_ctrl2_[o + 2]) << 16) | (uint32_t(mem_ctrl2_[o + 3]) << 24);
    }
    if (addr < 0x1F801080u) return irq_.load(addr);
    if (addr < 0x1F801100u) return dma_.load(addr);
    if (addr < 0x1F801140u) return timers_.load(addr);
    if (addr <= 0x1F801803u) return cdrom_.load(addr);
    if (addr == 0x1F801810u) return gpu_.load_gpuread();
    if (addr == 0x1F801814u) return gpu_.load_gpustat();
    if (addr == 0x1F801820u) return 0;  // MDEC data
    if (addr == 0x1F801824u) return 0x80000000u;  // MDEC status (not busy)
    if (addr >= 0x1F801C00u && addr < 0x1F802000u) return spu_.load32(addr);
    return 0xFFFFFFFFu;
}

void Psx::io_write32(uint32_t addr, uint32_t value) {
    if (addr < 0x1F801040u) {
        const uint32_t o = addr & 0x3Cu;
        mem_ctrl1_[o] = uint8_t(value);
        mem_ctrl1_[o + 1] = uint8_t(value >> 8);
        mem_ctrl1_[o + 2] = uint8_t(value >> 16);
        mem_ctrl1_[o + 3] = uint8_t(value >> 24);
        return;
    }
    if (addr < 0x1F801050u) {
        joypad_.write(addr, value);
        return;
    }
    if (addr < 0x1F801060u) {
        const uint32_t o = addr & 0xCu;
        sio_[o] = uint8_t(value);
        sio_[o + 1] = uint8_t(value >> 8);
        sio_[o + 2] = uint8_t(value >> 16);
        sio_[o + 3] = uint8_t(value >> 24);
        return;
    }
    if (addr < 0x1F801070u) {
        const uint32_t o = addr & 0xCu;
        mem_ctrl2_[o] = uint8_t(value);
        mem_ctrl2_[o + 1] = uint8_t(value >> 8);
        mem_ctrl2_[o + 2] = uint8_t(value >> 16);
        mem_ctrl2_[o + 3] = uint8_t(value >> 24);
        return;
    }
    if (addr < 0x1F801080u) {
        irq_.write(addr, value);
        return;
    }
    if (addr < 0x1F801100u) {
        dma_.write(addr, value);
        return;
    }
    if (addr < 0x1F801140u) {
        timers_.write(addr, value);
        return;
    }
    if (addr < 0x1F801810u) {
        cdrom_.write(addr, value);
        return;
    }
    if (addr < 0x1F801820u) {
        gpu_.write(addr, value);
        return;
    }
    if (addr < 0x1F801830u) {
        // MDEC stub
        return;
    }
    if (addr >= 0x1F801C00u && addr < 0x1F802000u) {
        spu_.write32(addr, value);
        return;
    }
}

uint16_t Psx::io_read16(uint32_t addr) {
    if (addr >= 0x1F801C00u && addr < 0x1F802000u) return spu_.read16(addr);
    if (addr < 0x1F801050u && addr >= 0x1F801040u) return uint16_t(joypad_.load(addr));
    if (addr < 0x1F801080u && addr >= 0x1F801070u) return uint16_t(irq_.load(addr));
    if (addr < 0x1F801100u && addr >= 0x1F801080u) return uint16_t(dma_.load(addr));
    if (addr < 0x1F801140u && addr >= 0x1F801100u) return uint16_t(timers_.load(addr));
    return uint16_t(io_read32(addr & ~3u) >> ((addr & 2u) * 8));
}

void Psx::io_write16(uint32_t addr, uint16_t value) {
    if (addr >= 0x1F801C00u && addr < 0x1F802000u) {
        spu_.write16(addr, value);
        return;
    }
    if (addr < 0x1F801050u && addr >= 0x1F801040u) {
        joypad_.write(addr, value);
        return;
    }
    if (addr < 0x1F801080u && addr >= 0x1F801070u) {
        irq_.write(addr, value);
        return;
    }
    if (addr < 0x1F801100u && addr >= 0x1F801080u) {
        dma_.write(addr, value);
        return;
    }
    if (addr < 0x1F801140u && addr >= 0x1F801100u) {
        timers_.write(addr, value);
        return;
    }
    // Merge into 32-bit write for other regs.
    const uint32_t aligned = addr & ~3u;
    uint32_t cur = io_read32(aligned);
    if (addr & 2u) cur = (cur & 0x0000FFFFu) | (uint32_t(value) << 16);
    else cur = (cur & 0xFFFF0000u) | value;
    io_write32(aligned, cur);
}

uint8_t Psx::io_read8(uint32_t addr) {
    if (addr >= 0x1F801800u && addr <= 0x1F801803u) return uint8_t(cdrom_.load(addr));
    if (addr >= 0x1F801040u && addr < 0x1F801050u) return uint8_t(joypad_.load(addr));
    return uint8_t(io_read32(addr & ~3u) >> ((addr & 3u) * 8));
}

void Psx::io_write8(uint32_t addr, uint8_t value) {
    if (addr >= 0x1F801800u && addr <= 0x1F801803u) {
        cdrom_.write(addr, value);
        return;
    }
    if (addr >= 0x1F801040u && addr < 0x1F801050u) {
        joypad_.write(addr, value);
        return;
    }
    const uint32_t aligned = addr & ~3u;
    const int shift = int(addr & 3u) * 8;
    uint32_t cur = io_read32(aligned);
    cur = (cur & ~(0xFFu << shift)) | (uint32_t(value) << shift);
    io_write32(aligned, cur);
}

}  // namespace dsp
