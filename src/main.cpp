#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "drivers/bagman.h"
#include "frontend/sdl_app.h"

namespace {

void print_usage(const char* program) {
    std::printf(
        "Usage: %s [options] <bagman.zip | rom directory>\n"
        "\n"
        "Options:\n"
        "  --scale N          window scale factor (default 3)\n"
        "  --dip VALUE        DIP switch byte, decimal or 0x hex (default 0xfe)\n"
        "  --mute             disable audio\n"
        "  --fullscreen       start in full screen\n"
        "  --screenshot FILE  headless mode: render frames and write FILE (BMP)\n"
        "  --frames N         frames to run in headless mode (default 300)\n"
        "  --help             show this help\n"
        "\n"
        "Controls: arrows move, Left Ctrl/Space jump, 1/2 start, 5/6 insert coin,\n"
        "          P pause, F3 reset, Esc quit.\n",
        program);
}

}  // namespace

int main(int argc, char** argv) {
    dsp::AppOptions options;
    options.rom_path.clear();
    uint8_t dip = 0xfe;

    for (int index = 1; index < argc; index++) {
        std::string argument = argv[index];
        auto next = [&](const char* name) -> const char* {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n", name);
                std::exit(1);
            }
            return argv[++index];
        };
        if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (argument == "--scale") {
            options.scale = std::atoi(next("--scale"));
            if (options.scale < 1) options.scale = 1;
        } else if (argument == "--dip") {
            dip = uint8_t(std::strtoul(next("--dip"), nullptr, 0));
        } else if (argument == "--mute") {
            options.mute = true;
        } else if (argument == "--fullscreen") {
            options.fullscreen = true;
        } else if (argument == "--screenshot") {
            options.screenshot = next("--screenshot");
            if (options.frames == 0) options.frames = 300;
        } else if (argument == "--frames") {
            options.frames = std::atoi(next("--frames"));
        } else if (!argument.empty() && argument[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", argument.c_str());
            return 1;
        } else {
            options.rom_path = argument;
        }
    }

    if (options.rom_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    dsp::Bagman machine;
    std::string error;
    if (!machine.init(options.rom_path, &error)) {
        std::fprintf(stderr, "cannot start bagman: %s\n", error.c_str());
        return 1;
    }
    machine.set_dip_switches(dip);
    for (const std::string& warning : machine.warnings()) {
        std::fprintf(stderr, "warning: %s\n", warning.c_str());
    }

    dsp::SdlApp app(options);
    return app.run(machine);
}
