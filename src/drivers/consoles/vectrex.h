#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "machine/via6522.h"
#include "sound/ay8910.h"

namespace dsp {

// Vectrex — capa gráfica (alg) fiel a jhawthorn/vecx:
//   DAC sin signo (xsh = ora ^ 0x80, 128 = centro)
//   dx = xsh - rsh, dy = rsh - ysh  (rsh/ysh/zsh capturados por el mux PB0..PB2)
//   zsh = (xsh > 0x80) ? xsh - 0x80 : 0
//   RAMP = PB7 (o T1PB7 si ACR.7); BLANK = !CB2: último bit del SR si ACR.4,
//   si no el nivel PCR/handshake (vecx via_cb2s / via_cb2h)
//   ZERO (CA2 bajo) devuelve el haz al centro cada ciclo (puede dibujar el tramo)
//   El vector se cierra al blank o al cambiar dx/dy/intensidad.
class Vectrex : public Machine {
public:
    static constexpr uint32_t kCpuClock = 1500000;
    static constexpr int kFps = 50;
    static constexpr int kCyclesPerFrameDefault = int(kCpuClock) / kFps;
    static constexpr int kAlgMaxX = 33000;
    static constexpr int kAlgMaxY = 41000;
    static constexpr int kHalfX = kAlgMaxX / 2;
    static constexpr int kHalfY = kAlgMaxY / 2;
    static constexpr int kWidth = 512;
    static constexpr int kHeight = 640;
    static constexpr int kSampleRate = 44100;
    // Fraction of the previous frame still glowing (P31 phosphor fades fast).
    static constexpr float kPersistence = 0.45f;

    Vectrex();
    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int, uint8_t) override {}
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Vectrex"; }
    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t cpu_read(uint16_t a);
    void cpu_write(uint16_t a, uint8_t v);
    void on_cycles(int cycles);
    uint8_t via_pa_in();
    uint8_t via_pb_in();
    void via_pa_out(uint8_t v);
    void via_pb_out(uint8_t v);
    void on_ca2(bool level);
    void snd_update();    // vecx: evaluado en cada escritura ORA/ORB
    void alg_update();    // vecx: mux + dx/dy
    uint8_t joy_pot(int ch) const;
    bool on_screen(float x, float y) const;
    void add_segment();
    void step_one_cycle();
    void render_vectors();
    void draw_line(float x0, float y0, float x1, float y1, float v);
    void plot(int x, int y, float v);

    struct Segment { float x0, y0, x1, y1, intensity; };

    M6809 cpu_;
    Via6522 via_;
    AY8910 ay_;
    std::array<uint8_t, 0x2000> bios_{};
    std::vector<uint8_t> cart_;
    std::array<uint8_t, 0x400> ram_{};

    // VIA ports (vecx via_ora / via_orb)
    uint8_t porta_ = 0, portb_ = 0;

    // Analog sample & holds (unsigned, 128 = center)
    uint8_t xsh_ = 128;  // X DAC (ora ^ 0x80)
    uint8_t rsh_ = 128;  // zero reference
    uint8_t ysh_ = 128;  // Y
    uint8_t zsh_ = 0;    // intensity 0..127
    uint8_t jsh_ = 128;  // joystick comparator input
    int dx_ = 0, dy_ = 0;

    // Sound chip latch
    uint8_t snd_select_ = 0;

    // Beam position, centered analog coords (0,0 = screen center)
    float pos_x_ = 0.0f, pos_y_ = 0.0f;
    bool zero_active_ = false;  // CA2 low
    // Cycles executed beyond the frame budget, carried into the next frame.
    // The CPU always finishes the instruction in progress, so a frame
    // overruns by a cycle or two; dropping that instead of carrying it
    // shifts the VIA timers a little further every frame, and after a few
    // hundred frames the BIOS is timing everything against a counter that
    // is hundreds of cycles out of phase.
    int frame_carry_ = 0;

    // Current vector (vecx alg_vector*)
    bool vectoring_ = false;
    float vx0_ = 0, vy0_ = 0, vx1_ = 0, vy1_ = 0, vintensity_ = 0;
    int vdx_ = 0, vdy_ = 0;
    uint8_t vzsh_ = 0;
    std::vector<Segment> segments_;
    std::array<float, size_t(kWidth) * kHeight> glow_{};
    std::array<float, size_t(kWidth) * kHeight> frame_{};
    std::array<uint32_t, size_t(kWidth) * kHeight> framebuffer_{};

    uint8_t buttons_ = 0;
    int8_t joy_x_ = 0, joy_y_ = 0;
    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
};

}  // namespace dsp
