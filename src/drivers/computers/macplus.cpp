#include "drivers/computers/macplus.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "core/rom_loader.h"
#include "machine/mac_dcmp.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kPlusEvenV3 = {{"342-0341-c.u6d", 0x10000, 0, 0xf69697e6}};
const std::vector<RomEntry> kPlusOddV3 = {{"342-0342-b.u8d", 0x10000, 0, 0x49f25913}};
const std::vector<RomEntry> kPlusEvenV2 = {{"342-0341-b.u6d", 0x10000, 0, 0x65341487}};
const std::vector<RomEntry> kPlusOddV2 = {{"342-0342-a.u8d", 0x10000, 0, 0xfb766270}};
const std::vector<RomEntry> kPlusEvenV1 = {{"342-0341-a.u6d", 0x10000, 0, 0x5095fe39}};
const std::vector<RomEntry> kPlusOddV1 = {{"342-0342-a.u8d", 0x10000, 0, 0xfb766270}};

bool interleave_rom(const std::vector<uint8_t>& even, const std::vector<uint8_t>& odd,
                    std::vector<uint8_t>& dest) {
    if (even.size() < 0x10000 || odd.size() < 0x10000) return false;
    dest.assign(MacPlus::kRomSize, 0xff);
    for (uint32_t i = 0; i < 0x10000; i++) {
        dest[i * 2] = even[i];
        dest[i * 2 + 1] = odd[i];
    }
    return true;
}

}  // namespace

MacPlus::MacPlus() : cpu_(kCpuClock), via_(kCpuClock / 10) {
    ram_.assign(kRamSize, 0);
    rom_.assign(kRomSize, 0xff);
    cpu_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                             [this](uint32_t a, uint16_t v) { write_word(a, v); });
    cpu_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                           [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cpu_cycles(c); });
    via_.set_port_a([this]() { return via_pa_r(); }, [this](uint8_t v) { via_pa_w(v); });
    via_.set_port_b([this]() { return via_pb_r(); }, [this](uint8_t v) { via_pb_w(v); });
    via_.set_irq_callback([this](IrqLine line) {
        via_irq_ = line != IrqLine::Clear;
        update_irqs();
    });
}

bool MacPlus::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> even, odd;
    std::string ignored;
    even.assign(0x10000, 0xff);
    odd.assign(0x10000, 0xff);
    bool ok = false;
    if (loader.load(kPlusEvenV3, even, &ignored) && loader.load(kPlusOddV3, odd, &ignored))
        ok = true;
    else if (loader.load(kPlusEvenV2, even, &ignored) && loader.load(kPlusOddV2, odd, &ignored))
        ok = true;
    else if (loader.load(kPlusEvenV1, even, &ignored) && loader.load(kPlusOddV1, odd, error))
        ok = true;
    if (!ok || !interleave_rom(even, odd, rom_)) {
        if (error && error->empty()) *error = "Macintosh Plus ROM not found in " + rom_path;
        return false;
    }
    find_start_manager_mountvol();
    patch_rom_startboot();
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void MacPlus::find_start_manager_mountvol() {
    // 128K Start Manager: MOVEA.L A7,A0 / MOVE.W BootDrive,$16(A0) / _MountVol
    static const uint8_t kPat[] = {0x20, 0x4f, 0x31, 0x78, 0x02, 0x10, 0x00, 0x16, 0xa0, 0x0f};
    mount_vol_pc_ = 0;
    for (size_t i = 0; i + sizeof(kPat) <= rom_.size(); ++i) {
        if (std::equal(std::begin(kPat), std::end(kPat), rom_.begin() + static_cast<std::ptrdiff_t>(i))) {
            mount_vol_pc_ = 0x400000u + uint32_t(i) + 8u;
            break;
        }
    }
}

void MacPlus::patch_rom_startboot() {
    // 128K header +$A is StartBoot: BRA to the cold memory test. System 7's
    // 'boot' resource writes the boot blocks and JMP ROMBase+$A, expecting
    // StartBoot to take over. On a Plus that BRA wipes RAM. Point +$A at
    // the Start Manager tail ($400A90) so those JMPs keep loading System
    // and the Finder instead of cold-booting. Later hits become RTS once
    // CurApName is Finder so the tail is not re-entered on every write.
    if (rom_.size() < 0x10 || rom_[0x0a] != 0x60 || rom_[0x0b] != 0x00) return;
    if (rom_[0x0c] != 0x00 || rom_[0x0d] != 0x56) return;  // v3 BRA $400062
    constexpr uint32_t kStartMgr = 0x400a90;
    const int32_t disp = int32_t(kStartMgr - 0x40000cu);
    rom_[0x0c] = uint8_t(disp >> 8);
    rom_[0x0d] = uint8_t(disp);
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom_.size(); i += 2)
        sum += (uint32_t(rom_[i]) << 8) | rom_[i + 1];
    rom_[0] = uint8_t(sum >> 24);
    rom_[1] = uint8_t(sum >> 16);
    rom_[2] = uint8_t(sum >> 8);
    rom_[3] = uint8_t(sum);
}

void MacPlus::launch_finder_from_rom_a() {
    // ROM+$A is the Start Manager tail. Re-entering it after the Finder is
    // named (System 7's 'boot' stub JMPs here on every boot-block write)
    // would smash A5. Bounce to the tail RTS at $400BB8 instead.
    if (ram_at(0x0910) != 6 || ram_at(0x0911) != 'F') return;
    finder_launch_ = true;
    cpu_.pc_.l = 0x00400bb8;
}

void MacPlus::redirect_launch_to_boot2() {
    // PCE never hits 128K _Launch: System 7's 'boot' id 2 (Process Manager)
    // starts the Finder. The Plus ROM JSRs boot+$2 too early for that, so we
    // take over here — after System is open — and GetResource the same
    // resource. Copy the Launch PB off the stack first; GetResource would
    // smash it. Do not UseResFile(SysMap): a zero SysMap hides 'boot'.
    boot2_tried_ = true;
    const uint32_t frame = cpu_.a[7].l;
    if (frame + 6 <= kRamSize) cpu_.a[7].l = frame + 6;
    uint32_t top = read_long(0x010c) & 0xffffffu;
    if (top < 0x100 || top > kRamSize) top = 0x003fa700;
    top = (top - 0x80) & ~1u;
    write_long(0x010c, top);
    const uint32_t pb = top;
    const uint32_t src = launch_a0_ & 0xffffffu;
    for (uint32_t i = 0; i < 32 && src + i < kRamSize; i++) write_byte(pb + i, read_byte(src + i));
    const uint32_t stub = top + 32;
    static const uint8_t kStub[] = {
        0x2f, 0x3c, 0x62, 0x6f, 0x6f, 0x74,  // MOVE.L #'boot',-(A7)
        0x3f, 0x3c, 0x00, 0x02,              // MOVE.W #2,-(A7)
        0xa9, 0xa0,                          // _GetResource
        0x20, 0x1f,                          // MOVE.L (A7)+,D0
        0x67, 0x08,                          // BEQ fail
        0x20, 0x40,                          // MOVEA.L D0,A0
        0x20, 0x50,                          // MOVEA.L (A0),A0
        0x4e, 0xd0,                          // JMP (A0)
        0x4e, 0x71,                          // NOP
        0x20, 0x7c, 0x00, 0x00, 0x00, 0x00,  // fail: MOVEA.L #pb,A0
        0xa9, 0xf2,                          // _Launch
        0x70, 0x29,                          // MOVEQ #41,D0
        0xa9, 0xc9,                          // _SysError
    };
    for (size_t i = 0; i < sizeof(kStub); ++i) write_byte(stub + uint32_t(i), kStub[i]);
    write_long(stub + 26, pb);
    cpu_.pc_.l = stub;
}

uint32_t MacPlus::read_long(uint32_t address) {
    return (uint32_t(read_byte(address)) << 24) | (uint32_t(read_byte(address + 1)) << 16) |
           (uint32_t(read_byte(address + 2)) << 8) | read_byte(address + 3);
}

void MacPlus::write_long(uint32_t address, uint32_t value) {
    write_byte(address, uint8_t(value >> 24));
    write_byte(address + 1, uint8_t(value >> 16));
    write_byte(address + 2, uint8_t(value >> 8));
    write_byte(address + 3, uint8_t(value));
}

bool MacPlus::maybe_decompress_ptr(uint32_t ptr, uint32_t handle) {
    ptr &= 0xffffffu;
    if (ptr < 0x100 || ptr + 18 > kRamSize) return false;
    if (read_long(ptr) != 0xa89f6572u) return false;
    const uint32_t expect = read_long(ptr + 8);
    if (expect < 2 || expect > 0x100000) return false;
    uint32_t data_len = expect + 0x100;
    if (data_len < 0x20000) data_len = 0x20000;
    if (ptr + data_len > kRamSize) data_len = kRamSize - ptr;
    std::vector<uint8_t> src(data_len);
    for (uint32_t i = 0; i < data_len; i++) src[i] = read_byte(ptr + i);
    const std::vector<uint8_t> out = mac_decompress_resource(src.data(), src.size());
    if (out.empty()) return false;
    uint32_t buf = read_long(0x010c) & 0xffffffu;
    const uint32_t need = uint32_t((out.size() + 1) & ~size_t(1));
    if (buf < need + 0x2000) return false;
    buf = (buf - need) & ~1u;
    for (size_t i = 0; i < out.size(); i++) write_byte(buf + uint32_t(i), out[i]);
    if (need > out.size()) write_byte(buf + uint32_t(out.size()), 0);
    if (handle >= 0x100 && handle + 4 <= kRamSize) write_long(handle, buf);
    write_long(0x010c, buf);
    decompress_count_++;
    return true;
}

void MacPlus::maybe_decompress_handle(uint32_t handle) {
    handle &= 0xffffffu;
    if (handle < 0x100 || handle + 4 > kRamSize) return;
    maybe_decompress_ptr(read_long(handle) & 0xffffffu, handle);
}

void MacPlus::sweep_compressed_handles() {
    uint32_t hi = read_long(0x010c) & 0xffffffu;
    if (hi < 0x8000 || hi > kRamSize) hi = 0x40000;
    uint32_t start = read_long(0x02a6) & 0xffffffu;
    if (start < 0x1000 || start >= hi) start = 0x1400;
    for (uint32_t h = start; h + 8 < hi; h += 4) {
        const uint32_t p = read_long(h) & 0xffffffu;
        if (p < 0x1008 || p + 18 >= kRamSize) continue;
        if (read_long(p) != 0xa89f6572u) continue;
        maybe_decompress_handle(h);
    }
}

void MacPlus::sanitize_mountvol_pb() {
    const uint32_t pb = cpu_.a[0].l & 0xffffffu;
    if (pb + 0x16u >= kRamSize) return;
    // Only ioVRefNum is written; the rest is whatever the RAM test left on
    // the stack. ioNamePtr = -1 is a 255-byte Pascal string of open-bus $FF
    // and File Manager never comes back, so System 7 stays on the Happy Mac.
    for (uint32_t off : {0x0cu, 0x12u}) {
        ram_at(pb + off, 0);
        ram_at(pb + off + 1, 0);
        ram_at(pb + off + 2, 0);
        ram_at(pb + off + 3, 0);
    }
}

void MacPlus::reset() {
    std::fill(ram_.begin(), ram_.end(), 0);
    overlay_ = true;
    screen_buffer_ = 1;
    main_sound_ = true;
    snd_enable_ = false;
    snd_vol_ = 3;
    via_irq_ = false;
    scc_irq_ = false;
    mouse_button_ = false;
    mouse_bit_[0] = mouse_bit_[1] = 0;
    mouse_last_[0] = mouse_last_[1] = 0;
    last_pointer_x_ = last_pointer_y_ = 0;
    pointer_seen_ = false;
    rtc_ca2_ = false;
    finder_launch_ = false;
    boot2_tried_ = false;
    last_trap_ = 0;
    trap_count_ = 0;
    last_syserr_ = 0;
    launch_count_ = 0;
    launch_a0_ = 0;
    decompress_pc_ = 0;
    decompress_count_ = 0;
    read_ret_pc_ = 0;
    read_pb_ = 0;
    for (uint16_t& t : trap_log_) t = 0;
    scc_ptr_[0] = scc_ptr_[1] = 0;
    scc_wr1_[0] = scc_wr1_[1] = 0;
    scc_dcd_[0] = scc_dcd_[1] = false;
    via_acc_ = 0;
    rtc_acc_ = 0;
    kbd_acc_ = 0;
    kbd_cmd_ = 0;
    kbd_reply_ = 0x7b;
    kbd_shift_ = 0x7b;
    kbd_bits_ = 0;
    kbd_qhead_ = kbd_qtail_ = 0;
    kbd_enter_ = kbd_escape_ = kbd_space_ = false;
    audio_.clear();
    via_.reset();
    iwm_.reset();
    rtc_.reset();
    scsi_.reset();
    cpu_.reset();
}

bool MacPlus::load_media(const std::string& path, std::string* error) {
    std::string floppy_error;
    if (iwm_.load_file(path, &floppy_error)) return true;
    if (scsi_.load_file(path, error)) return true;
    if (error && error->empty()) *error = floppy_error;
    return false;
}

void MacPlus::update_irqs() {
    cpu_.set_irq(2, scc_irq_ ? IrqLine::Assert : IrqLine::Clear);
    cpu_.set_irq(1, via_irq_ ? IrqLine::Assert : IrqLine::Clear);
}

void MacPlus::on_cpu_cycles(int cycles) {
    const uint32_t pc = cpu_.pc();
    if (decompress_pc_ && pc == decompress_pc_) {
        decompress_pc_ = 0;
        uint32_t h = cpu_.a[0].l & 0xffffffu;
        if (!h) h = cpu_.d[0].l & 0xffffffu;
        maybe_decompress_handle(h);
    }
    if (read_ret_pc_ && pc == read_ret_pc_) {
        read_ret_pc_ = 0;
        const uint32_t pb = read_pb_ & 0xffffffu;
        if (pb + 0x24u < kRamSize) {
            const uint32_t buf = read_long(pb + 0x20) & 0xffffffu;
            if (buf >= 0x1008 && buf + 22 < kRamSize) {
                uint32_t src = 0;
                if (read_long(buf) == 0xa89f6572u)
                    src = buf;
                else if (read_long(buf + 4) == 0xa89f6572u)
                    src = buf + 4;  // HFS resource record is length + payload
                if (src) {
                    const uint32_t hint = read_long(buf - 4) & 0xffffffu;
                    const uint32_t before = decompress_count_;
                    if (!maybe_decompress_ptr(src, hint)) maybe_decompress_ptr(src, 0);
                    if (decompress_count_ != before) write_long(pb + 0x20, read_long(0x010c));
                }
            }
        }
    }
    if (mount_vol_pc_ && pc == mount_vol_pc_) sanitize_mountvol_pb();
    if (pc == 0x40000au || pc == 0x40000cu) launch_finder_from_rom_a();
    const uint32_t ppc = cpu_.ppc();
    const uint16_t op = uint16_t((uint16_t(read_byte(ppc)) << 8) | read_byte(ppc + 1));
    if ((op & 0xf000) == 0xa000) {
        last_trap_ = op;
        trap_log_[trap_count_ & 31u] = op;
        trap_count_++;
        if (op == 0xa9c9) last_syserr_ = cpu_.d[0].wl();
        if (op == 0xa9f2) {
            launch_count_++;
            launch_a0_ = cpu_.a[0].l;
            if (!boot2_tried_) redirect_launch_to_boot2();
        }
        if (op == 0xa9a0 || op == 0xa81a || op == 0xa9a2 || op == 0xa1a0 || op == 0xa11a ||
            op == 0xa80c || op == 0xa81f)
            decompress_pc_ = (ppc + 2) & 0xffffffu;
        if (op == 0xa00f) sanitize_mountvol_pb();
        if (op == 0xa9f0 || op == 0xa9f2) sweep_compressed_handles();
        if (op == 0xa002) {
            read_ret_pc_ = (ppc + 2) & 0xffffffu;
            read_pb_ = cpu_.a[0].l;
        }
    }
    iwm_.tick(cycles);
    via_acc_ += cycles;
    while (via_acc_ >= 10) {
        via_acc_ -= 10;
        via_.tick(1);
    }
    const uint8_t sr_mode = uint8_t((via_.acr() >> 2) & 7);
    if ((sr_mode == 3 || sr_mode == 7) && via_.sr_busy()) {
        kbd_acc_ += cycles;
        if (kbd_acc_ >= 80) {
            kbd_acc_ = 0;
            clock_keyboard();
        }
    } else {
        kbd_acc_ = 0;
    }
    rtc_acc_ += cycles;
    if (rtc_acc_ >= int64_t(kCpuClock)) {
        rtc_acc_ -= int64_t(kCpuClock);
        rtc_.tick_seconds();
        rtc_ca2_ = !rtc_ca2_;
        via_.write_ca2(rtc_ca2_);
    }
}

uint8_t MacPlus::via_pa_r() {
    // PA7 is SCC Wait/Request (idle high). PA6–PA0 are outputs after the
    // ROM programs DDR-A; return them pulled-up so a read-modify-write
    // before that, or a mixed out_a() byte, cannot force the alternate
    // screen (PA6=0) or pulse overlay (PA4=0).
    return 0xff;
}

uint8_t MacPlus::via_pb_r() {
    uint8_t val = 0x40;
    val = uint8_t(val | (mouse_bit_[1] << 5));
    val = uint8_t(val | (mouse_bit_[0] << 4));
    if (!mouse_button_) val = uint8_t(val | 0x08);
    if (rtc_.data_r()) val = uint8_t(val | 0x01);
    return val;
}

void MacPlus::kbd_enqueue(uint8_t code) {
    if (kbd_qtail_ - kbd_qhead_ >= 8) return;
    kbd_queue_[kbd_qtail_++ & 7] = code;
}

uint8_t MacPlus::kbd_dequeue() {
    if (kbd_qhead_ == kbd_qtail_) return 0x7b;
    return kbd_queue_[kbd_qhead_++ & 7];
}

uint8_t MacPlus::keyboard_reply(uint8_t command) {
    switch (command) {
        case 0x10:  // Inquiry
        case 0x14:  // Instant
            return kbd_dequeue();
        case 0x16:  // Model number: US Macintosh Plus (M0110A)
            return 0x0b;
        case 0x36:  // Self-test
            return 0x7d;
        default:
            return 0x7b;
    }
}

void MacPlus::clock_keyboard() {
    const uint8_t sr_mode = uint8_t((via_.acr() >> 2) & 7);
    if (sr_mode == 3) {
        via_.set_cb2_data((kbd_shift_ & 0x80) != 0);
        kbd_shift_ = uint8_t(kbd_shift_ << 1);
    }
    via_.write_cb1(true);
    via_.write_cb1(false);
    if (sr_mode == 7) {
        kbd_cmd_ = uint8_t((kbd_cmd_ << 1) | (via_.cb2() ? 1 : 0));
        if (++kbd_bits_ >= 8 || !via_.sr_busy()) {
            kbd_reply_ = keyboard_reply(kbd_cmd_);
            kbd_shift_ = kbd_reply_;
            kbd_cmd_ = 0;
            kbd_bits_ = 0;
        }
    } else if (sr_mode == 3 && !via_.sr_busy()) {
        kbd_shift_ = kbd_reply_;
    }
}

void MacPlus::via_pa_w(uint8_t data) {
    const uint8_t ddr = via_.ddr_a();
    if (ddr & 0x40) screen_buffer_ = (data & 0x40) ? 1 : 0;
    if (ddr & 0x20) iwm_.set_hdsel((data & 0x20) != 0);
    if (ddr & 0x08) main_sound_ = (data & 0x08) != 0;
    if (ddr & 0x07) snd_vol_ = data & 7;
    // PCE/macplus: on a Plus, overlay is VIA PA4 and follows the pin
    // live. The 6522 callback is already DDR-masked, so a later volume
    // RMW keeps PA4 low once the ROM has driven it that way.
    if (ddr & 0x10) overlay_ = (data & 0x10) != 0;
}

void MacPlus::via_pb_w(uint8_t data) {
    snd_enable_ = (data & 0x80) == 0;
    rtc_.ce_w((data & 0x04) != 0);
    rtc_.clk_w((data & 0x02) != 0);
    rtc_.data_w((data & 0x01) != 0);
}

uint8_t MacPlus::scc_read(uint32_t address) {
    const int which = (address >> 1) & 3;
    const int ch = (which & 1) ? 0 : 1;  // 0 = A, 1 = B
    const bool data_reg = (which & 2) != 0;
    if (data_reg) return 0;
    uint8_t rr = scc_ptr_[ch];
    scc_ptr_[ch] = 0;
    if (rr == 0) {
        // RR0 idle: Rx empty, Tx buffer empty (bit2), CTS (bit5). LocalTalk
        // and the System 7 .MPP INIT spin if CTS never asserts. DCD (bit3)
        // stays the mouse quadrature line on both channels — do not force it.
        uint8_t v = 0x24;
        if (scc_dcd_[ch]) v = uint8_t(v | 0x08);
        return v;
    }
    return 0;
}

void MacPlus::scc_write(uint32_t address, uint8_t value) {
    const int which = (address >> 1) & 3;
    const int ch = (which & 1) ? 0 : 1;
    const bool data_reg = (which & 2) != 0;
    if (data_reg) return;
    if (scc_ptr_[ch] == 0) {
        // WR0: bits 2-0 select the next register; bits 5-3 are a command.
        // Command 2 (Reset Ext/Status) is how the Plus ROM mouse ISR drops
        // IPL2 after a DCD edge. Ignoring it leaves scc_irq_ stuck and the
        // CPU never sees VBL again.
        const uint8_t cmd = uint8_t((value >> 3) & 7);
        scc_ptr_[ch] = uint8_t(value & 7);
        if (cmd == 2 || cmd == 7) {
            scc_irq_ = false;
            update_irqs();
        }
        return;
    }
    if (scc_ptr_[ch] == 1) scc_wr1_[ch] = value;
    scc_ptr_[ch] = 0;
}

uint32_t MacPlus::ram_index(uint32_t address) const {
    address &= 0xffffff;
    // $600000–$7FFFFF is the 2MB overlay-time RAM window and always aliases
    // the first 2MB. The $000000–$3FFFFF decode is unique through all 4MB so
    // the ROM memory test can set MemTop to $400000 instead of seeing a wrap.
    if (address >= 0x600000 && address < 0x800000) address -= 0x600000;
    return address & (kRamSize - 1);
}

uint8_t MacPlus::read_byte(uint32_t address) {
    address &= 0xffffff;
    if (address < 0x400000) {
        if (overlay_) return rom_[address & (kRomSize - 1)];
        return ram_at(address);
    }
    // 128K Plus ROMs decode A17 onto /OE: $420000 is open bus, $440000 still
    // hits ROM[0]. The ROM compares those two longs and only sets HWCfgFlags
    // (SCSI present) when they differ — a 512KE mirrors the ROM and has no SCSI.
    if (address < 0x500000) {
        if (address & 0x20000) return 0xff;
        return rom_[address & (kRomSize - 1)];
    }
    if (address >= 0x580000 && address < 0x600000) return scsi_.read(address);
    if (address >= 0x600000 && address < 0x800000) return ram_at(address);
    if (address >= 0x800000 && address < 0xa00000) return scc_read(address);
    if (address >= 0xc00000 && address < 0xe00000) return iwm_.read(uint8_t((address >> 9) & 0x0f));
    if (address >= 0xe80000 && address < 0xf00000) return via_.read(uint8_t((address >> 9) & 0x0f));
    return 0xff;
}

void MacPlus::write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffff;
    // Overlay only remaps reads at 0; writes always land in RAM (write-through),
    // and $600000 is the overlay-time RAM window documented for 128K/512K/Plus.
    if (address < 0x400000) {
        ram_at(address, value);
        return;
    }
    if (address >= 0x580000 && address < 0x600000) {
        scsi_.write(address, value);
        return;
    }
    if (address >= 0x600000 && address < 0x800000) {
        ram_at(address, value);
        return;
    }
    if (address >= 0xa00000 && address < 0xc00000) {
        scc_write(address, value);
        return;
    }
    if (address >= 0xc00000 && address < 0xe00000) {
        iwm_.write(uint8_t((address >> 9) & 0x0f), value);
        return;
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        via_.write(uint8_t((address >> 9) & 0x0f), value);
        return;
    }
}

uint16_t MacPlus::read_word(uint32_t address) {
    address &= 0xfffffe;
    if (address >= 0x580000 && address < 0x600000) {
        const uint8_t v = scsi_.read(address);
        return uint16_t((uint16_t(v) << 8) | v);
    }
    if (address >= 0xc00000 && address < 0xe00000) {
        const uint8_t v = iwm_.read(uint8_t((address >> 9) & 0x0f));
        return uint16_t((uint16_t(v) << 8) | v);
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        const uint8_t v = via_.read(uint8_t((address >> 9) & 0x0f));
        return uint16_t((uint16_t(v) << 8) | v);
    }
    if (address >= 0x800000 && address < 0xa00000) {
        const uint8_t v = scc_read(address);
        return uint16_t(uint16_t(v) << 8);
    }
    return uint16_t((uint16_t(read_byte(address)) << 8) | read_byte(address + 1));
}

void MacPlus::write_word(uint32_t address, uint16_t value) {
    address &= 0xfffffe;
    if (address >= 0x580000 && address < 0x600000) {
        scsi_.write(address | 1u, uint8_t(value));
        return;
    }
    if (address >= 0xc00000 && address < 0xe00000) {
        iwm_.write(uint8_t((address >> 9) & 0x0f), uint8_t(value));
        return;
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        via_.write(uint8_t((address >> 9) & 0x0f), uint8_t(value >> 8));
        return;
    }
    if (address >= 0xa00000 && address < 0xc00000) {
        scc_write(address, uint8_t(value));
        return;
    }
    write_byte(address, uint8_t(value >> 8));
    write_byte(address + 1, uint8_t(value));
}

void MacPlus::render() {
    const uint32_t video_off =
        kRamSize - (screen_buffer_ ? 0x5900u : 0xD900u);
    const uint8_t* video = ram_.data() + video_off;
    uint32_t* out = framebuffer_.data();
    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < kWidth; x += 16) {
            const uint16_t word = uint16_t((uint16_t(video[0]) << 8) | video[1]);
            video += 2;
            for (int b = 0; b < 16; b++) {
                const bool black = (word & (0x8000 >> b)) != 0;
                *out++ = black ? 0xff000000u : 0xffffffffu;
            }
        }
    }
}

void MacPlus::run_frame() {
    for (int line = 0; line < kVTotal; line++) {
        const bool vblank = line < kVBlankLines;
        via_.write_ca1(vblank);
        via_.set_pb_line(6, !vblank);
        cpu_.run(kCyclesPerLine);

        if (!vblank && snd_enable_) {
            const uint32_t snd_off = kRamSize - (main_sound_ ? 0x0300u : 0x5F00u);
            const uint8_t sample = ram_[(snd_off + uint32_t(line - kVBlankLines) * 2) & (kRamSize - 1)];
            int32_t s = (int32_t(sample) - 128) * (snd_vol_ + 1) * 32;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            audio_.push_back(int16_t(s));
        } else {
            audio_.push_back(0);
        }
    }
    render();
    if (launch_count_ && decompress_count_ == 0) sweep_compressed_handles();
}

void MacPlus::set_inputs(const MachineInputs& inputs) {
    auto edge = [this](bool down, bool& prev, uint8_t make) {
        if (down && !prev) kbd_enqueue(make);
        if (!down && prev) kbd_enqueue(uint8_t(make | 0x80));
        prev = down;
    };
    edge(inputs.key(Key::Enter), kbd_enter_, 0x24);
    edge(inputs.key(Key::Escape), kbd_escape_, 0x35);
    edge(inputs.key(Key::Space), kbd_space_, 0x31);

    mouse_button_ = inputs.pointer_button1;
    if (!inputs.has_pointer) return;
    if (!pointer_seen_) {
        last_pointer_x_ = inputs.pointer_x;
        last_pointer_y_ = inputs.pointer_y;
        pointer_seen_ = true;
        return;
    }
    int dx = inputs.pointer_x - last_pointer_x_;
    int dy = inputs.pointer_y - last_pointer_y_;
    last_pointer_x_ = inputs.pointer_x;
    last_pointer_y_ = inputs.pointer_y;
    auto pulse = [this](int ch, int dir) {
        if (!dir) return;
        scc_dcd_[ch] = !scc_dcd_[ch];
        mouse_last_[ch] = uint8_t(scc_dcd_[ch]);
        if (dir < 0)
            mouse_bit_[ch] = uint8_t(mouse_last_[ch] ? 0 : 1);
        else
            mouse_bit_[ch] = mouse_last_[ch];
        if (scc_wr1_[ch] & 0x01) {
            scc_irq_ = true;
            update_irqs();
        }
    };
    // One quadrature step per axis per frame keeps the ROM's mouse ISR happy.
    if (dx) pulse(0, dx > 0 ? 1 : -1);
    if (dy) pulse(1, dy > 0 ? 1 : -1);
}

void MacPlus::set_dip_switch(int, uint8_t) {}

void MacPlus::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
