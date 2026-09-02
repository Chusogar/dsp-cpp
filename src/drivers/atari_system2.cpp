#include "drivers/atari_system2.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// A slice of a ROM file. `source` is the offset inside the file, so a MAME
// ROM_CONTINUE becomes a second chunk of the same file. `dest` is the offset
// inside the target region; for the interleaved T-11 program an odd `dest`
// selects the high byte lane of every word.
struct RomChunk {
    const char* name;
    uint32_t crc;
    uint32_t source;
    uint32_t length;
    uint32_t dest;
};

// MAME atarisy2.cpp, paperboy ROM set.
const std::vector<RomChunk> kPaperboyMain = {
    {"cpu_l07.rv3", 0x4024bb9b, 0, 0x4000, 0x008000},
    {"cpu_n07.rv3", 0x0260901a, 0, 0x4000, 0x008001},
    {"cpu_f06.rv2", 0x3fea86ac, 0, 0x4000, 0x010000},
    {"cpu_n06.rv2", 0x711b17ba, 0, 0x4000, 0x010001},
    {"cpu_j06.rv1", 0xa754b12d, 0, 0x4000, 0x030000},
    {"cpu_p06.rv1", 0x89a1ff9c, 0, 0x4000, 0x030001},
    {"cpu_k06.rv1", 0x290bb034, 0, 0x4000, 0x050000},
    {"cpu_r06.rv1", 0x826993de, 0, 0x4000, 0x050001},
    {"cpu_l06.rv2", 0x8a754466, 0, 0x4000, 0x070000},
    {"cpu_s06.rv2", 0x224209f9, 0, 0x4000, 0x070001},
};

const std::vector<RomChunk> kPaperboySound = {
    {"cpu_a02.rv3", 0xba251bc4, 0, 0x4000, 0x4000},
    {"cpu_b02.rv2", 0xe4e7a8b9, 0, 0x4000, 0x8000},
    {"cpu_c02.rv2", 0xd44c2aa2, 0, 0x4000, 0xc000},
};

const std::vector<RomChunk> kPaperboyPlayfield = {
    {"vid_a06.rv1", 0xb32ffddf, 0, 0x8000, 0x00000},
    {"vid_b06.rv1", 0x301b849d, 0, 0x4000, 0x0c000},
    {"vid_c06.rv1", 0x7bb59d68, 0, 0x8000, 0x10000},
    {"vid_d06.rv1", 0x1a1d4ba8, 0, 0x4000, 0x1c000},
};

const std::vector<RomChunk> kPaperboyMotion = {
    {"vid_l06.rv1", 0x067ef202, 0, 0x8000, 0x00000},
    {"vid_k06.rv1", 0x76b977c4, 0, 0x8000, 0x08000},
    {"vid_j06.rv1", 0x2a3cc8d0, 0, 0x8000, 0x10000},
    {"vid_h06.rv1", 0x6763a321, 0, 0x8000, 0x18000},
    {"vid_s06.rv1", 0x0a321b7b, 0, 0x8000, 0x20000},
    {"vid_p06.rv1", 0x5bd089ee, 0, 0x8000, 0x28000},
    {"vid_n06.rv1", 0xc34a517d, 0, 0x8000, 0x30000},
    {"vid_m06.rv1", 0xdf723956, 0, 0x8000, 0x38000},
};

const std::vector<RomChunk> kPaperboyAlpha = {
    {"vid_t06.rv1", 0x60d7aebb, 0, 0x2000, 0x0000},
};

const std::vector<RomChunk> kPaperboyEeprom = {
    {"paperboy-eeprom.bin", 0x756b90cc, 0, 0x200, 0x0000},
};

// MAME atarisy2.cpp, ssprint ROM set (Super Sprint, rev 4).
const std::vector<RomChunk> kSuperSprintMain = {
    {"136042-330.7l", 0xee312027, 0, 0x4000, 0x008000},
    {"136042-331.7n", 0x2ef15354, 0, 0x4000, 0x008001},
    {"136042-329.6f", 0xed1d6205, 0, 0x8000, 0x010000},
    {"136042-325.6n", 0xaecaa2bf, 0, 0x8000, 0x010001},
    {"136042-127.6k", 0xde6c4db9, 0, 0x8000, 0x050000},
    {"136042-123.6r", 0xaff23b5a, 0, 0x8000, 0x050001},
    {"136042-126.6l", 0x92f5392c, 0, 0x8000, 0x070000},
    {"136042-122.6s", 0x0381f362, 0, 0x8000, 0x070001},
};

const std::vector<RomChunk> kSuperSprintSound = {
    {"136042-419.2bc", 0xb277915a, 0, 0x4000, 0x8000},
    {"136042-420.2d", 0x170b2c53, 0, 0x4000, 0xc000},
};

const std::vector<RomChunk> kSuperSprintPlayfield = {
    {"136042-105.6a", 0x911499fe, 0x0000, 0x8000, 0x20000},
    {"136042-105.6a", 0x911499fe, 0x8000, 0x8000, 0x00000},
    {"136042-106.6b", 0xa39b25ed, 0x0000, 0x8000, 0x08000},
    {"136042-101.7a", 0x6d015c72, 0x0000, 0x8000, 0x30000},
    {"136042-101.7a", 0x6d015c72, 0x8000, 0x8000, 0x10000},
    {"136042-102.7b", 0x54e21f0a, 0x0000, 0x8000, 0x18000},
    {"136042-107.6c", 0xb7ded658, 0x0000, 0x8000, 0x60000},
    {"136042-107.6c", 0xb7ded658, 0x8000, 0x8000, 0x40000},
    {"136042-108.6de", 0x4a804a4c, 0x0000, 0x8000, 0x48000},
    {"136042-104.7de", 0x339644ed, 0x0000, 0x8000, 0x70000},
    {"136042-104.7de", 0x339644ed, 0x8000, 0x8000, 0x50000},
    {"136042-103.7c", 0x64d473a8, 0x0000, 0x8000, 0x58000},
};

const std::vector<RomChunk> kSuperSprintMotion = {
    {"136042-113.6l", 0xf869b0fc, 0, 0x8000, 0x00000},
    {"136042-112.6k", 0xabcbc114, 0, 0x8000, 0x08000},
    {"136042-110.6jh", 0x9e91e734, 0, 0x8000, 0x10000},
    {"136042-109.6fh", 0x3a051f36, 0, 0x8000, 0x18000},
    {"136042-117.6rs", 0xb15c1b90, 0, 0x8000, 0x20000},
    {"136042-116.6pr", 0x1dcdd5aa, 0, 0x8000, 0x28000},
    {"136042-115.6n", 0xfb5677d9, 0, 0x8000, 0x30000},
    {"136042-114.6m", 0x35e70a8d, 0, 0x8000, 0x38000},
};

const std::vector<RomChunk> kSuperSprintAlpha = {
    {"136042-118.6t", 0x8489d113, 0, 0x4000, 0x0000},
};

const std::vector<RomChunk> kSuperSprintEeprom = {
    {"ssprint-eeprom.bin", 0x9301ed27, 0, 0x200, 0x0000},
};

// MAME atarisy2.cpp, 720 ROM set (720 Degrees, rev 4).
const std::vector<RomChunk> kDegrees720Main = {
    {"136047-3126.7lm", 0x43abd367, 0, 0x4000, 0x008000},
    {"136047-3127.7mn", 0x772e1e5b, 0, 0x4000, 0x008001},
    {"136047-3128.6fh", 0xbf6f425b, 0, 0x10000, 0x010000},
    {"136047-4131.6mn", 0x2ea8a20f, 0, 0x10000, 0x010001},
    {"136047-1129.6hj", 0xeabf0b01, 0, 0x10000, 0x030000},
    {"136047-1132.6p", 0xa24f333e, 0, 0x10000, 0x030001},
    {"136047-1130.6k", 0x93fba845, 0, 0x10000, 0x050000},
    {"136047-1133.6r", 0x53c177be, 0, 0x10000, 0x050001},
};

const std::vector<RomChunk> kDegrees720Sound = {
    {"136047-2134.2a", 0x0db4ca28, 0, 0x4000, 0x4000},
    {"136047-1135.2b", 0xb1f157d0, 0, 0x4000, 0x8000},
    {"136047-2136.2cd", 0x00b06bec, 0, 0x4000, 0xc000},
};

const std::vector<RomChunk> kDegrees720Playfield = {
    {"136047-1121.6a", 0x7adb5f9a, 0, 0x8000, 0x00000},
    {"136047-1122.6b", 0x41b60141, 0, 0x8000, 0x08000},
    {"136047-1123.7a", 0x501881d5, 0, 0x8000, 0x10000},
    {"136047-1124.7b", 0x096f2574, 0, 0x8000, 0x18000},
    {"136047-1117.6c", 0x5a55f149, 0, 0x8000, 0x20000},
    {"136047-1118.6d", 0x9bb2429e, 0, 0x8000, 0x28000},
    {"136047-1119.7d", 0x8f7b20e5, 0, 0x8000, 0x30000},
    {"136047-1120.7c", 0x46af6d35, 0, 0x8000, 0x38000},
};

const std::vector<RomChunk> kDegrees720Motion = {
    {"136047-1109.6t", 0x0a46b693, 0x0000, 0x8000, 0x020000},
    {"136047-1109.6t", 0x0a46b693, 0x8000, 0x8000, 0x000000},
    {"136047-1110.6sr", 0x457d7e38, 0x0000, 0x8000, 0x028000},
    {"136047-1110.6sr", 0x457d7e38, 0x8000, 0x8000, 0x008000},
    {"136047-1111.6p", 0xffad0a5b, 0x0000, 0x8000, 0x030000},
    {"136047-1111.6p", 0xffad0a5b, 0x8000, 0x8000, 0x010000},
    {"136047-1112.6n", 0x06664580, 0x0000, 0x8000, 0x038000},
    {"136047-1112.6n", 0x06664580, 0x8000, 0x8000, 0x018000},
    {"136047-1113.6m", 0x7445dc0f, 0x0000, 0x8000, 0x060000},
    {"136047-1113.6m", 0x7445dc0f, 0x8000, 0x8000, 0x040000},
    {"136047-1114.6l", 0x23eaceb0, 0x0000, 0x8000, 0x068000},
    {"136047-1114.6l", 0x23eaceb0, 0x8000, 0x8000, 0x048000},
    {"136047-1115.6kj", 0x0cc8de53, 0x0000, 0x8000, 0x070000},
    {"136047-1115.6kj", 0x0cc8de53, 0x8000, 0x8000, 0x050000},
    {"136047-1116.6jh", 0x2d8f1369, 0x0000, 0x8000, 0x078000},
    {"136047-1116.6jh", 0x2d8f1369, 0x8000, 0x8000, 0x058000},
    {"136047-1101.5t", 0x2ac77b80, 0x0000, 0x8000, 0x0a0000},
    {"136047-1101.5t", 0x2ac77b80, 0x8000, 0x8000, 0x080000},
    {"136047-1102.5sr", 0xf19c3b06, 0x0000, 0x8000, 0x0a8000},
    {"136047-1102.5sr", 0xf19c3b06, 0x8000, 0x8000, 0x088000},
    {"136047-1103.5p", 0x78f9ab90, 0x0000, 0x8000, 0x0b0000},
    {"136047-1103.5p", 0x78f9ab90, 0x8000, 0x8000, 0x090000},
    {"136047-1104.5n", 0x77ce4a7f, 0x0000, 0x8000, 0x0b8000},
    {"136047-1104.5n", 0x77ce4a7f, 0x8000, 0x8000, 0x098000},
    {"136047-1105.5m", 0xbef5a025, 0x0000, 0x8000, 0x0e0000},
    {"136047-1105.5m", 0xbef5a025, 0x8000, 0x8000, 0x0c0000},
    {"136047-1106.5l", 0x92a159c8, 0x0000, 0x8000, 0x0e8000},
    {"136047-1106.5l", 0x92a159c8, 0x8000, 0x8000, 0x0c8000},
    {"136047-1107.5kj", 0x0a94a3ef, 0x0000, 0x8000, 0x0f0000},
    {"136047-1107.5kj", 0x0a94a3ef, 0x8000, 0x8000, 0x0d0000},
    {"136047-1108.5jh", 0x9815eda6, 0x0000, 0x8000, 0x0f8000},
    {"136047-1108.5jh", 0x9815eda6, 0x8000, 0x8000, 0x0d8000},
};

const std::vector<RomChunk> kDegrees720Alpha = {
    {"136047-1125.4t", 0x6b7e2328, 0, 0x4000, 0x0000},
};

const std::vector<RomChunk> kDegrees720Eeprom = {
    {"720-eeprom.bin", 0xcfe1c24e, 0, 0x200, 0x0000},
};

// MAME atarisy2.cpp, apb ROM set (APB - All Points Bulletin, rev 7).
const std::vector<RomChunk> kApbMain = {
    {"136051-2126.7l", 0x8edf4726, 0, 0x4000, 0x008000},
    {"136051-2127.7n", 0xe2b2aff2, 0, 0x4000, 0x008001},
    {"136051-7128.6f", 0xc08504d2, 0, 0x10000, 0x010000},
    {"136051-7129.6n", 0x79adb57f, 0, 0x10000, 0x010001},
    {"136051-1130.6j", 0xf64c752e, 0, 0x10000, 0x030000},
    {"136051-1131.6p", 0x0a506e04, 0, 0x10000, 0x030001},
    {"136051-1132.6l", 0x6d0e7a4e, 0, 0x10000, 0x070000},
    {"136051-1133.6s", 0xaf88d429, 0, 0x10000, 0x070001},
};

const std::vector<RomChunk> kApbSound = {
    {"136051-5134.2a", 0x1c8bdeed, 0, 0x4000, 0x4000},
    {"136051-5135.2bc", 0xed6adb91, 0, 0x4000, 0x8000},
    {"136051-5136.2d", 0x341f8486, 0, 0x4000, 0xc000},
};

const std::vector<RomChunk> kApbPlayfield = {
    {"136051-1118.6a", 0x93752c49, 0x0000, 0x8000, 0x00000},
    {"136051-1120.6bc", 0x043086f8, 0x0000, 0x8000, 0x28000},
    {"136051-1120.6bc", 0x043086f8, 0x8000, 0x8000, 0x08000},
    {"136051-1122.7a", 0x5ee79481, 0x0000, 0x8000, 0x30000},
    {"136051-1122.7a", 0x5ee79481, 0x8000, 0x8000, 0x10000},
    {"136051-1124.7bc", 0x27760395, 0x0000, 0x8000, 0x38000},
    {"136051-1124.7bc", 0x27760395, 0x8000, 0x8000, 0x18000},
    {"136051-1117.6cd", 0xcfc3f8a3, 0x0000, 0x8000, 0x40000},
    {"136051-1119.6de", 0x68850612, 0x0000, 0x8000, 0x68000},
    {"136051-1119.6de", 0x68850612, 0x8000, 0x8000, 0x48000},
    {"136051-1121.7de", 0xc7977062, 0x0000, 0x8000, 0x70000},
    {"136051-1121.7de", 0xc7977062, 0x8000, 0x8000, 0x50000},
    {"136051-1123.7cd", 0x3c96c848, 0x0000, 0x8000, 0x78000},
    {"136051-1123.7cd", 0x3c96c848, 0x8000, 0x8000, 0x58000},
};

const std::vector<RomChunk> kApbMotion = {
    {"136051-1105.6t", 0x9b78a88e, 0x0000, 0x8000, 0x020000},
    {"136051-1105.6t", 0x9b78a88e, 0x8000, 0x8000, 0x000000},
    {"136051-1106.6rs", 0x4787ff58, 0x0000, 0x8000, 0x028000},
    {"136051-1106.6rs", 0x4787ff58, 0x8000, 0x8000, 0x008000},
    {"136051-1107.6pr", 0x0e85f2ac, 0x0000, 0x8000, 0x030000},
    {"136051-1107.6pr", 0x0e85f2ac, 0x8000, 0x8000, 0x010000},
    {"136051-1108.6n", 0x70ff9308, 0x0000, 0x8000, 0x038000},
    {"136051-1108.6n", 0x70ff9308, 0x8000, 0x8000, 0x018000},
    {"136051-1113.6m", 0x4a445356, 0x0000, 0x8000, 0x060000},
    {"136051-1113.6m", 0x4a445356, 0x8000, 0x8000, 0x040000},
    {"136051-1114.6kl", 0xb9b27f3c, 0x0000, 0x8000, 0x068000},
    {"136051-1114.6kl", 0xb9b27f3c, 0x8000, 0x8000, 0x048000},
    {"136051-1115.6jk", 0xa7671dd8, 0x0000, 0x8000, 0x070000},
    {"136051-1115.6jk", 0xa7671dd8, 0x8000, 0x8000, 0x050000},
    {"136051-1116.6h", 0x879fc7de, 0x0000, 0x8000, 0x078000},
    {"136051-1116.6h", 0x879fc7de, 0x8000, 0x8000, 0x058000},
    {"136051-1101.5t", 0x0ef13513, 0x0000, 0x8000, 0x0a0000},
    {"136051-1101.5t", 0x0ef13513, 0x8000, 0x8000, 0x080000},
    {"136051-1102.5rs", 0x401e06fd, 0x0000, 0x8000, 0x0a8000},
    {"136051-1102.5rs", 0x401e06fd, 0x8000, 0x8000, 0x088000},
    {"136051-1103.5pr", 0x50d820e8, 0x0000, 0x8000, 0x0b0000},
    {"136051-1103.5pr", 0x50d820e8, 0x8000, 0x8000, 0x090000},
    {"136051-1104.5n", 0x912d878f, 0x0000, 0x8000, 0x0b8000},
    {"136051-1104.5n", 0x912d878f, 0x8000, 0x8000, 0x098000},
    {"136051-1109.5m", 0x6716a408, 0x0000, 0x8000, 0x0e0000},
    {"136051-1109.5m", 0x6716a408, 0x8000, 0x8000, 0x0c0000},
    {"136051-1110.5kl", 0x7e184981, 0x0000, 0x8000, 0x0e8000},
    {"136051-1110.5kl", 0x7e184981, 0x8000, 0x8000, 0x0c8000},
    {"136051-1111.5jk", 0x353a14fd, 0x0000, 0x8000, 0x0f0000},
    {"136051-1111.5jk", 0x353a14fd, 0x8000, 0x8000, 0x0d0000},
    {"136051-1112.5h", 0x3af7c50f, 0x0000, 0x8000, 0x0f8000},
    {"136051-1112.5h", 0x3af7c50f, 0x8000, 0x8000, 0x0d8000},
};

const std::vector<RomChunk> kApbAlpha = {
    {"136051-1125.4t", 0x05a0341c, 0, 0x4000, 0x0000},
};

// MAME init_paperboy()/init_ssprint() mirror the program ROMs differently.
enum class MainExpand { None, Paperboy, SuperSprint };

struct GameInfo {
    const char* title;
    int slapstic;
    // MAME m_pedal_count.
    int pedal_count;
    bool has_tms;
    bool rotated;
    MainExpand expand;
    uint32_t playfield_size;
    uint32_t motion_size;
    uint32_t alpha_size;
    const std::vector<RomChunk>* main;
    const std::vector<RomChunk>* sound;
    const std::vector<RomChunk>* playfield;
    const std::vector<RomChunk>* motion;
    const std::vector<RomChunk>* alpha;
    const std::vector<RomChunk>* eeprom;
};

const GameInfo& game_info(AtariSystem2::Game game) {
    static const GameInfo kPaperboy = {
        /*title=*/"Paperboy",
        /*slapstic=*/105,
        /*pedal_count=*/0,
        /*has_tms=*/true,
        /*rotated=*/false,
        /*expand=*/MainExpand::Paperboy,
        /*playfield_size=*/0x20000,
        /*motion_size=*/0x40000,
        /*alpha_size=*/0x2000,
        &kPaperboyMain,
        &kPaperboySound,
        &kPaperboyPlayfield,
        &kPaperboyMotion,
        &kPaperboyAlpha,
        &kPaperboyEeprom,
    };
    static const GameInfo kSuperSprint = {
        /*title=*/"Super Sprint",
        /*slapstic=*/108,
        /*pedal_count=*/3,
        /*has_tms=*/false,
        /*rotated=*/false,
        /*expand=*/MainExpand::SuperSprint,
        /*playfield_size=*/0x80000,
        /*motion_size=*/0x40000,
        /*alpha_size=*/0x4000,
        &kSuperSprintMain,
        &kSuperSprintSound,
        &kSuperSprintPlayfield,
        &kSuperSprintMotion,
        &kSuperSprintAlpha,
        &kSuperSprintEeprom,
    };
    static const GameInfo kApb = {
        /*title=*/"APB - All Points Bulletin",
        /*slapstic=*/110,
        /*pedal_count=*/2,
        /*has_tms=*/true,
        /*rotated=*/true,
        /*expand=*/MainExpand::None,
        /*playfield_size=*/0x80000,
        /*motion_size=*/0x100000,
        /*alpha_size=*/0x4000,
        &kApbMain,
        &kApbSound,
        &kApbPlayfield,
        &kApbMotion,
        &kApbAlpha,
        /*eeprom=*/nullptr,
    };
    static const GameInfo kDegrees720 = {
        /*title=*/"720 Degrees",
        /*slapstic=*/107,
        /*pedal_count=*/-1,
        /*has_tms=*/true,
        /*rotated=*/false,
        /*expand=*/MainExpand::None,
        /*playfield_size=*/0x40000,
        /*motion_size=*/0x100000,
        /*alpha_size=*/0x4000,
        &kDegrees720Main,
        &kDegrees720Sound,
        &kDegrees720Playfield,
        &kDegrees720Motion,
        &kDegrees720Alpha,
        &kDegrees720Eeprom,
    };
    switch (game) {
        case AtariSystem2::Game::SuperSprint: return kSuperSprint;
        case AtariSystem2::Game::Apb: return kApb;
        case AtariSystem2::Game::Degrees720: return kDegrees720;
        case AtariSystem2::Game::Paperboy: break;
    }
    return kPaperboy;
}

// Copies every chunk into `region`. `interleaved` splits the T-11 program into
// the even/odd byte lanes of each word.
bool load_region(RomLoader& loader, const std::vector<RomChunk>& chunks,
                 std::vector<uint8_t>& region, bool interleaved,
                 std::vector<std::string>& warnings, std::string* error) {
    std::map<std::string, std::vector<uint8_t>> files;
    for (const RomChunk& chunk : chunks) {
        auto it = files.find(chunk.name);
        if (it == files.end()) {
            std::vector<uint8_t> data;
            if (!loader.try_read(chunk.name, data)) {
                if (error != nullptr) *error = std::string("missing ROM ") + chunk.name;
                return false;
            }
            if (crc32_of(data.data(), data.size()) != chunk.crc) {
                warnings.push_back(std::string("CRC mismatch on ") + chunk.name);
            }
            it = files.emplace(chunk.name, std::move(data)).first;
        }
        const std::vector<uint8_t>& data = it->second;
        if (size_t(chunk.source) + chunk.length > data.size()) {
            if (error != nullptr) *error = std::string("short ROM ") + chunk.name;
            return false;
        }
        const uint32_t span = interleaved ? chunk.length * 2 : chunk.length;
        if (size_t(chunk.dest & ~1u) + span > region.size()) {
            if (error != nullptr) *error = std::string("ROM out of range ") + chunk.name;
            return false;
        }
        if (interleaved) {
            const uint32_t base = chunk.dest & ~1u;
            const uint32_t lane = chunk.dest & 1u;
            for (uint32_t i = 0; i < chunk.length; i++) {
                region[base + i * 2 + lane] = data[chunk.source + i];
            }
        } else {
            std::memcpy(&region[chunk.dest], &data[chunk.source], chunk.length);
        }
    }
    return true;
}

GfxLayout alpha_layout(uint32_t region_size) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = int(region_size / 16);
    layout.planes = 2;
    layout.char_increment = 8 * 8 * 2;
    layout.plane_offsets = {0, 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
    layout.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
    return layout;
}

GfxLayout playfield_layout(uint32_t region_size) {
    const int kHalf = int(region_size / 2) * 8;
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = int(region_size / 2 / 16);
    layout.planes = 4;
    layout.char_increment = 8 * 8 * 2;
    layout.plane_offsets = {0, 4, kHalf + 0, kHalf + 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
    layout.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
    return layout;
}

GfxLayout motion_layout(uint32_t region_size) {
    const int kHalf = int(region_size / 2) * 8;
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = int(region_size / 2 / 64);
    layout.planes = 4;
    layout.char_increment = 16 * 16 * 2;
    layout.plane_offsets = {0, 4, kHalf + 0, kHalf + 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27};
    layout.y_offsets = {0,   32,  64,  96,  128, 160, 192, 224,
                        256, 288, 320, 352, 384, 416, 448, 480};
    return layout;
}

AtariMotionObjects::Config motion_config() {
    AtariMotionObjects::Config config;
    config.tile_width = 16;
    config.tile_height = 16;
    config.bankcount = 1;
    config.linked = true;
    config.split = false;
    config.slipheight = 0;
    config.maxperline = 0;
    config.palettebase = 0;
    config.link_entry = {0, 0, 0, 0x07f8};
    config.code_entry = {{0, 0x07ff, 0, 0}, {0x0007, 0, 0, 0}};
    config.color_entry = {{0, 0, 0, 0x3000}, {0, 0, 0, 0}};
    config.xpos_entry = {0, 0, 0xffc0, 0};
    config.ypos_entry = {0x7fc0, 0, 0, 0};
    config.height_entry = {0, 0x3800, 0, 0};
    config.hflip_entry = {0, 0x4000, 0, 0};
    config.priority_entry = {0, 0, 0, 0xc000};
    config.neighbor_entry = {0, 0x8000, 0, 0};
    return config;
}

// MAME atarisy2_state::RRRRGGGGBBBBIIII.
uint32_t palette_entry(uint16_t raw) {
    constexpr int ZB = 115, Z3 = 78, Z2 = 37, Z1 = 17, Z0 = 9;
    static const int kIntensity[16] = {
        0,        ZB + Z0,       ZB + Z1,       ZB + Z1 + Z0,
        ZB + Z2,  ZB + Z2 + Z0,  ZB + Z2 + Z1,  ZB + Z2 + Z1 + Z0,
        ZB + Z3,  ZB + Z3 + Z0,  ZB + Z3 + Z1,  ZB + Z3 + Z1 + Z0,
        ZB + Z3 + Z2, ZB + Z3 + Z2 + Z0, ZB + Z3 + Z2 + Z1, ZB + Z3 + Z2 + Z1 + Z0};
    static const int kColor[16] = {0x0, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9,
                                   0xa, 0xb, 0xc, 0xd, 0xe, 0xe, 0xf, 0xf};
    const int i = kIntensity[raw & 15];
    const uint32_t red = uint32_t((kColor[(raw >> 12) & 15] * i) >> 4);
    const uint32_t green = uint32_t((kColor[(raw >> 8) & 15] * i) >> 4);
    const uint32_t blue = uint32_t((kColor[(raw >> 4) & 15] * i) >> 4);
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

}  // namespace

AtariSystem2::AtariSystem2(Game game)
    : game_(game),
      main_cpu_(kMainClock, 0x36ff),
      sound_cpu_(kSoundClock),
      ym_(kYmClock),
      pokey1_(kPokeyClock),
      pokey2_(kPokeyClock),
      tms_(kMasterClock / 4 / 4 / 2, Tms5220::Variant::Tms5220C),
      slapstic_(game_info(game).slapstic, nullptr) {
    const GameInfo& info = game_info(game);
    rotated_ = info.rotated;
    has_tms_ = info.has_tms;
    pedal_count_ = info.pedal_count;
    rom_.assign(0x90000 / 2, 0);
    alpha_.assign(size_t(kScreenWidth) * kScreenHeight, kTransparent);
    playfield_.assign(size_t(kPlayfieldWidth) * kPlayfieldHeight, 0);
    playfield_category_.assign(size_t(kPlayfieldWidth) * kPlayfieldHeight, 0);
    mo_pen_.assign(size_t(kScreenWidth) * kScreenHeight, kMoTransparent);
    mo_priority_.assign(size_t(kScreenWidth) * kScreenHeight, 0);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint16_t value, uint16_t mem_mask) {
            main_write(address, value, mem_mask);
        });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler(
        [this](bool on) { sound_cpu_.set_irq(on ? IrqLine::Hold : IrqLine::Clear); });
    pokey1_.set_allpot_handler([this](uint8_t) { return dsw_[0]; });
    pokey2_.set_allpot_handler([this](uint8_t) { return dsw_[1]; });

    motion_objects_ = std::make_unique<AtariMotionObjects>(
        motion_config(), nullptr, mob_ram_.data(), kScreenWidth + 16, kScreenHeight + 16);
}

const char* AtariSystem2::title() const { return game_info(game_).title; }

bool AtariSystem2::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool AtariSystem2::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    const GameInfo& info = game_info(game_);

    std::vector<uint8_t> main_bytes(0x90000, 0);
    if (!load_region(loader, *info.main, main_bytes, true, warnings_, error)) return false;
    switch (info.expand) {
        case MainExpand::Paperboy:
            // MAME init_paperboy(): mirror the 16k program ROM pairs over the
            // whole 64k bank.
            for (uint32_t i = 0x10000; i < 0x90000; i += 0x20000) {
                std::memcpy(&main_bytes[i + 0x08000], &main_bytes[i], 0x8000);
                std::memcpy(&main_bytes[i + 0x10000], &main_bytes[i], 0x8000);
                std::memcpy(&main_bytes[i + 0x18000], &main_bytes[i], 0x8000);
            }
            break;
        case MainExpand::SuperSprint:
            // MAME init_ssprint(): the 32k pairs are mirrored once.
            for (uint32_t i = 0x10000; i < 0x90000; i += 0x20000) {
                std::memcpy(&main_bytes[i + 0x10000], &main_bytes[i], 0x10000);
            }
            break;
        case MainExpand::None: break;
    }
    for (size_t i = 0; i < rom_.size(); i++) {
        rom_[i] = uint16_t(main_bytes[i * 2] | (main_bytes[i * 2 + 1] << 8));
    }

    std::vector<uint8_t> sound_rom(0x10000, 0);
    if (!load_region(loader, *info.sound, sound_rom, false, warnings_, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> playfield_rom(info.playfield_size, 0);
    if (!load_region(loader, *info.playfield, playfield_rom, false, warnings_, error)) {
        return false;
    }
    playfield_gfx_.decode(playfield_layout(info.playfield_size), playfield_rom);

    std::vector<uint8_t> motion_rom(info.motion_size, 0);
    if (!load_region(loader, *info.motion, motion_rom, false, warnings_, error)) return false;
    // ROMREGION_INVERT: the motion object ROMs are stored inverted, which turns
    // the transparent pen into 15.
    for (uint8_t& value : motion_rom) value = uint8_t(~value);
    motion_gfx_.decode(motion_layout(info.motion_size), motion_rom);

    std::vector<uint8_t> alpha_rom(info.alpha_size, 0);
    if (!load_region(loader, *info.alpha, alpha_rom, false, warnings_, error)) return false;
    alpha_gfx_.decode(alpha_layout(info.alpha_size), alpha_rom);

    eeprom_.fill(0xff);
    if (info.eeprom != nullptr) {
        std::vector<uint8_t> eeprom(0x200, 0xff);
        std::string eeprom_error;
        std::vector<std::string> eeprom_warnings;
        if (load_region(loader, *info.eeprom, eeprom, false, eeprom_warnings, &eeprom_error)) {
            std::copy(eeprom.begin(), eeprom.end(), eeprom_.begin());
        } else {
            warnings_.push_back("default EEPROM missing: " + eeprom_error);
        }
    }

    const std::vector<std::string>& loader_warnings = loader.warnings();
    warnings_.insert(warnings_.end(), loader_warnings.begin(), loader_warnings.end());
    return true;
}

void AtariSystem2::reset() {
    slapstic_.reset();
    vram_bank_ = slapstic_.current_bank();
    main_cpu_.reset();
    ym_.reset();
    pokey1_.reset();
    pokey2_.reset();
    tms_.reset();
    // /RS is tied high on System 2 hardware.
    tms_.set_rsq(true);
    tms_.set_wsq(true);

    ram_.fill(0);
    alpha_ram_.fill(0);
    mob_ram_.fill(0);
    playfield_top_.fill(0);
    playfield_bottom_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);

    rom_bank_[0] = 0;
    rom_bank_[1] = 0;
    xscroll_ = 0;
    yscroll_reg_ = 0;
    yscroll_ = 0;
    yscroll_pending_ = 0;
    yscroll_reset_ = false;
    playfield_tile_bank_.fill(0);
    xscroll_line_.fill(0);
    yscroll_line_.fill(0);

    interrupt_enable_ = 0;
    video_int_state_ = false;
    scanline_int_state_ = false;
    p2portwr_state_ = false;
    p2portrd_state_ = false;
    update_interrupts();

    sound_reset_state_ = false;
    sound_cpu_in_reset_ = false;
    set_sound_reset(true);
    sound_latch_ = 0;
    main_latch_ = 0;
    sound_pending_ = false;
    main_pending_ = false;
    sound_irq_counter_ = 0;
    sound_cpu_.set_irq(IrqLine::Clear);
    sound_cpu_.set_nmi(IrqLine::Clear);

    adc_value_ = 0;
    adc_input_ = {0x80, 0x80, 0x00, 0x00};
    leta_input_.fill(0);
    dial_ = 0;
    spin_position_ = 0;
    spin_center_count_ = 0;
    spin_rotate_count_ = 0;
    buttons_ = 0;
    coin1_ = coin2_ = coin3_ = false;
    start3_ = false;
    service_coin_ = false;
    self_test_ = false;

    line_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();

    alpha_dirty_.fill(true);
    playfield_dirty_.fill(true);
    std::fill(alpha_.begin(), alpha_.end(), kTransparent);
    std::fill(playfield_.begin(), playfield_.end(), 0);
    std::fill(playfield_category_.begin(), playfield_category_.end(), 0);
    std::fill(mo_pen_.begin(), mo_pen_.end(), kMoTransparent);
    std::fill(mo_priority_.begin(), mo_priority_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

void AtariSystem2::update_interrupts() {
    main_cpu_.set_irq(T11::CP3_LINE, video_int_state_ ? IrqLine::Assert : IrqLine::Clear);
    main_cpu_.set_irq(T11::CP2_LINE, scanline_int_state_ ? IrqLine::Assert : IrqLine::Clear);
    main_cpu_.set_irq(T11::CP1_LINE, p2portwr_state_ ? IrqLine::Assert : IrqLine::Clear);
    main_cpu_.set_irq(T11::CP0_LINE, p2portrd_state_ ? IrqLine::Assert : IrqLine::Clear);
}

void AtariSystem2::write_sound_chip_reset(uint8_t value) {
    if ((value & 1) == (sound_reset_state_ ? 1 : 0)) return;
    sound_reset_state_ = (value & 1) != 0;
    ym_.reset();
    // Only the 0 -> 1 transition halts the speech chip.
    if (!sound_reset_state_ || !has_tms_) return;
    tms_.reset();
    tms_.set_rsq(true);
    tms_.set_wsq(true);
}

void AtariSystem2::set_sound_reset(bool in_reset) {
    if (in_reset == sound_cpu_in_reset_) return;
    sound_cpu_in_reset_ = in_reset;
    if (in_reset) {
        sound_cpu_.set_irq(IrqLine::Clear);
        sound_cpu_.set_nmi(IrqLine::Clear);
        sound_pending_ = false;
        main_pending_ = false;
    } else {
        sound_cpu_.reset();
    }
}

void AtariSystem2::bank_select(int index, uint16_t data) {
    uint8_t bank = uint8_t(((data >> 10) & 077) ^ 3);
    // MAME: bitswap<6>(bank, 5, 4, 1, 0, 3, 2).
    bank = uint8_t((bank & 0x30) | (((bank >> 1) & 1) << 3) | ((bank & 1) << 2) |
                   (((bank >> 3) & 1) << 1) | ((bank >> 2) & 1));
    rom_bank_[size_t(index & 1)] = 0x8000u + uint32_t(bank) * 0x1000u;
}

void AtariSystem2::set_palette(int index, uint16_t value) {
    palette_ram_[size_t(index)] = value;
    palette_[size_t(index)] = palette_entry(value);
}

void AtariSystem2::write_xscroll(uint16_t value) {
    xscroll_ = value;
    if (playfield_tile_bank_[0] != (value & 0x0f)) {
        playfield_tile_bank_[0] = uint16_t(value & 0x0f);
        playfield_dirty_.fill(true);
    }
}

void AtariSystem2::write_yscroll(uint16_t value) {
    yscroll_reg_ = value;
    // Bit 4 clear clocks the new scroll value in right away; otherwise it is
    // latched until the top of the next frame.
    if ((value & 0x10) == 0) {
        yscroll_ = uint16_t((value >> 6) - uint16_t(line_));
        yscroll_reset_ = false;
    } else {
        yscroll_pending_ = uint16_t(value >> 6);
        yscroll_reset_ = true;
    }
    if (playfield_tile_bank_[1] != (value & 0x0f)) {
        playfield_tile_bank_[1] = uint16_t(value & 0x0f);
        playfield_dirty_.fill(true);
    }
}

uint16_t AtariSystem2::switch_r() const {
    // "1800": bits 0-3 unused, 4 = main latch pending, 5 = sound latch pending,
    // 6/7 = buttons (active low). "1801" bit 7 = self test (active low).
    uint16_t low = 0xcf;
    // Super Sprint puts the third start button on bit 3.
    if (game_ == Game::SuperSprint && start3_) low = uint16_t(low & ~0x08);
    if (main_pending_) low |= 0x10;
    if (sound_pending_) low |= 0x20;
    low = uint16_t(low & ~uint16_t(buttons_));
    uint16_t high = 0xff;
    if (self_test_) high = uint16_t(high & ~0x80);
    return uint16_t(low | (high << 8));
}

uint8_t AtariSystem2::switch_6502_r() const {
    uint8_t result = 0xf4;
    if (sound_pending_) result |= 0x01;
    if (main_pending_) result |= 0x02;
    if (has_tms_ && !tms_.readyq()) result = uint8_t(result & ~0x04);
    if (service_coin_) result = uint8_t(result & ~0x10);
    if (game_ == Game::SuperSprint) {
        // Super Sprint moves the three coin inputs one bit up.
        if (coin1_) result = uint8_t(result & ~0x20);
        if (coin2_) result = uint8_t(result & ~0x40);
        if (coin3_) result = uint8_t(result & ~0x80);
    } else {
        if (coin3_) result = uint8_t(result & ~0x20);
        if (coin1_) result = uint8_t(result & ~0x40);
        if (coin2_) result = uint8_t(result & ~0x80);
    }
    return result;
}

uint8_t AtariSystem2::adc_channel_value(int channel) const {
    return adc_input_[size_t(channel & 3)];
}

uint8_t AtariSystem2::leta_r(int channel) const {
    // 720 wires its rotary control to the first two channels: channel 0 counts
    // the center disc gaps and channel 1 the rotation.
    if (pedal_count_ == -1 && channel <= 1) {
        return channel == 0 ? uint8_t(spin_center_count_) : spin_rotate_count_;
    }
    return leta_input_[size_t(channel & 3)];
}

uint16_t AtariSystem2::main_read(uint16_t address) {
    if (address < 0x1000) return ram_[address >> 1];
    if (address < 0x1400) return palette_ram_[(address >> 1) & 0xff];
    if (address < 0x1480) return adc_value_;
    if (address < 0x1800) return 0xffff;
    if (address < 0x1c00) return switch_r();
    if (address < 0x2000) {
        p2portwr_state_ = false;
        update_interrupts();
        main_pending_ = false;
        return uint16_t(main_latch_ | 0xff00);
    }
    if (address < 0x4000) {
        const uint16_t offset = uint16_t((address - 0x2000) >> 1);
        switch (vram_bank_) {
            case 0:
                if (address < 0x3800) return alpha_ram_[offset];
                return mob_ram_[offset & 0x3ff];
            case 2: return playfield_top_[offset];
            case 3: return playfield_bottom_[offset];
            default: return 0xffff;
        }
    }
    if (address < 0x6000) return rom_[rom_bank_[0] + ((address & 0x1fff) >> 1)];
    if (address < 0x8000) return rom_[rom_bank_[1] + ((address & 0x1fff) >> 1)];
    if (address < 0x8200) {
        const uint16_t value = rom_[address >> 1];
        vram_bank_ = slapstic_.tweak(uint16_t((address & 0x1ff) >> 1));
        return value;
    }
    return rom_[address >> 1];
}

void AtariSystem2::main_write(uint16_t address, uint16_t value, uint16_t mem_mask) {
    auto combine = [&](uint16_t old) {
        return uint16_t((old & ~mem_mask) | (value & mem_mask));
    };
    // The 8 bit registers live on the low half of an even address, so a write
    // that only covers the odd half never reaches them.
    const bool low_byte = (mem_mask & 0x00ff) != 0;
    if (address < 0x1000) {
        ram_[address >> 1] = combine(ram_[address >> 1]);
        return;
    }
    if (address < 0x1400) {
        const int index = (address >> 1) & 0xff;
        set_palette(index, combine(palette_ram_[size_t(index)]));
        return;
    }
    if (address < 0x1480) {
        bank_select((address >> 1) & 1, value);
        return;
    }
    if (address < 0x1500) {
        if (low_byte) adc_value_ = adc_channel_value((address >> 1) & 7);
        return;
    }
    if (address < 0x1580) return;
    if (address < 0x15a0) {
        if (!low_byte) return;
        p2portrd_state_ = false;
        update_interrupts();
        return;
    }
    if (address < 0x15c0) {
        if (!low_byte) return;
        set_sound_reset((value & 1) != 0);
        // MAME sound_reset_w() also releases the sound chip reset line.
        write_sound_chip_reset(0);
        return;
    }
    if (address < 0x15e0) {
        if (!low_byte) return;
        scanline_int_state_ = false;
        update_interrupts();
        return;
    }
    if (address < 0x1600) {
        if (!low_byte) return;
        video_int_state_ = false;
        update_interrupts();
        return;
    }
    if (address < 0x1680) {
        if (low_byte) interrupt_enable_ = uint8_t(value & 0x0f);
        return;
    }
    if (address < 0x1700) {
        if (!low_byte) return;
        sound_latch_ = uint8_t(value);
        sound_pending_ = true;
        sound_cpu_.set_nmi(IrqLine::Assert);
        return;
    }
    if (address < 0x1780) {
        write_xscroll(combine(xscroll_));
        return;
    }
    if (address < 0x1800) {
        write_yscroll(combine(yscroll_reg_));
        return;
    }
    if (address < 0x2000) return;  // watchdog / sound response are read only
    if (address < 0x4000) {
        const uint16_t offset = uint16_t((address - 0x2000) >> 1);
        switch (vram_bank_) {
            case 0:
                if (address < 0x3800) {
                    const uint16_t data = combine(alpha_ram_[offset]);
                    if (alpha_ram_[offset] != data) {
                        alpha_ram_[offset] = data;
                        alpha_dirty_[offset] = true;
                    }
                } else {
                    const uint16_t index = uint16_t(offset & 0x3ff);
                    mob_ram_[index] = combine(mob_ram_[index]);
                }
                return;
            case 2: {
                const uint16_t data = combine(playfield_top_[offset]);
                if (playfield_top_[offset] != data) {
                    playfield_top_[offset] = data;
                    playfield_dirty_[offset] = true;
                }
                return;
            }
            case 3: {
                const uint16_t data = combine(playfield_bottom_[offset]);
                if (playfield_bottom_[offset] != data) {
                    playfield_bottom_[offset] = data;
                    playfield_dirty_[size_t(offset) + 0x1000] = true;
                }
                return;
            }
            default: return;
        }
    }
    if (address >= 0x8000 && address < 0x8200) {
        vram_bank_ = slapstic_.tweak(uint16_t((address & 0x1ff) >> 1));
    }
}

uint8_t AtariSystem2::sound_read(uint16_t address) {
    if ((address & ~0x2000) <= 0x0fff) return sound_memory_[address & 0x0fff];
    if (address >= 0x4000) return sound_memory_[address];
    const uint16_t base = address & ~0x2600;
    if (base >= 0x1000 && base <= 0x11ff) return eeprom_[address & 0x1ff];
    if ((address & ~0x2780) >= 0x1800 && (address & ~0x2780) <= 0x180f) {
        return pokey1_.read(address & 0x0f);
    }
    if ((address & ~0x278c) >= 0x1810 && (address & ~0x278c) <= 0x1813) {
        return leta_r(address & 3);
    }
    if ((address & ~0x2780) >= 0x1830 && (address & ~0x2780) <= 0x183f) {
        return pokey2_.read(address & 0x0f);
    }
    if ((address & ~0x278f) == 0x1840) return switch_6502_r();
    if ((address & ~0x278e) == 0x1850 || (address & ~0x278e) == 0x1851) return ym_.status();
    if ((address & ~0x278f) == 0x1860) {
        p2portrd_state_ = (interrupt_enable_ & 0x01) != 0;
        update_interrupts();
        sound_pending_ = false;
        sound_cpu_.set_nmi(IrqLine::Clear);
        return sound_latch_;
    }
    return 0xff;
}

void AtariSystem2::sound_write(uint16_t address, uint8_t value) {
    if ((address & ~0x2000) <= 0x0fff) {
        sound_memory_[address & 0x0fff] = value;
        return;
    }
    if (address >= 0x4000) return;
    const uint16_t base = address & ~0x2600;
    if (base >= 0x1000 && base <= 0x11ff) {
        eeprom_[address & 0x1ff] = value;
        return;
    }
    if ((address & ~0x2780) >= 0x1800 && (address & ~0x2780) <= 0x180f) {
        pokey1_.write(address & 0x0f, value);
        return;
    }
    if ((address & ~0x2780) >= 0x1830 && (address & ~0x2780) <= 0x183f) {
        pokey2_.write(address & 0x0f, value);
        return;
    }
    if ((address & ~0x278e) == 0x1850) {
        ym_.select_register(value);
        return;
    }
    if ((address & ~0x278e) == 0x1851) {
        ym_.write(value);
        return;
    }
    const uint16_t io = address & ~0x2781;
    switch (io) {
        case 0x1870:  // speech data
            if (has_tms_) tms_.set_data_latch(value);
            return;
        case 0x1874:  // response to the main CPU
            p2portwr_state_ = (interrupt_enable_ & 0x02) != 0;
            update_interrupts();
            main_latch_ = value;
            main_pending_ = true;
            return;
        case 0x1876: return;  // coin counters
        case 0x1878:
            sound_cpu_.set_irq(IrqLine::Clear);
            return;
        case 0x187a: return;  // mixer
        case 0x187c:
            // Speech clock select: MASTER_CLOCK/4 / (16 - (12 | bit 5)) / 2.
            if (has_tms_) {
                tms_.set_clock(kMasterClock / 4 / uint32_t(16 - (12 | ((value >> 5) & 1))) / 2);
            }
            return;
        case 0x187e:
            write_sound_chip_reset(value);
            return;
        default: break;
    }
    if ((address & ~0x2780) == 0x1872 || (address & ~0x2780) == 0x1873) {
        if (has_tms_) tms_.set_wsq((address & 1) == 0);
        return;
    }
}

void AtariSystem2::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles * 2);
    pokey1_.run(cycles);
    pokey2_.run(cycles);
    if (has_tms_) {
        const int tms_clocks =
            int((int64_t(cycles) * int64_t(tms_.clock()) + (kSoundClock / 2)) / kSoundClock);
        tms_.tick(tms_clocks);
    }

    // Periodic sound IRQ at MASTER_CLOCK/2/16/16/16/10 = 244.140625 Hz.
    sound_irq_counter_ += cycles;
    constexpr int kSoundIrqCycles = 7331;
    while (sound_irq_counter_ >= kSoundIrqCycles) {
        sound_irq_counter_ -= kSoundIrqCycles;
        sound_cpu_.set_irq(IrqLine::Assert);
    }

    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        const int32_t sample = ym_.update() + pokey1_.update() + pokey2_.update() +
                               (has_tms_ ? tms_.last_sample() : 0);
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void AtariSystem2::draw_alpha_tile(int offset) {
    const int x = offset % 64;
    const int y = offset / 64;
    if (y >= 48) return;
    const uint16_t data = alpha_ram_[size_t(offset)];
    const int color = (data >> 13) & 0x07;
    const int base = 64 + (color << 2);
    const uint8_t* pixels = alpha_gfx_.element(data & 0x3ff);
    for (int row = 0; row < 8; row++) {
        const size_t target = size_t((y * 8 + row) * kScreenWidth + x * 8);
        for (int column = 0; column < 8; column++) {
            const uint8_t pen = pixels[row * 8 + column];
            alpha_[target + size_t(column)] =
                pen == 0 ? kTransparent : int16_t(base + pen);
        }
    }
}

void AtariSystem2::draw_playfield_tile(int offset) {
    const int x = offset % 128;
    const int y = offset / 128;
    const uint16_t data = offset < 0x1000 ? playfield_top_[size_t(offset)]
                                          : playfield_bottom_[size_t(offset) & 0xfff];
    const int code = (int(playfield_tile_bank_[(data >> 10) & 1]) << 10) | (data & 0x3ff);
    const int color = (data >> 11) & 7;
    const uint8_t category = uint8_t((~data >> 14) & 3);
    const int base = 128 + (color << 4);
    const uint8_t* pixels = playfield_gfx_.element(code);
    for (int row = 0; row < 8; row++) {
        const size_t target = size_t((y * 8 + row) * kPlayfieldWidth + x * 8);
        for (int column = 0; column < 8; column++) {
            playfield_[target + size_t(column)] = uint16_t(base + pixels[row * 8 + column]);
            playfield_category_[target + size_t(column)] = category;
        }
    }
}

void AtariSystem2::draw_motion_objects() {
    std::fill(mo_pen_.begin(), mo_pen_.end(), kMoTransparent);
    std::fill(mo_priority_.begin(), mo_priority_.end(), 0);
    motion_objects_->draw(0, 0, -1,
                          [this](int code, int color, bool hflip, bool vflip, int x, int y, int gfx,
                                 int priority) {
                              (void)gfx;
                              const uint8_t* pixels = motion_gfx_.element(code);
                              for (int row = 0; row < 16; row++) {
                                  const int ty = y + row;
                                  if (ty < 0 || ty >= kScreenHeight) continue;
                                  const int src_row = vflip ? (15 - row) : row;
                                  for (int column = 0; column < 16; column++) {
                                      const int tx = x + column;
                                      if (tx < 0 || tx >= kScreenWidth) continue;
                                      const int src_col = hflip ? (15 - column) : column;
                                      const uint8_t pen = pixels[src_row * 16 + src_col];
                                      if (pen == 15) continue;
                                      const size_t index = size_t(ty * kScreenWidth + tx);
                                      mo_pen_[index] = uint16_t((color + pen) & 0xff);
                                      mo_priority_[index] = uint8_t(priority);
                                  }
                              }
                          });
}

void AtariSystem2::compose_frame() {
    for (int y = 0; y < kScreenHeight; y++) {
        const int sx = int(xscroll_line_[size_t(y)]) & (kPlayfieldWidth - 1);
        const int sy = int(yscroll_line_[size_t(y)]) & (kPlayfieldHeight - 1);
        const int py = (y + sy) & (kPlayfieldHeight - 1);
        for (int x = 0; x < kScreenWidth; x++) {
            const int px = (x + sx) & (kPlayfieldWidth - 1);
            const size_t pf_index = size_t(py * kPlayfieldWidth + px);
            const size_t screen = size_t(y * kScreenWidth + x);
            uint16_t pen = playfield_[pf_index];
            const uint16_t mo = mo_pen_[screen];
            if (mo != kMoTransparent) {
                const int mopriority = int(mo_priority_[screen]);
                if ((mopriority + int(playfield_category_[pf_index])) & 2) {
                    // High priority playfield: the object only wins when the
                    // playfield pen is below 8.
                    if ((pen & 0x08) == 0) pen = mo;
                } else {
                    pen = mo;
                }
            }
            const int16_t character = alpha_[screen];
            if (character != kTransparent) pen = uint16_t(character);
            // ROT270 on APB: the leftmost column becomes the bottom row.
            const size_t target =
                rotated_ ? size_t((kScreenWidth - 1 - x) * kScreenHeight + y) : screen;
            framebuffer_[target] = palette_[size_t(pen) & 0xff];
        }
    }
}

void AtariSystem2::update_video() {
    for (int offset = 0; offset < int(alpha_dirty_.size()); offset++) {
        if (!alpha_dirty_[size_t(offset)]) continue;
        draw_alpha_tile(offset);
        alpha_dirty_[size_t(offset)] = false;
    }
    for (int offset = 0; offset < int(playfield_dirty_.size()); offset++) {
        if (!playfield_dirty_[size_t(offset)]) continue;
        draw_playfield_tile(offset);
        playfield_dirty_[size_t(offset)] = false;
    }
    draw_motion_objects();
    compose_frame();
}

void AtariSystem2::run_frame() {
    const int main_cycles = int(double(kMainClock) / kFramesPerSecond / kScanlines + 0.5);
    const int sound_cycles = int(double(kSoundClock) / kFramesPerSecond / kScanlines + 0.5);

    if (yscroll_reset_) {
        yscroll_ = yscroll_pending_;
        yscroll_reset_ = false;
    }

    for (line_ = 0; line_ < kScanlines; line_++) {
        // The 32V interrupt is clocked every 64 scanlines.
        if ((line_ % 64) == 0) {
            scanline_int_state_ = (interrupt_enable_ & 0x04) != 0;
            update_interrupts();
        }
        main_cpu_.run(main_cycles);
        if (!sound_cpu_in_reset_) sound_cpu_.run(sound_cycles);
        if (line_ < kScreenHeight) {
            xscroll_line_[size_t(line_)] = uint16_t(xscroll_ >> 6);
            yscroll_line_[size_t(line_)] = yscroll_;
        }
        if (line_ == kScreenHeight) {
            update_video();
            video_int_state_ = (interrupt_enable_ & 0x08) != 0;
            update_interrupts();
        }
    }
    line_ = 0;
}

void AtariSystem2::set_inputs(const MachineInputs& inputs) {
    buttons_ = 0;
    switch (game_) {
        case Game::SuperSprint:
            // Bits 7/6/3 are the three start buttons.
            if (inputs.player1.start) buttons_ |= 0x80;
            if (inputs.player2.start) buttons_ |= 0x40;
            start3_ = inputs.player1.button3;
            break;
        case Game::Apb:
            // Bits 1/3 are the two extra buttons; there is no button on 6/7.
            if (inputs.player1.button2) buttons_ |= 0x02;
            if (inputs.player1.button3) buttons_ |= 0x08;
            break;
        case Game::Paperboy:
        case Game::Degrees720:
            if (inputs.player1.button1 || inputs.player1.start) buttons_ |= 0x80;
            if (inputs.player1.button2 || inputs.player2.start) buttons_ |= 0x40;
            break;
    }
    coin1_ = inputs.coin1;
    coin2_ = inputs.coin2;
    coin3_ = inputs.player2.button4;
    service_coin_ = inputs.player2.select;
    self_test_ = inputs.player1.select;
    update_analog(inputs);
}

void AtariSystem2::update_analog(const MachineInputs& inputs) {
    // The pedals are 6 bit pots read inverted, so a released pedal is $3f.
    auto pedal = [](bool pressed) { return uint8_t(pressed ? 0x00 : 0x3f); };
    // The steering wheels are quadrature dials counting up while turning right.
    auto turn = [](int& value, const InputState& player) {
        if (player.right && !player.left) value += 2;
        if (player.left && !player.right) value -= 2;
        return uint8_t(value);
    };
    switch (game_) {
        case Game::Paperboy: {
            // Paperboy steers with a pair of 8 bit pots limited to $10-$f0.
            uint8_t x = 0x80;
            uint8_t y = 0x80;
            if (inputs.player1.left && !inputs.player1.right) x = 0x10;
            if (inputs.player1.right && !inputs.player1.left) x = 0xf0;
            if (inputs.player1.up && !inputs.player1.down) y = 0x10;
            if (inputs.player1.down && !inputs.player1.up) y = 0xf0;
            adc_input_[0] = x;
            adc_input_[1] = y;
            break;
        }
        case Game::SuperSprint: {
            // Three cars: the pedals are the three buttons of the keyboard
            // player, and only the first steering wheel has arrow keys.
            adc_input_[0] = pedal(inputs.player1.button1);
            adc_input_[1] = pedal(inputs.player1.button2);
            adc_input_[2] = pedal(inputs.player1.button4);
            leta_input_[0] = turn(dial_, inputs.player1);
            break;
        }
        case Game::Apb: {
            adc_input_[1] = pedal(inputs.player1.button1);
            leta_input_[0] = turn(dial_, inputs.player1);
            break;
        }
        case Game::Degrees720: {
            // A held arrow key spins the disc at a comfortable few counts per
            // frame; a full turn of the real control is 144 counts.
            constexpr int kSpinSpeed = 3;
            int direction = 0;
            if (inputs.player1.right && !inputs.player1.left) direction = kSpinSpeed;
            if (inputs.player1.left && !inputs.player1.right) direction = -kSpinSpeed;
            update_spinner(direction);
            break;
        }
    }
}

// MAME leta_r(): the 720 controller has a 144 count rotation disc plus a center
// disc whose two teeth are seen as extra counts around positions 2/3 and
// 141/142.
void AtariSystem2::update_spinner(int direction) {
    const int steps = direction < 0 ? -direction : direction;
    const int sign = direction < 0 ? -1 : 1;
    for (int step = 0; step < steps; step++) {
        if (sign < 0) {
            spin_position_--;
            if (spin_position_ < 0) {
                spin_position_ = 143;
            } else if (spin_position_ == 2 || spin_position_ == 3 || spin_position_ == 141 ||
                       spin_position_ == 142) {
                spin_center_count_--;
            }
        } else {
            spin_position_++;
            if (spin_position_ > 143) {
                spin_position_ = 0;
            } else if (spin_position_ == 2 || spin_position_ == 3 || spin_position_ == 141 ||
                       spin_position_ == 142) {
                spin_center_count_++;
            }
        }
        spin_rotate_count_ = uint8_t(int(spin_rotate_count_) + sign);
    }
}

void AtariSystem2::set_dip_switch(int bank, uint8_t value) {
    if (bank < 0 || bank > 1) return;
    dsw_[bank] = value;
}

void AtariSystem2::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
