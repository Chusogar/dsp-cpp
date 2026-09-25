#include "drivers/consoles/vectrex.h"

#include "core/rom_loader.h"

#include <algorithm>
#include <cmath>
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

}  // namespace

Vectrex::Vectrex() : cpu_(kCpuClock), via_(kCpuClock), ay_(kCpuClock) {
    cpu_.set_memory_handlers([this](uint16_t a) { return cpu_read(a); },
                             [this](uint16_t a, uint8_t v) { cpu_write(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });
    via_.set_port_a([this] { return via_pa_in(); }, [this](uint8_t v) { via_pa_out(v); });
    via_.set_port_b([this] { return via_pb_in(); }, [this](uint8_t v) { via_pb_out(v); });
    via_.set_ca2_handler([this](bool level) { on_ca2(level); });
    via_.set_irq_callback([this](IrqLine s) { cpu_.set_irq(s); });
    ay_.set_port_handlers(
        [this]() { return uint8_t(0xf0 | (~buttons_ & 0x0f)); },
        []() { return uint8_t(0xff); }, {}, {});
}

bool Vectrex::init(const std::string& rom_path, std::string* error) {
    // The BIOS can be a bare 8 KB image, or a MAME "vectrex" set (zip or
    // directory) holding exec_rom.bin.
    std::vector<uint8_t> rom;
    const char* names[] = {"exec_rom.bin", "vectrex.bin", "bios.bin",
                           "exec_rom_intl_284001-1.bin"};
    const bool plain = read_file(rom_path, rom) && !(rom.size() >= 2 && rom[0] == 'P' && rom[1] == 'K');
    if (!plain) {
        rom.clear();
        RomLoader loader;
        std::string open_error;
        if (loader.open(rom_path, &open_error)) {
            for (const char* n : names)
                if (loader.try_read(n, rom) && rom.size() >= bios_.size()) break;
        }
    }
    if (rom.size() < bios_.size()) {
        if (error) *error = "Vectrex BIOS (8 KB exec_rom.bin) not found in " + rom_path;
        return false;
    }
    std::copy(rom.begin(), rom.begin() + long(bios_.size()), bios_.begin());
    if (error) error->clear();
    reset();
    return true;
}

bool Vectrex::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> cart;
    if (!read_file(path, cart)) {
        if (error) *error = "cannot open cartridge: " + path;
        return false;
    }
    if (cart.size() > 0x8000) cart.resize(0x8000);
    cart_ = std::move(cart);
    reset();
    return true;
}

void Vectrex::reset() {
    ram_.fill(0);
    via_.reset();
    ay_.reset();
    porta_ = portb_ = 0;
    xsh_ = rsh_ = ysh_ = 128;
    zsh_ = 0;
    jsh_ = 128;
    dx_ = dy_ = 0;
    snd_select_ = 0;
    zero_active_ = false;
    frame_carry_ = 0;
    pos_x_ = pos_y_ = 0.0f;
    vectoring_ = false;
    segments_.clear();
    glow_.fill(0.0f);
    framebuffer_.fill(0xff000000u);
    audio_.clear();
    audio_acc_ = 0;
    buttons_ = 0;
    joy_x_ = joy_y_ = 0;
    cpu_.reset();
}

uint8_t Vectrex::cpu_read(uint16_t a) {
    if (a < 0x8000) return a < cart_.size() ? cart_[a] : 0xff;
    if (a >= 0xe000) return bios_[a & 0x1fff];
    if ((a & 0xe000) == 0xc000) {
        if (a & 0x1000) return via_.read(uint8_t(a & 0x0f));
        if (a & 0x0800) return ram_[a & 0x3ff];
    }
    return 0xff;
}

void Vectrex::cpu_write(uint16_t a, uint8_t v) {
    if (a < 0x8000 || a >= 0xe000) return;
    if ((a & 0xe000) == 0xc000) {
        if (a & 0x0800) ram_[a & 0x3ff] = v;
        if (a & 0x1000) via_.write(uint8_t(a & 0x0f), v);
    }
}

// ---- VIA ports (vecx read8/write8 port behaviour) ----

uint8_t Vectrex::via_pa_in() {
    if ((portb_ & 0x18) == 0x08) return ay_.read();  // PSG drives port A
    return porta_;
}

uint8_t Vectrex::via_pb_in() {
    uint8_t v = portb_;
    if (via_.acr() & 0x80)
        v = uint8_t((v & 0x7f) | (via_.out_b() & 0x80));  // T1 owns PB7
    if (jsh_ > xsh_) v |= 0x20;                            // comparator (vecx)
    else v &= uint8_t(~0x20);
    return v;
}

void Vectrex::snd_update() {
    switch (portb_ & 0x18) {
    case 0x00: break;  // PSG disabled
    case 0x08: break;  // PSG drives port A (read path)
    case 0x10:         // BDIR=1 BC1=0: write data
        if (snd_select_ != 14) ay_.write(porta_);
        break;
    case 0x18:         // BDIR=1 BC1=1: latch register address
        if ((porta_ & 0xf0) == 0x00) {
            snd_select_ = porta_ & 0x0f;
            ay_.control(porta_);
        }
        break;
    }
}

void Vectrex::alg_update() {
    switch (portb_ & 0x06) {
    case 0x00:
        jsh_ = joy_pot(0);
        if ((portb_ & 0x01) == 0) ysh_ = xsh_;  // mux on: Y
        break;
    case 0x02:
        jsh_ = joy_pot(1);
        if ((portb_ & 0x01) == 0) rsh_ = xsh_;  // mux on: zero reference
        break;
    case 0x04:
        jsh_ = joy_pot(2);
        if ((portb_ & 0x01) == 0)                // mux on: intensity
            zsh_ = xsh_ > 0x80 ? uint8_t(xsh_ - 0x80) : uint8_t(0);
        break;
    case 0x06:
        jsh_ = joy_pot(3);
        break;
    }
    dx_ = int(xsh_) - int(rsh_);
    dy_ = int(rsh_) - int(ysh_);
}

void Vectrex::via_pa_out(uint8_t v) {
    porta_ = v;
    xsh_ = uint8_t(v ^ 0x80);  // DAC feeds the X sample & hold
    snd_update();
    alg_update();
}

void Vectrex::via_pb_out(uint8_t v) {
    portb_ = v;
    snd_update();
    alg_update();
}

void Vectrex::on_ca2(bool level) {
    zero_active_ = !level;  // CA2 low = ZERO
}

uint8_t Vectrex::joy_pot(int ch) const {
    switch (ch) {
    case 0: return uint8_t(128 + joy_x_);
    case 1: return uint8_t(128 + joy_y_);
    default: return 128;  // unused channels read centered
    }
}

bool Vectrex::on_screen(float x, float y) const {
    return x > float(-kHalfX) && x < float(kHalfX) &&
           y > float(-kHalfY) && y < float(kHalfY);
}

void Vectrex::add_segment() {
    segments_.push_back({vx0_, vy0_, vx1_, vy1_, vintensity_});
}

// ---- Analog step, one 6809 cycle (vecx alg_sstep) ----

void Vectrex::step_one_cycle() {
    via_.tick(1);

    // /BLANK: CB2 high = unblanked. With ACR bit 4 set the shift register
    // drives CB2 and the beam follows the last bit it shifted out (text is
    // drawn that way); otherwise the PCR / handshake level does (vecx
    // via_cb2s / via_cb2h). Switching ACR must not unblank the beam.
    const bool unblanked = (via_.acr() & 0x10) ? via_.cb2_shift_level() : via_.cb2_handshake_level();

    float sdx = 0.0f, sdy = 0.0f;
    if (zero_active_) {
        // ZERO: head back to the origin this cycle (vecx: ALG_MAX/2 - curr).
        sdx = -pos_x_;
        sdy = -pos_y_;
    } else {
        bool ramp;
        if (via_.acr() & 0x80)
            ramp = (via_.out_b() & 0x80) != 0;  // T1 drives PB7
        else
            ramp = (portb_ & 0x80) != 0;

        // The integrators run for exactly as long as the RAMP line is
        // low, with no settling delay around it.
        if (!ramp) {
            sdx = float(dx_);
            sdy = float(dy_);
        }
    }

    if (!vectoring_) {
        if (unblanked && on_screen(pos_x_, pos_y_)) {
            vectoring_ = true;
            vx0_ = vx1_ = pos_x_;
            vy0_ = vy1_ = pos_y_;
            vdx_ = int(sdx);
            vdy_ = int(sdy);
            vzsh_ = zsh_;
            vintensity_ = float(zsh_) / 127.0f;
        }
    } else if (!unblanked) {
        add_segment();
        vectoring_ = false;
    } else if (int(sdx) != vdx_ || int(sdy) != vdy_ || zsh_ != vzsh_) {
        // Vector parameters changed: close the line and restart at the
        // current point if it is still inside the tube.
        add_segment();
        if (on_screen(pos_x_, pos_y_)) {
            vx0_ = vx1_ = pos_x_;
            vy0_ = vy1_ = pos_y_;
            vdx_ = int(sdx);
            vdy_ = int(sdy);
            vzsh_ = zsh_;
            vintensity_ = float(zsh_) / 127.0f;
        } else {
            vectoring_ = false;
        }
    }

    pos_x_ += sdx;
    pos_y_ += sdy;

    if (vectoring_ && on_screen(pos_x_, pos_y_)) {
        vx1_ = pos_x_;
        vy1_ = pos_y_;
    }
}

void Vectrex::on_cycles(int cycles) {
    for (int i = 0; i < cycles; ++i) step_one_cycle();
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kCpuClock)) {
        audio_acc_ -= int64_t(kCpuClock);
        const int32_t s = ay_.update();
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

// ---- Rasterization ----
//
// Every vector the beam traced during the frame is drawn once as an
// antialiased line whose brightness is its Z (intensity) level, the way
// vecx and MAME present the tube. Strokes redrawn in the same frame add up
// but saturate at full brightness, and the previous frame fades with a
// short phosphor decay instead of piling up: a long persistence smeared
// anything that moves (scrolling text, the ship) into a bright band.

void Vectrex::plot(int x, int y, float v) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight || v <= 0.0f) return;
    float& p = frame_[size_t(y) * kWidth + size_t(x)];
    p = std::min(1.0f, p + v);
}

void Vectrex::draw_line(float x0, float y0, float x1, float y1, float v) {
    // Xiaolin Wu style: one sample per pixel along the major axis, the
    // coverage split between the two pixels across it, plus a faint halo
    // so a line reads about 1.5 px wide like the real beam.
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::max(std::fabs(dx), std::fabs(dy));
    if (len < 0.5f) {  // a dot
        const int px = int(std::floor(x0)), py = int(std::floor(y0));
        plot(px, py, v);
        plot(px + 1, py, v * 0.35f);
        plot(px - 1, py, v * 0.35f);
        plot(px, py + 1, v * 0.35f);
        plot(px, py - 1, v * 0.35f);
        return;
    }
    const int steps = int(std::ceil(len));
    const bool steep = std::fabs(dy) > std::fabs(dx);
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const float fx = x0 + dx * t, fy = y0 + dy * t;
        if (steep) {
            const int py = int(std::floor(fy + 0.5f));
            const int px = int(std::floor(fx));
            const float f = fx - float(px);
            plot(px, py, v * (1.0f - f));
            plot(px + 1, py, v * f);
            plot(px - 1, py, v * 0.2f * (1.0f - f));
            plot(px + 2, py, v * 0.2f * f);
        } else {
            const int px = int(std::floor(fx + 0.5f));
            const int py = int(std::floor(fy));
            const float f = fy - float(py);
            plot(px, py, v * (1.0f - f));
            plot(px, py + 1, v * f);
            plot(px, py - 1, v * 0.2f * (1.0f - f));
            plot(px, py + 2, v * 0.2f * f);
        }
    }
}

void Vectrex::render_vectors() {
    if (vectoring_) {
        // Flush the in-progress vector so the frame shows complete strokes;
        // keep accumulating it afterwards.
        add_segment();
        vx0_ = vx1_;
        vy0_ = vy1_;
    }
    frame_.fill(0.0f);
    const float sx = float(kWidth) / float(kAlgMaxX);
    const float sy = float(kHeight) / float(kAlgMaxY);
    const float cx = float(kWidth) * 0.5f, cy = float(kHeight) * 0.5f;
    for (const auto& s : segments_) {
        if (s.intensity <= 0.0f) continue;  // zsh = 0: invisible stroke
        // dy is already rsh - ysh, which carries vecx's Y inversion, so the
        // raster must not flip it a second time.
        draw_line(cx + s.x0 * sx, cy + s.y0 * sy, cx + s.x1 * sx, cy + s.y1 * sy,
                  std::min(1.0f, s.intensity * 1.25f));
    }
    segments_.clear();
    for (size_t i = 0; i < glow_.size(); ++i) {
        const float g = std::max(glow_[i] * kPersistence, frame_[i]);
        glow_[i] = g;
        const int v = int(std::pow(g, 0.8f) * 255.0f + 0.5f);
        framebuffer_[i] = 0xff000000u | (uint32_t(v) << 16) | (uint32_t(v) << 8) | uint32_t(v);
    }
}

void Vectrex::run_frame() {
    int period = kCyclesPerFrameDefault;
    const uint16_t t2 = via_.t2_latch();  // BIOS frames with T2 = 0x7530 (30000)
    if (t2 >= 20000 && t2 <= 60000) period = int(t2);
    int remaining = period - frame_carry_;
    while (remaining > 0) {
        const int ran = cpu_.run(std::min(remaining, 1000));
        if (ran <= 0) break;
        remaining -= ran;
    }
    frame_carry_ = -remaining;  // overrun rolls into the next frame
    render_vectors();
}

void Vectrex::set_inputs(const MachineInputs& in) {
    buttons_ = 0;
    if (in.player1.button1) buttons_ |= 0x01;
    if (in.player1.button2) buttons_ |= 0x02;
    if (in.player1.button3) buttons_ |= 0x04;
    if (in.player1.button4) buttons_ |= 0x08;
    joy_x_ = int8_t(in.player1.left ? -127 : (in.player1.right ? 127 : 0));
    joy_y_ = int8_t(in.player1.down ? -127 : (in.player1.up ? 127 : 0));
}

void Vectrex::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
