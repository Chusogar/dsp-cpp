#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"

// Arcade
#include "drivers/bagman.h"
#include "drivers/doubledragon.h"
#include "drivers/gauntlet.h"
#include "drivers/mikie.h"
#include "drivers/taitosj.h"
#include "drivers/mrdo.h"
#include "drivers/atari_system1.h"
#include "drivers/mcr.h"
#include "drivers/dec0.h"
#include "drivers/m62.h"
#include "drivers/snk.h"
#include "drivers/cps1.h"
#include "drivers/m72.h"
#include "drivers/starwars.h"
#include "drivers/outrun.h"
#include "drivers/hangon.h"
#include "drivers/system16.h"

// Computers
#include "drivers/spectrum.h"
#include "drivers/spectrum_128k.h"
#include "drivers/spectrum_3.h"
#include "drivers/amstrad_cpc.h"
#include "drivers/msx1.h"
#include "drivers/c64.h"
#include "drivers/exelv.h"
#include "drivers/pentagon.h"
#include "drivers/scorpion.h"

// Consoles
#include "drivers/sms.h"
#include "drivers/gamegear.h"
#include "drivers/genesis.h"
#include "drivers/pv1000.h"
#include "drivers/colecovision.h"
#include "drivers/sg1000.h"
#include "drivers/gameboy.h"
#include "drivers/nes.h"
#include "drivers/atari_lynx.h"
#include "drivers/scv.h"

#include "frontend/sdl_app.h"

namespace {

struct DipSetting {
    int bank;
    uint8_t value;
};

void print_supported_emulators() {
    std::printf(
        "Supported emulators (--game NAME):\n"
        "\n"
        "  Arcade:\n"
        "    bagman, mikie, gauntlet, mrdo, ddragon, ddragon2,\n"
        "    elevator, junglek, indydoom, peter, marble, starwars, roadrunn,\n"
        "    tapper, tron, shollow, domino, wacko, dotron, timber,\n"
		"    robocop, baddudes, hippodrm, slyspy, bouldash,\n"
        "    kungfum, spelunkr, spelunk2, ldrun, ldrun2,\n"
        "    ikari, athena, tnk3, aso,\n"
        "    ghouls, ffight, kod, sf2, strider, 3wonders, captcomm,\n"
        "    knights, sf2ce, dino, punisher, willow, 1941, nemo,\n"
        "    rtype, hharry, rtype2,\n"
        "    outrun, hangon, fantzone, shinobi, tetris, altbeast\n"
        "\n"
        "  Computers:\n"
        "    spectrum48, spectrum128, plus3, pentagon, scorpion,\n"
        "    cpc464, cpc664, cpc6128, msx, c64,\n"
        "    exl100, exeltel\n"
        "\n"
        "  Consoles:\n"
        "    sms, gamegear, genesis, megadrive, genesis-pal, genesis-jp,\n"
        "    pv1000, coleco, sg1000, gb, nes, lynx, scv\n"
        "\n");
}

void print_usage(const char* program) {
    std::printf(
        "Usage: %s --game NAME [options] <romset.zip | rom directory>\n"
        "\n",
        program);
    print_supported_emulators();
    std::printf(
        "Options:\n"
        "  --game NAME        emulator / game to run (required; see list above)\n"
        "  --tape FILE        tape/cart: Spectrum/CPC/C64 (.tap/.tzx/.cdt/.prg/.t64),\n"
        "                     or EXL-100 / EXELTEL cartridge (.bin/.rom)\n"
        "  --disk FILE        floppy: CPC/Spectrum +3 .dsk/.edsk, Pentagon/Scorpion .trd/.scl\n"
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
        "pause moves to F2.\n");
}

std::unique_ptr<dsp::Machine> create_machine(const std::string& game) {

	// arcade
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
	if (game == "indydoom") return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::Indy);
	if (game == "peter") return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::PeterPak);	
	if (game == "marble") return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::Marble);
	if (game == "starwars" || game == "star-wars") {
		return std::make_unique<dsp::StarWars>();
	}
	if (game == "roadrunn" || game == "roadrunner") {
		return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::RoadRunner);
	}

	if (game == "tapper") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Tapper);
	if (game == "tron") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Tron);
    if (game == "shollow") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Shollow);
	if (game == "domino") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Domino);
	if (game == "wacko") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Wacko);
	if (game == "dotron") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Dotron);
	if (game == "timber") return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Timber);

	if (game == "robocop") return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::Robocop);
    if (game == "baddudes" || game == "drgninja") {
        return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::BadDudes);
    }
    if (game == "hippodrm" || game == "hippodrome") {
        return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::Hippodrome);
    }
    if (game == "slyspy" || game == "secretag") {
        return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::SlySpy);
    }
    if (game == "bouldash") return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::BoulderDash);

    if (game == "kungfum" || game == "kungfu") {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::KungFuMaster);
    }
    if (game == "spelunkr" || game == "spelunker") {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::Spelunker);
    }
    if (game == "spelunk2" || game == "spelunker2") {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::Spelunker2);
    }
    if (game == "ldrun" || game == "loderunner") {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::LodeRunner);
    }
    if (game == "ldrun2" || game == "loderunner2") {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::LodeRunner2);
    }
    if (game == "ikari") return std::make_unique<dsp::Snk>(dsp::Snk::Game::Ikari);
    if (game == "athena") return std::make_unique<dsp::Snk>(dsp::Snk::Game::Athena);
    if (game == "tnk3") return std::make_unique<dsp::Snk>(dsp::Snk::Game::Tnk3);
    if (game == "aso") return std::make_unique<dsp::Snk>(dsp::Snk::Game::Aso);
    if (game == "ghouls") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Ghouls);
    if (game == "ffight" || game == "finalfight") {
        return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Ffight);
    }
    if (game == "kod") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Kod);
    if (game == "sf2") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Sf2);
    if (game == "strider") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Strider);
    if (game == "3wonders" || game == "wonder3") {
        return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Wonder3);
    }
    if (game == "captcomm") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Captcomm);
    if (game == "knights") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Knights);
    if (game == "sf2ce") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Sf2ce);
    if (game == "dino") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Dino);
    if (game == "punisher") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Punisher);
    if (game == "willow") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Willow);
    if (game == "1941") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Ca1941);
    if (game == "nemo") return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Nemo);
    if (game == "rtype") return std::make_unique<dsp::M72>(dsp::M72::Game::Rtype);
    if (game == "hharry") return std::make_unique<dsp::M72>(dsp::M72::Game::Hharry);
    if (game == "rtype2") return std::make_unique<dsp::M72>(dsp::M72::Game::Rtype2);
    if (game == "outrun") return std::make_unique<dsp::Outrun>();
    if (game == "hangon" || game == "hang-on") return std::make_unique<dsp::HangOn>();
    if (game == "fantzone" || game == "fantasyzone") {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Fantzone);
    }
    if (game == "shinobi") return std::make_unique<dsp::System16>(dsp::System16::Game::Shinobi);
    if (game == "tetris") return std::make_unique<dsp::System16>(dsp::System16::Game::Tetris);
    if (game == "altbeast" || game == "alteredbeast") {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Altbeast);
    }
    

	// computers
    if (game == "spectrum48" || game == "spectrum") return std::make_unique<dsp::Spectrum48k>();
    if (game == "cpc464") return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC464);
    if (game == "cpc664") return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC664);
    if (game == "cpc6128" || game == "cpc") {
        return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC6128);
    }
	if (game == "spectrum128") return std::make_unique<dsp::Spectrum128k>(dsp::Spectrum128k::Model::Spec128k);
	if (game == "plus3") return std::make_unique<dsp::Spectrum3>();
	if (game == "pentagon" || game == "pentagon1024" || game == "pent1024") {
	    return std::make_unique<dsp::Pentagon1024>();
	}
	if (game == "scorpion" || game == "scorpion256" || game == "scorpio" || game == "zs256") {
	    return std::make_unique<dsp::Scorpion256>();
	}
	if (game == "msx") return std::make_unique<dsp::Msx1>();
	if (game == "c64" || game == "commodore64" || game == "commodore") {
        return std::make_unique<dsp::C64>();
    }
	if (game == "exl100" || game == "exl-100" || game == "exelvision") {
	    return std::make_unique<dsp::Exelv>(dsp::Exelv::Model::Exl100);
	}
	if (game == "exeltel") {
	    return std::make_unique<dsp::Exelv>(dsp::Exelv::Model::Exeltel);
	}
    	
	// consoles
	if (game == "sms") return std::make_unique<dsp::Sms>();
	if (game == "gamegear") return std::make_unique<dsp::GameGear>();
	if (game == "genesis" || game == "megadrive" || game == "mega-drive" ||
	    game == "md" || game == "gen") {
	    return std::make_unique<dsp::Genesis>();
	}
	if (game == "genesis-pal" || game == "megadrive-pal") {
	    return std::make_unique<dsp::Genesis>(dsp::Genesis::Region::Europe);
	}
	if (game == "genesis-jp" || game == "megadrive-jp") {
	    return std::make_unique<dsp::Genesis>(dsp::Genesis::Region::Japan);
	}
	if (game == "pv1000") return std::make_unique<dsp::Pv1000>();
	if (game == "coleco") return std::make_unique<dsp::ColecoVision>();
	if (game == "sg1000") return std::make_unique<dsp::Sg1000>();
	if (game == "gb") return std::make_unique<dsp::GameBoy>();
	if (game == "nes") return std::make_unique<dsp::Nes>();
	if (game == "lynx") return std::make_unique<dsp::AtariLynx>();
	if (game == "scv") return std::make_unique<dsp::Scv>();


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

    // No --game: list supported emulators and exit
    if (game.empty()) {
        std::printf("%s: specify an emulator with --game NAME\n\n", argv[0]);
        print_supported_emulators();
        std::printf("Example: %s --game spectrum48 roms/\n"
                    "         %s --game bagman bagman.zip\n"
                    "Use --help for all options.\n",
                    argv[0], argv[0]);
        return 1;
    }

    if (options.rom_path.empty()) {
        std::fprintf(stderr, "missing ROM path (zip or directory)\n\n");
        print_usage(argv[0]);
        return 1;
    }

    std::unique_ptr<dsp::Machine> machine = create_machine(game);
    if (machine == nullptr) {
        std::fprintf(stderr, "unknown game: %s\n\n", game.c_str());
        print_supported_emulators();
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
