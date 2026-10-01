#include "drivers/computers/atari_st.h"

#include <algorithm>
#include <cstring>

#include <zlib.h>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kTos104 = {{"tos104.bin", 0x30000, 0x0000, 0x90f4fbff}};
const std::vector<RomEntry> kTos102 = {{"tos102.bin", 0x30000, 0x0000, 0xd3c32283}};
const std::vector<RomEntry> kTos100 = {{"tos100.bin", 0x30000, 0x0000, 0xd331af30}};

struct IkbdMap {
    Key key;
    uint8_t code;
};

const IkbdMap kIkbd[] = {
    {Key::Escape, 0x01},    {Key::Num1, 0x02},     {Key::Num2, 0x03},
    {Key::Num3, 0x04},      {Key::Num4, 0x05},     {Key::Num5, 0x06},
    {Key::Num6, 0x07},      {Key::Num7, 0x08},     {Key::Num8, 0x09},
    {Key::Num9, 0x0a},      {Key::Num0, 0x0b},     {Key::Minus, 0x0c},
    {Key::Equals, 0x0d},    {Key::Backspace, 0x0e},{Key::Tab, 0x0f},
    {Key::Q, 0x10},         {Key::W, 0x11},        {Key::E, 0x12},
    {Key::R, 0x13},         {Key::T, 0x14},        {Key::Y, 0x15},
    {Key::U, 0x16},         {Key::I, 0x17},        {Key::O, 0x18},
    {Key::P, 0x19},         {Key::Enter, 0x1c},    {Key::LeftCtrl, 0x1d},
    {Key::A, 0x1e},         {Key::S, 0x1f},        {Key::D, 0x20},
    {Key::F, 0x21},         {Key::G, 0x22},        {Key::H, 0x23},
    {Key::J, 0x24},         {Key::K, 0x25},        {Key::L, 0x26},
    {Key::Semicolon, 0x27}, {Key::Quote, 0x28},    {Key::LeftShift, 0x2a},
    {Key::Z, 0x2c},         {Key::X, 0x2d},        {Key::C, 0x2e},
    {Key::V, 0x2f},         {Key::B, 0x30},        {Key::N, 0x31},
    {Key::M, 0x32},         {Key::Comma, 0x33},    {Key::Period, 0x34},
    {Key::Slash, 0x35},     {Key::RightShift, 0x36},{Key::Space, 0x39},
    {Key::CapsLock, 0x3a},  {Key::F1, 0x3b},       {Key::F2, 0x3c},
    {Key::F3, 0x3d},        {Key::F4, 0x3e},       {Key::F5, 0x3f},
    {Key::F6, 0x40},        {Key::F7, 0x41},       {Key::F8, 0x42},
    {Key::F9, 0x43},        {Key::F10, 0x44},      {Key::Home, 0x47},
    {Key::Up, 0x48},        {Key::Left, 0x4b},     {Key::Right, 0x4d},
    {Key::Down, 0x50},
};

// Hataroid's on-screen keyboard (tools/gen_st_vkb.py).
struct VkbKey {
    const char* label;
    uint8_t scancode;
    bool poly;      // F keys: quad in v[0..7]; others: rect v[0..3]
    bool modifier;  // Shift / Ctrl / Alt latch until the next key
    short v[8];
};
#include "drivers/computers/atari_st_vkb_data.inc"

constexpr int kVkbTexW = 768;
constexpr int kVkbTexH = 256;

bool vkb_contains(const VkbKey& k, int tx, int ty) {
    if (!k.poly) return tx >= k.v[0] && tx <= k.v[2] && ty >= k.v[1] && ty <= k.v[3];
    // Convex quad (the slanted function keys): same side of every edge.
    int sign = 0;
    for (int i = 0; i < 4; i++) {
        const int x1 = k.v[i * 2], y1 = k.v[i * 2 + 1];
        const int x2 = k.v[((i + 1) % 4) * 2], y2 = k.v[((i + 1) % 4) * 2 + 1];
        const int cross = (x2 - x1) * (ty - y1) - (y2 - y1) * (tx - x1);
        if (cross == 0) continue;
        const int s = cross > 0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return true;
}

void vkb_bounds(const VkbKey& k, int* x1, int* y1, int* x2, int* y2) {
    if (!k.poly) {
        *x1 = k.v[0];
        *y1 = k.v[1];
        *x2 = k.v[2];
        *y2 = k.v[3];
        return;
    }
    *x1 = *x2 = k.v[0];
    *y1 = *y2 = k.v[1];
    for (int i = 1; i < 4; i++) {
        *x1 = std::min(*x1, int(k.v[i * 2]));
        *x2 = std::max(*x2, int(k.v[i * 2]));
        *y1 = std::min(*y1, int(k.v[i * 2 + 1]));
        *y2 = std::max(*y2, int(k.v[i * 2 + 1]));
    }
}

uint32_t st_color(uint16_t w) {
    const int r = (w >> 8) & 7;
    const int g = (w >> 4) & 7;
    const int b = w & 7;
    return 0xff000000u | uint32_t(r * 36) << 16 | uint32_t(g * 36) << 8 | uint32_t(b * 36);
}

}  // namespace

AtariSt::AtariSt() : cpu_(kCpuClock), psg_(2000000, 1.2f) {
    vkb_init();
    ram_.assign(kRamSize, 0);
    rom_.assign(kRomSize, 0xff);
    cpu_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                             [this](uint32_t a, uint16_t v) { write_word(a, v); });
    cpu_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                           [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cpu_cycles(c); });
    // Copy-protected loaders (Top Gun's) patch the instruction words the
    // 68000 has already prefetched.
    cpu_.set_prefetch_emulation(true);
    cpu_.set_reset_instruction_handler([this]() {
        mfp_.reset();
        floppy_.reset();
        psg_.reset();
        ikbd_rx_.clear();
        ikbd_pending_.clear();
        mfp_.set_gpip_bit(7, 1);
        mfp_.set_gpip_bit(5, 1);
        mfp_.set_gpip_bit(4, 1);
    });
    cpu_.set_irq_acknowledge([this](int level) {
        if (level == 6) return mfp_.irq_ack();
        return -1;
    });
    psg_.set_port_handlers(nullptr, nullptr,
                           [this](uint8_t v) {
                               psg_port_a_ = v;
                               floppy_.set_psg_port_a(v);
                           },
                           nullptr);
    mfp_.set_irq_callback([this](bool asserted) {
        cpu_.set_irq(6, asserted ? IrqLine::Assert : IrqLine::Clear);
    });
    floppy_.set_ram(ram_.data(), uint32_t(ram_.size()));
}

bool AtariSt::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::string ignored;
    rom_.assign(kRomSize, 0xff);
    if (!loader.load(kTos104, rom_, &ignored) && !loader.load(kTos102, rom_, &ignored) &&
        !loader.load(kTos100, rom_, error)) {
        if (error && error->empty()) *error = "Atari ST TOS ROM not found in " + rom_path;
        return false;
    }
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void AtariSt::reset() {
    std::fill(ram_.begin(), ram_.end(), 0);
    palette_.fill(0);
    rom_at_zero_ = true;
    memcfg_ = 0;
    video_hi_ = video_mid_ = video_lo_ = 0;
    sync_mode_ = 0x02;
    resolution_ = 0;
    psg_port_a_ = 0xff;
    acia_control_ = 0;
    acia_rdr_ = 0;
    ikbd_rx_.clear();
    ikbd_pending_.clear();
    ikbd_cmd_.clear();
    ikbd_cmd_need_ = 0;
    ikbd_reset_modes();
    last_pointer_x_ = last_pointer_y_ = 0;
    pointer_frac_x_ = pointer_frac_y_ = 0;
    pointer_seen_ = false;
    seed_valid_ = false;
    last_pointer_b1_ = last_pointer_b2_ = false;
    video_count_ = 0;
    blit_halftone_.fill(0);
    blit_sxinc_ = blit_syinc_ = blit_dxinc_ = blit_dyinc_ = 0;
    blit_src_ = blit_dst_ = 0;
    blit_emask_[0] = blit_emask_[1] = blit_emask_[2] = 0;
    blit_xcount_ = blit_ycount_ = 0;
    blit_hop_ = blit_op_ = blit_ctrl_ = blit_skew_ = 0;
    blit_defer_start_ = false;
    keys_down_.fill(false);
    vkb_pressed_ = -1;
    vkb_latched_.fill(false);
    vkb_button_down_ = false;
    mfp_acc_ = 0;
    audio_acc_ = 0;
    audio_.clear();
    mfp_.reset();
    psg_.reset();
    floppy_.reset();
    floppy_.set_ram(ram_.data(), uint32_t(ram_.size()));
    // Colour monitor: GPIP bit 7 high. FDC/ACIA idle high (active low IRQs).
    mfp_.set_gpip_bit(7, 1);
    mfp_.set_gpip_bit(5, 1);
    mfp_.set_gpip_bit(4, 1);
    cpu_.reset();
}

bool AtariSt::load_media(const std::string& path, std::string* error) {
    if (!floppy_.load_file(path, error)) return false;
    floppy_.set_ram(ram_.data(), uint32_t(ram_.size()));
    return true;
}

void AtariSt::ikbd_push(uint8_t value) {
    // ~1 ms at 8 MHz. The 6850 has a one-byte buffer; extra bytes wait until
    // TOS reads RDR so GPIP4 can rise and fall again (MFP is edge triggered).
    ikbd_pending_.push_back({value, 8000});
}

void AtariSt::service_acia() {
    if (!ikbd_rx_.empty()) return;
    if (ikbd_pending_.empty() || ikbd_pending_.front().cycles > 0) return;
    ikbd_rx_.push_back(ikbd_pending_.front().value);
    acia_rdr_ = ikbd_pending_.front().value;
    ikbd_pending_.pop_front();
    mfp_.set_gpip_bit(4, 0);
}

namespace {

// Bytes taken by each IKBD command, command byte included (0 = unknown,
// ignored). From the Atari "Intelligent Keyboard (ikbd) Protocol".
int ikbd_command_length(uint8_t cmd) {
    switch (cmd) {
        case 0x07: return 2;   // set mouse button action
        case 0x08: return 1;   // relative mouse
        case 0x09: return 5;   // absolute mouse: xmax, ymax
        case 0x0a: return 3;   // mouse keycode mode
        case 0x0b: return 3;   // mouse threshold
        case 0x0c: return 3;   // mouse scale
        case 0x0d: return 1;   // interrogate mouse position
        case 0x0e: return 6;   // load mouse position
        case 0x0f: return 1;   // Y origin at the bottom
        case 0x10: return 1;   // Y origin at the top
        case 0x11: return 1;   // resume
        case 0x12: return 1;   // disable mouse
        case 0x13: return 1;   // pause output
        case 0x14: return 1;   // joystick event reporting
        case 0x15: return 1;   // joystick interrogation mode
        case 0x16: return 1;   // joystick interrogate
        case 0x17: return 2;   // joystick monitoring
        case 0x18: return 1;   // fire button monitoring
        case 0x19: return 7;   // joystick keycode mode
        case 0x1a: return 1;   // disable joysticks
        case 0x1b: return 7;   // set time of day
        case 0x1c: return 1;   // interrogate time of day
        case 0x20: return 4;   // memory load: address, count (+ data)
        case 0x21: return 3;   // memory read
        case 0x22: return 3;   // controller execute
        case 0x80: return 2;   // reset ($80 $01)
        default: break;
    }
    if (cmd >= 0x87 && cmd <= 0x9a) return 1;  // status inquiries
    return 0;
}

uint8_t bcd_inc(uint8_t v) {
    v = uint8_t(v + 1);
    if ((v & 0x0f) > 9) v = uint8_t((v & 0xf0) + 0x10);
    return v;
}

}  // namespace

void AtariSt::ikbd_reset_modes() {
    mouse_mode_ = MouseMode::Relative;
    joy_mode_ = JoyMode::Event;
    ikbd_paused_ = false;
    mouse_y_bottom_ = false;
    mouse_button_action_ = 0;
    abs_x_ = abs_y_ = 0;
    abs_max_x_ = 319;
    abs_max_y_ = 199;
    abs_buttons_ = 0;
    abs_left_ = abs_right_ = false;
    joy_monitor_rate_ = 0;
    joy_monitor_count_ = 0;
}

void AtariSt::ikbd_byte(uint8_t value) {
    if (ikbd_cmd_.empty()) {
        const int need = ikbd_command_length(value);
        if (need == 0) return;  // not a command: the 6301 ignores it
        ikbd_cmd_need_ = need;
    }
    ikbd_cmd_.push_back(value);
    // Memory load: the count byte says how many data bytes follow.
    if (ikbd_cmd_[0] == 0x20 && ikbd_cmd_.size() == 4) ikbd_cmd_need_ = 4 + ikbd_cmd_[3];
    if (int(ikbd_cmd_.size()) < ikbd_cmd_need_) return;
    const std::vector<uint8_t> cmd = ikbd_cmd_;
    ikbd_cmd_.clear();
    ikbd_cmd_need_ = 0;
    ikbd_command(cmd);
}

void AtariSt::ikbd_command(const std::vector<uint8_t>& cmd) {
    // Any command but Pause lifts a pause.
    if (cmd[0] != 0x13) ikbd_paused_ = false;
    switch (cmd[0]) {
        case 0x80:
            if (cmd[1] != 0x01) return;
            ikbd_reset_modes();
            ikbd_push(0xf1);  // self test passed / version
            return;
        case 0x07: mouse_button_action_ = cmd[1]; return;
        case 0x08: mouse_mode_ = MouseMode::Relative; return;
        case 0x09:
            mouse_mode_ = MouseMode::Absolute;
            abs_max_x_ = (cmd[1] << 8) | cmd[2];
            abs_max_y_ = (cmd[3] << 8) | cmd[4];
            abs_x_ = std::min(abs_x_, abs_max_x_);
            abs_y_ = std::min(abs_y_, abs_max_y_);
            return;
        case 0x0a: mouse_mode_ = MouseMode::Keycode; return;
        case 0x0b:
        case 0x0c: return;
        case 0x0d: {
            if (mouse_mode_ != MouseMode::Absolute) return;
            ikbd_push(0xf7);
            ikbd_push(abs_buttons_);
            abs_buttons_ = 0;
            const int y = mouse_y_bottom_ ? abs_max_y_ - abs_y_ : abs_y_;
            ikbd_push(uint8_t(abs_x_ >> 8));
            ikbd_push(uint8_t(abs_x_));
            ikbd_push(uint8_t(y >> 8));
            ikbd_push(uint8_t(y));
            return;
        }
        case 0x0e: {
            abs_x_ = std::clamp((cmd[2] << 8) | cmd[3], 0, abs_max_x_);
            const int y = std::clamp((cmd[4] << 8) | cmd[5], 0, abs_max_y_);
            abs_y_ = mouse_y_bottom_ ? abs_max_y_ - y : y;
            return;
        }
        case 0x0f: mouse_y_bottom_ = true; return;
        case 0x10: mouse_y_bottom_ = false; return;
        case 0x11: return;
        case 0x12: mouse_mode_ = MouseMode::Off; return;
        case 0x13: ikbd_paused_ = true; return;
        case 0x14:
            joy_mode_ = JoyMode::Event;
            mouse_mode_ = MouseMode::Off;  // joystick 0 replaces the mouse
            return;
        case 0x15:
            joy_mode_ = JoyMode::Interrogate;
            mouse_mode_ = MouseMode::Off;
            return;
        case 0x16:
            ikbd_push(0xfd);
            ikbd_push(joy_state_[0]);
            ikbd_push(joy_state_[1]);
            return;
        case 0x17:
            joy_mode_ = JoyMode::Monitor;
            mouse_mode_ = MouseMode::Off;
            joy_monitor_rate_ = cmd[1];
            joy_monitor_count_ = 0;
            return;
        case 0x18:
        case 0x19:
        case 0x1a:
            joy_mode_ = JoyMode::Off;
            return;
        case 0x1b:
            for (int i = 0; i < 6; i++) {
                // Bytes that are not valid BCD leave that field unchanged.
                if ((cmd[size_t(1 + i)] & 0x0f) <= 9 && (cmd[size_t(1 + i)] >> 4) <= 9) clock_[i] = cmd[size_t(1 + i)];
            }
            clock_frames_ = 0;
            return;
        case 0x1c:
            ikbd_push(0xfc);
            for (uint8_t b : clock_) ikbd_push(b);
            return;
        case 0x20:
        case 0x22: return;
        case 0x21:
            ikbd_push(0xf6);
            ikbd_push(0x20);
            for (int i = 0; i < 6; i++) ikbd_push(0);
            return;
        default:
            break;
    }
    if (cmd[0] >= 0x87 && cmd[0] <= 0x9a) {
        // Status inquiry: the reply looks like the matching set command.
        uint8_t reply[7] = {0, 0, 0, 0, 0, 0, 0};
        switch (cmd[0]) {
            case 0x87: reply[0] = 0x07; reply[1] = mouse_button_action_; break;
            case 0x88:
                reply[0] = mouse_mode_ == MouseMode::Absolute ? 0x09
                           : mouse_mode_ == MouseMode::Keycode ? 0x0a : 0x08;
                if (mouse_mode_ == MouseMode::Absolute) {
                    reply[1] = uint8_t(abs_max_x_ >> 8);
                    reply[2] = uint8_t(abs_max_x_);
                    reply[3] = uint8_t(abs_max_y_ >> 8);
                    reply[4] = uint8_t(abs_max_y_);
                }
                break;
            case 0x8b: reply[0] = 0x0b; reply[1] = 1; reply[2] = 1; break;
            case 0x8c: reply[0] = 0x0c; reply[1] = 1; reply[2] = 1; break;
            case 0x8f:
            case 0x90: reply[0] = mouse_y_bottom_ ? 0x0f : 0x10; break;
            case 0x92: reply[0] = mouse_mode_ == MouseMode::Off ? 0x12 : 0x00; break;
            case 0x94:
            case 0x95:
            case 0x99:
                reply[0] = joy_mode_ == JoyMode::Interrogate ? 0x15 : 0x14;
                break;
            case 0x9a: reply[0] = joy_mode_ == JoyMode::Off ? 0x1a : 0x00; break;
            default: break;
        }
        ikbd_push(0xf6);
        for (uint8_t b : reply) ikbd_push(b);
    }
}

void AtariSt::ikbd_mouse_report(int dx, int dy, bool left, bool right) {
    if (ikbd_paused_) return;
    const bool left_changed = left != abs_left_;
    const bool right_changed = right != abs_right_;
    if (left_changed) abs_buttons_ = uint8_t(abs_buttons_ | (left ? 0x04 : 0x08));
    if (right_changed) abs_buttons_ = uint8_t(abs_buttons_ | (right ? 0x01 : 0x02));
    abs_left_ = left;
    abs_right_ = right;
    switch (mouse_mode_) {
        case MouseMode::Relative:
            ikbd_mouse_packet(dx, mouse_y_bottom_ ? -dy : dy, left, right);
            return;
        case MouseMode::Absolute:
            abs_x_ = std::clamp(abs_x_ + dx, 0, abs_max_x_);
            abs_y_ = std::clamp(abs_y_ + dy, 0, abs_max_y_);
            // Button action bit 2: buttons send key codes $74 / $75.
            if (mouse_button_action_ & 0x04) {
                if (left_changed) ikbd_push(left ? 0x74 : 0xf4);
                if (right_changed) ikbd_push(right ? 0x75 : 0xf5);
            }
            return;
        case MouseMode::Keycode:
        case MouseMode::Off:
            return;
    }
}

void AtariSt::ikbd_joysticks(const MachineInputs& inputs) {
    auto state = [](const InputState& p) {
        uint8_t v = 0;
        if (p.up) v |= 0x01;
        if (p.down) v |= 0x02;
        if (p.left) v |= 0x04;
        if (p.right) v |= 0x08;
        if (p.button1) v |= 0x80;
        return v;
    };
    // Joystick 1 is the game port; joystick 0 shares the mouse port.
    const uint8_t now[2] = {state(inputs.player2), state(inputs.player1)};
    for (int j = 0; j < 2; j++) {
        const bool changed = now[j] != joy_state_[j];
        joy_state_[j] = now[j];
        if (!changed || ikbd_paused_ || joy_mode_ != JoyMode::Event) continue;
        if (j == 0 && mouse_mode_ != MouseMode::Off) continue;  // the mouse owns port 0
        ikbd_push(uint8_t(0xfe + j));
        ikbd_push(joy_state_[j]);
    }
    if (joy_mode_ == JoyMode::Monitor && !ikbd_paused_) {
        // Rate is in 1/100 s; frames are 1/50 s.
        if (++joy_monitor_count_ * 2 >= std::max(joy_monitor_rate_, 2)) {
            joy_monitor_count_ = 0;
            ikbd_push(uint8_t(((joy_state_[1] >> 7) << 1) | (joy_state_[0] >> 7)));
            ikbd_push(uint8_t(((joy_state_[1] & 0x0f) << 4) | (joy_state_[0] & 0x0f)));
        }
    }
}

void AtariSt::ikbd_clock_tick() {
    if (++clock_frames_ < 50) return;
    clock_frames_ = 0;
    static const uint8_t kLimit[6] = {0x99, 0x12, 0x31, 0x23, 0x59, 0x59};
    for (int i = 5; i >= 3; i--) {
        if (clock_[i] < kLimit[i]) {
            clock_[i] = bcd_inc(clock_[i]);
            return;
        }
        clock_[i] = 0;
    }
}

std::vector<uint8_t> AtariSt::ikbd_pending_bytes() const {
    std::vector<uint8_t> out;
    out.reserve(ikbd_pending_.size());
    for (const IkbdByte& b : ikbd_pending_) out.push_back(b.value);
    return out;
}

void AtariSt::ikbd_mouse_packet(int dx, int dy, bool left, bool right) {
    uint8_t head = 0xf8;
    if (left) head = uint8_t(head | 2);
    if (right) head = uint8_t(head | 1);
    ikbd_push(head);
    ikbd_push(uint8_t(dx));
    ikbd_push(uint8_t(dy));
}

void AtariSt::ikbd_mouse(const MachineInputs& inputs) {
    if (vkb_visible_ && inputs.has_pointer && !inputs.pointer_relative &&
        inputs.pointer_y >= kVkbTop) {
        // The pointer works the on-screen keyboard: freeze the ST mouse and
        // keep tracking so it does not jump when the pointer comes back up.
        last_pointer_x_ = inputs.pointer_x;
        last_pointer_y_ = std::min(inputs.pointer_y, kVkbTop - 1);
        return;
    }
    if (!inputs.has_pointer) {
        pointer_seen_ = false;  // re-seed when the pointer comes back
        seed_valid_ = false;
        return;
    }
    if (inputs.pointer_resync) {
        // The mouse came back into the window: line up again on its next move.
        pointer_seen_ = false;
        seed_valid_ = false;
    }
    int dx_host = 0, dy_host = 0;
    if (inputs.pointer_relative) {
        // Captured host mouse: plain motion, no absolute position to track.
        dx_host = inputs.pointer_dx;
        dy_host = inputs.pointer_dy;
        pointer_seen_ = true;
    } else if (!pointer_seen_) {
        // The pointer just came (back) over the window. The ST cursor is
        // wherever the program left it, so line it up: slam it into the
        // top-left corner (every program clamps there), then move it to
        // the host position. Wait for the first real movement, so the sync
        // is not spent while TOS or the game is still starting up.
        if (!seed_valid_ || (inputs.pointer_x == seed_x_ && inputs.pointer_y == seed_y_)) {
            seed_valid_ = true;
            seed_x_ = inputs.pointer_x;
            seed_y_ = inputs.pointer_y;
            return;
        }
        last_pointer_x_ = inputs.pointer_x;
        last_pointer_y_ = inputs.pointer_y;
        last_pointer_b1_ = inputs.pointer_button1;
        last_pointer_b2_ = inputs.pointer_button2;
        pointer_seen_ = true;
        pointer_frac_x_ = pointer_frac_y_ = 0;
        for (int i = 0; i < 6; i++)
            ikbd_mouse_report(-127, -127, inputs.pointer_button1, inputs.pointer_button2);
        dx_host = inputs.pointer_x;
        dy_host = inputs.pointer_y;
    } else {
        dx_host = inputs.pointer_x - last_pointer_x_;
        dy_host = inputs.pointer_y - last_pointer_y_;
        last_pointer_x_ = inputs.pointer_x;
        last_pointer_y_ = inputs.pointer_y;
        // Uncaptured absolute pointer: the ST only gets relative packets and
        // each program clamps its own cursor, so the two drift apart. While
        // the host pointer rests on a window edge keep pushing outwards; the
        // ST cursor stops on the same edge and both line up again.
        if (inputs.pointer_x <= 0) dx_host -= 16;
        if (inputs.pointer_x >= kWidth - 1) dx_host += 16;
        if (inputs.pointer_y <= 0) dy_host -= 16;
        if (inputs.pointer_y >= kHeight - 1) dy_host += 16;
    }

    // The shifter framebuffer is 640×400 with low/med doubled. IKBD deltas are
    // TOS screen pixels (320×200 low, 640×200 med, 640×400 high).
    const int mode = resolution_ & 3;
    const int xs = (mode == 0) ? 2 : 1;
    const int ys = (mode == 2) ? 1 : 2;
    pointer_frac_x_ += dx_host;
    pointer_frac_y_ += dy_host;
    int dx = pointer_frac_x_ / xs;
    int dy = pointer_frac_y_ / ys;
    pointer_frac_x_ -= dx * xs;
    pointer_frac_y_ -= dy * ys;

    const bool left = inputs.pointer_button1;
    const bool right = inputs.pointer_button2;
    const bool bchange = left != last_pointer_b1_ || right != last_pointer_b2_;
    last_pointer_b1_ = left;
    last_pointer_b2_ = right;
    if (!dx && !dy && !bchange) return;

    // Relative reports are signed 8-bit. Split so a fast host flick cannot
    // wrap; avoid -128 (0x80) which is also the IKBD reset prefix.
    bool send_button = bchange;
    while (dx || dy || send_button) {
        int sx = dx;
        int sy = dy;
        if (sx > 127) sx = 127;
        if (sx < -127) sx = -127;
        if (sy > 127) sy = 127;
        if (sy < -127) sy = -127;
        ikbd_mouse_report(sx, sy, left, right);
        dx -= sx;
        dy -= sy;
        send_button = false;
        if (!dx && !dy) break;
    }
}

void AtariSt::ikbd_keys(const MachineInputs& inputs) {
    for (const IkbdMap& map : kIkbd) {
        const bool down = inputs.key(map.key);
        const size_t idx = size_t(map.key);
        if (down && !keys_down_[idx]) ikbd_push(map.code);
        if (!down && keys_down_[idx]) ikbd_push(uint8_t(map.code | 0x80));
        keys_down_[idx] = down;
    }
    ikbd_mouse(inputs);
    ikbd_joysticks(inputs);
}

uint8_t AtariSt::acia_status() const {
    uint8_t s = 0x02;  // TDRE
    if (!ikbd_rx_.empty()) s = uint8_t(s | 0x01 | 0x80);
    return s;
}

uint8_t AtariSt::acia_read_data() {
    // The 6850 receive data register keeps the last byte: reading only
    // clears RDRF. Games poll it directly (World Class Rugby's crack intro
    // does `cmpi.b #$39,$fffc02` while TOS's ACIA interrupt has already
    // consumed the Space make code).
    if (ikbd_rx_.empty()) return acia_rdr_;
    acia_rdr_ = ikbd_rx_.front();
    ikbd_rx_.pop_front();
    if (ikbd_rx_.empty()) mfp_.set_gpip_bit(4, 1);
    return acia_rdr_;
}

void AtariSt::acia_write_control(uint8_t value) {
    acia_control_ = value;
    if ((value & 3) == 3) {
        ikbd_rx_.clear();
        ikbd_pending_.clear();
        mfp_.set_gpip_bit(4, 1);
    }
}

void AtariSt::acia_write_data(uint8_t value) { ikbd_byte(value); }

void AtariSt::update_irqs() {
    const bool fdc = floppy_.irq();
    mfp_.set_gpip_bit(5, fdc ? 0 : 1);
}

void AtariSt::on_cpu_cycles(int cycles) {
    floppy_.tick(cycles);
    if (!ikbd_pending_.empty()) ikbd_pending_.front().cycles -= cycles;
    service_acia();
    update_irqs();
    mfp_acc_ += int64_t(cycles) * Mc68901::kClock;
    while (mfp_acc_ >= int64_t(kCpuClock)) {
        mfp_acc_ -= int64_t(kCpuClock);
        mfp_.tick(1);
    }
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kCpuClock)) {
        audio_acc_ -= int64_t(kCpuClock);
        int32_t s = psg_.update();
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        audio_.push_back(int16_t(s));
    }
}

uint8_t AtariSt::read_byte(uint32_t address) {
    address &= 0xffffff;
    if (rom_at_zero_ && address < 8) return rom_[address];
    if (address < ram_.size()) return ram_[address];
    if (address >= 0xfc0000 && address < 0xfc0000 + rom_.size()) {
        return rom_[address - 0xfc0000];
    }
    if (address >= 0xff8000) {
        switch (address) {
            case 0xff8001: return memcfg_;
            case 0xff8201: return video_hi_;
            case 0xff8203: return video_mid_;
            case 0xff8205: return uint8_t(video_count_ >> 16);
            case 0xff8207: return uint8_t(video_count_ >> 8);
            case 0xff8209: return uint8_t(video_count_);
            case 0xff820a: return sync_mode_;
            case 0xff8260: return resolution_;
            case 0xff8604: {
                const uint16_t v = floppy_.dma_data_r();
                update_irqs();
                return uint8_t(v >> 8);
            }
            case 0xff8605: {
                const uint16_t v = floppy_.dma_data_r();
                update_irqs();
                return uint8_t(v);
            }
            case 0xff8606: return uint8_t(floppy_.dma_status() >> 8);
            case 0xff8607: return uint8_t(floppy_.dma_status());
            case 0xff8609: return floppy_.dma_addr_r(0);
            case 0xff860b: return floppy_.dma_addr_r(1);
            case 0xff860d: return floppy_.dma_addr_r(2);
            case 0xff8800:
            case 0xff8801: return psg_.read();
            case 0xfffc00: return acia_status();
            case 0xfffc02: return acia_read_data();
            case 0xfffc04: return 0x02;  // MIDI ACIA TDRE
            case 0xfffc06: return 0;
            default: break;
        }
        if (address >= 0xff8240 && address < 0xff8260) {
            const int n = int(address - 0xff8240) >> 1;
            const uint16_t w = palette_[size_t(n)];
            return (address & 1) ? uint8_t(w) : uint8_t(w >> 8);
        }
        if (address >= 0xfffa00 && address <= 0xfffa2f && (address & 1)) {
            return mfp_.read(int((address - 0xfffa01) >> 1));
        }
        if (address >= 0xff8a00 && address <= 0xff8a3d) {
            if (address == 0xff8a3a) return blit_hop_;
            if (address == 0xff8a3b) return blit_op_;
            if (address == 0xff8a3c) return blit_ctrl_;
            if (address == 0xff8a3d) return blit_skew_;
            const uint16_t w = blit_get_word(address & 0xfffffe);
            return (address & 1) ? uint8_t(w) : uint8_t(w >> 8);
        }
    }
    return 0xff;
}

void AtariSt::write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffff;
    if (rom_at_zero_ && address < 8) return;
    if (address < ram_.size()) {
        ram_[address] = value;
        return;
    }
    if (address >= 0xff8000) {
        switch (address) {
            case 0xff8001:
                memcfg_ = value;
                rom_at_zero_ = false;
                return;
            case 0xff8201: video_hi_ = value; return;
            case 0xff8203: video_mid_ = value; return;
            case 0xff820d: video_lo_ = value; return;
            case 0xff820a: sync_mode_ = value; return;
            case 0xff8260: resolution_ = uint8_t(value & 3); return;
            case 0xff8604: floppy_.dma_data_w(value); update_irqs(); return;
            case 0xff8605:
                floppy_.dma_data_w((floppy_.dma_mode() & 0xff00) | value);
                update_irqs();
                return;
            case 0xff8606: floppy_.dma_mode_w(uint16_t(value) << 8); return;
            case 0xff8607: floppy_.dma_mode_w(value); return;
            case 0xff8609: floppy_.dma_addr_w(0, value); return;
            case 0xff860b: floppy_.dma_addr_w(1, value); return;
            case 0xff860d: floppy_.dma_addr_w(2, value); return;
            case 0xff8800:
            case 0xff8801: psg_.control(value); return;
            case 0xff8802:
            case 0xff8803: psg_.write(value); return;
            case 0xfffc00: acia_write_control(value); return;
            case 0xfffc02: acia_write_data(value); return;
            default: break;
        }
        if (address >= 0xff8240 && address < 0xff8260) {
            const int n = int(address - 0xff8240) >> 1;
            uint16_t w = palette_[size_t(n)];
            if (address & 1) w = uint16_t((w & 0xff00) | value);
            else w = uint16_t((w & 0x00ff) | (uint16_t(value) << 8));
            palette_[size_t(n)] = uint16_t(w & 0x0777);
            return;
        }
        if (address >= 0xfffa00 && address <= 0xfffa2f && (address & 1)) {
            mfp_.write(int((address - 0xfffa01) >> 1), value);
            return;
        }
        if (address >= 0xff8a00 && address <= 0xff8a3d) {
            if (address == 0xff8a3a) {
                blit_hop_ = uint8_t(value & 3);
                return;
            }
            if (address == 0xff8a3b) {
                blit_op_ = uint8_t(value & 15);
                return;
            }
            if (address == 0xff8a3c) {
                blit_ctrl_ = value;
                if ((value & 0x80) && !blit_defer_start_) run_blitter();
                return;
            }
            if (address == 0xff8a3d) {
                blit_skew_ = value;
                return;
            }
            const uint32_t even = address & 0xfffffe;
            uint16_t w = blit_get_word(even);
            if (address & 1) w = uint16_t((w & 0xff00) | value);
            else w = uint16_t((w & 0x00ff) | (uint16_t(value) << 8));
            blit_set_word(even, w);
            return;
        }
    }
}

uint16_t AtariSt::read_word(uint32_t address) {
    address &= 0xfffffe;
    if (rom_at_zero_ && address < 8) {
        return uint16_t((rom_[address] << 8) | rom_[address + 1]);
    }
    if (address + 1 < ram_.size()) {
        return uint16_t((ram_[address] << 8) | ram_[address + 1]);
    }
    if (address >= 0xfc0000 && address + 1 < 0xfc0000 + rom_.size()) {
        const uint32_t o = address - 0xfc0000;
        return uint16_t((rom_[o] << 8) | rom_[o + 1]);
    }
    if (address == 0xff8604) {
        const uint16_t v = floppy_.dma_data_r();
        update_irqs();
        return v;
    }
    if (address == 0xff8606) return floppy_.dma_status();
    if (address == 0xff8800) return uint16_t(psg_.read() << 8);
    return uint16_t((read_byte(address) << 8) | read_byte(address + 1));
}

void AtariSt::write_word(uint32_t address, uint16_t value) {
    address &= 0xfffffe;
    if (rom_at_zero_ && address < 8) return;
    if (address + 1 < ram_.size()) {
        ram_[address] = uint8_t(value >> 8);
        ram_[address + 1] = uint8_t(value);
        return;
    }
    if (address == 0xff8604) {
        floppy_.dma_data_w(value);
        update_irqs();
        return;
    }
    if (address == 0xff8606) {
        floppy_.dma_mode_w(value);
        return;
    }
    if (address == 0xff8800) {
        psg_.control(uint8_t(value >> 8));
        psg_.write(uint8_t(value));
        return;
    }
    if (address >= 0xff8240 && address < 0xff8260) {
        palette_[size_t(address - 0xff8240) >> 1] = uint16_t(value & 0x0777);
        return;
    }
    // A word write to $FF8A3C stores control then skew. Starting the blit on
    // the first byte would run with the previous skew.
    if (address == 0xff8a3c) {
        blit_defer_start_ = true;
        write_byte(address, uint8_t(value >> 8));
        blit_defer_start_ = false;
        write_byte(address + 1, uint8_t(value));
        if (blit_ctrl_ & 0x80) run_blitter();
        return;
    }
    write_byte(address, uint8_t(value >> 8));
    write_byte(address + 1, uint8_t(value));
}

uint16_t AtariSt::blit_get_word(uint32_t even_addr) const {
    even_addr &= 0xfffffe;
    if (even_addr >= 0xff8a00 && even_addr < 0xff8a20) {
        return blit_halftone_[size_t(even_addr - 0xff8a00) >> 1];
    }
    switch (even_addr) {
        case 0xff8a20: return uint16_t(blit_sxinc_);
        case 0xff8a22: return uint16_t(blit_syinc_);
        case 0xff8a24: return uint16_t(blit_src_ >> 16);
        case 0xff8a26: return uint16_t(blit_src_);
        case 0xff8a28: return blit_emask_[0];
        case 0xff8a2a: return blit_emask_[1];
        case 0xff8a2c: return blit_emask_[2];
        case 0xff8a2e: return uint16_t(blit_dxinc_);
        case 0xff8a30: return uint16_t(blit_dyinc_);
        case 0xff8a32: return uint16_t(blit_dst_ >> 16);
        case 0xff8a34: return uint16_t(blit_dst_);
        case 0xff8a36: return blit_xcount_;
        case 0xff8a38: return blit_ycount_;
        default: return 0;
    }
}

void AtariSt::blit_set_word(uint32_t even_addr, uint16_t value) {
    even_addr &= 0xfffffe;
    if (even_addr >= 0xff8a00 && even_addr < 0xff8a20) {
        blit_halftone_[size_t(even_addr - 0xff8a00) >> 1] = value;
        return;
    }
    switch (even_addr) {
        case 0xff8a20: blit_sxinc_ = int16_t(value); return;
        case 0xff8a22: blit_syinc_ = int16_t(value); return;
        case 0xff8a24: blit_src_ = (blit_src_ & 0xffff) | (uint32_t(value) << 16); return;
        case 0xff8a26: blit_src_ = (blit_src_ & 0xffff0000u) | value; return;
        case 0xff8a28: blit_emask_[0] = value; return;
        case 0xff8a2a: blit_emask_[1] = value; return;
        case 0xff8a2c: blit_emask_[2] = value; return;
        case 0xff8a2e: blit_dxinc_ = int16_t(value); return;
        case 0xff8a30: blit_dyinc_ = int16_t(value); return;
        case 0xff8a32: blit_dst_ = (blit_dst_ & 0xffff) | (uint32_t(value) << 16); return;
        case 0xff8a34: blit_dst_ = (blit_dst_ & 0xffff0000u) | value; return;
        case 0xff8a36: blit_xcount_ = value; return;
        case 0xff8a38: blit_ycount_ = value; return;
        default: return;
    }
}

uint16_t AtariSt::blit_mem_read(uint32_t address) const {
    address &= 0xfffffe;
    if (address + 1 < ram_.size()) {
        return uint16_t((ram_[address] << 8) | ram_[address + 1]);
    }
    // Line-A text blits the system font out of TOS ($FC0000 / $E00000).
    auto from_rom = [&](uint32_t base) -> uint16_t {
        if (address >= base && address + 1 < base + rom_.size()) {
            const uint32_t o = address - base;
            return uint16_t((rom_[o] << 8) | rom_[o + 1]);
        }
        return 0;
    };
    if (address >= 0xfc0000) return from_rom(0xfc0000);
    if (address >= 0xe00000) return from_rom(0xe00000);
    return 0xffff;
}

void AtariSt::blit_mem_write(uint32_t address, uint16_t value) {
    address &= 0xfffffe;
    if (address + 1 < ram_.size()) {
        ram_[address] = uint8_t(value >> 8);
        ram_[address + 1] = uint8_t(value);
    }
}

void AtariSt::run_blitter() {
    // Line-A polls $FF8A3C bit 7. Finish the blit immediately so TOS never
    // sits in `tst.b (a5); bmi.s` after opening a GEM window.
    if (blit_ycount_ == 0) {
        blit_ctrl_ = uint8_t(blit_ctrl_ & 0x3f);
        return;
    }

    const int hop = blit_hop_ & 3;
    const int op = blit_op_ & 15;
    const int skew = blit_skew_ & 15;
    const bool fxsr = (blit_skew_ & 0x80) != 0;
    const bool nfsr = (blit_skew_ & 0x40) != 0;
    const bool smudge = (blit_ctrl_ & 0x20) != 0;
    int line = blit_ctrl_ & 15;

    static const bool kOpSrc[16] = {false, true,  true,  true,  true,  false, true,  true,
                                    true,  true,  false, true,  true,  true,  true,  false};
    static const bool kOpDst[16] = {false, true,  true,  false, true,  true,  true,  true,
                                    true,  true,  true,  true,  false, true,  true,  false};
    const bool hop_src = (hop & 2) != 0 || (hop == 1 && smudge);
    const bool need_src = kOpSrc[op] && hop_src;

    uint32_t xspan = blit_xcount_ ? uint32_t(blit_xcount_) : 65536u;
    uint32_t yleft = blit_ycount_;
    uint32_t src = blit_src_ & 0xfffffe;
    uint32_t dst = blit_dst_ & 0xfffffe;
    uint32_t buffer = 0;
    uint32_t words = 0;
    constexpr uint32_t kMaxWords = 0x100000;

    auto add_src = [&](int16_t inc) {
        src = uint32_t(int32_t(src) + inc) & 0xfffffe;
    };
    auto add_dst = [&](int16_t inc) {
        dst = uint32_t(int32_t(dst) + inc) & 0xfffffe;
    };
    auto fetch_src = [&]() {
        if (blit_sxinc_ < 0) buffer >>= 16;
        else buffer <<= 16;
        const uint32_t w = blit_mem_read(src);
        if (blit_sxinc_ < 0) buffer |= w << 16;
        else buffer |= w;
    };

    while (yleft && words < kMaxWords) {
        uint32_t xleft = xspan;
        bool skip_src = false;
        while (xleft && words < kMaxWords) {
            const bool first = xleft == xspan;
            const bool last = xleft == 1;
            uint16_t mask = blit_emask_[1];
            if (first || xspan == 1) mask = blit_emask_[0];
            else if (last) mask = blit_emask_[2];

            bool fetched = false;
            if (need_src) {
                if (first && fxsr) {
                    fetch_src();
                    add_src(blit_sxinc_);
                }
                if (!skip_src) {
                    fetch_src();
                    fetched = true;
                } else {
                    // NFSR skips the read but the source buffer still shifts,
                    // so the last word uses the previously fetched one.
                    // Without the shift a descending blit (TOS's Desktop
                    // Info Atari logo) repeated the same word.
                    if (blit_sxinc_ < 0) buffer >>= 16;
                    else buffer <<= 16;
                }
            }

            const uint16_t srcw = uint16_t(buffer >> skew);
            const uint16_t ht =
                smudge ? blit_halftone_[size_t(srcw & 15)] : blit_halftone_[size_t(line)];
            uint16_t hopv = 0xffff;
            if (hop == 1) hopv = ht;
            else if (hop == 2) hopv = srcw;
            else if (hop == 3) hopv = uint16_t(srcw & ht);

            const bool read_dst = kOpDst[op] || mask != 0xffff;
            const uint16_t dstw = read_dst ? blit_mem_read(dst) : 0;
            uint16_t lop = 0;
            switch (op) {
                case 0: lop = 0; break;
                case 1: lop = uint16_t(hopv & dstw); break;
                case 2: lop = uint16_t(hopv & ~dstw); break;
                case 3: lop = hopv; break;
                case 4: lop = uint16_t(~hopv & dstw); break;
                case 5: lop = dstw; break;
                case 6: lop = uint16_t(hopv ^ dstw); break;
                case 7: lop = uint16_t(hopv | dstw); break;
                case 8: lop = uint16_t(~hopv & ~dstw); break;
                case 9: lop = uint16_t(~hopv ^ dstw); break;
                case 10: lop = uint16_t(~dstw); break;
                case 11: lop = uint16_t(hopv | ~dstw); break;
                case 12: lop = uint16_t(~hopv); break;
                case 13: lop = uint16_t(~hopv | dstw); break;
                case 14: lop = uint16_t(~hopv | ~dstw); break;
                default: lop = 0xffff; break;
            }
            blit_mem_write(dst, uint16_t((lop & mask) | (dstw & ~mask)));
            words++;

            if (xleft == 2 && nfsr) skip_src = true;
            if (fetched) {
                if (last || skip_src) add_src(blit_syinc_);
                else add_src(blit_sxinc_);
            }
            if (last) {
                add_dst(blit_dyinc_);
                line = blit_dyinc_ >= 0 ? ((line + 1) & 15) : ((line - 1) & 15);
            } else {
                add_dst(blit_dxinc_);
            }
            xleft--;
        }
        yleft--;
    }

    blit_src_ = src;
    blit_dst_ = dst;
    blit_xcount_ = blit_xcount_ ? blit_xcount_ : uint16_t(xspan);
    blit_ycount_ = 0;
    blit_ctrl_ = uint8_t((blit_ctrl_ & 0x30) | (line & 15));
}

void AtariSt::render() {
    const uint32_t vbase =
        (uint32_t(video_hi_) << 16) | (uint32_t(video_mid_) << 8) | video_lo_;
    const int mode = resolution_ & 3;
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) pal[i] = st_color(palette_[size_t(i)]);
    std::fill(framebuffer_.begin(), framebuffer_.end(), pal[0]);

    auto pix = [&](int x, int y, uint32_t c) {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
        framebuffer_[size_t(y) * kWidth + x] = c;
    };

    if (mode == 2) {
        // High: 640×400, 1 bitplane.
        for (int y = 0; y < 400; y++) {
            const uint32_t row = vbase + uint32_t(y) * 80u;
            for (int x = 0; x < 640; x += 16) {
                if (row + uint32_t(x / 8) + 1 >= ram_.size()) continue;
                const uint16_t w =
                    uint16_t((ram_[row + uint32_t(x / 8)] << 8) | ram_[row + uint32_t(x / 8) + 1]);
                for (int b = 0; b < 16; b++) {
                    pix(x + b, y, (w & (0x8000 >> b)) ? pal[1] : pal[0]);
                }
            }
        }
        return;
    }
    if (mode == 1) {
        // Medium: 640×200, 2 bitplanes, line-doubled.
        for (int y = 0; y < 200; y++) {
            const uint32_t row = vbase + uint32_t(y) * 160u;
            for (int x = 0; x < 640; x += 16) {
                const uint32_t o = row + uint32_t(x / 4);
                if (o + 3 >= ram_.size()) continue;
                uint16_t p0 = uint16_t((ram_[o] << 8) | ram_[o + 1]);
                uint16_t p1 = uint16_t((ram_[o + 2] << 8) | ram_[o + 3]);
                for (int b = 0; b < 16; b++) {
                    const int c = ((p0 >> 15) & 1) | (((p1 >> 15) & 1) << 1);
                    pix(x + b, y * 2, pal[c]);
                    pix(x + b, y * 2 + 1, pal[c]);
                    p0 = uint16_t(p0 << 1);
                    p1 = uint16_t(p1 << 1);
                }
            }
        }
        return;
    }
    // Low: 320×200, 4 bitplanes, doubled in X and Y.
    for (int y = 0; y < 200; y++) {
        const uint32_t row = vbase + uint32_t(y) * 160u;
        for (int x = 0; x < 320; x += 16) {
            const uint32_t o = row + uint32_t(x / 2);
            if (o + 7 >= ram_.size()) continue;
            uint16_t p0 = uint16_t((ram_[o] << 8) | ram_[o + 1]);
            uint16_t p1 = uint16_t((ram_[o + 2] << 8) | ram_[o + 3]);
            uint16_t p2 = uint16_t((ram_[o + 4] << 8) | ram_[o + 5]);
            uint16_t p3 = uint16_t((ram_[o + 6] << 8) | ram_[o + 7]);
            for (int b = 0; b < 16; b++) {
                const int c = ((p0 >> 15) & 1) | (((p1 >> 15) & 1) << 1) |
                              (((p2 >> 15) & 1) << 2) | (((p3 >> 15) & 1) << 3);
                pix(x * 2 + b * 2, y * 2, pal[c]);
                pix(x * 2 + b * 2 + 1, y * 2, pal[c]);
                pix(x * 2 + b * 2, y * 2 + 1, pal[c]);
                pix(x * 2 + b * 2 + 1, y * 2 + 1, pal[c]);
                p0 = uint16_t(p0 << 1);
                p1 = uint16_t(p1 << 1);
                p2 = uint16_t(p2 << 1);
                p3 = uint16_t(p3 << 1);
            }
        }
    }
}

void AtariSt::run_frame() {
    const uint32_t vbase =
        (uint32_t(video_hi_) << 16) | (uint32_t(video_mid_) << 8) | video_lo_;
    const int pitch = (resolution_ & 3) == 2 ? 80 : 160;
    cpu_.set_irq(4, IrqLine::Hold);  // VBL
    for (int line = 0; line < kLines; line++) {
        if (line >= 63 && line < 263) {
            video_count_ = vbase + uint32_t(line - 63) * uint32_t(pitch);
        } else {
            video_count_ = vbase;
        }
        mfp_.pulse_tb();
        // HBL (autovector level 2) every line. TOS's handler raises the
        // interrupted code's IPL to 3, which is why programs run at $x300;
        // loaders that fold SR into their decryption key depend on it.
        cpu_.set_irq(2, IrqLine::Hold);
        cpu_.run(kCyclesPerLine);
        update_irqs();
    }
    ikbd_clock_tick();
    render();
    if (vkb_visible_) vkb_compose();
}

void AtariSt::set_inputs(const MachineInputs& inputs) {
    // F11 (not an ST key) shows / hides the on-screen keyboard.
    const bool toggle = inputs.key(Key::F11);
    if (toggle && !vkb_toggle_down_) set_vkb_visible(!vkb_visible_);
    vkb_toggle_down_ = toggle;
    if (vkb_visible_) vkb_input(inputs);
    ikbd_keys(inputs);
}

// ---------------------------------------------------------------------------
// On-screen keyboard: Hataroid's 1040ST picture, scaled to the bottom of the
// 640x400 framebuffer. The mouse clicks keys; they go to the IKBD exactly
// like host keys (make on press, break on release). Shift, Control and
// Alternate latch until the next key, so combinations can be clicked.

void AtariSt::vkb_init() {
    std::vector<uint8_t> rgb(size_t(kVkbTexW) * kVkbTexH * 3);
    uLongf len = uLongf(rgb.size());
    if (uncompress(rgb.data(), &len, kVkbPictureUk, uLong(sizeof(kVkbPictureUk))) != Z_OK) {
        rgb.assign(rgb.size(), 0x80);
    }
    // Box-filter 768x256 down to 640 x kVkbHeight.
    vkb_image_.assign(size_t(kWidth) * kVkbHeight, 0);
    for (int y = 0; y < kVkbHeight; y++) {
        const int sy0 = y * kVkbTexH / kVkbHeight;
        const int sy1 = std::max(sy0 + 1, (y + 1) * kVkbTexH / kVkbHeight);
        for (int x = 0; x < kWidth; x++) {
            const int sx0 = x * kVkbTexW / kWidth;
            const int sx1 = std::max(sx0 + 1, (x + 1) * kVkbTexW / kWidth);
            int r = 0, g = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                for (int sx = sx0; sx < sx1; sx++) {
                    const uint8_t* p = &rgb[(size_t(sy) * kVkbTexW + size_t(sx)) * 3];
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    n++;
                }
            }
            vkb_image_[size_t(y) * kWidth + size_t(x)] =
                0xff000000u | uint32_t(r / n) << 16 | uint32_t(g / n) << 8 | uint32_t(b / n);
        }
    }
}

void AtariSt::set_vkb_visible(bool visible) {
    if (!visible) vkb_release_all();
    vkb_visible_ = visible;
    if (visible) vkb_compose();
}

int AtariSt::vkb_hit(int x, int y) const {
    if (y < kVkbTop || y >= kHeight || x < 0 || x >= kWidth) return -1;
    const int tx = x * kVkbTexW / kWidth;
    const int ty = (y - kVkbTop) * kVkbTexH / kVkbHeight;
    for (size_t i = 0; i < sizeof(kVkbKeys) / sizeof(kVkbKeys[0]); i++) {
        if (vkb_contains(kVkbKeys[i], tx, ty)) return int(i);
    }
    return -1;
}

bool AtariSt::vkb_key_centre(uint8_t scancode, int* x, int* y) const {
    for (const VkbKey& k : kVkbKeys) {
        if (k.scancode != scancode) continue;
        int x1, y1, x2, y2;
        vkb_bounds(k, &x1, &y1, &x2, &y2);
        *x = ((x1 + x2) / 2) * kWidth / kVkbTexW;
        *y = kVkbTop + ((y1 + y2) / 2) * kVkbHeight / kVkbTexH;
        return true;
    }
    return false;
}

void AtariSt::vkb_release_all() {
    if (vkb_pressed_ >= 0) {
        ikbd_push(uint8_t(kVkbKeys[vkb_pressed_].scancode | 0x80));
        vkb_pressed_ = -1;
    }
    for (int code = 0; code < 128; code++) {
        if (vkb_latched_[size_t(code)]) ikbd_push(uint8_t(code | 0x80));
    }
    vkb_latched_.fill(false);
    vkb_button_down_ = false;
}

void AtariSt::vkb_input(const MachineInputs& inputs) {
    vkb_pointer_on_ = inputs.has_pointer && inputs.pointer_y >= kVkbTop;
    vkb_pointer_x_ = inputs.pointer_x;
    vkb_pointer_y_ = inputs.pointer_y;
    const bool button = inputs.has_pointer && inputs.pointer_button1;
    if (button && !vkb_button_down_ && vkb_pointer_on_) {
        const int hit = vkb_hit(inputs.pointer_x, inputs.pointer_y);
        if (hit >= 0) {
            const VkbKey& key = kVkbKeys[hit];
            if (key.modifier) {
                // Toggle the latch.
                bool& latched = vkb_latched_[size_t(key.scancode & 0x7f)];
                latched = !latched;
                ikbd_push(latched ? key.scancode : uint8_t(key.scancode | 0x80));
            } else {
                vkb_pressed_ = hit;
                ikbd_push(key.scancode);
            }
        }
    }
    if (!button && vkb_pressed_ >= 0) {
        ikbd_push(uint8_t(kVkbKeys[vkb_pressed_].scancode | 0x80));
        vkb_pressed_ = -1;
        // A clicked key consumes the latched modifiers.
        for (int code = 0; code < 128; code++) {
            if (vkb_latched_[size_t(code)]) {
                ikbd_push(uint8_t(code | 0x80));
                vkb_latched_[size_t(code)] = false;
            }
        }
    }
    vkb_button_down_ = button;
}

void AtariSt::vkb_compose() {
    display_ = framebuffer_;
    // The keyboard covers the bottom of the picture.
    std::copy(vkb_image_.begin(), vkb_image_.end(), display_.begin() + ptrdiff_t(kVkbTop) * kWidth);
    // Pressed and latched keys light up.
    auto tint = [&](const VkbKey& k) {
        int x1, y1, x2, y2;
        vkb_bounds(k, &x1, &y1, &x2, &y2);
        const int sx1 = x1 * kWidth / kVkbTexW, sx2 = x2 * kWidth / kVkbTexW;
        const int sy1 = kVkbTop + y1 * kVkbHeight / kVkbTexH;
        const int sy2 = std::min(kHeight - 1, kVkbTop + y2 * kVkbHeight / kVkbTexH);
        for (int y = sy1; y <= sy2; y++) {
            const int ty = (y - kVkbTop) * kVkbTexH / kVkbHeight;
            for (int x = sx1; x <= sx2 && x < kWidth; x++) {
                if (!vkb_contains(k, x * kVkbTexW / kWidth, ty)) continue;
                uint32_t& p = display_[size_t(y) * kWidth + size_t(x)];
                const uint32_t r = ((p >> 16) & 0xff) / 2;
                const uint32_t g = ((p >> 8) & 0xff) / 2 + 40;
                const uint32_t b = (p & 0xff) / 2 + 110;
                p = 0xff000000u | r << 16 | g << 8 | b;
            }
        }
    };
    for (size_t i = 0; i < sizeof(kVkbKeys) / sizeof(kVkbKeys[0]); i++) {
        const VkbKey& k = kVkbKeys[i];
        if (int(i) == vkb_pressed_ || (k.modifier && vkb_latched_[size_t(k.scancode & 0x7f)])) tint(k);
    }
    // The host cursor is hidden over the window: draw an arrow on the
    // keyboard so the user sees what they click.
    if (vkb_pointer_on_) {
        static const char* kArrow[] = {
            "X.........", "XX........", "X#X.......", "X##X......", "X###X.....",
            "X####X....", "X#####X...", "X######X..", "X#######X.", "X####XXXXX",
            "X#X##X....", "XX.X##X...", "X..X##X...", ".....X##X.", ".....XXXX.",
        };
        for (int dy = 0; dy < 15; dy++) {
            for (int dx = 0; dx < 10; dx++) {
                const char c = kArrow[dy][dx];
                const int x = vkb_pointer_x_ + dx, y = vkb_pointer_y_ + dy;
                if (c == '.' || x >= kWidth || y >= kHeight) continue;
                display_[size_t(y) * kWidth + size_t(x)] = c == 'X' ? 0xff000000u : 0xffffffffu;
            }
        }
    }
}

void AtariSt::set_dip_switch(int, uint8_t) {}

void AtariSt::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
