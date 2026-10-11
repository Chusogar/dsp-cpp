#include "hid.h"

#include <cstring>

namespace dsp {
namespace port {
namespace hid {

namespace {

// HID keyboard usage (0x00-0x67) -> dsp key; Key::Count = unused.
Key usage_to_key(uint8_t u) {
    if (u >= 0x04 && u <= 0x1d) return Key(int(Key::A) + (u - 0x04));
    if (u >= 0x1e && u <= 0x26) return Key(int(Key::Num1) + (u - 0x1e));
    if (u >= 0x3a && u <= 0x45) return Key(int(Key::F1) + (u - 0x3a));
    switch (u) {
        case 0x27: return Key::Num0;
        case 0x28: return Key::Enter;
        case 0x29: return Key::Escape;
        case 0x2a: return Key::Backspace;
        case 0x2b: return Key::Tab;
        case 0x2c: return Key::Space;
        case 0x2d: return Key::Minus;
        case 0x2e: return Key::Equals;
        case 0x2f: return Key::At;         // [ (where @ sits on many 8-bit keyboards)
        case 0x30: return Key::Asterisk;   // ]
        case 0x31: return Key::Backslash;
        case 0x33: return Key::Semicolon;
        case 0x34: return Key::Quote;
        case 0x35: return Key::Backquote;
        case 0x36: return Key::Comma;
        case 0x37: return Key::Period;
        case 0x38: return Key::Slash;
        case 0x39: return Key::CapsLock;
        case 0x4a: return Key::Home;
        case 0x4c: return Key::Delete;
        case 0x4f: return Key::Right;
        case 0x50: return Key::Left;
        case 0x51: return Key::Down;
        case 0x52: return Key::Up;
        case 0x55: return Key::Asterisk;   // keypad *
        case 0x57: return Key::Plus;       // keypad +
        case 0x58: return Key::Enter;      // keypad Enter
        default: break;
    }
    return Key::Count;
}

}  // namespace

void apply_keyboard_report(const uint8_t* report, size_t length, HostInput& in) {
    if (length < 3) return;
    in.keys.fill(false);
    const uint8_t mod = report[0];
    in.keys[size_t(Key::LeftCtrl)] = mod & 0x01;
    in.keys[size_t(Key::LeftShift)] = mod & 0x02;
    in.left_alt = (mod & 0x04) != 0;
    in.keys[size_t(Key::LeftGui)] = mod & 0x08;
    in.keys[size_t(Key::RightCtrl)] = mod & 0x10;
    in.keys[size_t(Key::RightShift)] = mod & 0x20;
    in.keys[size_t(Key::RightAlt)] = mod & 0x40;
    in.keys[size_t(Key::RightGui)] = mod & 0x80;
    for (size_t i = 2; i < length && i < 8; ++i) {
        const Key k = usage_to_key(report[i]);
        if (k != Key::Count) in.keys[size_t(k)] = true;
    }
}

void apply_mouse_report(const uint8_t* report, size_t length, HostInput& in) {
    if (length < 3) return;
    in.mouse_connected = true;
    in.mouse_left = report[0] & 1;
    in.mouse_right = report[0] & 2;
    in.mouse_dx += int8_t(report[1]);
    in.mouse_dy += int8_t(report[2]);
}

bool Gamepad::parse(const uint8_t* d, size_t length) {
    fields_.clear();
    uses_ids_ = false;
    buttons_ = 0;
    struct Globals {
        uint16_t page = 0;
        int32_t min = 0, max = 0;
        uint32_t size = 0, count = 0;
        uint8_t id = 0;
    } g;
    Globals stack[4];
    int depth = 0;
    std::vector<uint32_t> usages;  // (page << 16) | usage
    uint32_t usage_min = 0, usage_max = 0;
    bool have_range = false;
    uint32_t offsets[256] = {};  // input bit offset per report id

    size_t i = 0;
    while (i < length) {
        const uint8_t prefix = d[i++];
        if (prefix == 0xfe) {  // long item
            if (i + 1 >= length) break;
            i += 2 + d[i];
            continue;
        }
        const uint8_t size_code = prefix & 3;
        const size_t size = size_code == 3 ? 4 : size_code;
        if (i + size > length) break;
        uint32_t value = 0;
        for (size_t b = 0; b < size; ++b) value |= uint32_t(d[i + b]) << (8 * b);
        int32_t svalue = int32_t(value);
        if (size == 1) svalue = int8_t(value);
        else if (size == 2) svalue = int16_t(value);
        i += size;
        const uint8_t type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) {  // global
            switch (tag) {
                case 0: g.page = uint16_t(value); break;
                case 1: g.min = svalue; break;
                case 2: g.max = (g.min >= 0 && svalue < 0) ? int32_t(value) : svalue; break;
                case 7: g.size = value; break;
                case 8: g.id = uint8_t(value); uses_ids_ = true; break;
                case 9: g.count = value; break;
                case 10: if (depth < 4) stack[depth++] = g; break;
                case 11: if (depth > 0) g = stack[--depth]; break;
                default: break;
            }
        } else if (type == 2) {  // local
            const uint32_t full = size == 4 ? value : (uint32_t(g.page) << 16) | value;
            if (tag == 0) usages.push_back(full);
            else if (tag == 1) { usage_min = full; have_range = true; }
            else if (tag == 2) { usage_max = full; have_range = true; }
        } else if (type == 0) {  // main
            if (tag == 8) {  // input
                const bool constant = value & 1, variable = value & 2;
                uint32_t& offset = offsets[g.id];
                for (uint32_t n = 0; n < g.count; ++n) {
                    if (!constant && variable && g.size > 0 && g.size <= 32) {
                        uint32_t u = 0;
                        if (have_range) u = usage_min + n <= usage_max ? usage_min + n : usage_max;
                        else if (n < usages.size()) u = usages[n];
                        else if (!usages.empty()) u = usages.back();
                        if ((u >> 16) == 0) u |= uint32_t(g.page) << 16;
                        Field f;
                        f.report_id = g.id;
                        f.bit = offset;
                        f.size = uint8_t(g.size);
                        f.min = g.min;
                        f.max = g.max;
                        f.page = uint16_t(u >> 16);
                        f.usage = uint16_t(u);
                        const bool wanted = (f.page == 0x01 && (f.usage == 0x30 || f.usage == 0x31 || f.usage == 0x39)) ||
                                            (f.page == 0x09 && f.usage >= 1 && f.usage <= 16);
                        if (wanted) fields_.push_back(f);
                        if (wanted && f.page == 0x09 && f.usage > buttons_) buttons_ = f.usage;
                    }
                    offset += g.size;
                }
            }
            usages.clear();
            have_range = false;
            usage_min = usage_max = 0;
        }
    }
    return !fields_.empty();
}

int32_t Gamepad::read(const uint8_t* data, size_t length, const Field& f) {
    uint32_t v = 0;
    for (uint32_t b = 0; b < f.size; ++b) {
        const uint32_t bit = f.bit + b;
        if (bit / 8 >= length) break;
        v |= uint32_t((data[bit / 8] >> (bit % 8)) & 1) << b;
    }
    if (f.min < 0 && f.size < 32 && (v & (1u << (f.size - 1)))) v |= ~0u << f.size;  // sign
    return int32_t(v);
}

void Gamepad::apply_report(const uint8_t* report, size_t length, HostInput& in) const {
    uint8_t id = 0;
    if (uses_ids_) {
        if (length == 0) return;
        id = report[0];
        ++report;
        --length;
    }
    bool any = false;
    bool buttons[17] = {};
    bool up = false, down = false, left = false, right = false;
    for (const Field& f : fields_) {
        if (f.report_id != id) continue;
        any = true;
        const int32_t v = read(report, length, f);
        if (f.page == 0x09) {
            buttons[f.usage] = v != 0;
        } else if (f.usage == 0x39) {  // hat: 0 = up, clockwise in 8 steps
            const int32_t h = v - f.min;
            const int32_t steps = f.max - f.min + 1;
            if (h >= 0 && h < steps && steps >= 4) {
                const int dir = steps == 4 ? h * 2 : h;  // 4-way hats
                up = up || dir == 7 || dir == 0 || dir == 1;
                right = right || (dir >= 1 && dir <= 3);
                down = down || (dir >= 3 && dir <= 5);
                left = left || (dir >= 5 && dir <= 7);
            }
        } else {  // X / Y
            const int64_t range = int64_t(f.max) - f.min;
            if (range <= 0) continue;
            const bool low = int64_t(v) - f.min < range / 4;
            const bool high = int64_t(f.max) - v < range / 4;
            if (f.usage == 0x30) {
                left = left || low;
                right = right || high;
            } else {
                up = up || low;
                down = down || high;
            }
        }
    }
    if (!any) return;
    in.pad_connected = true;
    in.pad_up = up;
    in.pad_down = down;
    in.pad_left = left;
    in.pad_right = right;
    in.pad_a = buttons[1];
    in.pad_b = buttons[2];
    in.pad_x = buttons[3];
    in.pad_y = buttons[4];
    // Select/Start: buttons 9/10 on most pads, 7/8 on those with 8 buttons.
    in.pad_select = buttons_ >= 10 ? buttons[9] : buttons[7];
    in.pad_start = buttons_ >= 10 ? buttons[10] : buttons[8];
}

}  // namespace hid
}  // namespace port
}  // namespace dsp
