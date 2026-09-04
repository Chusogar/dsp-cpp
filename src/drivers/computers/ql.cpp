#include "drivers/computers/ql.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kJsLow = {
    {"ql.js 0000.ic33|ql.js.0000.ic33|js.ic33", 0x8000, 0x0000, 0x1bbad3b8},
};
const std::vector<RomEntry> kJsHigh = {
    {"ql.js 8000.ic34|ql.js.8000.ic34|js.ic34", 0x4000, 0x8000, 0xc970800e},
};
const std::vector<RomEntry> kJmLow = {
    {"ql.jm 0000.ic33|ql.jm.0000.ic33|jm.ic33", 0x8000, 0x0000, 0x1f8e840a},
};
const std::vector<RomEntry> kJmHigh = {
    {"ql.jm 8000.ic34|ql.jm.8000.ic34|jm.ic34", 0x4000, 0x8000, 0x9168a2e9},
};
const std::vector<RomEntry> kMinerva = {
    {"minerva.rom", 0xc000, 0x0000, 0x930befe3},
};
const std::vector<RomEntry> kIpcRom = {
    {"ipc8049.ic24", 0x0800, 0x0000, 0x6a0d1f20},
};

// Host Key → (row, bit). Rows match MAME Y0..Y7, active-high.
struct KeyMap {
    Key key;
    int row;
    uint8_t bit;
};

const KeyMap kKeyMap[] = {
    {Key::F4, 0, 0x01},      {Key::F1, 0, 0x02},      {Key::Num5, 0, 0x04},
    {Key::F2, 0, 0x08},      {Key::F3, 0, 0x10},      {Key::F5, 0, 0x20},
    {Key::Num4, 0, 0x40},    {Key::Num7, 0, 0x80},
    {Key::Enter, 1, 0x01},   {Key::Left, 1, 0x02},    {Key::Up, 1, 0x04},
    {Key::Escape, 1, 0x08},  {Key::Right, 1, 0x10},   {Key::Slash, 1, 0x20},
    {Key::Space, 1, 0x40},   {Key::Down, 1, 0x80},
    {Key::Z, 2, 0x02},       {Key::Period, 2, 0x04},  {Key::C, 2, 0x08},
    {Key::B, 2, 0x10},       {Key::M, 2, 0x40},       {Key::Quote, 2, 0x80},
    {Key::CapsLock, 3, 0x02},{Key::K, 3, 0x04},       {Key::S, 3, 0x08},
    {Key::F, 3, 0x10},       {Key::Equals, 3, 0x20},  {Key::G, 3, 0x40},
    {Key::Semicolon, 3, 0x80},
    {Key::L, 4, 0x01},       {Key::Num3, 4, 0x02},    {Key::H, 4, 0x04},
    {Key::Num1, 4, 0x08},    {Key::A, 4, 0x10},       {Key::P, 4, 0x20},
    {Key::D, 4, 0x40},       {Key::J, 4, 0x80},
    {Key::Num9, 5, 0x01},    {Key::W, 5, 0x02},       {Key::I, 5, 0x04},
    {Key::Tab, 5, 0x08},     {Key::R, 5, 0x10},       {Key::Minus, 5, 0x20},
    {Key::Y, 5, 0x40},       {Key::O, 5, 0x80},
    {Key::Num8, 6, 0x01},    {Key::Num2, 6, 0x02},    {Key::Num6, 6, 0x04},
    {Key::Q, 6, 0x08},       {Key::E, 6, 0x10},       {Key::Num0, 6, 0x20},
    {Key::T, 6, 0x40},       {Key::U, 6, 0x80},
    {Key::LeftShift, 7, 0x01},{Key::RightShift, 7, 0x01},
    {Key::LeftCtrl, 7, 0x02},{Key::RightCtrl, 7, 0x02},
    {Key::Cbm, 7, 0x04},     {Key::X, 7, 0x08},       {Key::V, 7, 0x10},
    {Key::N, 7, 0x40},       {Key::Comma, 7, 0x80},
};

bool load_pair(RomLoader& loader, const std::vector<RomEntry>& low,
               const std::vector<RomEntry>& high, std::vector<uint8_t>& dest) {
    std::string ignored;
    dest.assign(0xc000, 0);
    if (!loader.load(low, dest, &ignored)) return false;
    return loader.load(high, dest, &ignored);
}

}  // namespace

SinclairQl::SinclairQl() : cpu_(kCpuClock), ipc_(kIpcClock, Mcs48::Chip::I8749) {
    cpu_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                             [this](uint32_t a, uint16_t v) { write_word(a, v); });
    cpu_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                           [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });

    ipc_.set_io_handlers([this](uint16_t p) { return ipc_port_in(p); },
                         [this](uint16_t p, uint8_t v) { ipc_port_out(p, v); });
    ipc_.set_external_handlers([](uint16_t) { return uint8_t(0xff); },
                               [this](uint16_t, uint8_t) {
                                   zx8302_.comctl_w(0);
                                   zx8302_.comctl_w(1);
                               });

    zx8302_.set_ipl1l_callback([this](int state) {
        zx8302_irq2_ = state;
        update_cpu_irqs();
    });
    zx8302_.set_comdata_callback([this](int state) { comdata_to_ipc_ = state; });
    zx8302_.set_baudx4_callback([this](int state) { baudx4_ = state; });
}

bool SinclairQl::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> firmware;
    std::string ignored;
    if (!load_pair(loader, kJsLow, kJsHigh, firmware) &&
        !load_pair(loader, kJmLow, kJmHigh, firmware)) {
        firmware.clear();
        if (!loader.load(kMinerva, firmware, &ignored) || firmware.size() < 0xc000) {
            if (error) *error = "QL ROM (JS/JM/Minerva) not found in " + rom_path;
            return false;
        }
    }
    rom_.fill(0);
    std::memcpy(rom_.data(), firmware.data(), std::min(firmware.size(), rom_.size()));

    std::vector<uint8_t> ipc;
    if (!loader.load(kIpcRom, ipc, &ignored) || ipc.size() < 0x800) {
        if (error) *error = "QL IPC ROM ipc8049.ic24 not found in " + rom_path;
        return false;
    }
    std::memcpy(ipc_.rom(), ipc.data(), 0x800);
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void SinclairQl::reset() {
    video_.reset();
    zx8302_.reset();
    keys_.fill(0);
    comdata_to_ipc_ = 1;
    ipc_ipl_ = 3;
    zx8302_irq2_ = 0;
    baudx4_ = 0;
    speaker_ = 0;
    keylatch_ = 0;
    ipc_cycle_acc_ = 0;
    baud_acc_ = 0;
    rtc_frames_ = 0;
    flash_frames_ = 0;
    audio_acc_ = 0;
    audio_.clear();
    ipc_.reset();
    // Let the 8749 reach the COMDATA wait / keyboard scan loop.
    run_ipc(8000);
    cpu_.reset();
    update_cpu_irqs();
}

void SinclairQl::run_frame() {
    const int cycles = int(double(kCpuClock) / kFps);
    cpu_.run(cycles);
    zx8302_.vsync_w(1);
    video_.render(framebuffer_.data());

    if (++rtc_frames_ >= int(kFps + 0.5)) {
        rtc_frames_ = 0;
        zx8302_.tick_rtc();
    }
    if (++flash_frames_ >= int(kFps / 2)) {
        flash_frames_ = 0;
        video_.tick_flash();
    }

    const int samples = int(double(kSampleRate) / kFps);
    const int16_t level = int16_t(speaker_ ? 4000 : 0);
    audio_.insert(audio_.end(), size_t(samples), level);
}

void SinclairQl::set_inputs(const MachineInputs& inputs) {
    inputs_ = inputs;
    apply_keyboard(inputs);
}

void SinclairQl::set_dip_switch(int, uint8_t) {}

void SinclairQl::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

void SinclairQl::apply_keyboard(const MachineInputs& inputs) {
    keys_.fill(0);
    for (const KeyMap& map : kKeyMap) {
        if (inputs.key(map.key)) keys_[size_t(map.row)] = uint8_t(keys_[size_t(map.row)] | map.bit);
    }
}

uint8_t SinclairQl::keyboard_rows() const {
    uint8_t data = 0;
    for (int row = 0; row < 8; row++) {
        if ((keylatch_ & (1 << row)) != 0) data = uint8_t(data | keys_[size_t(row)]);
    }
    return data;
}

uint8_t SinclairQl::read_byte(uint32_t address) {
    address &= 0xfffff;
    if (address < 0xc000) return rom_[address];
    if (address >= 0x18000 && address <= 0x18003) return zx8302_.rtc_r(address & 3);
    if (address == 0x18020) return zx8302_.status_r();
    if (address == 0x18021) return zx8302_.irq_status_r();
    if (address >= 0x18022 && address <= 0x18023) return zx8302_.mdv_track_r(address & 1);
    if (address >= 0x20000 && address < 0x40000) return video_.ram_r(address - 0x20000);
    return 0;
}

void SinclairQl::write_byte(uint32_t address, uint8_t value) {
    address &= 0xfffff;
    if (address >= 0x18000 && address <= 0x18001) {
        zx8302_.rtc_w(value);
        return;
    }
    if (address == 0x18002) {
        zx8302_.control_w(value);
        return;
    }
    if (address == 0x18003) {
        zx8302_.ipc_command_w(value);
        run_ipc(64);
        return;
    }
    if (address == 0x18020) {
        zx8302_.mdv_control_w(value);
        return;
    }
    if (address == 0x18021) {
        zx8302_.irq_acknowledge_w(value);
        return;
    }
    if (address == 0x18022) {
        zx8302_.data_w(value);
        return;
    }
    if (address == 0x18063) {
        video_.control_w(value);
        return;
    }
    if (address >= 0x20000 && address < 0x40000) {
        video_.ram_w(address - 0x20000, value);
    }
}

uint16_t SinclairQl::read_word(uint32_t address) {
    return uint16_t((uint16_t(read_byte(address)) << 8) | read_byte(address + 1));
}

void SinclairQl::write_word(uint32_t address, uint16_t value) {
    write_byte(address, uint8_t(value >> 8));
    write_byte(address + 1, uint8_t(value));
}

void SinclairQl::on_cpu_cycles(int cycles) {
    ipc_cycle_acc_ += int64_t(cycles) * int64_t(ipc_.clock());
    const int64_t cpu_clock = int64_t(kCpuClock);
    while (ipc_cycle_acc_ >= cpu_clock) {
        ipc_cycle_acc_ -= cpu_clock;
        run_ipc(1);
    }
    // Default 9600 baud × 4 until SuperBASIC programs the TCR.
    baud_acc_ += int64_t(cycles) * 38400;
    while (baud_acc_ >= cpu_clock) {
        baud_acc_ -= cpu_clock;
        zx8302_.tick_baudx4();
    }
}

void SinclairQl::run_ipc(int cycles) {
    if (cycles > 0) ipc_.run(cycles);
}

void SinclairQl::update_cpu_irqs() {
    const bool irq2 = zx8302_irq2_ != 0 || ipc_ipl_ == 2;
    const bool irq5 = ipc_ipl_ == 1;
    const bool irq7 = ipc_ipl_ == 0;
    cpu_.set_irq(2, irq2 ? IrqLine::Assert : IrqLine::Clear);
    cpu_.set_irq(5, irq5 ? IrqLine::Assert : IrqLine::Clear);
    cpu_.set_irq(7, irq7 ? IrqLine::Assert : IrqLine::Clear);
}

void SinclairQl::ipc_port_out(uint16_t port, uint8_t value) {
    if (port == MCS48_PORT_P1) {
        keylatch_ = value;
        return;
    }
    if (port != MCS48_PORT_P2) return;
    speaker_ = (value >> 1) & 1;
    const int ipl = ((value >> 2) & 1) << 1 | ((value >> 3) & 1);
    if (ipl != ipc_ipl_) {
        ipc_ipl_ = ipl;
        update_cpu_irqs();
    }
    zx8302_.comdata_w((value >> 7) & 1);
}

uint8_t SinclairQl::ipc_port_in(uint16_t port) const {
    if (port == MCS48_PORT_P2) return uint8_t(comdata_to_ipc_ << 7);
    if (port == MCS48_PORT_BUS) return keyboard_rows();
    if (port == MCS48_PORT_T1) return uint8_t(baudx4_ & 1);
    return 0xff;
}

}  // namespace dsp
