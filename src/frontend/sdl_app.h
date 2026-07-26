#pragma once

#include <string>

#include "drivers/bagman.h"

namespace dsp {

struct AppOptions {
    std::string rom_path = "bagman";
    int scale = 3;
    bool mute = false;
    bool fullscreen = false;
    // Headless mode: run `frames` frames and write a BMP screenshot, no window.
    std::string screenshot;
    int frames = 0;
};

// SDL2 front end: window, texture blitting, audio queue and keyboard input.
class SdlApp {
public:
    explicit SdlApp(const AppOptions& options) : options_(options) {}

    int run(Bagman& machine);

private:
    int run_headless(Bagman& machine);

    AppOptions options_;
};

}  // namespace dsp
