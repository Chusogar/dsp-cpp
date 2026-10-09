#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <typeinfo>
#include <set>
#include <map>
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
#include "drivers/arcade/punchout.h"
#include "drivers/arcade/starwars.h"
#include "drivers/arcade/model3.h"
#include "drivers/arcade/asteroid.h"
#include "drivers/arcade/polepos.h"
#include "drivers/arcade/hangon.h"
#include "drivers/arcade/system16.h"
#include "drivers/arcade/system18.h"
#include "drivers/arcade/sega_system1.h"
#include "drivers/arcade/outrun.h"
#include "drivers/arcade/xboard.h"
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
#include "drivers/arcade/shuuz.h"
#include "drivers/arcade/gng.h"
#include "drivers/arcade/bublbobl.h"
#include "drivers/arcade/ambush.h"
#include "drivers/arcade/arabian.h"
#include "drivers/arcade/bionicc.h"
#include "drivers/arcade/blktiger.h"
#include "drivers/arcade/blockout.h"
#include "drivers/arcade/shaolinsroad.h"
#include "drivers/arcade/tehkanwc.h"
#include "drivers/arcade/appoooh.h"
#include "drivers/arcade/arkanoid.h"
#include "drivers/arcade/renegade.h"
#include "drivers/arcade/retofinv.h"
#include "drivers/arcade/baraduke_hw.h"
#include "drivers/arcade/bankpanic_hw.h"
#include "drivers/arcade/balsente.h"
#include "drivers/arcade/slapfight.h"
#include "drivers/arcade/williams.h"

// Computers
#include "drivers/computers/spectrum.h"
#include "drivers/computers/spectrum_128k.h"
#include "drivers/computers/spectrum_3.h"
#include "drivers/computers/specnext.h"
#include "drivers/computers/zx81.h"
#include "drivers/computers/amstrad_cpc.h"
#include "drivers/computers/pcw.h"
#include "drivers/computers/msx1.h"
#include "drivers/computers/msx2.h"
#include "drivers/computers/c64.h"
#include "drivers/computers/vic20.h"
#include "drivers/computers/c128.h"
#include "drivers/computers/plus4.h"
#include "drivers/computers/apple2.h"
#include "drivers/computers/apple2gs.h"
#include "drivers/computers/exelv.h"
#include "drivers/computers/pentagon.h"
#include "drivers/computers/scorpion.h"
#include "drivers/computers/ql.h"
#include "drivers/computers/atari_st.h"
#include "drivers/computers/amiga.h"
#include "drivers/computers/macplus.h"
#include "drivers/computers/macii.h"
#include "drivers/computers/samcoupe.h"
#include "drivers/computers/atari8.h"

// Consoles
#include "drivers/consoles/sms.h"
#include "drivers/consoles/gamegear.h"
#include "drivers/consoles/genesis.h"
#include "drivers/consoles/sega32x.h"
#include "drivers/consoles/pv1000.h"
#include "drivers/consoles/pv2000.h"
#include "drivers/consoles/colecovision.h"
#include "drivers/consoles/sg1000.h"
#include "drivers/consoles/gameboy.h"
#include "drivers/consoles/gba.h"
#include "drivers/consoles/nes.h"
#include "drivers/consoles/atari_lynx.h"
#include "drivers/consoles/wonderswan.h"
#include "drivers/consoles/a2600.h"
#include "drivers/consoles/scv.h"
#include "drivers/consoles/pcengine.h"
#include "drivers/consoles/a7800.h"
#include "drivers/consoles/vectrex.h"
#include "drivers/consoles/snes.h"
#include "drivers/consoles/psx.h"


#include "frontend/sdl_app.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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
        "    elevator, junglek, indydoom, peter, marble, skullxbo, shuuz, starwars, esb, swtrilgy (Sega Model 3), punchout, asteroid, roadrunn,\n"
        "    paperboy, ssprint, apb, 720,\n"
        "    tapper, tron, shollow, domino, wacko, dotron, timber,\n"
		"    robocop, baddudes, hippodrm, slyspy, bouldash,\n"
        "    kungfum, spelunkr, spelunk2, ldrun, ldrun2,\n"
        "    ikari, athena, tnk3, aso,\n"
        "    ghouls, ffight, kod, sf2, strider, 3wonders, captcomm,\n"
        "    knights, sf2ce, dino, punisher, willow, 1941, nemo,\n"
        "    rtype, hharry, rtype2,\n"
        "    polepos, polepos2\n"
        "    outrun, aburner2, hangon, enduro, sharrier, fantzone, shinobi,\n"
		"    alexkidd, aliensyn, wb3, tetris, altbeast, goldnaxe, ddux, eswat,\n"
		"    passsht, aurail, riotcity, sdi, sdib, cotton, bayroute, sonicbom,\n"
		"    timescan, mwalk, astorm, bloxeed, cltchitr, ddcrew, desertbr,\n"
		"    hamaway, lghost, pontoon, shdancer, wwallyj,\n"
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
		"    ambush, arabian, bionicc, blktiger, blockout, shaolins, tehkanwc, appoooh, robowres, arkanoid, renegade\n"
		"    retofinv, slapfight, tigerheli,\n"
		"    baraduke, metrocrs, bankp, combh\n"
		"    defender, mayday, colony7, joust, robotron, stargate\n"
		"    sentetst, cshift, hattrick, gghost, otwalls, snakepit, triviag1,\n"
		"    snakjack, stocker, triviabb, triviag2, triviayp, triviasp,\n"
		"    gimeabrk, minigolf, teamht, grudge, triviaes, toggle, nstocker,\n"
		"    sfootbal, spiker, stompin, nametune, rescraid\n"
		"\n"
        "  Computers:\n"
        "    spectrum48, spectrum128, plus3, pentagon, scorpion, specnext (tbblue),\n"
        "    zx80, zx81, ts1000,\n"
        "    cpc464, cpc664, cpc6128, pcw8256, pcw, pcw8512, msx, msx2, msx2-jp, msx2-eu, nms8250, c64, vic20, vic20p,\n"
        "    c128, commodore128, plus4, c16,\n"
        "    apple2, apple2gs, apple2plus, apple2e, apple2ee, exl100, exeltel, ql,\n"
        "    st, atarist, atari-st, amiga, a500, amiga500,\n"
        "    macplus, mac, macintosh, plus, macii, samcoupe\n"
		"    a800, a800xl, a800xe\n"
        "\n"
        "  Consoles:\n"
        "    sms, gamegear, genesis, megadrive, genesis-pal, genesis-jp, 32x, 32x-pal, 32x-jp,\n"
        "    pv1000, pv2000, coleco, sg1000, gb, gba, nes, lynx, wswan, wscolor, wsc,\n"
        "    scv, pcengine, sgx, a2600, atari2600, vcs, a7800, vectrex, snes,\n"
        "    psx, playstation, ps1\n"
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
        "  --listarcades      list the arcade drivers (name, title, other names)\n"
        "  --listconsoles     list the console drivers\n"
        "  --listcomputers    list the computer drivers\n"
        "  --tape FILE        tape/cart/snap: Spectrum/CPC/C64/MSX (.tap/.tzx/.cdt/.prg/.t64/.cas),\n"
        "                     Spectrum family snapshots (.sna) and RZX playback (.rzx),\n"
        "                     EXL-100 / EXELTEL cartridge (.bin/.rom) or cassette (.k7/.wav),\n"
        "                     PV-2000 cart (.bin/.rom),\n"
        "                     or QL microdrive .mdv/.qlpak or QXL.WIN\n"
        "  --cart FILE        cartridge image (Game Boy Advance .gba, plain or zipped)\n"
        "  --disk FILE        floppy: CPC/Spectrum +3/PCW .dsk/.edsk, MSX2 .dsk, Apple II .dsk/.do/.po/.nib,\n"
        "                     Pentagon/Scorpion .trd/.scl/.sna/.rzx, QL microdrive .mdv/.qlpak or QXL.WIN,\n"
        "                     Atari ST .st/.msa/.stx, Amiga .adf, Macintosh 400K/800K/1.44MB SuperDrive .dsk/.img/.dc42,\n"
        "                     or a Macintosh SCSI hard disk .img/.dsk (DDM+APM like MAME,\n"
        "                     or a raw 512-byte HFS volume served as-is)\n"
        "                     (repeat --disk/--tape to fill QL mdv1 then mdv2, or\n"
        "                     Atari ST drive A then B, or Amiga DF0 then DF1)\n"
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

// Matches the --game names in create_machine(). In listing mode every name is
// recorded and none matches, so the same code also enumerates all drivers.
class DriverQuery {
public:
    explicit DriverQuery(const std::string& game) : game_(&game) {}
    explicit DriverQuery(std::vector<std::string>* names) : names_(names) {}

    bool is(const std::string& name) {
        if (names_ != nullptr) {
            names_->push_back(name);
            return false;
        }
        return *game_ == name;
    }
    bool listing() const { return names_ != nullptr; }
    const std::string& game() const { return *game_; }

private:
    const std::string* game_ = nullptr;
    std::vector<std::string>* names_ = nullptr;
};

std::unique_ptr<dsp::Machine> create_machine(DriverQuery& q) {

	// arcade
    if (q.is("bagman")) return std::make_unique<dsp::Bagman>();
    if (q.is("mikie")) return std::make_unique<dsp::Mikie>();
    if (q.is("trackfld") || q.is("trackfield") || q.is("trackandfield")) {
        return std::make_unique<dsp::TrackFld>();
    }
    if (q.is("gauntlet")) return std::make_unique<dsp::Gauntlet>();
	if (q.is("mrdo")) return std::make_unique<dsp::MrDo>();
    
    if (q.is("ddragon")) {
        return std::make_unique<dsp::DoubleDragon>(dsp::DoubleDragon::Variant::DDragon);
    }
    if (q.is("ddragon2")) {
        return std::make_unique<dsp::DoubleDragon>(dsp::DoubleDragon::Variant::DDragon2);
    }
    if (q.is("elevator") || q.is("elevatob") || q.is("elevaction")) {
        return std::make_unique<dsp::TaitoSJ>(dsp::TaitoSJ::Variant::ElevatorAction);
    }
    if (q.is("junglek") || q.is("jungleking")) {
        return std::make_unique<dsp::TaitoSJ>(dsp::TaitoSJ::Variant::JungleKing);
    }
	if (q.is("indydoom")) return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::Indy);
	if (q.is("peter")) return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::PeterPak);	
	if (q.is("marble")) return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::Marble);
	if (q.is("punchout") || q.is("punch-out")) return std::make_unique<dsp::PunchOut>();
	if (q.is("swtrilgy") || q.is("model3")) return std::make_unique<dsp::Model3>();
	if (q.is("starwars") || q.is("star-wars")) {
		return std::make_unique<dsp::StarWars>(dsp::StarWars::Game::StarWars);
	}
	if (q.is("esb")) {
		return std::make_unique<dsp::StarWars>(dsp::StarWars::Game::Esb);
	}
	if (q.is("asteroid") || q.is("asteroids")) {
		return std::make_unique<dsp::Asteroid>();
	}
	if (q.is("roadrunn") || q.is("roadrunner")) {
		return std::make_unique<dsp::AtariSystem1>(dsp::AtariSystem1::Game::RoadRunner);
	}
	if (q.is("paperboy")) {
		return std::make_unique<dsp::AtariSystem2>(dsp::AtariSystem2::Game::Paperboy);
	}
	if (q.is("ssprint")) {
		return std::make_unique<dsp::AtariSystem2>(dsp::AtariSystem2::Game::SuperSprint);
	}
	if (q.is("apb")) {
		return std::make_unique<dsp::AtariSystem2>(dsp::AtariSystem2::Game::Apb);
	}
	if (q.is("720") || q.is("720degrees")) {
		return std::make_unique<dsp::AtariSystem2>(dsp::AtariSystem2::Game::Degrees720);
	}

	if (q.is("tapper")) return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Tapper);
	if (q.is("tron")) return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Tron);
    if (q.is("shollow")) return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Shollow);
	if (q.is("domino")) return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Domino);
	if (q.is("wacko")) return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Wacko);
	if (q.is("dotron")) return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Dotron);
	if (q.is("timber")) return std::make_unique<dsp::Mcr>(dsp::Mcr::Game::Timber);

	if (q.is("robocop")) return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::Robocop);
    if (q.is("baddudes") || q.is("drgninja")) {
        return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::BadDudes);
    }
    if (q.is("hippodrm") || q.is("hippodrome")) {
        return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::Hippodrome);
    }
    if (q.is("slyspy") || q.is("secretag")) {
        return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::SlySpy);
    }
    if (q.is("bouldash")) return std::make_unique<dsp::Dec0>(dsp::Dec0::Variant::BoulderDash);

    if (q.is("kungfum") || q.is("kungfu")) {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::KungFuMaster);
    }
    if (q.is("spelunkr") || q.is("spelunker")) {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::Spelunker);
    }
    if (q.is("spelunk2") || q.is("spelunker2")) {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::Spelunker2);
    }
    if (q.is("ldrun") || q.is("loderunner")) {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::LodeRunner);
    }
    if (q.is("ldrun2") || q.is("loderunner2")) {
        return std::make_unique<dsp::IremM62>(dsp::IremM62::Game::LodeRunner2);
    }
    if (q.is("ikari")) return std::make_unique<dsp::Snk>(dsp::Snk::Game::Ikari);
    if (q.is("athena")) return std::make_unique<dsp::Snk>(dsp::Snk::Game::Athena);
    if (q.is("tnk3")) return std::make_unique<dsp::Snk>(dsp::Snk::Game::Tnk3);
    if (q.is("aso")) return std::make_unique<dsp::Snk>(dsp::Snk::Game::Aso);
    if (q.is("ghouls")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Ghouls);
    if (q.is("ffight") || q.is("finalfight")) {
        return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Ffight);
    }
    if (q.is("kod")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Kod);
    if (q.is("sf2")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Sf2);
    if (q.is("strider")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Strider);
    if (q.is("3wonders") || q.is("wonder3")) {
        return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Wonder3);
    }
    if (q.is("captcomm")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Captcomm);
    if (q.is("knights")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Knights);
    if (q.is("sf2ce")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Sf2ce);
    if (q.is("dino")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Dino);
    if (q.is("punisher")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Punisher);
    if (q.is("willow")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Willow);
    if (q.is("1941")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Ca1941);
    if (q.is("nemo")) return std::make_unique<dsp::Cps1>(dsp::Cps1::Game::Nemo);
    if (q.is("rtype")) return std::make_unique<dsp::M72>(dsp::M72::Game::Rtype);
    if (q.is("hharry")) return std::make_unique<dsp::M72>(dsp::M72::Game::Hharry);
    if (q.is("rtype2")) return std::make_unique<dsp::M72>(dsp::M72::Game::Rtype2);
	if (q.is("polepos") || q.is("poleposition")) {
		return std::make_unique<dsp::PolePos>(dsp::PolePos::Game::PolePosition);
	}
	if (q.is("polepos2") || q.is("poleposition2")) {
		return std::make_unique<dsp::PolePos>(dsp::PolePos::Game::PolePosition2);
	}
    
	if (q.is("outrun")) return std::make_unique<dsp::Outrun>();
	if (q.is("aburner2")) return std::make_unique<dsp::XBoard>();

    if (q.is("hangon") || q.is("hang-on")) return std::make_unique<dsp::HangOn>();
    if (q.is("enduro") || q.is("enduror") || q.is("enduro-racer")) {
        return std::make_unique<dsp::HangOn>(dsp::HangOn::Game::Enduro);
    }
    if (q.is("sharrier") || q.is("spaceharrier") || q.is("space-harrier")) {
        return std::make_unique<dsp::HangOn>(dsp::HangOn::Game::Sharrier);
    }
    if (q.is("fantzone") || q.is("fantasyzone")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Fantzone);
    }
    if (q.is("shinobi")) return std::make_unique<dsp::System16>(dsp::System16::Game::Shinobi);
    if (q.is("alexkidd") || q.is("alexkid")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Alexkidd);
    }
    if (q.is("aliensyn") || q.is("aliensynd") || q.is("aliensyndrome")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Aliensyn);
    }
    if (q.is("wb3") || q.is("wonderboy3") || q.is("wonderboyiii")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Wb3);
    }
    if (q.is("tetris")) return std::make_unique<dsp::System16>(dsp::System16::Game::Tetris);
    if (q.is("altbeast") || q.is("alteredbeast")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Altbeast);
    }
    if (q.is("goldnaxe") || q.is("goldenaxe")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Goldnaxe);
    }
    if (q.is("ddux") || q.is("dynamitedux")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Ddux);
    }
    if (q.is("eswat") || q.is("e-swat")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Eswat);
    }
    if (q.is("passsht") || q.is("passingshot")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Passsht);
    }
    if (q.is("aurail")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Aurail);
    }
    if (q.is("riotcity") || q.is("riot")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Riotcity);
    }
    if (q.is("sdi") || q.is("sdib")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Sdi);
    }
    if (q.is("cotton")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Cotton);
    }
    if (q.is("bayroute")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Bayroute);
    }
    if (q.is("sonicbom") || q.is("sonicboom")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Sonicbom);
    }
    if (q.is("timescan") || q.is("timescanner")) {
        return std::make_unique<dsp::System16>(dsp::System16::Game::Timescan);
    }
    if (q.is("mwalk") || q.is("moonwalker") || q.is("moonwalk")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Mwalk);
    }
    if (q.is("astorm") || q.is("alienstorm")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Astorm);
    }
    if (q.is("bloxeed")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Bloxeed);
    }
    if (q.is("cltchitr") || q.is("clutchhitter")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Cltchitr);
    }
    if (q.is("ddcrew")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Ddcrew);
    }
    if (q.is("desertbr") || q.is("desertbreaker")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Desertbr);
    }
    if (q.is("hamaway") || q.is("hammeraway")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Hamaway);
    }
    if (q.is("lghost") || q.is("laserghost")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Lghost);
    }
    if (q.is("pontoon")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Pontoon);
    }
    if (q.is("shdancer") || q.is("shadowdancer")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Shdancer);
    }
    if (q.is("wwallyj") || q.is("wwally") || q.is("wally")) {
        return std::make_unique<dsp::System18>(dsp::System18::Game::Wwallyj);
    }

	// Sega System 1
	if (q.is("pitfall2") || q.is("pitfallii") || q.is("pitfall")) {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::Pitfall2);
	}
	if (q.is("teddyboy") || q.is("teddy") || q.is("tdboy")) {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::TeddyBoy);
	}
	if (q.is("wboy") || q.is("wonderboy")) {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::WonderBoy);
	}
	if (q.is("mrviking") || q.is("viking")) {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::MrViking);
	}
	if (q.is("seganinj") || q.is("seganinja") || q.is("ninja")) {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::SegaNinja);
	}
	if (q.is("upndown") || q.is("up-n-down") || q.is("upanddown")) {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::UpNDown);
	}
	if (q.is("flicky")) {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::Flicky);
	}
	if (q.is("gardia")) {
		return std::make_unique<dsp::SegaSystem1>(dsp::SegaSystem1::Game::Gardia);
	}

	if (q.is("galaxian")) return std::make_unique<dsp::Galaxian>(dsp::Galaxian::Game::Galaxian);
	if (q.is("mooncrst") || q.is("mooncresta")) return std::make_unique<dsp::Galaxian>(dsp::Galaxian::Game::MoonCresta);
	if (q.is("scramble")) return std::make_unique<dsp::Galaxian>(dsp::Galaxian::Game::Scramble);
	if (q.is("frogger")) return std::make_unique<dsp::Galaxian>(dsp::Galaxian::Game::Frogger);

	if (q.is("opwolf") || q.is("operationwolf") || q.is("operation-wolf")) {
		return std::make_unique<dsp::OpWolf>();
	}

	// Sega / Gremlin VIC Dual
	if (q.is("depthch") || q.is("depthcharge")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::DepthCharge);
	}
	if (q.is("safari")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Safari);
	if (q.is("frogs")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Frogs);
	if (q.is("sspaceat") || q.is("spaceattack")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::SpaceAttack);
	}
	if (q.is("sspacaho") || q.is("spaceattackheadon")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::SpaceAttackHeadOn);
	}
	if (q.is("headon")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::HeadOn);
	if (q.is("headon2")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::HeadOn2);
	if (q.is("headon2s") || q.is("headon2sl") || q.is("headon2slim")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::HeadOn2Slim);
	}
	if (q.is("invho2") || q.is("invincoheadon2")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::InvincoHeadOn2);
	}
	if (q.is("nsub") || q.is("n-sub")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::NSub);
	}
	if (q.is("samurai")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Samurai);
	if (q.is("invinco")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Invinco);
	if (q.is("invds") || q.is("invincodeepscan")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::InvincoDeepScan);
	}
	if (q.is("tranqgun") || q.is("tranquillizergun")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::TranqGun);
	}
	if (q.is("spacetrk") || q.is("spacetrek")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::SpaceTrek);
	}
	if (q.is("carnival")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Carnival);
	if (q.is("brdrline") || q.is("borderline")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Borderline);
	}
	if (q.is("digger")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Digger);
	if (q.is("pulsar")) return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Pulsar);
	if (q.is("heiankyo") || q.is("heiankyoalien")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::Heiankyo);
	}
	if (q.is("alphaho") || q.is("alphafighter")) {
		return std::make_unique<dsp::VicDual>(dsp::VicDual::Game::AlphaFighter);
	}

	if (q.listing()) {
		for (const std::string& name : dsp::NeoGeo::game_names()) q.is(name);
	} else if (dsp::NeoGeo::is_game_name(q.game())) {
		return std::make_unique<dsp::NeoGeo>(q.game());
	}

	if (q.is("pirates")) {
		return std::make_unique<dsp::Pirates>(dsp::Pirates::Game::Pirates);
	}
	if (q.is("genix")) {
		return std::make_unique<dsp::Pirates>(dsp::Pirates::Game::Genix);
	}

	if (q.is("shadoww") || q.is("shadow_warriors") || q.is("gaiden") ||
	    q.is("ninjagaiden"))
	    return std::make_unique<dsp::ShadowWarriors>();

	if (q.is("armedf")) { return std::make_unique<dsp::ArmedfHw>(dsp::ArmedfHw::Game::ArmedF); }
	if (q.is("terraf")) { return std::make_unique<dsp::ArmedfHw>(dsp::ArmedfHw::Game::TerraForce); }
	if (q.is("cclimbr2")) { return std::make_unique<dsp::ArmedfHw>(dsp::ArmedfHw::Game::CrazyClimber2); }
	if (q.is("legion")) { return std::make_unique<dsp::ArmedfHw>(dsp::ArmedfHw::Game::Legion); }

	if (q.is("wwfsstar")) { return std::make_unique<dsp::Wwfsstar>(); }
	if (q.is("citycon")) return std::make_unique<dsp::CityCon>();
    if (q.is("commando")) return std::make_unique<dsp::Commando>();
    if (q.is("actfancer") || q.is("actfancr")) return std::make_unique<dsp::ActFancer>();
    if (q.is("ajax") || q.is("typhoon")) return std::make_unique<dsp::Ajax>();
    if (q.is("aliens")) return std::make_unique<dsp::Aliens>();
    if (q.is("simpsons")) return std::make_unique<dsp::Simpsons>();
    
	if (q.is("galaga")) return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::Galaga);
	if (q.is("digdug")) return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::DigDug);
	if (q.is("xevious")) return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::Xevious);
	if (q.is("sxevious")) return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::SuperXevious);
	if (q.is("bosco")) return std::make_unique<dsp::GalagaHw>(dsp::GalagaHw::Game::Bosconian);

	if (q.is("atetris")) { return std::make_unique<dsp::AtariTetris>(); }

	if (q.is("skullxbo")) return std::make_unique<dsp::Skullxbo>();
	if (q.is("shuuz") || q.is("shuzz")) return std::make_unique<dsp::Shuuz>();
	if (q.is("gng")) return std::make_unique<dsp::Gng>();
	if (q.is("bublbobl") || q.is("bubblebobble") || q.is("bublbobble")) {
	    return std::make_unique<dsp::BublBobl>();
	}
	if (q.is("ambush")) return std::make_unique<dsp::Ambush>();
	if (q.is("arabian")) return std::make_unique<dsp::Arabian>();
	if (q.is("bionicc") || q.is("bioniccommando")) return std::make_unique<dsp::BionicCommando>();
	if (q.is("blktiger")) return std::make_unique<dsp::BlackTiger>();
	if (q.is("blockout")) return std::make_unique<dsp::BlockOut>();
	if (q.is("shaolin") || q.is("shaolins")) return std::make_unique<dsp::ShaolinsRoad>();
	if (q.is("tehkanwc")) return std::make_unique<dsp::TehkanWc>();
	if (q.is("appoooh")) return std::make_unique<dsp::Appoooh>(dsp::Appoooh::Variant::Appoooh);
	if (q.is("robowres")) return std::make_unique<dsp::Appoooh>(dsp::Appoooh::Variant::RoboWres);
	if (q.is("arkanoid")) return std::make_unique<dsp::Arkanoid>();
	if (q.is("renegade")) return std::make_unique<dsp::Renegade>();
	if (q.is("retofinv")) return std::make_unique<dsp::Retofinv>();
	if (q.is("slapfight")) return std::make_unique<dsp::SlapFight>(dsp::SlapFight::Variant::SlapFight);
	if (q.is("tigerheli")) return std::make_unique<dsp::SlapFight>(dsp::SlapFight::Variant::TigerHeli);
	if (q.is("baraduke") || q.is("aliensec")) {
	    return std::make_unique<dsp::BaradukeHw>(dsp::BaradukeHw::Game::Baraduke);
	}
	if (q.is("metrocrs") || q.is("metrocross")) {
	    return std::make_unique<dsp::BaradukeHw>(dsp::BaradukeHw::Game::MetroCross);
	}
	if (q.is("bankp") || q.is("bankpanic")) {
	    return std::make_unique<dsp::BankPanicHw>(dsp::BankPanicHw::Game::BankPanic);
	}
	if (q.is("combh") || q.is("combathawk")) {
	    return std::make_unique<dsp::BankPanicHw>(dsp::BankPanicHw::Game::CombatHawk);
	}

	if (q.is("defender")) return std::make_unique<dsp::Williams>(dsp::Williams::Game::Defender);
	if (q.is("mayday")) return std::make_unique<dsp::Williams>(dsp::Williams::Game::Mayday);
	if (q.is("colony7")) return std::make_unique<dsp::Williams>(dsp::Williams::Game::Colony7);
	if (q.is("joust")) return std::make_unique<dsp::Williams>(dsp::Williams::Game::Joust);
	if (q.is("robotron")) return std::make_unique<dsp::Williams>(dsp::Williams::Game::Robotron);
	if (q.is("stargate")) return std::make_unique<dsp::Williams>(dsp::Williams::Game::Stargate);

	if (q.is("sentetst")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Sentetst);
	if (q.is("cshift")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Cshift);
	if (q.is("hattrick")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Hattrick);
	if (q.is("gghost")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Gghost);
	if (q.is("otwalls")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Otwalls);
	if (q.is("snakepit")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Snakepit);
	if (q.is("triviag1")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Triviag1);
	if (q.is("snakjack")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Snakjack);
	if (q.is("stocker")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Stocker);
	if (q.is("triviabb")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Triviabb);
	if (q.is("triviag2")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Triviag2);
	if (q.is("triviayp")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Triviayp);
	if (q.is("triviasp")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Triviasp);
	if (q.is("gimeabrk")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Gimeabrk);
	if (q.is("minigolf")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Minigolf);
	if (q.is("teamht")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Teamht);
	if (q.is("grudge")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Grudge);
	if (q.is("triviaes")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Triviaes);
	if (q.is("toggle")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Toggle);
	if (q.is("nstocker")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Nstocker);
	if (q.is("sfootbal")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Sfootbal);
	if (q.is("spiker")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Spiker);
	if (q.is("stompin")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Stompin);
	if (q.is("nametune")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Nametune);
	if (q.is("rescraid")) return std::make_unique<dsp::Balsente>(dsp::Balsente::Game::Rescraid);

	// computers
    if (q.is("spectrum48") || q.is("spectrum")) return std::make_unique<dsp::Spectrum48k>();
    if (q.is("zx80")) return std::make_unique<dsp::Zx81>(dsp::Zx81::Model::Zx80);
    if (q.is("zx81") || q.is("ts1000")) return std::make_unique<dsp::Zx81>();
    if (q.is("cpc464")) return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC464);
    if (q.is("cpc664")) return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC664);
    if (q.is("cpc6128") || q.is("cpc")) {
        return std::make_unique<dsp::AmstradCpc>(dsp::AmstradCpc::Model::CPC6128);
    }
    if (q.is("pcw8256") || q.is("pcw")) {
        return std::make_unique<dsp::Pcw>(dsp::Pcw::Model::PCW8256);
    }
    if (q.is("pcw8512")) {
        return std::make_unique<dsp::Pcw>(dsp::Pcw::Model::PCW8512);
    }
	if (q.is("spectrum128")) return std::make_unique<dsp::Spectrum128k>(dsp::Spectrum128k::Model::Spec128k);
	if (q.is("plus3")) return std::make_unique<dsp::Spectrum3>();
	if (q.is("specnext") || q.is("tbblue") || q.is("next") || q.is("zxnext")) {
	    return std::make_unique<dsp::SpecNext>();
	}
	if (q.is("pentagon") || q.is("pentagon1024") || q.is("pent1024")) {
	    return std::make_unique<dsp::Pentagon1024>();
	}
	if (q.is("scorpion") || q.is("scorpion256") || q.is("scorpio") || q.is("zs256")) {
	    return std::make_unique<dsp::Scorpion256>();
	}
	if (q.is("msx")) return std::make_unique<dsp::Msx1>();
	if (q.is("msx2") || q.is("nms8250") || q.is("philips-msx2")) {
	    return std::make_unique<dsp::Msx2>();
	}
	if (q.is("msx2-jp") || q.is("msx2jp")) {
	    return std::make_unique<dsp::Msx2>(dsp::Msx2::Region::Japan);
	}
	if (q.is("msx2-eu") || q.is("msx2eu")) {
	    return std::make_unique<dsp::Msx2>(dsp::Msx2::Region::Europe);
	}
	if (q.is("c64") || q.is("commodore64") || q.is("commodore")) {
        return std::make_unique<dsp::C64>();
    }
    if (q.is("vic20p") || q.is("vic-20p") || q.is("vic20-pal")) {
        return std::make_unique<dsp::Vic20>(dsp::Vic20::Region::Pal);
    }
    if (q.is("vic20n") || q.is("vic20-ntsc") || q.is("vic-20n")) {
        return std::make_unique<dsp::Vic20>(dsp::Vic20::Region::Ntsc);
    }
    if (q.is("vic20") || q.is("vic-20")) {
        return std::make_unique<dsp::Vic20>(dsp::Vic20::Region::Pal);
    }
	if (q.is("c128") || q.is("commodore128")) {
        return std::make_unique<dsp::C128>();
    }
    if (q.is("plus4") || q.is("plus-4") || q.is("c264")) {
        return std::make_unique<dsp::Plus4>(dsp::Plus4::Model::Plus4_64K,
                                            dsp::Plus4::Region::Pal);
    }
    if (q.is("plus4n") || q.is("plus4-ntsc")) {
        return std::make_unique<dsp::Plus4>(dsp::Plus4::Model::Plus4_64K,
                                            dsp::Plus4::Region::Ntsc);
    }
    if (q.is("c16") || q.is("commodore16")) {
        return std::make_unique<dsp::Plus4>(dsp::Plus4::Model::C16_16K,
                                            dsp::Plus4::Region::Pal);
    }
    if (q.is("c16n") || q.is("c16-ntsc")) {
        return std::make_unique<dsp::Plus4>(dsp::Plus4::Model::C16_16K,
                                            dsp::Plus4::Region::Ntsc);
    }
    if (q.is("apple2orig") || q.is("apple2integer") || q.is("appleii-integer")) {
        return std::make_unique<dsp::Apple2>(dsp::Apple2::Model::II);
    }
    if (q.is("apple2") || q.is("appleii") || q.is("apple2plus") || q.is("apple2p") ||
        q.is("apple2+")) {
        return std::make_unique<dsp::Apple2>(dsp::Apple2::Model::IIPlus);
    }
    if (q.is("apple2e") || q.is("appleiie")) {
        return std::make_unique<dsp::Apple2>(dsp::Apple2::Model::IIe);
    }
    if (q.is("apple2ee") || q.is("apple2eplus") || q.is("apple2e+") ||
        q.is("apple2enhanced") || q.is("appleiiee")) {
        return std::make_unique<dsp::Apple2>(dsp::Apple2::Model::IIeEnhanced);
    }
	if (q.is("apple2gs")) { return std::make_unique<dsp::Apple2GS>(); }
	if (q.is("exl100") || q.is("exl-100") || q.is("exelvision")) {
	    return std::make_unique<dsp::Exelv>(dsp::Exelv::Model::Exl100);
	}
	if (q.is("exeltel")) {
	    return std::make_unique<dsp::Exelv>(dsp::Exelv::Model::Exeltel);
	}
	if (q.is("ql") || q.is("sinclairql") || q.is("sinclair-ql")) {
	    return std::make_unique<dsp::SinclairQl>();
	}
	if (q.is("st") || q.is("atarist") || q.is("atari-st") || q.is("1040st") ||
	    q.is("520st")) {
	    return std::make_unique<dsp::AtariSt>();
	}
	if (q.is("amiga") || q.is("a500") || q.is("amiga500") || q.is("amiga-500")) {
	    return std::make_unique<dsp::Amiga500>();
	}
	if (q.is("macplus") || q.is("mac") || q.is("macintosh") || q.is("plus") ||
	    q.is("mac-plus")) {
	    return std::make_unique<dsp::MacPlus>();
	}
	if (q.is("macii")) { return std::make_unique<dsp::MacII>(); }
	if (q.is("samcoupe")) { return std::make_unique<dsp::SamCoupe>(); }
	if (q.is("a800")) { return std::make_unique<dsp::Atari8>(dsp::Atari8::Model::A800); }
	if (q.is("a800xl")) { return std::make_unique<dsp::Atari8>(dsp::Atari8::Model::A800XL); }
	if (q.is("a800xe")) { return std::make_unique<dsp::Atari8>(dsp::Atari8::Model::A800XE); }
		
	// consoles
	if (q.is("sms")) return std::make_unique<dsp::Sms>();
	if (q.is("gamegear")) return std::make_unique<dsp::GameGear>();
	if (q.is("genesis") || q.is("megadrive") || q.is("mega-drive") ||
	    q.is("md") || q.is("gen")) {
	    return std::make_unique<dsp::Genesis>();
	}
	if (q.is("genesis-pal") || q.is("megadrive-pal")) {
	    return std::make_unique<dsp::Genesis>(dsp::Genesis::Region::Europe);
	}
	if (q.is("genesis-jp") || q.is("megadrive-jp")) {
	    return std::make_unique<dsp::Genesis>(dsp::Genesis::Region::Japan);
	}
	if (q.is("32x") || q.is("sega32x") || q.is("mars")) return std::make_unique<dsp::Sega32X>();
	if (q.is("32x-pal") || q.is("sega32x-pal")) {
	    return std::make_unique<dsp::Sega32X>(dsp::Genesis::Region::Europe);
	}
	if (q.is("32x-jp") || q.is("sega32x-jp")) {
	    return std::make_unique<dsp::Sega32X>(dsp::Genesis::Region::Japan);
	}
	if (q.is("pv1000")) return std::make_unique<dsp::Pv1000>();
	if (q.is("pv2000") || q.is("pv-2000") || q.is("casio-pv2000")) {
	    return std::make_unique<dsp::Pv2000>();
	}
	if (q.is("coleco")) return std::make_unique<dsp::ColecoVision>();
	if (q.is("sg1000")) return std::make_unique<dsp::Sg1000>();
	if (q.is("gb")) return std::make_unique<dsp::GameBoy>();
    if (q.is("gba") || q.is("agb") || q.is("gameboyadvance")) return std::make_unique<dsp::Gba>();
	if (q.is("nes")) return std::make_unique<dsp::Nes>();
	if (q.is("lynx")) return std::make_unique<dsp::AtariLynx>();
	if (q.is("wswan") || q.is("wonderswan") || q.is("ws")) {
	    return std::make_unique<dsp::WonderSwan>(dsp::WonderSwan::Model::WonderSwan);
	}
	if (q.is("wscolor") || q.is("wsc") || q.is("wonderswancolor") ||
	    q.is("swancrystal") || q.is("wonderswan-color")) {
	    return std::make_unique<dsp::WonderSwan>(dsp::WonderSwan::Model::WonderSwanColor);
	}
	if (q.is("a2600") || q.is("atari2600") || q.is("vcs") || q.is("2600")) {
	    return std::make_unique<dsp::A2600>();
	}
	if (q.is("scv")) return std::make_unique<dsp::Scv>();
	if (q.is("vectrex")) { return std::make_unique<dsp::Vectrex>(); }
	if (q.is("a7800")) { return std::make_unique<dsp::A7800>(); }
    
	
	if (q.is("pce") || q.is("pcengine") || q.is("tg16"))
	    return std::make_unique<dsp::PcEngine>();

	if (q.is("snes")) { return std::make_unique<dsp::Snes>(); }
	if (q.is("psx") || q.is("playstation") || q.is("ps1")) {
	    return std::make_unique<dsp::Psx>();
	}

    return nullptr;
}

std::unique_ptr<dsp::Machine> create_machine(const std::string& game) {
    DriverQuery q(game);
    return create_machine(q);
}

// One driver as listed by --listarcades / --listconsoles / --listcomputers
// and the web launcher: its --game name, other names for the same machine,
// title and type.
struct DriverInfo {
    std::string name;
    std::vector<std::string> aliases;
    std::string title;
    dsp::MachineType type;
};

// Every driver, in the order of create_machine(). Each name is instantiated
// (not initialised: no ROMs are read) to ask its type and title; names that
// build the same class with the same title are folded into aliases.
const std::vector<DriverInfo>& list_drivers() {
    static std::vector<DriverInfo> drivers;
    if (!drivers.empty()) return drivers;
    std::vector<std::string> names;
    DriverQuery q(&names);
    create_machine(q);
    std::map<std::string, size_t> by_key;
    std::set<std::string> seen;
    for (const std::string& name : names) {
        if (!seen.insert(name).second) continue;
        std::unique_ptr<dsp::Machine> machine = create_machine(name);
        if (machine == nullptr) continue;
        const dsp::Machine& m = *machine;
        const std::string key = std::string(typeid(m).name()) + "|" + m.title();
        auto it = by_key.find(key);
        if (it != by_key.end()) {
            drivers[it->second].aliases.push_back(name);
            continue;
        }
        by_key[key] = drivers.size();
        drivers.push_back(DriverInfo{name, {}, m.title(), m.machine_type()});
    }
    return drivers;
}

void print_drivers(dsp::MachineType type) {
    std::vector<const DriverInfo*> list;
    for (const DriverInfo& d : list_drivers())
        if (d.type == type) list.push_back(&d);
    std::sort(list.begin(), list.end(),
              [](const DriverInfo* a, const DriverInfo* b) { return a->name < b->name; });
    std::printf("%s drivers (%zu):\n", dsp::machine_type_name(type), list.size());
    for (const DriverInfo* d : list) {
        std::printf("  %-20s %s", d->name.c_str(), d->title.c_str());
        if (!d->aliases.empty()) {
            std::printf("  (also:");
            for (const std::string& a : d->aliases) std::printf(" %s", a.c_str());
            std::printf(")");
        }
        std::printf("\n");
    }
}

}  // namespace

#ifdef __EMSCRIPTEN__
std::unique_ptr<dsp::Machine> g_web_machine;  // the machine the page is running
#endif

static int run_dsp(int argc, char** argv) {
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
        } else if (argument == "--listarcades") {
            print_drivers(dsp::MachineType::Arcade);
            return 0;
        } else if (argument == "--listconsoles") {
            print_drivers(dsp::MachineType::Console);
            return 0;
        } else if (argument == "--listcomputers") {
            print_drivers(dsp::MachineType::Computer);
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
        } else if (argument == "--cart") {
            media.emplace_back(next("--cart"));
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
#ifdef __EMSCRIPTEN__
    // The browser drives the frame loop after run() hands it over, so the
    // machine must outlive this call (until the page starts another one).
    g_web_machine = std::move(machine);
    return app.run(*g_web_machine);
#else
    return app.run(*machine);
#endif
}

int main(int argc, char** argv) { return run_dsp(argc, argv); }

#ifdef __EMSCRIPTEN__
// Entry point of the web page (web/shell.html.in): the command-line arguments,
// one per line. Called from JavaScript instead of main() so the page can start
// the emulator once it has the ROM, and so it does not depend on how a given
// Emscripten release exposes main(argc, argv).
extern "C" EMSCRIPTEN_KEEPALIVE int dsp_web_start(const char* lines) {
    static std::vector<std::string> args;
    static std::vector<char*> argv;
    args.assign(1, "dsp");
    std::string current;
    for (const char* p = lines; *p; ++p) {
        if (*p == '\n') {
            if (!current.empty()) args.push_back(current);
            current.clear();
        } else {
            current += *p;
        }
    }
    if (!current.empty()) args.push_back(current);
    argv.clear();
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    // Another driver may be running: stop its loop before freeing it.
    dsp::SdlApp::stop_web();
    g_web_machine.reset();
    return run_dsp(int(args.size()), argv.data());
}

// Every driver for the page's "Machine Type" / driver combos, as JSON:
// [{"name":..,"title":..,"type":"arcade|console|computer","aliases":[..]}, ...]
extern "C" EMSCRIPTEN_KEEPALIVE const char* dsp_web_list() {
    static std::string json;
    if (!json.empty()) return json.c_str();
    auto quote = [](const std::string& text) {
        std::string out = "\"";
        for (unsigned char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += char(c);
            } else if (c < 0x20) {
                out += ' ';
            } else {
                out += char(c);
            }
        }
        return out + "\"";
    };
    json = "[";
    for (const DriverInfo& d : list_drivers()) {
        if (json.size() > 1) json += ",";
        std::string type = dsp::machine_type_name(d.type);
        for (char& c : type) c = char(std::tolower(static_cast<unsigned char>(c)));
        json += "{\"name\":" + quote(d.name) + ",\"title\":" + quote(d.title) +
                ",\"type\":" + quote(type) + ",\"aliases\":[";
        for (size_t i = 0; i < d.aliases.size(); ++i) json += (i ? "," : "") + quote(d.aliases[i]);
        json += "]}";
    }
    json += "]";
    return json.c_str();
}
#endif
