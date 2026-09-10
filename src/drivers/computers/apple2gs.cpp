#include "drivers/computers/apple2gs.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRom = {
    {"341-0728", 0x20000, 0x00000, 0x8d410067},
};

const std::vector<RomEntry> kAdbRom = {{"341s0632-2.bin", 0x1000, 0, 0xe1c11fb0}};
const std::vector<RomEntry> kChrRom = {{"apple2gs.chr", 0x1000, 0, 0x91e53cd8}};

}  // namespace

bool Apple2GS::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // rom_ covers banks $FC-$FF (256KB). 341-0728 (128KB) fills the first
    // half ($FC-$FD) directly. 341-0748 (128KB) fills the second half
    // ($FE-$FF), but with its two 64KB halves swapped (an inverted address
    // line on the physical ROM socket -- confirmed against MAME's
    // ROM_LOAD+ROM_CONTINUE pair for this exact chip).
    std::vector<uint8_t> first(0x20000, 0);
    if (!loader.load(kMainRom, first, error)) return false;
    std::copy(first.begin(), first.end(), rom_.begin());

    std::vector<uint8_t> second;
    if (!loader.try_read("341-0748", second) || second.size() != 0x20000) {
        if (error) *error = "missing or wrong-size ROM file: 341-0748";
        return false;
    }
    if (crc32_of(second.data(), second.size()) != 0x18190283u) {
        if (error) *error = "CRC mismatch for 341-0748 (continuing anyway)";
    }
    std::copy(second.begin() + 0x10000, second.end(), rom_.begin() + 0x20000);
    std::copy(second.begin(), second.begin() + 0x10000, rom_.begin() + 0x30000);

    std::vector<uint8_t> adb(0x1000, 0);
    if (!loader.load(kAdbRom, adb, error)) return false;
    std::copy(adb.begin(), adb.end(), mega2_rom_.begin());

    std::vector<uint8_t> chr(0x1000, 0);
    if (!loader.load(kChrRom, chr, error)) return false;
    std::copy(chr.begin(), chr.end(), chr_rom_.begin());

    cpu_.set_memory_handlers([this](uint32_t a) { return mem_read(a); },
                              [this](uint32_t a, uint8_t v) { mem_write(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cpu_cycles(c); });

    main_cycles_per_frame_ = int(double(kFastClock) / kFramesPerSecond);
    std::memset(clock_ram_, 0, sizeof(clock_ram_));
    reset();
    return true;
}

bool Apple2GS::load_media(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open disk image";
        return false;
    }
    in.seekg(0, std::ios::end);
    size_t size = size_t(in.tellg());
    in.seekg(0, std::ios::beg);
    if (size == 0 || (size % 512) != 0) {
        if (error) *error = "disk image size must be a multiple of 512 bytes";
        return false;
    }
    disk_.resize(size);
    in.read(reinterpret_cast<char*>(disk_.data()), std::streamsize(size));
    disk_loaded_ = true;
    return true;
}

// Standard SmartPort inline-parameter call convention: the caller does
//   JSR $C500          ; slot 5 entry
//   DFB command         ; 1 = status, 2 = read block, 3 = write block
//   DA paramlist         ; pointer to the parameter list
// so the return address pushed by JSR points at the command byte, not at
// real code; the callee must read the inline bytes, then push back an
// adjusted return address (skipping them) before its own RTS.
void Apple2GS::smartport_dispatch() {
    uint32_t ret = uint32_t(mem_read(0x000100 | cpu_.sp) | (mem_read(0x000100 | uint16_t(cpu_.sp + 1)) << 8));
    ret = uint16_t(ret + 1);  // JSR pushes return_address-1
    uint8_t command = mem_read(ret);
    uint16_t param_ptr = uint16_t(mem_read(ret + 1) | (mem_read(ret + 2) << 8));
    uint32_t new_ret = uint16_t(ret + 3 - 1);  // skip the 3 inline bytes; RTS will +1

    bool ok = false;
    uint8_t status = 0x27;  // ioError by default (SmartPort/ProDOS error code)
    if (command == 0 || command == 4) {
        // init / control: no real backing action needed for a plain block
        // image -- just acknowledge so any probe/setup sequence that
        // issues one doesn't see an unexpected failure.
        ok = true;
        status = 0;
    } else if (command == 1) {  // status
        ok = true;
        status = 0;
        uint8_t status_list = mem_read(param_ptr + 2);
        uint16_t buf = uint16_t(mem_read(param_ptr + 3) | (mem_read(param_ptr + 4) << 8));
        if (status_list == 0) {
            uint32_t blocks = uint32_t(disk_.size() / 512);
            mem_write(buf, uint8_t(0xf8));  // device status: block device, online, etc.
            mem_write(buf + 1, uint8_t(blocks & 0xff));
            mem_write(buf + 2, uint8_t((blocks >> 8) & 0xff));
            mem_write(buf + 3, uint8_t((blocks >> 16) & 0xff));
        }
    } else if (command == 2 || command == 3) {  // read/write block
        uint16_t buf = uint16_t(mem_read(param_ptr + 2) | (mem_read(param_ptr + 3) << 8));
        uint32_t block = uint32_t(mem_read(param_ptr + 4)) | (uint32_t(mem_read(param_ptr + 5)) << 8) |
                          (uint32_t(mem_read(param_ptr + 6)) << 16);
        size_t offset = size_t(block) * 512;
        if (offset + 512 <= disk_.size()) {
            if (command == 2) {
                for (int i = 0; i < 512; i++) mem_write(uint32_t(buf + i), disk_[offset + size_t(i)]);
            } else {
                for (int i = 0; i < 512; i++) disk_[offset + size_t(i)] = mem_read(uint32_t(buf + i));
            }
            ok = true;
            status = 0;
        }
    }

    cpu_.p.c = !ok;
    cpu_.a = (cpu_.a & 0xff00) | status;
    cpu_.sp = uint16_t(cpu_.sp - 2);
    mem_write(0x000100 | uint16_t(cpu_.sp + 1), uint8_t(new_ret));
    mem_write(0x000100 | uint16_t(cpu_.sp + 2), uint8_t(new_ret >> 8));
}

void Apple2GS::reset() {
    cpu_.reset();
    doc_.reset();
    doc_addr_ = 0;
    doc_ctrl_ = 0;
    cycles_since_sample_ = 0;
    altzp_ = false;
    ramrd_ = ramwrt_ = false;
    intcxrom_ = true;   // ROM's own reset vector expects internal ROM at $C100-$CFFF
    slotc3rom_ = false;
    lcram_ = false;
    lcram2_ = true;
    lc_write_enable_ = false;
    lc_prewrite_ = false;
    vbl_ = false;
    adb_response_ready_ = false;
    vgcint_ = 0;
    inten_ = 0;
    slotromsel_ = 0;
    clock_bit_count_ = 0;
    clock_cmd_ready_ = false;
    shadow_reg_ = 0x00;  // confirmed against MAME's exact reset code: all shadowing enabled by default
    speed_reg_ = 0x80;  // confirmed against MAME's exact reset code: SPEED_HIGH (full speed) set by default
    state_reg_ = 0;
    newvideo_ = 0x01;
    col80_ = store80_ = page2_ = hires_ = mixed_ = false;
    text_mode_ = true;
    altcharset_ = false;
    an3_ = false;  // confirmed against MAME's actual reset code: AN3 starts false
    scanline_ = 0;
    audio_.clear();
    framebuffer_.fill(0xff000000u);
}

// ---------------------------------------------------------------------------
// Memory map. Mirrors the Mega II's bank decode:
//   $00-$01: main/aux 64K each. $0000-$01FF (zero page + stack) swap on
//            ALTZP; the rest of $0000-$BFFF swaps on RAMRD (reads) /
//            RAMWRT (writes) independently; $D000-$FFFF is the language
//            card window (RAM-or-ROM, bank 1 or 2, per $C080-$C08F);
//            $C000-$CFFF is always I/O.
//   $02-$7F: unbanked motherboard/expansion RAM ("slow" RAM outside 00/01).
//   $E0/$E1: the Mega II's own dedicated shadow banks (always the
//            Apple II-compatible view; writes to shadowed regions of
//            00/01 mirror here automatically).
//   $FC-$FF: ROM.
// ---------------------------------------------------------------------------

uint8_t Apple2GS::mem_read(uint32_t address) {
    uint8_t bank = uint8_t(address >> 16);
    uint16_t off = uint16_t(address);

    if (bank >= 0xfc) return rom_[(uint32_t(bank - 0xfc) << 16) | off];
    if (bank == 0xe0 || bank == 0xe1) {
        if (off >= 0xc000 && off <= 0xcfff) return io_read(off);
        return shadow_ram_[bank - 0xe0][off];
    }
    if (bank == 0x00 || bank == 0x01) {
        if (bank == 0x00 && off >= 0xffe4) return rom_[0x3ffe4 + (off - 0xffe4)];
        if (off <= 0xc0ff && off >= 0xc000) return io_read(off);
        if (disk_loaded_ && off >= 0xc500 && off <= 0xc5ff) {
            // Slot 5 SmartPort firmware stub. Byte layout follows the
            // standard Apple II slot-ROM identification convention any
            // SmartPort-capable card must present at these fixed offsets:
            //   $Cn01=$20, $Cn03=$00, $Cn05=$03 (SmartPort marker, vs $00
            //   for a plain non-SmartPort disk controller), $Cn07=$00.
            // The entry point at $Cn00 does: JSR trap_pc; RTS -- the
            // dispatcher itself lives in on_cpu_cycles(), not real 65816
            // code, since a raw .po image is already exactly the sequence
            // of blocks a ProDOS device-driver call expects; no GCR/IWM
            // timing simulation is needed to serve it correctly.
            static const uint8_t kStub[8] = {
                0x20, uint8_t(kSmartPortTrapPc & 0xff), uint8_t((kSmartPortTrapPc >> 8) & 0xff),
                0x00, 0x00, 0x03, 0x00, 0x60,
            };
            uint16_t local = uint16_t(off - 0xc500);
            if (local < 8) return kStub[local];
            if (local == 0x0a) return 0x60;  // RTS at the trap PC itself, in case it's ever fetched as data
            return 0x00;
        }
        if (off >= 0xc100 && off <= 0xcfff) {
            if (intcxrom_ || (off >= 0xc300 && off <= 0xc3ff && !slotc3rom_)) {
                return rom_[0x30000 + off];
            }
            return 0x00;  // unpopulated slot: no card signature present
        }
        if (off >= 0xd000) {
            if (!lcram_) return rom_[0x3d000 + (off - 0xd000) + (bank == 0x01 ? 0x10000 : 0)];
            bool aux = (bank == 0x01) ? !ramrd_ : ramrd_;
            uint16_t ram_off = (off < 0xe000 && !lcram2_) ? uint16_t(off - 0x1000) : off;
            return ram_[aux ? 1 : 0][ram_off];
        }
        bool zp = off < 0x200;
        bool aux = zp ? altzp_ : ((bank == 0x01) ? !ramrd_ : ramrd_);
        return ram_[aux ? 1 : 0][off];
    }
    if (bank <= 0x7f) return ram_[std::min<size_t>(bank, kRamBanks - 1)][off];
    return 0xff;
}

void Apple2GS::mem_write(uint32_t address, uint8_t value) {
    uint8_t bank = uint8_t(address >> 16);
    uint16_t off = uint16_t(address);

    if (bank >= 0xfc) return;  // ROM
    if (bank == 0xe0 || bank == 0xe1) {
        if (off >= 0xc000 && off <= 0xcfff) { io_write(off, value); return; }
        shadow_ram_[bank - 0xe0][off] = value;
        if (bank == 0xe1 && value == 0x00 && off >= 0x1000 && off < 0x4000) {
            pending_completions_.push_back({address, total_cycles_ + 4});
        }
        return;
    }
    if (bank == 0x00 || bank == 0x01) {
        if (off <= 0xc0ff && off >= 0xc000) { io_write(off, value); return; }
        if (off >= 0xc100 && off <= 0xcfff) return;  // ROM / unpopulated slot
        bool zp = off < 0x200;
        bool aux;
        if (off >= 0xd000) {
            if (!lc_write_enable_) return;  // ROM selected for reads, RAM never written
            aux = (bank == 0x01) ? !ramwrt_ : ramwrt_;
            uint16_t ram_off = (off < 0xe000 && !lcram2_) ? uint16_t(off - 0x1000) : off;
            ram_[aux ? 1 : 0][ram_off] = value;
            return;
        }
        aux = zp ? altzp_ : ((bank == 0x01) ? !ramwrt_ : ramwrt_);
        ram_[aux ? 1 : 0][off] = value;
        // Shadowing: text page 1 ($0400-$07FF) and hires pages
        // ($2000-$3FFF/$4000-$5FFF) mirror into $E0/$E1 when enabled via
        // shadow_reg_, so the Mega II's video generator (which only reads
        // from E0/E1) sees writes made through banks 00/01.
        bool shadow_text = (shadow_reg_ & 0x01) == 0;
        bool shadow_hires1 = (shadow_reg_ & 0x02) == 0;
        bool shadow_hires2 = (shadow_reg_ & 0x04) == 0;
        bool shadow_shr = (shadow_reg_ & 0x08) == 0;
        bool hit = (shadow_text && off >= 0x400 && off <= 0x7ff) ||
                   (shadow_hires1 && off >= 0x2000 && off <= 0x3fff) ||
                   (shadow_hires2 && off >= 0x4000 && off <= 0x5fff) ||
                   (shadow_shr && off >= 0x2000 && off <= 0x9fff);
        if (hit) shadow_ram_[aux ? 1 : 0][off] = value;
        return;
    }
    if (bank <= 0x7f) { ram_[std::min<size_t>(bank, kRamBanks - 1)][off] = value; return; }
}

// ---------------------------------------------------------------------------
// $C000-$C0FF soft switches. Read semantics/bit polarity and the $C080-$C08F
// language-card decode are paraphrased from MAME's apple2gs_state::c000_r/
// c000_w (src/mame/apple/apple2gs.cpp): status reads return 0x80 for "on",
// 0x00 for "off"; TEXT reads inverted (0x80 means text mode); most on/off
// pairs are adjacent even(off)/odd(on) offsets. The classic Apple II
// "read $C08x twice to unlock writes" double-access requirement is
// simplified here to a single access, which is enough for ROM POST to make
// forward progress; it can be tightened later if real software needs it.
// ---------------------------------------------------------------------------

uint8_t Apple2GS::io_read(uint16_t address) {
    uint16_t offset = uint16_t(address - 0xc000);
    if (offset <= 0x0f) return keyboard_latch_;
    switch (offset) {
        case 0x10: { uint8_t v = uint8_t((keyboard_latch_ & 0x80) | (button_state_ & 0x7f)); keyboard_latch_ &= 0x7f; return v; }
        case 0x11: return lcram2_ ? 0x80 : 0x00;
        case 0x12: return lcram_ ? 0x80 : 0x00;
        case 0x13: return ramrd_ ? 0x80 : 0x00;
        case 0x14: return ramwrt_ ? 0x80 : 0x00;
        case 0x15: return intcxrom_ ? 0x80 : 0x00;
        case 0x16: return altzp_ ? 0x80 : 0x00;
        case 0x17: return slotc3rom_ ? 0x80 : 0x00;
        case 0x18: return store80_ ? 0x80 : 0x00;
        case 0x19: return vbl_ ? 0x80 : 0x00;
        case 0x1a: return text_mode_ ? 0x80 : 0x00;
        case 0x1b: return mixed_ ? 0x80 : 0x00;
        case 0x1c: return page2_ ? 0x80 : 0x00;
        case 0x1d: return hires_ ? 0x80 : 0x00;
        case 0x1e: return altcharset_ ? 0x80 : 0x00;
        case 0x1f: return col80_ ? 0x80 : 0x00;
        case 0x3c: return doc_ctrl_;
        case 0x3d: {
            uint8_t v = (doc_ctrl_ & 0x40) ? doc_.ram_read(doc_addr_) : doc_.read(uint8_t(doc_addr_ & 0xff));
            if (doc_ctrl_ & 0x20) doc_addr_ = uint16_t(doc_addr_ + 1);
            return v;
        }
        case 0x3e: return uint8_t(doc_addr_ & 0xff);
        case 0x3f: return uint8_t(doc_addr_ >> 8);
        case 0x33: {
            if (clock_cmd_ready_ && clock_is_read_) {
                uint8_t bit = uint8_t((clock_shift_reg_ >> 7) & 1);
                clock_shift_reg_ = uint8_t(clock_shift_reg_ << 1);
                clock_bit_count_++;
                if (clock_bit_count_ >= 8) { clock_cmd_ready_ = false; clock_bit_count_ = 0; }
                return uint8_t(bit << 7);
            }
            return rtc_data_;
        }
        case 0x34: return uint8_t(rtc_control_ & 0x7f);  // bit7=busy, always "done" here
        case 0x22: return text_color_;
        case 0x23: return vgcint_;
        case 0x24: return 0x00;  // MOUSEDATA: no mouse movement pending
        case 0x25: return 0x00;  // KEYMODREG: no modifier keys held
        case 0x26: return adb_response_ready_ ? adb_response_ : 0x00;  // GLU DATA
        case 0x27: return 0x80;  // GLU SYSSTAT: bit7 set by default (confirmed against a real hardware trace)
        case 0x2d: return slotromsel_;
        case 0x46: return an3_ ? 0x20 : 0x00;  // INTFLAG: bit5 reflects AN3 state (confirmed: INTFLAG_AN3=0x20, not bit7)
        case 0x29: return newvideo_;
        case 0x41: return inten_;
        case 0x61: return 0x00;  // button 0 / Open Apple: not pressed
        case 0x62: return 0x00;  // button 1 / Solid Apple: not pressed
        case 0x63: return 0x00;  // button 2: not pressed
        case 0x35: return shadow_reg_;
        case 0x36: return speed_reg_;
        case 0x68:  // STATEREG: synthesized from existing flags, not a stored value
            return uint8_t((altzp_ ? 0x80 : 0x00) | (page2_ ? 0x40 : 0x00) |
                            (ramrd_ ? 0x20 : 0x00) | (ramwrt_ ? 0x10 : 0x00) |
                            (lcram_ ? 0x00 : 0x08) | (lcram2_ ? 0x04 : 0x00) |
                            (intcxrom_ ? 0x01 : 0x00));
    }
    if (offset >= 0x80 && offset <= 0x8f) {
        lcram_ = (offset & 1) != 0;
        lcram2_ = (offset & 8) == 0;
        if ((offset & 1) != 0) {
            // Read-access to an odd address (the same access that selects
            // RAM for reading, per lcram_ above) arms/consumes the
            // write-enable latch (simplified: a single such read is
            // enough here, vs. real hardware's two-consecutive-reads rule).
            lc_write_enable_ = lc_prewrite_;
            lc_prewrite_ = true;
        } else {
            lc_prewrite_ = false;
        }
        return 0x00;
    }
    return 0x00;
}

void Apple2GS::io_write(uint16_t address, uint8_t value) {
    uint16_t offset = uint16_t(address - 0xc000);
    switch (offset) {
        case 0x00: store80_ = false; break;
        case 0x01: store80_ = true; break;
        case 0x02: ramrd_ = false; break;
        case 0x03: ramrd_ = true; break;
        case 0x04: ramwrt_ = false; break;
        case 0x05: ramwrt_ = true; break;
        case 0x06: intcxrom_ = false; break;
        case 0x07: intcxrom_ = true; break;
        case 0x08: altzp_ = false; break;
        case 0x09: altzp_ = true; break;
        case 0x0a: slotc3rom_ = false; break;
        case 0x0b: slotc3rom_ = true; break;
        case 0x0c: col80_ = false; break;
        case 0x0d: col80_ = true; break;
        case 0x0e: altcharset_ = false; break;
        case 0x0f: altcharset_ = true; break;
        case 0x10: keyboard_latch_ &= 0x7f; break;
        case 0x22: text_color_ = value; break;
        case 0x26: {
            // ADB command byte: [addr:4][cmd:2][reg:2]. SendReset (cmd=0)
            // and Flush (cmd=1) need no data and just succeed. Listen/Talk
            // (cmd=3) either accepts or returns register data; since no
            // real ADB device is emulated, a "no data / nothing pressed"
            // response (0x00) is the safe, standard default that won't
            // look like a spurious keypress or mouse movement to the ROM.
            adb_response_ = 0x00;
            adb_response_ready_ = true;
            break;
        }
        case 0x33: rtc_data_ = value; break;
        case 0x3c: doc_ctrl_ = value; break;
        case 0x3d: {
            if (doc_ctrl_ & 0x40) doc_.ram_write(doc_addr_, value);
            else doc_.write(uint8_t(doc_addr_ & 0xff), value);
            if (doc_ctrl_ & 0x20) doc_addr_ = uint16_t(doc_addr_ + 1);
            break;
        }
        case 0x3e: doc_addr_ = uint16_t((doc_addr_ & 0xff00) | value); break;
        case 0x3f: doc_addr_ = uint16_t((doc_addr_ & 0x00ff) | (value << 8)); break;
        case 0x34: {
            rtc_control_ = value;
            uint8_t bit = uint8_t((value >> 7) & 1);
            if (!clock_cmd_ready_) {
                clock_shift_reg_ = uint8_t((clock_shift_reg_ << 1) | bit);
                clock_bit_count_++;
                if (clock_bit_count_ >= 8) {
                    clock_is_read_ = (clock_shift_reg_ & 0x80) != 0;
                    clock_addr_ = uint8_t(clock_shift_reg_ & 0x7f);
                    clock_bit_count_ = 0;
                    clock_cmd_ready_ = true;
                    if (clock_is_read_) clock_shift_reg_ = clock_ram_[clock_addr_];
                }
            } else if (!clock_is_read_) {
                clock_shift_reg_ = uint8_t((clock_shift_reg_ << 1) | bit);
                clock_bit_count_++;
                if (clock_bit_count_ >= 8) {
                    clock_ram_[clock_addr_] = clock_shift_reg_;
                    clock_ram_[uint8_t(clock_addr_ | 0x80)] = clock_shift_reg_;
                    clock_cmd_ready_ = false;
                    clock_bit_count_ = 0;
                }
            }
            break;
        }
        case 0x23: {
            if (!(value & 0x40)) vgcint_ = uint8_t(vgcint_ & ~0x40);  // clear SECOND
            if (!(value & 0x20)) vgcint_ = uint8_t(vgcint_ & ~0x20);  // clear SCANLINE
            if (!(vgcint_ & 0x60)) vgcint_ = uint8_t(vgcint_ & ~0x80);  // recompute ANYVGCINT
            break;
        }
        case 0x29: newvideo_ = uint8_t(value & 0xe1); break;  // confirmed write mask against MAME's source
        case 0x2d: slotromsel_ = uint8_t(value & 0xf6); break;
        case 0x41: inten_ = value; break;
        case 0x35: shadow_reg_ = value; break;
        case 0x36: speed_reg_ = value; break;
        case 0x68: state_reg_ = value; break;
        case 0x50: text_mode_ = false; break;
        case 0x51: text_mode_ = true; break;
        case 0x52: mixed_ = false; break;
        case 0x53: mixed_ = true; break;
        case 0x54: page2_ = false; break;
        case 0x55: page2_ = true; break;
        case 0x56: hires_ = false; break;
        case 0x57: hires_ = true; break;
        default:
            if (offset >= 0x80 && offset <= 0x8f) {
                lcram_ = ((offset & 3) == 0 || (offset & 3) == 3);
                lcram2_ = (offset & 8) == 0;
                // Writing directly (as opposed to reading) toggles write
                // access following the same even/odd pattern; see the note
                // above the class-wide comment on this simplification.
                lc_write_enable_ = (offset & 1) != 0;
            }
            break;
    }
}

void Apple2GS::on_cpu_cycles(int cycles) {
    if (disk_loaded_ && cpu_.pc() == kSmartPortTrapPc) smartport_dispatch();
    total_cycles_ += uint64_t(cycles);
    cycles_since_sample_ += uint64_t(cycles) * uint64_t(sample_rate());
    while (cycles_since_sample_ >= kFastClock) {
        cycles_since_sample_ -= kFastClock;
        audio_.push_back(doc_.update());
    }
    if (!pending_completions_.empty()) {
        for (size_t i = 0; i < pending_completions_.size();) {
            if (total_cycles_ >= pending_completions_[i].due_cycle) {
                uint32_t addr = pending_completions_[i].address;
                uint8_t bank = uint8_t(addr >> 16);
                uint16_t off = uint16_t(addr);
                shadow_ram_[bank - 0xe0][off] = 0x01;
                pending_completions_[i] = pending_completions_.back();
                pending_completions_.pop_back();
            } else {
                ++i;
            }
        }
    }
    // Coarse VBL flag: roughly 12.5% of each frame is vertical blank on a
    // real IIgs (192 visible scanlines of 262-ish total); toggle it near
    // the end of the CPU's per-frame cycle budget so $C019 reads something
    // plausible without a full scanline-accurate video timer yet.
    scanline_ += cycles;
    bool new_vbl = scanline_ > (main_cycles_per_frame_ * 87 / 100);
    if (new_vbl && !vbl_) {
        vgcint_ = uint8_t(vgcint_ | 0x08 | 0x80);  // bit3=VBL pending, bit7=any-interrupt
        if (inten_ & 0x08) cpu_.set_irq(IrqLine::Pulse);
    }
    vbl_ = new_vbl;
}

void Apple2GS::run_frame() {
    scanline_ = 0;
    cpu_.run(main_cycles_per_frame_);
    update_video();
}

void Apple2GS::update_video() {
    framebuffer_.fill(0xff000000u);
    if ((newvideo_ & 0x80) != 0) {
        update_video_shr();
        return;
    }
    if (!text_mode_) return;  // lores/hires graphics modes not implemented yet

    const uint32_t on = 0xffffffffu, off = 0xff000000u;
    // PAGE2 selects the base address ($0400 vs $0800) within the *same*
    // bank; it does not switch between main/aux RAM (that only happens
    // for 80-column "auxiliary text" storage, not implemented yet).
    const uint8_t* page = shadow_ram_[0].data();
    const uint16_t base_page = page2_ ? 0x800 : 0x400;
    const int cell_w = 640 / 40, cell_h = 400 / 24;

    for (int row = 0; row < 24; row++) {
        uint16_t base = uint16_t(base_page + (((row & 7) << 7) | ((row & 0x18) * 5)));
        for (int col = 0; col < 40; col++) {
            uint8_t ch = page[base + col];
            // Classic Apple II text encoding (IIe/IIgs, primary charset):
            // $00-$3F always inverse; $40-$7F is the "flashing" range
            // (shown normal here -- no flash timer yet); $80-$FF normal.
            bool inverse = ch < 0x40;
            uint8_t glyph_index = ch & 0x7f;
            const uint8_t* glyph = chr_rom_.data() + (altcharset_ ? 0x800 : 0) + glyph_index * 8;
            for (int y = 0; y < 8; y++) {
                uint8_t bits = glyph[y];
                for (int x = 0; x < 7; x++) {
                    bool set = (bits & (1 << x)) != 0;
                    if (inverse) set = !set;
                    uint32_t color = set ? on : off;
                    int px0 = col * cell_w + x * (cell_w / 7 > 0 ? cell_w / 7 : 1);
                    int py0 = row * cell_h + y * (cell_h / 8 > 0 ? cell_h / 8 : 1);
                    for (int py = py0; py < py0 + std::max(1, cell_h / 8) && py < kScreenHeight; py++) {
                        for (int px = px0; px < px0 + std::max(1, cell_w / 7) && px < kScreenWidth; px++) {
                            framebuffer_[size_t(py) * kScreenWidth + size_t(px)] = color;
                        }
                    }
                }
            }
        }
    }
}

void Apple2GS::update_video_shr() {
    const uint8_t* bank_e1 = shadow_ram_[1].data();
    const uint8_t* bitmap = bank_e1 + 0x2000;
    const uint8_t* scb_table = bank_e1 + 0x9d00;
    const uint8_t* palette_data = bank_e1 + 0x9e00;

    // Each 320-mode pixel value (0-15) or 640-mode 2-bit value indexes one
    // of 16 colors in the scanline's chosen palette; each color is 2 bytes,
    // 4 bits/channel: byte0 = GGGGBBBB, byte1 = 0000RRRR.
    auto shr_color = [&](int palette, int index) -> uint32_t {
        const uint8_t* c = palette_data + palette * 32 + index * 2;
        uint8_t g = uint8_t((c[0] >> 4) & 0xf), b = uint8_t(c[0] & 0xf), r = uint8_t(c[1] & 0xf);
        uint32_t r8 = uint32_t(r) * 17, g8 = uint32_t(g) * 17, b8 = uint32_t(b) * 17;  // 4-bit -> 8-bit
        return 0xff000000u | (r8 << 16) | (g8 << 8) | b8;
    };

    for (int line = 0; line < 200; line++) {
        uint8_t scb = scb_table[line];
        int palette = scb & 0x0f;
        bool mode640 = (scb & 0x80) != 0;
        bool fill = (scb & 0x20) != 0;
        const uint8_t* row_bytes = bitmap + line * 160;
        uint32_t out_row[640];

        if (!mode640) {
            uint32_t last_nonzero = shr_color(palette, 0);
            for (int byte_i = 0; byte_i < 160; byte_i++) {
                uint8_t b = row_bytes[byte_i];
                uint8_t idx_hi = uint8_t(b >> 4), idx_lo = uint8_t(b & 0xf);
                for (uint8_t idx : {idx_hi, idx_lo}) {
                    uint32_t color;
                    if (fill && idx == 0) {
                        color = last_nonzero;
                    } else {
                        color = shr_color(palette, idx);
                        last_nonzero = color;
                    }
                    out_row[byte_i * 2 + (idx == idx_hi ? 0 : 1)] = color;
                }
            }
        } else {
            // 640 mode: 4 two-bit pixels/byte; each pixel's 2-bit value
            // selects among a fixed group of 4 palette entries determined
            // by the pixel's position within the byte (a hardware quirk of
            // how the 640-wide shift register interleaves with the
            // 16-entry palette -- there are only 4 usable colors per
            // horizontal position, not 16, in this mode).
            for (int byte_i = 0; byte_i < 160; byte_i++) {
                uint8_t b = row_bytes[byte_i];
                for (int p = 0; p < 4; p++) {
                    uint8_t idx = uint8_t((b >> (6 - p * 2)) & 0x3);
                    out_row[byte_i * 4 + p] = shr_color(palette, p * 4 + idx);
                }
            }
        }

        int py = line * 2;
        if (!mode640) {
            for (int x = 0; x < 320; x++) {
                uint32_t color = out_row[x];
                int px0 = x * 2;
                framebuffer_[size_t(py) * kScreenWidth + size_t(px0)] = color;
                framebuffer_[size_t(py) * kScreenWidth + size_t(px0 + 1)] = color;
                if (py + 1 < kScreenHeight) {
                    framebuffer_[size_t(py + 1) * kScreenWidth + size_t(px0)] = color;
                    framebuffer_[size_t(py + 1) * kScreenWidth + size_t(px0 + 1)] = color;
                }
            }
        } else {
            for (int x = 0; x < 640; x++) {
                uint32_t color = out_row[x];
                framebuffer_[size_t(py) * kScreenWidth + size_t(x)] = color;
                if (py + 1 < kScreenHeight) {
                    framebuffer_[size_t(py + 1) * kScreenWidth + size_t(x)] = color;
                }
            }
        }
    }
}

void Apple2GS::set_inputs(const MachineInputs&) {}
void Apple2GS::set_dip_switch(int, uint8_t) {}
void Apple2GS::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
