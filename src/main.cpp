#include <cctype>
#include <cstdio>
#include <cstdint>
#include "core/fs_compat.h"
#include <random>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <typeinfo>
#include <set>
#include <map>
#include <string>
#include <vector>

#include "core/machine.h"

#include "frontend/machine_registry.h"

#include "core/embedded_media.h"
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
		"    ambush, arabian, bionicc, blktiger, blockout, bloodbros, skysmash, blueprnt, saturnzi, grasspin, bombjack, calorie, boogwing, brkthru, darwin, btime, lnc, mmonkey, shaolins, tehkanwc, appoooh, robowres, arkanoid, renegade\n"
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
        "  --embedded         start the media compiled into this executable (the default\n"
        "                     when it has some and no arguments are given)\n"
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

void print_drivers(dsp::MachineType type) {
    std::vector<const dsp::DriverInfo*> list;
    for (const dsp::DriverInfo& d : dsp::list_drivers())
        if (d.type == type) list.push_back(&d);
    std::sort(list.begin(), list.end(),
              [](const dsp::DriverInfo* a, const dsp::DriverInfo* b) { return a->name < b->name; });
    std::printf("%s drivers (%zu):\n", dsp::machine_type_name(type), list.size());
    for (const dsp::DriverInfo* d : list) {
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

// Directory the embedded media are written to; removed when the program ends.
std::string g_embedded_dir;

void remove_embedded_dir() {
#ifndef __EMSCRIPTEN__
    if (!g_embedded_dir.empty()) {
        std::error_code ec;
        dsp::fsx::remove_all(g_embedded_dir, ec);
    }
#endif
}

// Media compiled into the executable (tools/embed_media.py): writes them out
// and returns their command line, or false when there are none.
bool embedded_command_line(std::vector<std::string>* args, std::string* error) {
    if (dsp_embedded_file_count == 0) return false;
#ifdef __EMSCRIPTEN__
    const std::string dir = "/embedded";  // MEMFS
#else
    std::error_code ec;
    const std::string dir = (dsp::fsx::temp_directory_path(ec) /
                             ("dsp-embedded-" + std::to_string(std::random_device{}())))
                                .string();
#endif
    if (!dsp::extract_embedded_media(dsp_embedded_files, dsp_embedded_file_count, dsp_embedded_args, dir,
                                     args, error))
        return false;
    if (g_embedded_dir.empty()) std::atexit(remove_embedded_dir);
    g_embedded_dir = dir;
    return true;
}

static int run_dsp(int argc, char** argv) {
    // Without arguments (or with --embedded), an executable with embedded
    // media starts them; the other options given with --embedded (--scale,
    // --fullscreen, --mute, --screenshot ...) are added after the embedded
    // command line. Any other command line works as usual.
    std::vector<std::string> embedded_args;
    std::vector<char*> embedded_argv;
    bool use_embedded = dsp_embedded_file_count > 0 && argc == 1;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--embedded") use_embedded = true;
    if (use_embedded) {
        std::string error;
        if (dsp_embedded_file_count == 0) {
            std::fprintf(stderr, "this executable has no embedded media\n");
            return 1;
        }
        if (!embedded_command_line(&embedded_args, &error)) {
            std::fprintf(stderr, "cannot unpack the embedded media: %s\n", error.c_str());
            return 1;
        }
        embedded_argv.push_back(argv[0]);
        for (std::string& a : embedded_args) embedded_argv.push_back(a.data());
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) != "--embedded") embedded_argv.push_back(argv[i]);
        embedded_argv.push_back(nullptr);
        argc = int(embedded_argv.size()) - 1;
        argv = embedded_argv.data();
    }

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

    std::unique_ptr<dsp::Machine> machine = dsp::create_machine(game);
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
// The page starts the embedded media when its URL names no game.
extern "C" EMSCRIPTEN_KEEPALIVE int dsp_web_has_embedded() { return dsp_embedded_file_count > 0 ? 1 : 0; }

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
    for (const dsp::DriverInfo& d : dsp::list_drivers()) {
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
