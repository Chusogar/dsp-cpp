#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Controls of a single player.
struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool button1 = false;
    bool button2 = false;
    bool start = false;
};

struct MachineInputs {
    InputState player1;
    InputState player2;
    bool coin1 = false;
    bool coin2 = false;
};

// Common interface implemented by every arcade driver, so the SDL2 front end
// does not need to know which game it is running.
class Machine {
public:
    virtual ~Machine() = default;

    // `rom_path` is a directory or a zip archive holding the ROM set.
    virtual bool init(const std::string& rom_path, std::string* error) = 0;
    virtual void reset() = 0;

    // Runs a full frame and renders it into the internal framebuffer.
    virtual void run_frame() = 0;

    virtual void set_inputs(const MachineInputs& inputs) = 0;
    // DIP switch bank, 0 based. Unknown banks are ignored.
    virtual void set_dip_switch(int bank, uint8_t value) = 0;

    // ARGB8888 framebuffer, screen_width() * screen_height() pixels.
    virtual const uint32_t* framebuffer() const = 0;
    virtual int screen_width() const = 0;
    virtual int screen_height() const = 0;
    virtual double frames_per_second() const = 0;

    // Consumes the audio samples generated so far (mono, signed 16 bit).
    virtual void drain_audio(std::vector<int16_t>& out) = 0;
    virtual int sample_rate() const = 0;

    virtual const char* title() const = 0;

    const std::vector<std::string>& warnings() const { return warnings_; }

protected:
    std::vector<std::string> warnings_;
};

}  // namespace dsp
