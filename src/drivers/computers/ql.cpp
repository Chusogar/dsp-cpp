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

constexpr uint32_t kWinTrapPort = 0x1bf00;
constexpr uint32_t kExtRamBase = 0x40000;
constexpr uint32_t kExtRamSize = 0x80000;

constexpr int kErrBo = -5;
constexpr int kErrNf = -7;
constexpr int kErrEf = -10;
constexpr int kErrNi = -19;

constexpr uint32_t kFsAcces = 0x1c;
constexpr uint32_t kFsFilnr = 0x1e;
constexpr uint32_t kFsNblok = 0x20;
constexpr uint32_t kFsNbyte = 0x22;
constexpr uint32_t kFsEblok = 0x24;
constexpr uint32_t kFsEbyte = 0x26;
constexpr uint32_t kFsFname = 0x32;
constexpr uint32_t kFsMname = 0x16;

}  // namespace

SinclairQl::SinclairQl() : cpu_(kCpuClock), ipc_(kIpcClock, Mcs48::Chip::I8749) {
    ext_ram_.assign(kExtRamSize, 0);
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
    zx8302_.set_mdseld_callback([this](int state) { mdv1_.comms_in_w(state); });
    zx8302_.set_mdselck_callback([this](int state) {
        mdv2_.clk_w(state);
        mdv1_.clk_w(state);
        mdv2_.comms_in_w(mdv1_.comms_out());
        update_mdv_gap();
    });
    zx8302_.set_mdrdw_callback([this](int state) {
        mdv1_.read_write_w(state);
        mdv2_.read_write_w(state);
    });
    zx8302_.set_erase_callback([this](int state) {
        mdv1_.erase_w(state);
        mdv2_.erase_w(state);
    });
    mdv1_.set_tx_pop([this]() { return zx8302_.mdv_tx_pop(); });
    mdv2_.set_tx_pop([this]() { return zx8302_.mdv_tx_pop(); });
    zx8302_.set_mdv_burst_callback([this]() {
        for (int i = 0; i < 8; i++) tick_mdv_bits();
    });
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
    std::memcpy(rom_.data(), firmware.data(), std::min(firmware.size(), size_t(0xc000)));
    install_ql_win_rom(rom_.data() + 0xc000);

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
    mdv_acc_ = 0;
    mdv_stall_ = 0;
    mdv1_.reset();
    mdv2_.reset();
    std::fill(ext_ram_.begin(), ext_ram_.end(), uint8_t(0));
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

bool SinclairQl::load_media(const std::string& path, std::string* error) {
    if (is_ql_win_file(path)) {
        if (win_.loaded()) {
            if (error) *error = "a QXL/WIN volume is already mounted";
            return false;
        }
        if (!win_.load_file(path, error)) return false;
        if (!mdv1_.loaded()) {
            std::vector<uint8_t> qlay;
            if (make_ql_listing_cartridge("QXLBOOT", "boot", "100 LRUN win1_boot\n", qlay, nullptr)) {
                mdv1_.load_image(qlay, nullptr);
            }
        }
        return true;
    }
    if (!mdv1_.loaded()) return mdv1_.load_file(path, error);
    if (!mdv2_.loaded()) return mdv2_.load_file(path, error);
    if (error) *error = "both QL microdrives already have a cartridge";
    return false;
}

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
    if (address < 0x10000) return rom_[address];
    if (address >= 0x18000 && address <= 0x18003) return zx8302_.rtc_r(address & 3);
    if (address == 0x18020) return zx8302_.status_r();
    if (address == 0x18021) return zx8302_.irq_status_r();
    if (address >= 0x18022 && address <= 0x18023) {
        return zx8302_.mdv_track_r(address & 1);
    }
    if (address >= 0x20000 && address < 0x40000) return video_.ram_r(address - 0x20000);
    if (address >= kExtRamBase && address < kExtRamBase + ext_ram_.size()) {
        return ext_ram_[address - kExtRamBase];
    }
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
        return;
    }
    if (address >= kExtRamBase && address < kExtRamBase + ext_ram_.size()) {
        ext_ram_[address - kExtRamBase] = value;
    }
}

uint16_t SinclairQl::read_word(uint32_t address) {
    return uint16_t((uint16_t(read_byte(address)) << 8) | read_byte(address + 1));
}

void SinclairQl::write_word(uint32_t address, uint16_t value) {
    address &= 0xfffff;
    if (address == kWinTrapPort) {
        win_trap(value);
        return;
    }
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
    mdv_acc_ += int64_t(cycles) * int64_t(kMdvBitRate);
    while (mdv_acc_ >= cpu_clock) {
        mdv_acc_ -= cpu_clock;
        tick_mdv_bits();
    }
}

void SinclairQl::update_mdv_gap() {
    int gap = 1;
    if (mdv1_.selected() && mdv1_.loaded()) {
        gap = mdv1_.gap();
    } else if (mdv2_.selected() && mdv2_.loaded()) {
        gap = mdv2_.gap();
    } else if (mdv1_.selected() || mdv2_.selected()) {
        gap = 1;
    }
    zx8302_.mdv_gap_w(gap);
}

void SinclairQl::tick_mdv_bits() {
    const bool running = mdv1_.motor() || mdv2_.motor();
    if (!running) return;
    update_mdv_gap();
    // JS SuperBASIC only polls RX-full for ~20 instructions (~320 cycles)
    // between pairs. A free-running 100 kHz stream is 600 cycles/pair, so
    // hold the unread pair briefly. Do not skip to the next gap: the ROM
    // re-arms SEARCH between a block header and its data preamble.
    if (zx8302_.mdv_delivering() && zx8302_.mdv_rx_full()) {
        if (++mdv_stall_ < 80) return;
    } else {
        mdv_stall_ = 0;
    }
    const bool data = (mdv1_.selected() && mdv1_.gap() == 0) ||
                      (mdv2_.selected() && mdv2_.gap() == 0);
    if (data) {
        zx8302_.mdv_raw1_w(mdv1_.data1() | mdv2_.data1());
        zx8302_.mdv_raw2_w(mdv1_.data2() | mdv2_.data2());
    }
    mdv1_.tick_bit();
    mdv2_.tick_bit();
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

void SinclairQl::set_d0(int err) { cpu_.d[0].l = uint32_t(int32_t(err)); }

void SinclairQl::copy_to_guest(uint32_t dest, const uint8_t* src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) write_byte(dest + i, src[i]);
}

uint32_t SinclairQl::chan_pos() {
    const uint32_t ch = cpu_.a[0].l;
    return uint32_t(read_word(ch + kFsNblok)) * 512u + read_word(ch + kFsNbyte);
}

void SinclairQl::set_chan_pos(uint32_t pos) {
    const uint32_t ch = cpu_.a[0].l;
    write_word(ch + kFsNblok, uint16_t(pos / 512));
    write_word(ch + kFsNbyte, uint16_t(pos % 512));
}

std::string SinclairQl::chan_name() {
    const uint32_t ch = cpu_.a[0].l;
    uint16_t nlen = read_word(ch + kFsFname);
    if (nlen > 36) nlen = 36;
    std::string name;
    name.resize(nlen);
    for (uint16_t i = 0; i < nlen; i++) name[i] = char(read_byte(ch + kFsFname + 2 + i));
    return name;
}

const QlWinFile* SinclairQl::chan_file() {
    const uint16_t id = read_word(cpu_.a[0].l + kFsFilnr);
    if (id == 0) return &win_.directory();
    if (id > win_.files().size()) return nullptr;
    return &win_.files()[id - 1];
}

void SinclairQl::win_trap(uint16_t cmd) {
    if (cmd == 1) {
        win_open();
        return;
    }
    if (cmd == 2) win_io();
}

void SinclairQl::win_open() {
    if (!win_.loaded()) {
        set_d0(kErrNf);
        return;
    }
    const uint32_t ch = cpu_.a[0].l;
    const uint32_t pd = cpu_.a[1].l;
    const int mode = int(int8_t(read_byte(ch + kFsAcces)));
    if (mode < 0 || mode == 2 || mode == 3) {
        set_d0(kErrNi);
        return;
    }

    const std::string name = chan_name();
    const QlWinFile* file = nullptr;
    uint16_t id = 0;
    if (mode == 4 || name.empty()) {
        file = &win_.directory();
        id = 0;
    } else {
        file = win_.find(name);
        if (!file) {
            set_d0(kErrNf);
            return;
        }
        for (size_t i = 0; i < win_.files().size(); i++) {
            if (&win_.files()[i] == file) {
                id = uint16_t(i + 1);
                break;
            }
        }
    }

    const uint32_t len = file->logical_size();
    write_word(ch + kFsFilnr, id);
    write_word(ch + kFsNblok, 0);
    write_word(ch + kFsNbyte, 0x40);
    write_word(ch + kFsEblok, uint16_t(len / 512));
    write_word(ch + kFsEbyte, uint16_t(len % 512));

    const std::string& med = win_.medium_name();
    const uint16_t mlen = uint16_t(std::min<size_t>(med.size(), 10));
    write_word(pd + kFsMname, mlen);
    for (int i = 0; i < 10; i++) {
        const char c = i < int(med.size()) ? med[size_t(i)] : ' ';
        write_byte(pd + kFsMname + 2 + uint32_t(i), uint8_t(c));
    }
    set_d0(0);
}

void SinclairQl::win_io() {
    const uint8_t key = uint8_t(cpu_.d[0].l);
    const QlWinFile* file = chan_file();
    if (!file) {
        set_d0(kErrNf);
        return;
    }
    const uint32_t eof = file->logical_size();
    uint32_t pos = chan_pos();

    auto fetch = [&](uint32_t dest, uint32_t want, bool stop_nl) -> int {
        uint32_t n = 0;
        while (n < want && pos < eof) {
            const uint8_t b = file->byte_at(pos++);
            write_byte(dest++, b);
            n++;
            if (stop_nl && b == 0x0a) break;
        }
        set_chan_pos(pos);
        cpu_.a[1].l = dest;
        cpu_.d[1].l = n;
        if (n == 0) return kErrEf;
        return 0;
    };

    switch (key) {
        case 0x00:  // IO.PEND
            set_d0(pos >= eof ? kErrEf : 0);
            return;
        case 0x01: {  // IO.FBYTE
            if (pos >= eof) {
                set_d0(kErrEf);
                return;
            }
            cpu_.d[1].l = file->byte_at(pos++);
            set_chan_pos(pos);
            set_d0(0);
            return;
        }
        case 0x02:  // IO.FLINE
            set_d0(fetch(cpu_.a[1].l, cpu_.d[2].wl(), true));
            return;
        case 0x03:  // IO.FSTRG
            set_d0(fetch(cpu_.a[1].l, cpu_.d[2].wl(), false));
            return;
        case 0x40:  // FS.CHECK
        case 0x41:  // FS.FLUSH
            set_d0(0);
            return;
        case 0x42: {  // FS.POSAB
            int32_t data_pos = int32_t(cpu_.d[1].l);
            if (data_pos < 0) data_pos = 0;
            uint32_t abs = uint32_t(data_pos) + 64;
            int err = 0;
            if (abs > eof) {
                abs = eof;
                err = kErrEf;
            }
            set_chan_pos(abs);
            cpu_.d[1].l = abs > 64 ? abs - 64 : 0;
            set_d0(err);
            return;
        }
        case 0x43: {  // FS.POSRE
            const int32_t cur = int32_t(pos > 64 ? pos - 64 : 0);
            int32_t data_pos = cur + int32_t(cpu_.d[1].l);
            if (data_pos < 0) data_pos = 0;
            uint32_t abs = uint32_t(data_pos) + 64;
            int err = 0;
            if (abs > eof) {
                abs = eof;
                err = kErrEf;
            }
            set_chan_pos(abs);
            cpu_.d[1].l = abs > 64 ? abs - 64 : 0;
            set_d0(err);
            return;
        }
        case 0x44:
        case 0x45: {  // FS.MDINF
            std::string med = win_.medium_name();
            med.resize(10, ' ');
            copy_to_guest(cpu_.a[1].l, reinterpret_cast<const uint8_t*>(med.data()), 10);
            cpu_.a[1].l += 10;
            const uint16_t empty = uint16_t(std::min<uint32_t>(win_.empty_sectors(), 0xffff));
            const uint16_t total = uint16_t(std::min<uint32_t>(win_.total_sectors(), 0xffff));
            cpu_.d[1].l = (uint32_t(empty) << 16) | total;
            set_d0(0);
            return;
        }
        case 0x47: {  // FS.HEADR
            const uint16_t want = cpu_.d[2].wl();
            if (want < 14) {
                set_d0(kErrBo);
                return;
            }
            const uint16_t n = uint16_t(std::min<uint32_t>(want, 64));
            copy_to_guest(cpu_.a[1].l, file->header.data(), n);
            cpu_.a[1].l += n;
            cpu_.d[1].l = n;
            set_d0(0);
            return;
        }
        case 0x48: {  // FS.LOAD
            uint32_t want = cpu_.d[2].l;
            if (pos + want > eof) want = eof - pos;
            uint32_t dest = cpu_.a[1].l;
            for (uint32_t i = 0; i < want; i++) write_byte(dest + i, file->byte_at(pos + i));
            cpu_.a[1].l = dest + want;
            set_chan_pos(pos + want);
            set_d0(0);
            return;
        }
        default:
            set_d0(kErrNi);
            return;
    }
}

}  // namespace dsp
