#pragma once

#include <string>

#include "core/machine.h"

namespace dsp {

struct AppOptions {
    std::string rom_path;
    int scale = 3;
    bool mute = false;
    bool fullscreen = false;
    // Headless mode: run `frames` frames and write a BMP screenshot, no window.
    std::string screenshot;
    int frames = 0;
};

// SDL2 front end: window, texture blitting, audio queue and keyboard input.
// Built natively (a blocking loop paced by the audio queue) or with Emscripten
// (the same loop body driven by emscripten_set_main_loop).
class SdlApp {
public:
    explicit SdlApp(const AppOptions& options) : options_(options) {}

    int run(Machine& machine);

#ifdef __EMSCRIPTEN__
    // Stops the machine the browser is running (if any) and releases the
    // window and audio, so the page can start another driver.
    static void stop_web();
#endif

    struct LoopState;  // per-frame state of the interactive loop (sdl_app.cpp)

private:

    int run_headless(Machine& machine);

    AppOptions options_;
};

}  // namespace dsp
