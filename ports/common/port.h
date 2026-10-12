#pragma once

// Shared front end of the microcontroller ports (ports/ttgo-vga32,
// ports/rp2350-pizero). A board supplies a Hal: picture out, sound out,
// keyboard/gamepad in, a clock and a place for the files; port_core.cpp does
// the rest: the ROM menu, starting a driver, the frame loop and its pacing.
//
// Everything a port runs is the normal dsp code (drivers, CPUs, sound chips,
// RomLoader), built with DSP_FS_POSIX so file access goes through stat/opendir
// on the board's SD card or the embedded files.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/embedded_media.h"
#include "core/machine.h"

namespace dsp {
namespace port {

// State of the keyboard and gamepad read by Hal::poll().
struct HostInput {
    std::array<bool, size_t(Key::Count)> keys{};  // keyboard, by dsp key
    bool left_alt = false;                         // not in dsp::Key
    // First gamepad (or joystick), already reduced to a d-pad and buttons.
    bool pad_connected = false;
    bool pad_up = false, pad_down = false, pad_left = false, pad_right = false;
    bool pad_a = false, pad_b = false, pad_x = false, pad_y = false;
    bool pad_start = false, pad_select = false;
    // Mouse, when the board has one: motion since the previous poll.
    bool mouse_connected = false;
    int mouse_dx = 0, mouse_dy = 0;
    bool mouse_left = false, mouse_right = false;

    bool key(Key k) const { return keys[size_t(k)]; }
};

class Hal {
public:
    virtual ~Hal() = default;

    // Name shown on the menu ("TTGO VGA32", "RP2350-PiZero").
    virtual const char* board_name() const = 0;

    // Picture: the board's screen size, and one frame to show. `argb` is
    // `width` x `height` ARGB8888; the board scales it with ScaleMap
    // (fit_rect / build) to `display_width` x `display_height` proportions.
    virtual int screen_width() const = 0;
    virtual int screen_height() const = 0;
    virtual void present(const uint32_t* argb, int width, int height, int display_width,
                         int display_height) = 0;
    // Menu screens: `rows` full-width rows (screen_width() pixels each, ARGB)
    // drawn unscaled from row `y`. The next present() must redraw its borders.
    virtual void draw_rows(int y, int rows, const uint32_t* argb) = 0;

    // Sound: mono signed 16-bit at output_rate(). push_audio() never blocks;
    // audio_queued() is how many samples are still waiting to be played.
    virtual int output_rate() const = 0;
    virtual void push_audio(const int16_t* samples, size_t count) = 0;
    virtual size_t audio_queued() const = 0;

    virtual void poll(HostInput& input) = 0;
    virtual uint64_t micros() = 0;
    virtual void sleep_us(uint32_t us) = 0;
    // Called while waiting in menus: USB host tasks, watchdog, etc.
    virtual void idle() {}

    // Folder with the user's files ("/sd"); empty when there is no card.
    virtual std::string storage_root() = 0;
    // Makes the embedded files readable under a folder and returns it
    // ("/embedded"): a read-only memory file system on the boards.
    virtual bool mount_embedded(const DspEmbeddedFile* files, size_t count, std::string* dir,
                                std::string* error) = 0;

    virtual void log(const char* text) = 0;
};

// Runs the port: never returns on the boards (the menu comes back when a game
// is left with F12 or Start+Select). `max_frames` > 0 stops after that many
// emulated frames (host tests), returning 0 on success.
int run(Hal& hal, int max_frames = 0);

// Nearest-neighbour scaling of a machine picture into the board's screen,
// keeping the display aspect ratio. Boards walk ys/xs to fetch source pixels.
struct ScaleMap {
    int src_w = 0, src_h = 0, disp_w = 0, disp_h = 0;
    int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;  // picture inside the screen
    std::vector<uint16_t> xs;  // source column for each dst column (dst_w)
    std::vector<uint16_t> ys;  // source row for each dst row (dst_h)

    // Rebuilds the tables when the source changes; true when it did (the
    // board then clears the borders).
    bool update(int src_width, int src_height, int display_width, int display_height,
                int screen_w, int screen_h);
    // Forces the next update() to report a change (after a menu was drawn).
    void invalidate() { src_w = 0; }
};

// Turns the host keyboard/gamepad into the machine's inputs, with the same
// keys as the SDL front end (arrows, Ctrl/Space/Z/X/C, 1/2 start, 5/6 coin).
MachineInputs map_inputs(const HostInput& in, const Machine& machine);

// Converts the machine's sample rate to the board's (linear interpolation).
class Resampler {
public:
    void reset(int in_rate, int out_rate);
    void process(const std::vector<int16_t>& in, std::vector<int16_t>& out);

private:
    uint32_t step_ = 1u << 16;  // input samples per output sample, 16.16
    uint32_t pos_ = 0;          // position between prev_ and the next input sample
    int16_t prev_ = 0;
};

// Splits a command line or a text file into arguments ("quotes" keep spaces;
// '#' starts a comment line).
std::vector<std::string> split_args(const std::string& text);

}  // namespace port
}  // namespace dsp
