#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"

// Arcade
#include "drivers/arcade/bagman.h"
#include "drivers/arcade/doubledragon.h"
#include "drivers/arcade/gauntlet.h"
#include "drivers/arcade/mikie.h"
#include "drivers/arcade/taitosj.h"
#include "drivers/arcade/mrdo.h"
#include "drivers/arcade/atari_system1.h"
#include "drivers/arcade/atari_system2.h"
#include "drivers/arcade/mcr.h"
#include "drivers/arcade/dec0.h"
#include "drivers/arcade/m62.h"
#include "drivers/arcade/snk.h"
#include "drivers/arcade/cps1.h"
#include "drivers/arcade/m72.h"
#include "drivers/arcade/starwars.h"
#include "drivers/arcade/asteroid.h"
#include "drivers/arcade/polepos.h"
#include "drivers/arcade/hangon.h"
#include "drivers/arcade/system16.h"
#include "drivers/arcade/sega_system1.h"
#include "drivers/arcade/outrun.h"
#include "drivers/arcade/galaxian.h"
#include "drivers/arcade/vicdual.h"
#include "drivers/arcade/opwolf.h"
#include "drivers/arcade/trackfld.h"
#include "drivers/arcade/pirates.h"
#include "drivers/arcade/armedf_hw.h"
#include "drivers/arcade/neogeo.h"
#include "drivers/arcade/wwfsstar.h"
#include "drivers/arcade/citycon.h"
#include "drivers/arcade/commando.h"
#include "drivers/arcade/actfancer.h"
#include "drivers/arcade/ajax.h"
#include "drivers/arcade/aliens.h"
#include "drivers/arcade/simpsons.h"
#include "drivers/arcade/galaga_hw.h"
#include "drivers/arcade/shadow_warriors_hw.h"
#include "drivers/arcade/tetris_atari_hw.h"
#include "drivers/arcade/skullxbo.h"
#include "drivers/arcade/gng.h"
#include "drivers/arcade/bublbobl.h"

// Computers
#include "drivers/computers/spectrum.h"
#include "drivers/computers/spectrum_128k.h"
#include "drivers/computers/spectrum_3.h"
#include "drivers/computers/amstrad_cpc.h"
#include "drivers/computers/msx1.h"
#include "drivers/computers/msx2.h"
#include "drivers/computers/c64.h"
#include "drivers/computers/apple2.h"
#include "drivers/computers/exelv.h"
#include "drivers/computers/pentagon.h"
#include "drivers/computers/scorpion.h"
#include "drivers/computers/ql.h"

// Consoles
#include "drivers/consoles/sms.h"
#include "drivers/consoles/gamegear.h"
#include "drivers/consoles/genesis.h"
#include "drivers/consoles/pv1000.h"
#include "drivers/consoles/pv2000.h"
#include "drivers/consoles/colecovision.h"
#include "drivers/consoles/sg1000.h"
#include "drivers/consoles/gameboy.h"
#include "drivers/consoles/nes.h"
#include "drivers/consoles/atari_lynx.h"
#include "drivers/consoles/a2600.h"
#include "drivers/consoles/scv.h"
#include "drivers/consoles/pcengine.h"


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
        "    bagman, mikie, trackfld, gauntlet, mrdo, ddragon, ddragon2,\n"
        "    elevator, junglek, indydoom, peter, marble, skullxbo, starwars, asteroid, roadrunn,\n"
        "    paperboy, ssprint, apb, 720,\n"
        "    tapper, tron, shollow, domino, wacko, dotron, timber,\n"
		"    robocop, baddudes, hippodrm, slyspy, bouldash,\n"
        "    kungfum, spelunkr, spelunk2, ldrun, ldrun2,\n"
        "    ikari, athena, tnk3, aso,\n"
        "    ghouls, ffight, kod, sf2, strider, 3wonders, captcomm,\n"
        "    knights, sf2ce, dino, punisher, willow, 1941, nemo,\n"
        "    rtype, hharry, rtype2,\n"
        "    polepos, polepos2\n"
        "    outrun, hangon, enduro, sharrier, fantzone, shinobi,\n"
		"    alexkidd, aliensyn, wb3, tetris, altbeast,\n"
        "    pitfall2, teddyboy, wboy, mrviking, seganinj, upndown,\n"
		"    flicky, gardia,\n"
		"    galaxian, mooncrst, scramble,\n"
		"    galaga, digdug, xevious, sxevious, bosco,\n"
		"    depthch, safari, frogs, sspaceat, sspacaho, headon, headon2,\n"
		"    headon2s, invho2, nsub, samurai, invinco, invds, tranqgun,\n"
		"    spacetrk, carnival, brdrline, digger, pulsar, heiankyo, alphaho,\n"
        "    neogeo, nam1975, maglord, mslug, kof94, kof95, kof97, kof98,\n"
		"    pbobblen, turfmast, tws96\n"
        "    fatfury, samsho, aof, lastblad, bstars, whp,\n"
        "    opwolf, pirates, genix, bublbobl,\n"
		"    terraf, armedf, cclimbr2, legion\n"
		"    citycon, commando\n"
		"    wwfsstar, atetris\n"
		"    shadoww, gaiden, ninjagaiden\n"
		"    actfancer, actfancr\n"
		"    ajax, typhoon, simpsons\n"
        "\n"
        "  Computers:\n"
        "    spectrum48, spectrum128, plus3, pentagon, scorpion,\n"
        "    cpc464, cpc664, cpc6128, msx, msx2, nms8250, c64,\n"
        "    apple2, apple2plus, apple2e, apple2ee, exl100, exeltel, ql\n"
        "\n"
        "  Consoles:\n"
        "    sms, gamegear, genesis, megadrive, genesis-pal, genesis-jp,\n"
        "    pv1000, pv2000, coleco, sg1000, gb, nes, lynx, scv, pcengine, sgx,\n"
        "    a2600, atari2600, vcs\n"
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
        "  --tape FILE        tape/cart: Spectrum/CPC/C64/MSX (.tap/.tzx/.cdt/.prg/.t64/.cas),\n"
        "                     EXL-100 / EXELTEL cartridge, PV-2000 cart (.bin/.rom),\n"
        "                     or QL microdrive .mdv/.qlpak\n"
        "  --disk FILE        floppy: CPC/Spectrum +3 .dsk/.edsk, MSX2 .dsk, Apple II .dsk/.do/.po/.nib,\n"
        "                     Pentagon/Scorpion .trd/.scl, or QL microdrive .mdv/.qlpak\n"
        "                     (repeat --disk/--tape to fill QL mdv1 then mdv2)\n"
        "  --scale N          window scale factor (default 3)\n"
        "  --dip [BANK:]VALUE DIP switch byte, decimal or 0x hex; bagman has one\n"
        "                     bank, mikie has three (0=A, 1=B, 2=C); trackfld: 0=A coinage,\n"
        "                     1=B lives/difficulty; opwolf: 0=A coinage,\n"
        "                     1=B difficulty/language; pirates/genix: settings\n"
        "                     live in the 93C46 EEPROM (no DIP banks); cpc: 0=colour(1)/\n"
        "                     green(0) monitor, 1=joysticks on the keyboard matrix\n"
        "                     (off by default, see README)\n"
        "  --mute             disable audio\n"
        "  --fullscreen       start in full screen\n"
        "  --screenshot FILE  headless mode: render frames and write FILE (BMP)\n"
        "  --frames N         frames to run in headless mode (default 300)\n"
        "  --help             show this help\n"
        "\n"
        "Controls: arrows move, Left Ctrl/Space button 1, Left Alt/Z button 2,\n"
        "          X button 3, C button 4 (NeoGeo D), 3/4 select (NeoGeo),\n"
        "          1/2 start, 5/6 insert coin, P pause, F3 reset, Esc quit.\n"
        "Track & Field: Left Ctrl / Left Alt / X are the three run/jump buttons.\n"
        "Operation Wolf: mouse aims the gun, Left Ctrl/Space fire, Left Alt/Z grenade;\n"
        "          arrows also move the sight if there is no mouse.\n"
        "On the Spectrum the host keyboard is the Spectrum keyboard (Left Shift is\n"
        "caps shift, Left Ctrl symbol shift, cursor keys the caps shift arrows) and\n"
        "pause moves to F2.\n");
}

std::unique_ptr<dsp::Machine> create_machine(const std::string& game) {

	// arcade
    if (game == "bagman") return std::make_unique<dsp::Bagman>();
    if (game == "mikie") return std::make_unique<dsp::Mikie>();
    if (game == "trackfld" || game == "trackfield" || game == "trackandfield") {
        return std::make_unique<dsp::TrackFld>();
    }
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
	if (game == "asteroid" || game == "asteroids") {
		return std::make_unique<dsp::Asteroid>();
	}
	if (game == "roadrunn" || game == "roadrunner") {
		return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::RoadRunner);
	}
	if (game == "paperboy") {
		return std::make_unique<dsp::AtariSystem2>(dsp::AtariSystem2::Game::Paperboy);
	}
	if (game == "ssprint") {
		return std::make_unique<dsp::AtariSystem2>(dsp::AtariSystem2::Game::SuperSprint);
	}
	if (game == "apb") {
		return std::make_unique<dsp::AtariSystem2>(dsp::AtariSystem2::Game::Apb);
	}
	if (game == "720" || game == "720degrees") {
		return std::make_unique<dsp::AtariSystem2>(dsp::AtariSystem2::Game::Degrees720);
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
	if (game == "polepos" || game == "poleposition") {
		return std::make_unique<dsp::PolePos>(dsp::PolePos::Game::PolePosition);
	}
	if (game == "polepos2" || game == "poleposition2") {
		return std::make_unique<dsp::PolePos>(dsp::PolePos::Game::PolePosition2);
	}
    
	if (game == "outrun") return std::make_unique<dsp::Outrun>();

    if (game == "hangon" || game == "hang-on") return std::make_unique<dsp::HangOn>();
    if (game == "enduro" || game == "enduror" || game == "enduro-racer") {
        return std::make_unique<dsp::HangOn>(dsp::HangOn::Game::Enduro);
    }
    if (game == "sharrier" || game == "spaceharrier" || game == "space-harrier") {
        return std::make_unique<dsp::HangOn>(dsp::HangOn::Game::Sharrier);
    }
    if (game == "fantzone" || game == "fantasyzone") {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Fantzone);
    }
    if (game == "shinobi") return std::make_unique<dsp::System16>(dsp::System16::Game::Shinobi);
    if (game == "alexkidd" || game == "alexkid") {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Alexkidd);
    }
    if (game == "aliensyn" || game == "aliensynd" || game == "aliensyndrome") {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Aliensyn);
    }
    if (game == "wb3" || game == "wonderboy3" || game == "wonderboyiii") {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Wb3);
    }
    if (game == "tetris") return std::make_unique<dsp::System16>(dsp::System16::Game::Tetris);
    if (game == "altbeast" || game == "alteredbeast") {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Altbeast);
    }
	
	// Sega System 1
	if (game == "pitfall2" || game == "pitfallii" || game == "pitfall") {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::Pitfall2);
	}
	if (game == "teddyboy" || game == "teddy" || game == "tdboy") {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::TeddyBoy);
	}
	if (game == "wboy" || game == "wonderboy") {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::WonderBoy);
	}
	if (game == "mrviking" || game == "viking") {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::MrViking);
	}
	if (game == "seganinj" || game == "seganinja" || game == "ninja") {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::SegaNinja);
	}
	if (game == "upndown" || game == "up-n-down" || game == "upanddown") {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::UpNDown);
	}
	if (game == "flicky") {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::Flicky);
	}
	if (game == "gardia") {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::Gardia);
	}

	if (game == "galaxian") return std::make_unique<dsp::Galaxian>(dsp::Galaxian::Game::Galaxian);
	if (game == "mooncrst" || game == "mooncresta") return std::make_unique<dsp::Galaxian>(dsp::Galaxian::Game::MoonCresta);
	if (game == "scramble") return std::make_unique<dsp::Galaxian>(dsp::Galaxian::Game::Scramble);
	if (game == "frogger") return std::make_unique<dsp::Galaxian>(dsp::Galaxian::Game::Frogger);

	if (game == "opwolf" || game == "operationwolf" || game == "operation-wolf") {
		return std::make_unique<dsp::OpWolf>();
	}

	// Sega / Gremlin VIC Dual
	if (game == "depthch" || game == "depthcharge") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::DepthCharge);
	}
	if (game == "safari") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Safari);
	if (game == "frogs") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Frogs);
	if (game == "sspaceat" || game == "spaceattack") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::SpaceAttack);
	}
	if (game == "sspacaho" || game == "spaceattackheadon") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::SpaceAttackHeadOn);
	}
	if (game == "headon") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::HeadOn);
	if (game == "headon2") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::HeadOn2);
	if (game == "headon2s" || game == "headon2slim") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::HeadOn2Slim);
	}
	if (game == "invho2" || game == "invincoheadon2") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::InvincoHeadOn2);
	}
	if (game == "nsub" || game == "n-sub") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::NSub);
	}
	if (game == "samurai") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Samurai);
	if (game == "invinco") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Invinco);
	if (game == "invds" || game == "invincodeepscan") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::InvincoDeepScan);
	}
	if (game == "tranqgun" || game == "tranquillizergun") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::TranqGun);
	}
	if (game == "spacetrk" || game == "spacetrek") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::SpaceTrek);
	}
	if (game == "carnival") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Carnival);
	if (game == "brdrline" || game == "borderline") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Borderline);
	}
	if (game == "digger") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Digger);
	if (game == "pulsar") return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Pulsar);
	if (game == "heiankyo" || game == "heiankyoalien") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Heiankyo);
	}
	if (game == "alphaho" || game == "alphafighter") {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::AlphaFighter);
	}

	if (dsp::NeoGeo::is_game_name(game)) {
		return std::make_unique<dsp::NeoGeo>(game);
	}

	if (game == "pirates") {
		return std::make_unique<dsp::Pirates>(dsp::Pirates::Game::Pirates);
	}
	if (game == "genix") {
		return std::make_unique<dsp::Pirates>(dsp::Pirates::Game::Genix);
	}

	if (game == "armedf") { return std::make_unique<dsp::ArmedfHw>(dsp::ArmedfHw::Game::ArmedF); }
	if (game == "terraf") { return std::make_unique<dsp::ArmedfHw>(dsp::ArmedfHw::Game::TerraForce); }
	if (game == "cclimbr2") { return std::make_unique<dsp::ArmedfHw>(dsp::ArmedfHw::Game::CrazyClimber2); }
	if (game == "legion") { return std::make_unique<dsp::ArmedfHw>(dsp::ArmedfHw::Game::Legion); }

	if (game == "wwfsstar") { return std::make_unique<dsp::Wwfsstar>(); }
	if (game == "citycon") return std::make_unique<dsp::CityCon>();
    if (game == "commando") return std::make_unique<dsp::Commando>();
    if (game == "actfancer" || game == "actfancr") return std::make_unique<dsp::ActFancer>();
    if (game == "ajax" || game == "typhoon") return std::make_unique<dsp::Ajax>();
    if (game == "aliens") return std::make_unique<dsp::Aliens>();
    if (game == "simpsons") return std::make_unique<dsp::Simpsons>();
    
	if (game == "galaga") return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::Galaga);
	if (game == "digdug") return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::DigDug);
	if (game == "xevious") return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::Xevious);
	if (game == "sxevious") return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::SuperXevious);
	if (game == "bosco") return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::Bosconian);

	if (game == "atetris") { return std::make_unique<dsp::AtariTetris>(); }

	if (game == "skullxbo") return std::make_unique<dsp::Skullxbo>();
	if (game == "gng") return std::make_unique<dsp::Gng>();
	if (game == "bublbobl" || game == "bubblebobble" || game == "bublbobble") {
	    return std::make_unique<dsp::BublBobl>();
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
	if (game == "msx2" || game == "nms8250" || game == "philips-msx2") {
	    return std::make_unique<dsp::Msx2>();
	}
	if (game == "c64" || game == "commodore64" || game == "commodore") {
        return std::make_unique<dsp::C64>();
    }
    if (game == "apple2orig" || game == "apple2integer" || game == "appleii-integer") {
        return std::make_unique<dsp::Apple2>(dsp::Apple2::Model::II);
    }
    if (game == "apple2" || game == "appleii" || game == "apple2plus" || game == "apple2p" ||
        game == "apple2+") {
        return std::make_unique<dsp::Apple2>(dsp::Apple2::Model::IIPlus);
    }
    if (game == "apple2e" || game == "appleiie") {
        return std::make_unique<dsp::Apple2>(dsp::Apple2::Model::IIe);
    }
    if (game == "apple2ee" || game == "apple2eplus" || game == "apple2e+" ||
        game == "apple2enhanced" || game == "appleiiee") {
        return std::make_unique<dsp::Apple2>(dsp::Apple2::Model::IIeEnhanced);
    }
	if (game == "exl100" || game == "exl-100" || game == "exelvision") {
	    return std::make_unique<dsp::Exelv>(dsp::Exelv::Model::Exl100);
	}
	if (game == "exeltel") {
	    return std::make_unique<dsp::Exelv>(dsp::Exelv::Model::Exeltel);
	}
	if (game == "ql" || game == "sinclairql" || game == "sinclair-ql") {
	    return std::make_unique<dsp::SinclairQl>();
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
	if (game == "pv2000" || game == "pv-2000" || game == "casio-pv2000") {
	    return std::make_unique<dsp::Pv2000>();
	}
	if (game == "coleco") return std::make_unique<dsp::ColecoVision>();
	if (game == "sg1000") return std::make_unique<dsp::Sg1000>();
	if (game == "gb") return std::make_unique<dsp::GameBoy>();
	if (game == "nes") return std::make_unique<dsp::Nes>();
	if (game == "lynx") return std::make_unique<dsp::AtariLynx>();
	if (game == "a2600" || game == "atari2600" || game == "vcs" || game == "2600") {
	    return std::make_unique<dsp::A2600>();
	}
	if (game == "scv") return std::make_unique<dsp::Scv>();
	
	if (game == "pce" || game == "pcengine" || game == "tg16")
	    return std::make_unique<dsp::PcEngine>();

	if (game == "shadoww" || game == "shadow_warriors" || game == "gaiden" ||
	    game == "ninjagaiden")
	    return std::make_unique<dsp::ShadowWarriors>();
	
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    dsp::AppOptions options;
    std::string game;
    std::vector<std::string> media;
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
            media.emplace_back(next("--tape"));
        } else if (argument == "--disk") {
            media.emplace_back(next("--disk"));
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
    for (const std::string& path : media) {
        if (!machine->load_media(path, &error)) {
            std::fprintf(stderr, "cannot load media %s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
    }
    for (const DipSetting& setting : dips) machine->set_dip_switch(setting.bank, setting.value);
    for (const std::string& warning : machine->warnings()) {
        std::fprintf(stderr, "warning: %s\n", warning.c_str());
    }

    dsp::SdlApp app(options);
    return app.run(*machine);
}