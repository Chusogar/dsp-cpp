#include "drivers/computers/vic20.h"

#include <cstring>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// VIC-20 BLK / IO decode helpers (same as MAME).
constexpr int kBlk0 = 0, kBlk4 = 4, kBlk5 = 5, kBlk6 = 6, kBlk7 = 7;
constexpr int kRam0 = 0, kIo0 = 4, kColor = 5;

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

}  // namespace

Vic20::Vic20(Region region)
    : region_(region),
      cpu_(region == Region::Pal ? Mos6560::kPalClock : Mos6560::kNtscClock),
      vic_(region == Region::Pal ? Mos6560::Variant::Pal6561 : Mos6560::Variant::Ntsc6560),
      via1_(region == Region::Pal ? Mos6560::kPalClock : Mos6560::kNtscClock),
      via2_(region == Region::Pal ? Mos6560::kPalClock : Mos6560::kNtscClock),
      framebuffer_(size_t(vic_.vis_width() * vic_.vis_height()), 0) {
    frames_per_second_ =
        double(vic_.clock()) / double(vic_.lines() * vic_.cycles_per_line());

    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    vic_.set_mem_read([this](uint16_t a) { return vic_videoram_r(a); });
    vic_.set_color_read([this](uint16_t a) {
        return uint8_t(color_ram_[a & 0x3ff] & 0x0f);
    });
    vic_.set_pot_read([]() { return uint8_t(0xff); }, []() { return uint8_t(0xff); });

    via1_.set_port_a([this]() { return via1_pa_r(); }, {});
    via1_.set_irq_callback([this](IrqLine s) {
        via1_nmi_ = (s != IrqLine::Clear);
        update_nmi();
    });

    via2_.set_port_a([this]() { return via2_pa_r(); },
                     [this](uint8_t v) { via2_pa_w(v); });
    via2_.set_port_b([this]() { return via2_pb_r(); },
                     [this](uint8_t v) { via2_pb_w(v); });
    via2_.set_irq_callback([this](IrqLine s) {
        via2_irq_ = (s != IrqLine::Clear);
        update_irq();
    });

    keyboard_.fill(0xff);
}

bool Vic20::init(const std::string& rom_path, std::string* error) {
    return load_roms(rom_path, error);
}

bool Vic20::load_roms(const std::string& path, std::string* error) {
    RomLoader loader;
    if (!loader.open(path, error)) return false;

    auto load_one = [&](const char* name, uint8_t* dst, size_t size) -> bool {
        std::vector<uint8_t> buf;
        if (!loader.try_read(name, buf) || buf.size() < size) return false;
        std::memcpy(dst, buf.data(), size);
        return true;
    };

    if (!load_one("901486-01.ue11", basic_rom_.data(), 0x2000) &&
        !load_one("901486-01", basic_rom_.data(), 0x2000) &&
        !load_one("basic", basic_rom_.data(), 0x2000) &&
        !load_one("basic.bin", basic_rom_.data(), 0x2000) &&
        !load_one("basic.rom", basic_rom_.data(), 0x2000)) {
        if (error) *error = "missing BASIC ROM (901486-01.ue11)";
        return false;
    }

    const bool want_pal = (region_ == Region::Pal);
    bool got_kernal = false;
    if (want_pal) {
        got_kernal = load_one("901486-07.ue12", kernal_rom_.data(), 0x2000) ||
                     load_one("901486-07", kernal_rom_.data(), 0x2000) ||
                     load_one("jiffydos vic-20 pal.ue12", kernal_rom_.data(), 0x2000);
    }
    if (!got_kernal) {
        got_kernal = load_one("901486-06.ue12", kernal_rom_.data(), 0x2000) ||
                     load_one("901486-06", kernal_rom_.data(), 0x2000) ||
                     load_one("901486-07.ue12", kernal_rom_.data(), 0x2000) ||
                     load_one("901486-07", kernal_rom_.data(), 0x2000) ||
                     load_one("kernal", kernal_rom_.data(), 0x2000) ||
                     load_one("kernal.bin", kernal_rom_.data(), 0x2000) ||
                     load_one("kernal.rom", kernal_rom_.data(), 0x2000);
    }
    if (!got_kernal) {
        if (error) *error = "missing KERNAL ROM (901486-06/07.ue12)";
        return false;
    }

    if (!load_one("901460-03.ud7", char_rom_.data(), 0x1000) &&
        !load_one("901460-03", char_rom_.data(), 0x1000) &&
        !load_one("chargen", char_rom_.data(), 0x1000) &&
        !load_one("chargen.bin", char_rom_.data(), 0x1000) &&
        !load_one("chargen.rom", char_rom_.data(), 0x1000)) {
        if (error) *error = "missing character ROM (901460-03.ud7)";
        return false;
    }

    reset();
    return true;
}

void Vic20::reset() {
    ram0_.fill(0);
    ram_main_.fill(0);
    color_ram_.fill(0);
    // Keep a previously attached BLK5 cart across soft reset.
    keyboard_.fill(0xff);
    key_row_ = key_col_ = 0xff;
    joy_ = 0xff;
    via1_nmi_ = via2_irq_ = false;
    pending_prg_.clear();
    boot_frames_ = 0;
    cpu_cycle_debt_ = 0;
    audio_acc_ = 0;
    audio_.clear();

    // Checkerboard-ish power-on RAM like MAME (optional; zeros also boot).
    uint8_t data = 0xff;
    for (size_t i = 0; i < ram0_.size(); i++) {
        ram0_[i] = data;
        if ((i % 64) == 0) data = uint8_t(data ^ 0xff);
    }
    data = 0xff;
    for (size_t i = 0; i < ram_main_.size(); i++) {
        ram_main_[i] = data;
        if ((i % 64) == 0) data = uint8_t(data ^ 0xff);
    }

    vic_.reset();
    via1_.reset();
    via2_.reset();
    cpu_.reset();
    update_irq();
    update_nmi();
}

void Vic20::update_irq() {
    cpu_.set_irq(via2_irq_ ? IrqLine::Assert : IrqLine::Clear);
}

void Vic20::update_nmi() {
    cpu_.set_nmi(via1_nmi_ ? IrqLine::Assert : IrqLine::Clear);
}

uint8_t Vic20::vic_videoram_r(uint16_t offset) {
    // VIC A13 inverted onto board A15: bit13 set → low RAM, clear → char ROM.
    if (offset & 0x2000) {
        switch ((offset >> 10) & 0x07) {
        case kRam0:
            return ram0_[offset & 0x3ff];
        default:
            // $1000-$1FFF main RAM window in the VIC 14-bit map.
            return ram_main_[offset & 0xfff];
        }
    }
    return char_rom_[offset & 0xfff];
}

uint8_t Vic20::read_byte(uint16_t addr) {
    const int blk = (addr >> 13) & 0x07;
    switch (blk) {
    case kBlk0:
        switch ((addr >> 10) & 0x07) {
        case kRam0:
            return ram0_[addr & 0x3ff];
        default:
            // $1000-$1FFF
            if ((addr & 0x1000) != 0) return ram_main_[addr & 0xfff];
            return vic_.bus_r();  // $0400-$0FFF open / expansion
        }
    case 1:
    case 2:
    case 3:
        return vic_.bus_r();  // BLK1-3 expansion open
    case kBlk4:
        switch ((addr >> 10) & 0x07) {
        default:
            return char_rom_[addr & 0xfff];
        case kIo0:
            if (addr & 0x10) return via1_.read(uint8_t(addr & 0x0f));
            if (addr & 0x20) return via2_.read(uint8_t(addr & 0x0f));
            if (addr >= 0x9000 && addr < 0x9010) return vic_.read(uint8_t(addr & 0x0f));
            return vic_.bus_r();
        case kColor:
            return uint8_t(color_ram_[addr & 0x3ff] | 0xf0);
        }
        break;
    case kBlk5:
        if (cart_size_ != 0) {
            const uint16_t off = uint16_t(addr & 0x1fff);
            if (off < cart_size_) return cart_blk5_[off];
        }
        return vic_.bus_r();  // cartridge open
    case kBlk6:
        return basic_rom_[addr & 0x1fff];
    case kBlk7:
        return kernal_rom_[addr & 0x1fff];
    default:
        break;
    }
    return vic_.bus_r();
}

void Vic20::write_byte(uint16_t addr, uint8_t value) {
    const int blk = (addr >> 13) & 0x07;
    switch (blk) {
    case kBlk0:
        switch ((addr >> 10) & 0x07) {
        case kRam0:
            ram0_[addr & 0x3ff] = value;
            break;
        default:
            if ((addr & 0x1000) != 0) ram_main_[addr & 0xfff] = value;
            break;
        }
        break;
    case kBlk4:
        switch ((addr >> 10) & 0x07) {
        case kIo0:
            if (addr & 0x10)
                via1_.write(uint8_t(addr & 0x0f), value);
            else if (addr & 0x20)
                via2_.write(uint8_t(addr & 0x0f), value);
            else if (addr >= 0x9000 && addr < 0x9010)
                vic_.write(uint8_t(addr & 0x0f), value);
            break;
        case kColor:
            color_ram_[addr & 0x3ff] = uint8_t(value & 0x0f);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

uint8_t Vic20::via1_pa_r() {
    // PA0 CLK IN, PA1 DATA IN (idle high), PA2-4 joy U/D/L, PA5 fire/lightpen,
    // PA6 cassette sense (high = no button).
    uint8_t data = 0x43;  // serial idle + cassette sense
    if (joy_ & 0x01) data = uint8_t(data | 0x04);  // up
    if (joy_ & 0x02) data = uint8_t(data | 0x08);  // down
    if (joy_ & 0x04) data = uint8_t(data | 0x10);  // left
    if (joy_ & 0x10) data = uint8_t(data | 0x20);  // fire
    return data;
}

uint8_t Vic20::via2_pa_r() {
    uint8_t data = 0xff;
    for (int i = 0; i < 8; i++) {
        if ((key_col_ & (1 << i)) == 0) data = uint8_t(data & keyboard_[size_t(i)]);
    }
    return data;
}

uint8_t Vic20::via2_pb_r() {
    // PB7 = joystick right (active low); other bits start high.
    uint8_t data = uint8_t(0x7f | (((joy_ >> 3) & 1) << 7));

    for (int i = 0; i < 8; i++) {
        if ((key_row_ & (1 << i)) == 0) {
            for (int c = 0; c < 8; c++) {
                if ((keyboard_[size_t(c)] & (1 << i)) == 0) data = uint8_t(data & ~(1 << c));
            }
        }
    }
    return data;
}

void Vic20::via2_pa_w(uint8_t data) { key_row_ = data; }
void Vic20::via2_pb_w(uint8_t data) { key_col_ = data; }

void Vic20::on_cycles(int cycles) {
    if (cycles <= 0) return;
    via1_.tick(cycles);
    via2_.tick(cycles);

    audio_acc_ += int64_t(cycles) * Mos6560::kSampleRate;
    const int64_t den = int64_t(vic_.clock());
    while (audio_acc_ >= den) {
        audio_acc_ -= den;
        audio_.push_back(vic_.update());
    }
}

bool Vic20::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open: " + path;
        return false;
    }
    if (ends_ci(path, ".crt") || ends_ci(path, ".bin") || ends_ci(path, ".rom")) {
        // Prefer a raw BLK5 cart dump when the size matches 4K/8K; otherwise
        // fall through so a misnamed PRG still injects.
        if (data.size() == 0x1000 || data.size() == 0x2000) {
            return load_cart(data, error);
        }
    }
    if (ends_ci(path, ".prg") || data.size() >= 3) {
        return queue_prg(data, error);
    }
    if (error) *error = "unsupported VIC-20 media: " + path;
    return false;
}

bool Vic20::load_cart(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() != 0x1000 && data.size() != 0x2000) {
        if (error) *error = "VIC-20 cart must be 4K or 8K";
        return false;
    }
    cart_blk5_.fill(0xff);
    std::memcpy(cart_blk5_.data(), data.data(), data.size());
    cart_size_ = data.size();
    // Autostart carts live at $A000; a reset lets the KERNAL jump into them.
    reset();
    return true;
}

bool Vic20::queue_prg(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 3) {
        if (error) *error = "PRG too small";
        return false;
    }
    pending_prg_ = data;
    boot_frames_ = 0;
    return true;
}

void Vic20::update_pending_prg() {
    if (pending_prg_.empty()) return;
    if (++boot_frames_ < kPrgInjectFrames) return;
    // Unexpanded VIC-20: TXTTAB must point at $1001 after BASIC cold start.
    if (ram0_[0x2B] != 0x01 || ram0_[0x2C] != 0x10) return;

    const std::vector<uint8_t> data = std::move(pending_prg_);
    pending_prg_.clear();
    inject_prg(data);
}

void Vic20::inject_prg(const std::vector<uint8_t>& data) {
    const uint16_t addr = uint16_t(data[0] | (data[1] << 8));
    const size_t n = data.size() - 2;
    for (size_t i = 0; i < n; i++) {
        write_byte(uint16_t(addr + i), data[i + 2]);
    }
    const uint16_t end = uint16_t(addr + n);
    if (addr != 0x1001) return;

    // VARTAB / ARYTAB / STREND + EAL for a BASIC program.
    ram0_[0x2D] = uint8_t(end & 0xFF); ram0_[0x2E] = uint8_t(end >> 8);
    ram0_[0x2F] = uint8_t(end & 0xFF); ram0_[0x30] = uint8_t(end >> 8);
    ram0_[0x31] = uint8_t(end & 0xFF); ram0_[0x32] = uint8_t(end >> 8);
    ram0_[0xAE] = uint8_t(end & 0xFF); ram0_[0xAF] = uint8_t(end >> 8);

    // Autostart via the KERNAL keyboard buffer ($0277 / count $C6), same as C64.
    static constexpr uint8_t kRun[] = {'R', 'U', 'N', 0x0D};
    for (size_t i = 0; i < sizeof(kRun); i++) ram0_[0x0277 + i] = kRun[i];
    ram0_[0xC6] = uint8_t(sizeof(kRun));
}

void Vic20::run_frame() {
    update_pending_prg();
    const int lines = vic_.lines();
    const int cpl = vic_.cycles_per_line();
    for (int line = 0; line < lines; ++line) {
        vic_.update_line(line);
        cpu_cycle_debt_ += cpl;
        if (cpu_cycle_debt_ > 0) {
            const int ran = cpu_.run(cpu_cycle_debt_);
            cpu_cycle_debt_ -= ran;
        }
    }
    vic_.blit_visible(framebuffer_.data());
}

void Vic20::set_inputs(const MachineInputs& inputs) {
    keyboard_.fill(0xff);
    auto press = [this](int column, uint8_t row_mask) {
        keyboard_[size_t(column)] = uint8_t(keyboard_[size_t(column)] & ~row_mask);
    };
    auto key = [&inputs](Key id) { return inputs.key(id); };

    // VIC-20 matrix (MAME COL0..COL7): keyboard_[col] holds active-low rows.
    // COL0: 1,3,5,7,9,+,£,DEL
    if (key(Key::Num1)) press(0, 0x01);
    if (key(Key::Num3)) press(0, 0x02);
    if (key(Key::Num5)) press(0, 0x04);
    if (key(Key::Num7)) press(0, 0x08);
    if (key(Key::Num9)) press(0, 0x10);
    if (key(Key::Plus) || key(Key::Equals)) press(0, 0x20);
    if (key(Key::Backslash)) press(0, 0x40);  // £
    if (key(Key::Backspace) || key(Key::Delete)) press(0, 0x80);

    // COL1: ←,W,R,Y,I,P,*,RETURN
    if (key(Key::Backquote)) press(1, 0x01);
    if (key(Key::W)) press(1, 0x02);
    if (key(Key::R)) press(1, 0x04);
    if (key(Key::Y)) press(1, 0x08);
    if (key(Key::I)) press(1, 0x10);
    if (key(Key::P)) press(1, 0x20);
    if (key(Key::Asterisk)) press(1, 0x40);
    if (key(Key::Enter)) press(1, 0x80);

    // COL2: CTRL,A,D,G,J,L,;,CRSR LR
    if (key(Key::Tab) || key(Key::LeftCtrl)) press(2, 0x01);  // CTRL (Tab also)
    if (key(Key::A)) press(2, 0x02);
    if (key(Key::D)) press(2, 0x04);
    if (key(Key::G)) press(2, 0x08);
    if (key(Key::J)) press(2, 0x10);
    if (key(Key::L)) press(2, 0x20);
    if (key(Key::Semicolon)) press(2, 0x40);
    if (key(Key::Right) || key(Key::Left)) press(2, 0x80);

    // COL3: RUN/STOP,LSHIFT,X,V,N,,,/,CRSR UD
    if (key(Key::Escape) || key(Key::Home)) press(3, 0x01);  // RUN/STOP
    if (key(Key::LeftShift)) press(3, 0x02);
    if (key(Key::X)) press(3, 0x04);
    if (key(Key::V)) press(3, 0x08);
    if (key(Key::N)) press(3, 0x10);
    if (key(Key::Comma)) press(3, 0x20);
    if (key(Key::Slash)) press(3, 0x40);
    if (key(Key::Down) || key(Key::Up)) press(3, 0x80);

    // COL4: SPACE,Z,C,B,M,.,RSHIFT,F1
    if (key(Key::Space)) press(4, 0x01);
    if (key(Key::Z)) press(4, 0x02);
    if (key(Key::C)) press(4, 0x04);
    if (key(Key::B)) press(4, 0x08);
    if (key(Key::M)) press(4, 0x10);
    if (key(Key::Period)) press(4, 0x20);
    if (key(Key::RightShift)) press(4, 0x40);
    if (key(Key::F1)) press(4, 0x80);

    // COL5: CBM,S,F,H,K,:,=,F3
    if (key(Key::Cbm) || key(Key::LeftGui) || key(Key::RightCtrl)) press(5, 0x01);
    if (key(Key::S)) press(5, 0x02);
    if (key(Key::F)) press(5, 0x04);
    if (key(Key::H)) press(5, 0x08);
    if (key(Key::K)) press(5, 0x10);
    if (key(Key::Quote)) press(5, 0x20);  // :
    if (key(Key::Minus)) press(5, 0x40);  // =
    if (key(Key::F3)) press(5, 0x80);

    // COL6: Q,E,T,U,O,@,↑,F5
    if (key(Key::Q)) press(6, 0x01);
    if (key(Key::E)) press(6, 0x02);
    if (key(Key::T)) press(6, 0x04);
    if (key(Key::U)) press(6, 0x08);
    if (key(Key::O)) press(6, 0x10);
    if (key(Key::At)) press(6, 0x20);
    if (key(Key::Delete)) press(6, 0x40);  // ↑
    if (key(Key::F5)) press(6, 0x80);

    // COL7: 2,4,6,8,0,-,HOME,F7
    if (key(Key::Num2)) press(7, 0x01);
    if (key(Key::Num4)) press(7, 0x02);
    if (key(Key::Num6)) press(7, 0x04);
    if (key(Key::Num8)) press(7, 0x08);
    if (key(Key::Num0)) press(7, 0x10);
    if (key(Key::Minus)) press(7, 0x20);
    if (key(Key::Home)) press(7, 0x40);
    if (key(Key::F7)) press(7, 0x80);

    // Cursor left/up need SHIFT on VIC-20.
    if (key(Key::Left) || key(Key::Up)) press(3, 0x02);

    // Joystick (active low).
    joy_ = 0xff;
    const auto& p = inputs.player1;
    if (p.up || key(Key::Up)) joy_ = uint8_t(joy_ & ~0x01);
    if (p.down || key(Key::Down)) joy_ = uint8_t(joy_ & ~0x02);
    if (p.left || key(Key::Left)) joy_ = uint8_t(joy_ & ~0x04);
    if (p.right || key(Key::Right)) joy_ = uint8_t(joy_ & ~0x08);
    if (p.button1) joy_ = uint8_t(joy_ & ~0x10);

    // RESTORE → VIA1 CA1 (NMI).
    via1_.write_ca1(!key(Key::F12));
}

void Vic20::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
