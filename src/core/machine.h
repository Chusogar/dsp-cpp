#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Host keys forwarded to the machines with a real keyboard (home computers).
// The arcade drivers ignore them.
enum class Key {
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Enter, Space, LeftShift, RightShift, LeftCtrl, RightCtrl, Backspace,
    Up, Down, Left, Right, Comma, Period, Semicolon, Quote, Slash, Minus,
    Escape, Tab, CapsLock, Home, Cbm, Equals, Plus, Asterisk, At,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Backslash, Backquote, Delete, LeftGui, RightGui, RightAlt,
    Count
};

// Controls of a single player.
struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool button1 = false;
    bool button2 = false;
    bool button3 = false;
    bool button4 = false;
    bool start = false;
    bool select = false;
};

struct MachineInputs {
    InputState player1;
    InputState player2;
    bool coin1 = false;
    bool coin2 = false;
    // Operator service button (e.g. Williams "Advance"), F1 in the front end.
    bool service = false;
    std::array<bool, size_t(Key::Count)> keys{};

    // Light gun / mouse, in screen pixels. `has_pointer` is set when the
    // front end has a real pointing device this frame.
    bool has_pointer = false;
    int pointer_x = 0;
    int pointer_y = 0;
    bool pointer_button1 = false;
    bool pointer_button2 = false;
    // Captured (relative) mouse: motion since the previous frame in screen
    // pixels, for drivers that report uses_relative_pointer(). When set,
    // pointer_x/pointer_y are meaningless.
    bool pointer_relative = false;
    // Set on the frame the host pointer enters the window: relative-mouse
    // drivers line their pointer up again (see uses_relative_pointer()).
    bool pointer_resync = false;
    int pointer_dx = 0;
    int pointer_dy = 0;

    bool key(Key value) const { return keys[size_t(value)]; }
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

    // True when the driver reads MachineInputs::keys, so the front end does not
    // steal letters for its own shortcuts.
    virtual bool uses_keyboard() const { return false; }

    // True when the driver aims with MachineInputs::pointer_*.
    virtual bool uses_pointer() const { return false; }

    // True for machines whose mouse is a relative device read by each
    // program in its own units (Atari ST, Amiga). The front end hides the
    // host cursor over the window, keeps reporting the (clamped) position
    // while the mouse is outside it, and flags pointer_resync when it comes
    // back in, so the emulated pointer is the only one on screen and lines
    // up with where the host mouse is.
    virtual bool uses_relative_pointer() const { return false; }

    // Attaches a tape, disk or cartridge image. Machines without removable
    // media reject it.
    virtual bool load_media(const std::string& path, std::string* error) {
        (void)path;
        if (error != nullptr) *error = "this machine has no removable media";
        return false;
    }

    // Spectrum / CPC: toggle cassette play (F6). Default no-op.
    virtual void tape_toggle_play() {}
    virtual bool tape_loaded() const { return false; }

    const std::vector<std::string>& warnings() const { return warnings_; }

protected:
    std::vector<std::string> warnings_;
};

}  // namespace dsp
