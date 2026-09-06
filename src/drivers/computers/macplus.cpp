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
    find_sony_driver();
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void MacPlus::find_sony_driver() {
    // PCE sony.c: DRVR header $4F00, Pascal name 5 ".Sony". Offsets +8/+10/+12/+14
    // are Open/Prime/Control/Status. Do not patch those bytes: the Plus
    // checksums the 128K ROM and a JMP in Prime is Sad Mac 01.
    sony_prime_rom_ = sony_ctl_rom_ = sony_stat_rom_ = 0;
    for (size_t i = 1; i + 24 < rom_.size(); ++i) {
        if (rom_[i - 1] != 0x05 || rom_[i] != 0x2e) continue;
        if (rom_[i + 1] != 'S' || rom_[i + 2] != 'o' || rom_[i + 3] != 'n' || rom_[i + 4] != 'y')
            continue;
        const size_t drv = i - 19;
        if (drv >= rom_.size() || rom_[drv] != 0x4f || rom_[drv + 1] != 0x00) continue;
        auto entry = [&](int which) {
            const uint16_t off =
                uint16_t((uint16_t(rom_[drv + 8 + which * 2]) << 8) | rom_[drv + 9 + which * 2]);
            return 0x400000u + uint32_t(drv + off);
        };
        sony_prime_rom_ = entry(1);
        sony_ctl_rom_ = entry(2);
        sony_stat_rom_ = entry(3);
        break;
    }
}

void MacPlus::maybe_sony_dispatch() {
    // After the first instruction of ROM .Sony Prime/Control/Status, serve
    // the 1.44MB SuperDrive image by LBA. The cycle handler runs after
    // each 68000 insn, so ppc is the entry and A7 already holds the
    // v3 MOVE.L $226,-(A7) frame.
    if (!iwm_.disk().hd()) return;
    const uint32_t ppc = cpu_.ppc() & 0xffffffu;
    if (sony_prime_rom_ && ppc == sony_prime_rom_) {
        if (read_word(sony_prime_rom_) == 0x2f38) cpu_.a[7].l += 4;
        sony_prime();
    } else if (sony_ctl_rom_ && ppc == sony_ctl_rom_) {
        if (read_word(sony_ctl_rom_) == 0x2f38) cpu_.a[7].l += 4;
        sony_control();
    } else if (sony_stat_rom_ && ppc == sony_stat_rom_) {
        sony_status();
    }
}

void MacPlus::mark_sony_inserted() {
    const uint32_t vars = read_long(0x0134) & 0xffffffu;
    if (vars < 0x100 || vars + 8 + 66 + 22 >= kRamSize) return;
    const uint32_t d1 = vars + 8 + 66;
    ram_at(d1 + 3, 0x02);
    ram_at(d1 + 5, 0xff);
    ram_at(d1 + 18, 0xff);
    ram_at(d1 + 19, 0xff);
    // Drive 2 is the empty external Sony. IWM sense is shared; if the
    // ROM thinks a disk is in, Finder asks to initialize it.
    if (vars + 8 + 132 + 4 < kRamSize) ram_at(vars + 8 + 132 + 3, 0);
}

void MacPlus::sony_return(int16_t result) {
    const uint32_t pb = cpu_.a[0].l & 0xffffffu;
    if (pb + 18 < kRamSize) write_word(pb + 16, uint16_t(result));
    cpu_.d[0].l = result < 0 ? uint32_t(int32_t(result)) : 0;
    const uint16_t trap = pb + 8 < kRamSize ? read_word(pb + 6) : 0;
    const uint32_t jiodone = read_long(0x08fc) & 0xffffffu;
    if ((trap & 0x0200) == 0 && jiodone >= 0x100 && jiodone < 0x420000) {
        cpu_.pc_.l = jiodone;
        return;
    }
    const uint32_t sp = cpu_.a[7].l & 0xffffffu;
    cpu_.pc_.l = read_long(sp);
    cpu_.a[7].l = sp + 4;
}

void MacPlus::sony_prime() {
    sony_prime_count_++;
    mark_sony_inserted();
    const uint32_t pb = cpu_.a[0].l & 0xffffffu;
    const uint32_t dce = cpu_.a[1].l & 0xffffffu;
    if (pb + 50 >= kRamSize) {
        sony_return(-50);
        return;
    }
    const uint16_t trap = read_word(pb + 6);
    const uint16_t vref = read_word(pb + 22);
    if (vref != 1 && vref != 0) {
        sony_return(-56);
        return;
    }
    const uint16_t posmode = read_word(pb + 44);
    uint32_t ofs = 0;
    switch (posmode & 0x0f) {
        case 1:
            ofs = read_long(pb + 46);
            break;
        case 3:
            ofs = read_long(pb + 46);
            if (dce + 20 < kRamSize) ofs += read_long(dce + 16);
            break;
        default:
            if (dce + 20 < kRamSize) ofs = read_long(dce + 16);
            break;
    }
    const uint32_t cnt = read_long(pb + 36);
    const uint32_t addr = read_long(pb + 32) & 0xffffffu;
    if (posmode & 0x40) {
        write_long(pb + 40, cnt);
        if (dce + 20 < kRamSize) write_long(dce + 16, ofs + cnt);
        sony_return(0);
        return;
    }
    if ((ofs & 511) || (cnt & 511) || cnt == 0) {
        sony_return(-50);
        return;
    }
    const uint32_t n = cnt / 512;
    const bool writing = (trap & 0xff) == 3;
    uint8_t buf[512];
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t lba = ofs / 512 + i;
        if (writing) {
            if (addr + i * 512 + 512 > kRamSize) {
                sony_return(-20);
                return;
            }
            for (int b = 0; b < 512; b++) buf[b] = read_byte(addr + i * 512 + uint32_t(b));
            if (!iwm_.disk().write_lba(lba, buf)) {
                sony_return(-20);
                return;
            }
        } else {
            if (!iwm_.disk().read_lba(lba, buf)) {
                sony_return(-19);
                return;
            }
            if (addr + i * 512 + 512 > kRamSize) {
                sony_return(-19);
                return;
            }
            for (int b = 0; b < 512; b++) write_byte(addr + i * 512 + uint32_t(b), buf[b]);
            sony_read_bytes_ += 512;
        }
    }
    write_long(pb + 40, cnt);
    if (dce + 20 < kRamSize) write_long(dce + 16, ofs + cnt);
    sony_return(0);
}

void MacPlus::sony_control() {
    const uint32_t pb = cpu_.a[0].l & 0xffffffu;
    if (pb + 28 >= kRamSize) {
        sony_return(-17);
        return;
    }
    const uint16_t vref = read_word(pb + 22);
    const uint16_t cs = read_word(pb + 26);
    if (vref != 1 && vref != 0 && cs != 7) {
        sony_return(-56);
        return;
    }
    switch (cs) {
        case 1:
            sony_return(-27);
            return;
        case 5:
            sony_return(0);
            return;
        case 6:
            sony_return(-50);
            return;
        case 7:
            sony_return(0);
            return;
        case 8:
            sony_return(0);
            return;
        case 9:
            sony_return(int16_t(0xffc8));
            return;
        case 21:
        case 22:
            sony_return(-17);
            return;
        case 23:
            // PCE get drive info: SuperDrive-capable internal unit.
            write_long(pb + 28, 0x00000400);
            sony_return(0);
            return;
        default:
            sony_return(-17);
            return;
    }
}

void MacPlus::sony_status() {
    const uint32_t pb = cpu_.a[0].l & 0xffffffu;
    if (pb + 34 >= kRamSize) {
        sony_return(-18);
        return;
    }
    const uint16_t vref = read_word(pb + 22);
    const uint16_t cs = read_word(pb + 26);
    if (vref != 1 && vref != 0) {
        sony_return(-64);
        return;
    }
    mark_sony_inserted();
    if (cs == 8) {
        const uint32_t vars = read_long(0x0134) & 0xffffffu;
        const uint32_t d1 = vars + 8 + 66;
        if (vars >= 0x100 && d1 + 22 < kRamSize) {
            for (int i = 0; i < 11; i++)
                write_word(pb + 28 + uint32_t(i) * 2, read_word(d1 + uint32_t(i) * 2));
        }
        sony_return(0);
        return;
    }
    if (cs == 6) {
        // PCE format list for a 1.44MB SuperDrive disk: 2880 blocks, HD MFM.
        const uint16_t maxfmt = read_word(pb + 28);
        const uint32_t ptr = read_long(pb + 30) & 0xffffffu;
        if (maxfmt && ptr + 8 < kRamSize) {
            write_long(ptr, 2880);
            write_long(ptr + 4, 0xd2120050);
            write_word(pb + 28, 1);
        }
        sony_return(0);
        return;
    }
    sony_return(-18);
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
    if (buf < need + 0x2008) return false;
    buf = (buf - (need + 8)) & ~1u;
    write_long(buf, 0x80000000u | (need + 8));
    write_long(buf + 4, handle);
    const uint32_t data = buf + 8;
    for (size_t i = 0; i < out.size(); i++) write_byte(data + uint32_t(i), out[i]);
    if (need > out.size()) write_byte(data + uint32_t(out.size()), 0);
    if (handle >= 0x100 && handle + 4 <= kRamSize) write_long(handle, data);
    write_long(0x010c, buf);
    const uint32_t limit = buf > 0x400 ? buf - 0x400 : buf;
    if ((read_long(0x0130) & 0xffffffu) > limit) write_long(0x0130, limit);
    for (uint32_t zaddr : {0x02aau, 0x02a6u, 0x0118u}) {
        const uint32_t zone = read_long(zaddr) & 0xffffffu;
        if (zone >= 0x1000 && zone + 4 < kRamSize && (read_long(zone) & 0xffffffu) > buf)
            write_long(zone, buf);
    }
    decompress_count_++;
    return true;
}

void MacPlus::maybe_decompress_handle(uint32_t handle) {
    handle &= 0xffffffu;
    if (handle < 0x100 || handle + 4 > kRamSize) return;
    maybe_decompress_ptr(read_long(handle) & 0xffffffu, handle);
}

void MacPlus::sanitize_mountvol_pb() {
    const uint32_t pb = cpu_.a[0].l & 0xffffffu;
    if (pb + 0x16u >= kRamSize) return;
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
    sony_prime_count_ = 0;
    sony_read_bytes_ = 0;
    last_trap_ = 0;
    last_trap_d0_ = 0;
    trap_count_ = 0;
    last_syserr_ = 0;
    last_scsi_dispatch_ = 0;
    scsi_dispatch_count_ = 0;
    for (uint16_t& s : scsi_dispatch_log_) s = 0;
    decompress_pc_ = 0;
    decompress_count_ = 0;
    read_ret_pc_ = 0;
    read_pb_ = 0;
    launch_count_ = 0;
    launch_a0_ = 0;
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
    // A Disk Copy 4.2 floppy is 84 + payload. That is not a multiple of
    // 512 when the wrapper is still on, so the SCSI probe would hide a
    // real floppy error behind "SCSI disk must be a multiple of 512 bytes".
    if (MacDsk::looks_like_mac_floppy(path)) {
        if (error) *error = floppy_error;
        return false;
    }
    if (scsi_.load_file(path, error)) return true;
    if (error && error->empty()) *error = floppy_error;
    return false;
}

void MacPlus::update_irqs() {
    cpu_.set_irq(2, scc_irq_ ? IrqLine::Assert : IrqLine::Clear);
    cpu_.set_irq(1, via_irq_ ? IrqLine::Assert : IrqLine::Clear);
}

void MacPlus::on_cpu_cycles(int cycles) {
    maybe_sony_dispatch();
    const uint32_t pc = cpu_.pc();
    if (decompress_pc_ && pc == decompress_pc_) {
        decompress_pc_ = 0;
        maybe_decompress_handle(cpu_.a[0].l);
        maybe_decompress_handle(cpu_.d[0].l);
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
                    src = buf + 4;
                if (src) {
                    const uint32_t hint = read_long(buf - 4) & 0xffffffu;
                    const uint32_t before = decompress_count_;
                    if (!maybe_decompress_ptr(src, hint)) maybe_decompress_ptr(src, 0);
                    if (decompress_count_ != before) write_long(pb + 0x20, read_long(0x010c));
                }
            }
        }
    }
    const uint32_t ppc = cpu_.ppc();
    const uint16_t op = uint16_t((uint16_t(read_byte(ppc)) << 8) | read_byte(ppc + 1));
    if ((op & 0xf000) == 0xa000) {
        last_trap_ = op;
        last_trap_d0_ = cpu_.d[0].l;
        trap_log_[trap_count_ & 31u] = op;
        trap_count_++;
        if (op == 0xa9c9) last_syserr_ = cpu_.d[0].wl();
        if (op == 0xa815) {
            last_scsi_dispatch_ = read_word((cpu_.a[7].l + 6) & 0xffffffu);
            scsi_dispatch_log_[scsi_dispatch_count_ & 31u] = last_scsi_dispatch_;
            scsi_dispatch_count_++;
        }
        if (op == 0xa9f2) {
            launch_count_++;
            launch_a0_ = cpu_.a[0].l;
        }
        if (op == 0xa829 || op == 0xa9a0 || op == 0xa81a || op == 0xa9a2 || op == 0xa1a0 ||
            op == 0xa11a || op == 0xa80c || op == 0xa81f)
            decompress_pc_ = (ppc + 2) & 0xffffffu;
        if (op == 0xa00f) sanitize_mountvol_pb();
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
    // MAME mac_via_in_a: 0x81 — PA7 SCC Wait/Request idle-high. Overlay
    // and the screen buffer are outputs; via_pa_w only honors DDR bits so
    // a mixed out_a() cannot clear PA4/PA6 while they are still inputs.
    return 0x81;
}

uint8_t MacPlus::via_pb_r() {
    // MAME mac_via_in_b: PB6 pulled high, mouse Y2/X2, button, RTC data.
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
    // MAME mac_via_out_a: PA6 screen buffer, PA5 IWM hdsel, PA4 overlay
    // (Plus only; SE clears overlay on the first $400000 fetch), PA3
    // main sound page, PA2–0 volume. The 6522 callback is the mixed
    // port, so honor DDR the way the pin would be driven.
    const uint8_t ddr = via_.ddr_a();
    if (ddr & 0x40) screen_buffer_ = (data & 0x40) ? 1 : 0;
    if (ddr & 0x20) iwm_.set_hdsel((data & 0x20) != 0);
    if (ddr & 0x08) main_sound_ = (data & 0x08) != 0;
    if (ddr & 0x07) snd_vol_ = data & 7;
    if (ddr & 0x10) overlay_ = (data & 0x10) != 0;
}

void MacPlus::via_pb_w(uint8_t data) {
    // MAME mac_via_out_b: PB7 /sndenb, PB2–0 RTC. Plus leaves SCSI IRQ
    // (SE PB6 mask) disconnected.
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
    // MAME macplus_map / mac128.cpp Memory Map:
    //   $000000–$3FFFFF  RAM, or ROM while VIA PA4 overlay is set
    //   $400000–$4FFFFF  ROM (A17 wired to /OE → $42xxxx open bus)
    //   $580000–$5FFFFF  NCR 5380 (Plus only; IRQ pin 23 unconnected)
    //   $600000–$7FFFFF  RAM window (MAME mac512ke_map; Plus hardware still
    //                    decodes it — macplus_map omits the handler)
    //   $800000–$9FFFFF  SCC read    $A00000–$BFFFFF  SCC write
    //   $C00000–$DFFFFF  IWM         $E80000–$EFFFFF  VIA (VPA)
    address &= 0xffffff;
    if (address < 0x400000) {
        if (overlay_) return rom_[address & (kRomSize - 1)];
        return ram_at(address);
    }
    // 128K Plus ROMs decode A17 onto /OE: $420000 is open bus, $440000 still
    // hits ROM[0]. MAME plants 0xFF at region+$20000 and 0xAA at +$40000 so
    // the same compare sets HWCfgFlags (SCSI). 512KE mirrors the ROM.
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
    // Overlay remaps reads at 0. Hardware write-throughs to RAM (Inside Mac);
    // MAME's ram_w drops those writes and relies on $600000, which macplus_map
    // does not install. $600000 stays the overlay-time RAM window.
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
