#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "drivers/amstrad_cpc.h"
#include "drivers/bagman.h"
#include "drivers/doubledragon.h"
#include "drivers/gauntlet.h"
#include "drivers/mikie.h"
#include "drivers/spectrum.h"
#include "drivers/spectrum_128k.h"
#include "drivers/spectrum_3.h"
#include "drivers/taitosj.h"
#include "drivers/sms.h"
#include "drivers/mrdo.h"
#include "drivers/atari_system1.h"
#include "drivers/mcr.h"

#include "frontend/sdl_app.h"

namespace {

struct DipSetting {
    int bank;
    uint8_t value;
};

void print_usage(const char* program) {
    std::printf(
        "Usage: %s [options] <romset.zip | rom directory>\n"
        "\n"
        "Games: sms, bagman (default), mikie, gauntlet, ddragon, ddragon2, elevator,\n"
        "       junglek, spectrum48, cpc464, cpc664, cpc6128\n"
        "\n"
        "Options:\n"
        "  --game NAME        game to run: sms, bagman, mikie, gauntlet, ddragon,\n"
        "                     ddragon2, elevator, junglek, spectrum48, cpc464,\n"
        "                     cpc664 or cpc6128\n"
        "  --tape FILE        ZX Spectrum or Amstrad CPC tape image (.tap/.tzx/.cdt)\n"
        "  --disk FILE        Amstrad CPC .dsk/.edsk floppy image (664/6128, needs\n"
        "                     amsdos.rom); use CAT / RUN\"filename\" from BASIC\n"
        "  --scale N          window scale factor (default 3)\n"
        "  --dip [BANK:]VALUE DIP switch byte, decimal or 0x hex; bagman has one\n"
        "                     bank, mikie has three (0=A, 1=B, 2=C); cpc: 0=colour(1)/\n"
        "                     green(0) monitor, 1=joysticks on the keyboard matrix\n"
        "                     (off by default, see README)\n"
        "  --mute             disable audio\n"
        "  --fullscreen       start in full screen\n"
        "  --screenshot FILE  headless mode: render frames and write FILE (BMP)\n"
        "  --frames N         frames to run in headless mode (default 300)\n"
        "  --help             show this help\n"
        "\n"
        "Controls: arrows move, Left Ctrl/Space button 1, Left Alt/Z button 2,\n"
        "          X button 3 (Double Dragon jump),\n"
        "          1/2 start, 5/6 insert coin, P pause, F3 reset, Esc quit.\n"
        "On the Spectrum the host keyboard is the Spectrum keyboard (Left Shift is\n"
        "caps shift, Left Ctrl symbol shift, cursor keys the caps shift arrows) and\n"
        "pause moves to F2.\n",
        program);
}

// Guesses the game from the ROM set name when --game is not given.
std::string guess_game(const std::string& rom_path) {
    std::string lowered;
    for (char character : rom_path) lowered += char(std::tolower(character));
    
	if (lowered.find("sms") != std::string::npos) return "sms";
    
	if (lowered.find("mikie") != std::string::npos) return "mikie";
    if (lowered.find("gauntlet") != std::string::npos) return "gauntlet";
    if (lowered.find("ddragon2") != std::string::npos) return "ddragon2";
    if (lowered.find("ddragon") != std::string::npos) return "ddragon";
    if (lowered.find("elevator") != std::string::npos) return "elevator";
    if (lowered.find("junglek") != std::string::npos || lowered.find("jungleking") != std::string::npos) {
        return "junglek";
    }
	if (lowered.find("mrdo") != std::string::npos) return "mrdo";
    
    if (lowered.find("spectrum") != std::string::npos || lowered.find("48.rom") != std::string::npos) {
        return "spectrum48";
    }
    if (lowered.find("cpc6128") != std::string::npos) return "cpc6128";
    if (lowered.find("cpc664") != std::string::npos) return "cpc664";
    if (lowered.find("cpc464") != std::string::npos) return "cpc464";

	if (lowered.find("spectrum128") != std::string::npos) return "spectrum128";
	if (lowered.find("plus3") != std::string::npos) return "plus3";

	if (lowered.find("indydoom") != std::string::npos) return "indydoom";
	if (lowered.find("peter") != std::string::npos) return "peter";
	if (lowered.find("marble") != std::string::npos) return "marble";

	if (lowered.find("tapper") != std::string::npos) return "tapper";
	if (lowered.find("tron") != std::string::npos) return "tron";
	if (lowered.find("shollow") != std::string::npos) return "shollow";
	if (lowered.find("domino") != std::string::npos) return "domino";
	if (lowered.find("whacko") != std::string::npos) return "whacko";
	if (lowered.find("dotron") != std::string::npos) return "dotron";
	if (lowered.find("timber") != std::string::npos) return "timber";


    return "bagman";
}

std::unique_ptr<dsp::Machine> create_machine(const std::string& game) {
    if (game == "sms") return std::make_unique<dsp::Sms>();
	if (game == "bagman") return std::make_unique<dsp::Bagman>();
    if (game == "mikie") return std::make_unique<dsp::Mikie>();
    if (game == "gauntlet") return std::make_unique<dsp::Gauntlet>();
	if (game == "mrdo") return std::make_unique<dsp::MrDo>();
    
    if (game == "ddragon") {
        return std::make_unique<dsp::DoubleDragon>(dsp::DoubleDragon::Variant::DDragon);
    }
    if (game == "ddragon2") {
        return std::make_unique<dsp::DoubleDragon>(dsp::DoubleDragon::Variant::DDragon2);
    }
    if (game == "elevator" || game == "elevatob" || game == "elevaction") {
        return std::make_unique<dsp::TaitoSJ>(dsp::TaitoSJ::Variant::ElevatorAction);
    }
    if (game == "junglek" || game == "jungleking") {
        return std::make_unique<dsp::TaitoSJ>(dsp::TaitoSJ::Variant::JungleKing);
    }
    if (game == "spectrum48" || game == "spectrum") return std::make_unique<dsp::Spectrum48k>();
    if (game == "cpc464") return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC464);
    if (game == "cpc664") return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC664);
    if (game == "cpc6128" || game == "cpc") {
        return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC6128);
    }

	if (game == "spectrum128") return std::make_unique<dsp::Spectrum128k>(dsp::Spectrum128k::Model::Spec128k);
	if (game == "plus3") return std::make_unique<dsp::Spectrum3>();
    
	if (game == "indydoom") return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::Indy);
	if (game == "peter") return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::PeterPak);	
	if (game == "marble") return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::Marble);

	// MCR
	if (game == "tapper") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Tapper);
	if (game == "tron") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Tron);
    if (game == "shollow") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Shollow);
	if (game == "domino") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Domino);
	if (game == "wacko") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Wacko);
	if (game == "dotron") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Dotron);
	if (game == "timber") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Timber);
	


    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    dsp::AppOptions options;
    std::string game;
    std::string tape;
    std::string disk;
    std::vector<DipSetting> dips;

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
        } else if (argument == "--game") {
            game = next("--game");
        } else if (argument == "--scale") {
            options.scale = std::atoi(next("--scale"));
            if (options.scale < 1) options.scale = 1;
        } else if (argument == "--dip") {
            std::string value = next("--dip");
            size_t separator = value.find(':');
            DipSetting setting{0, 0};
            if (separator == std::string::npos) {
                setting.value = uint8_t(std::strtoul(value.c_str(), nullptr, 0));
            } else {
                setting.bank = std::atoi(value.substr(0, separator).c_str());
                setting.value = uint8_t(std::strtoul(value.c_str() + separator + 1, nullptr, 0));
            }
            dips.push_back(setting);
        } else if (argument == "--tape") {
            tape = next("--tape");
        } else if (argument == "--disk") {
            disk = next("--disk");
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
    if (game.empty()) game = guess_game(options.rom_path);

    std::unique_ptr<dsp::Machine> machine = create_machine(game);
    if (machine == nullptr) {
        std::fprintf(stderr, "unknown game: %s\n", game.c_str());
        return 1;
    }

    std::string error;
    if (!machine->init(options.rom_path, &error)) {
        std::fprintf(stderr, "cannot start %s: %s\n", game.c_str(), error.c_str());
        return 1;
    }
    if (!tape.empty() && !machine->load_media(tape, &error)) {
        std::fprintf(stderr, "cannot load tape %s: %s\n", tape.c_str(), error.c_str());
        return 1;
    }
    if (!disk.empty() && !machine->load_media(disk, &error)) {
        std::fprintf(stderr, "cannot load disk %s: %s\n", disk.c_str(), error.c_str());
        return 1;
    }
    for (const DipSetting& setting : dips) machine->set_dip_switch(setting.bank, setting.value);
    for (const std::string& warning : machine->warnings()) {
        std::fprintf(stderr, "warning: %s\n", warning.c_str());
    }

    dsp::SdlApp app(options);
    return app.run(*machine);
}
