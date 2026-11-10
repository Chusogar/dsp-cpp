// lean_roms: writes the images a lean Atari machine (src/lean) runs from, as
// a "prepared" ROM set the boards read in place from flash (no copy in RAM):
//
//   lean_roms gauntlet roms/gauntlet.zip roms/gauntlet_lean
//   lean_roms indydoom roms/indytemp.zip roms/indytemp_lean   (atarisy1.zip beside it, or in it)
//   lean_roms roadrunn roms/roadrunn.zip roms/roadrunn_lean
//
// The folder is then given to dsp like any set (--game gauntlet roms/gauntlet_lean),
// e.g. embedded in a firmware with -DDSP_EMBED_ROM=roms/gauntlet_lean.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "core/fs_compat.h"
#include "drivers/arcade/atari_system1_lean.h"
#include "drivers/arcade/gauntlet.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: lean_roms gauntlet|indydoom|roadrunn ROMSET OUTDIR\n");
        return 2;
    }
    const std::string game = argv[1], rom = argv[2], out = argv[3];
    std::error_code ec;
    dsp::fsx::create_directories(dsp::fsx::path(out), ec);
    std::string error;
    bool ok = false;
    if (game == "gauntlet") {
        dsp::Gauntlet machine;
        ok = machine.init(rom, &error) && machine.write_prepared(out, &error);
    } else if (game == "indydoom" || game == "roadrunn") {
        dsp::AtariSystem1Lean machine(game == "indydoom" ? dsp::AtariSystem1Lean::Game::Indy
                                                         : dsp::AtariSystem1Lean::Game::RoadRunner);
        ok = machine.init(rom, &error) && machine.write_prepared(out, &error);
    } else {
        error = "unknown game " + game;
    }
    if (!ok) {
        std::fprintf(stderr, "lean_roms: %s\n", error.c_str());
        return 1;
    }
    std::printf("lean_roms: %s written to %s\n", game.c_str(), out.c_str());
    return 0;
}
