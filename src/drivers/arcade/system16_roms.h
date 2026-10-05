#pragma once

#include "core/rom_loader.h"

#include <vector>

namespace dsp {
namespace system16_roms {

// Golden Axe (MCU, matches Pascal dsp-emulator)
inline const std::vector<RomEntry> kGoldnaxeMain = {
    {"epr-12545.ic2", 0x40000, 0x00000, 0xa97c4e4d},
    {"epr-12544.ic1", 0x40000, 0x00001, 0x5e38f668},
};
inline const std::vector<RomEntry> kGoldnaxeSound = {
    {"epr-12390.ic8", 0x8000, 0x0000, 0x399fc5f5},
    {"mpr-12384.ic6", 0x20000, 0x8000, 0x6218d8e7},
};
inline const std::vector<RomEntry> kGoldnaxeMcu = {
    {"317-0123a.c2", 0x1000, 0x0000, 0xcf19e7d4},
};
inline const std::vector<RomEntry> kGoldnaxeTiles = {
    {"epr-12385.ic19", 0x20000, 0x00000, 0xb8a4e7e0},
    {"epr-12386.ic20", 0x20000, 0x20000, 0x25d7d779},
    {"epr-12387.ic21", 0x20000, 0x40000, 0xc7fcadf3},
};
inline const std::vector<RomEntry> kGoldnaxeSprites = {
    {"mpr-12378.ic9", 0x40000, 0x00001, 0x119e5a82},
    {"mpr-12379.ic12", 0x40000, 0x00000, 0x1a0e8c57},
    {"mpr-12380.ic10", 0x40000, 0x80001, 0xbb2c0853},
    {"mpr-12381.ic13", 0x40000, 0x80000, 0x81ba6ecc},
    {"mpr-12382.ic11", 0x40000, 0x100001, 0x81601c6f},
    {"mpr-12383.ic14", 0x40000, 0x100000, 0x5dbacf7a},
};

// Dynamite Dux (FD1094 MAME set)
inline const std::vector<RomEntry> kDduxMain = {
    {"epr-11191.a7", 0x20000, 0x00000, 0x500e400a},
    {"epr-11190.a5", 0x20000, 0x00001, 0x2a698308},
    {"epr-11915.a8", 0x20000, 0x40000, 0xd8ed3132},
    {"epr-11913.a6", 0x20000, 0x40001, 0x30c6cb92},
};
inline const std::vector<RomEntry> kDduxKey = {
    {"317-0096.key", 0x2000, 0x0000, 0x6fd7d26e},
};
inline const std::vector<RomEntry> kDduxSound = {
    {"epr-11916.a10", 0x8000, 0x0000, 0x7ab541cf},
};
inline const std::vector<RomEntry> kDduxTiles = {
    {"mpr-11917.a14", 0x10000, 0x00000, 0x6f772190},
    {"mpr-11918.a15", 0x10000, 0x10000, 0xc731db95},
    {"mpr-11919.a16", 0x10000, 0x20000, 0x64d5a491},
};
inline const std::vector<RomEntry> kDduxSprites = {
    {"mpr-11920.b1", 0x20000, 0x00001, 0xe5d1e3cd},
    {"mpr-11922.b5", 0x20000, 0x00000, 0x70b0c4dd},
    {"mpr-11921.b2", 0x20000, 0x40001, 0x61d2358c},
    {"mpr-11923.b6", 0x20000, 0x40000, 0xc9ffe47d},
};

// E-Swat (FD1094)
inline const std::vector<RomEntry> kEswatMain = {
    {"epr-12659.a2", 0x40000, 0x00000, 0xc5ab2db9},
    {"epr-12658.a1", 0x40000, 0x00001, 0xaf40bd71},
};
inline const std::vector<RomEntry> kEswatKey = {
    {"317-0130.key", 0x2000, 0x0000, 0xba7b717b},
};
inline const std::vector<RomEntry> kEswatSound = {
    {"epr-12617.a13", 0x8000, 0x0000, 0x7efecf23},
    {"mpr-12616.a11", 0x40000, 0x8000, 0x254347c2},
};
inline const std::vector<RomEntry> kEswatTiles = {
    {"mpr-12624.b11", 0x40000, 0x00000, 0x375a5ec4},
    {"mpr-12625.b12", 0x40000, 0x40000, 0x3b8c757e},
    {"mpr-12626.b13", 0x40000, 0x80000, 0x3efca25c},
};
inline const std::vector<RomEntry> kEswatSprites = {
    {"mpr-12618.b1", 0x40000, 0x00001, 0x0d1530bf},
    {"mpr-12621.b4", 0x40000, 0x00000, 0x18ff0799},
    {"mpr-12619.b2", 0x40000, 0x80001, 0x32069246},
    {"mpr-12622.b5", 0x40000, 0x80000, 0xa3dfe436},
    {"mpr-12620.b3", 0x40000, 0x100001, 0xf6b096e0},
    {"mpr-12623.b6", 0x40000, 0x100000, 0x6773fef6},
};

// Passing Shot (FD1094)
inline const std::vector<RomEntry> kPassshtMain = {
    {"epr-11871.a4", 0x10000, 0x00000, 0x0f9ccea5},
    {"epr-11870.a1", 0x10000, 0x00001, 0xdf43ebcf},
};
inline const std::vector<RomEntry> kPassshtKey = {
    {"317-0080.key", 0x2000, 0x0000, 0x222d016f},
};
inline const std::vector<RomEntry> kPassshtSound = {
    {"epr-11857.a7", 0x8000, 0x00000, 0x789edc06},
    {"epr-11858.a8", 0x8000, 0x08000, 0x08ab0018},
    {"epr-11859.a9", 0x8000, 0x18000, 0x8673e01b},
    {"epr-11860.a10", 0x8000, 0x28000, 0x10263746},
    {"epr-11861.a11", 0x8000, 0x38000, 0x38b54a71},
};
inline const std::vector<RomEntry> kPassshtTiles = {
    {"opr-11854.b9", 0x10000, 0x00000, 0xd31c0b6c},
    {"opr-11855.b10", 0x10000, 0x10000, 0xb78762b4},
    {"opr-11856.b11", 0x10000, 0x20000, 0xea49f666},
};
inline const std::vector<RomEntry> kPassshtSprites = {
    {"opr-11862.b1", 0x10000, 0x00001, 0xb6e94727},
    {"opr-11865.b5", 0x10000, 0x00000, 0x17e8d5d5},
    {"opr-11863.b2", 0x10000, 0x20001, 0x3e670098},
    {"opr-11866.b6", 0x10000, 0x20000, 0x50eb71cc},
    {"opr-11864.b3", 0x10000, 0x40001, 0x05733ca8},
    {"opr-11867.b7", 0x10000, 0x40000, 0x81e49697},
};

// Aurail (unencrypted, matches Pascal)
inline const std::vector<RomEntry> kAurailMain = {
    {"epr-13577.a7", 0x20000, 0x00000, 0x6701b686},
    {"epr-13576.a5", 0x20000, 0x00001, 0x1e428d94},
    {"epr-13447.a8", 0x20000, 0x40000, 0x70a52167},
    {"epr-13445.a6", 0x20000, 0x40001, 0x28dfc3dd},
};
inline const std::vector<RomEntry> kAurailSound = {
    {"epr-13448.a10", 0x8000, 0x0000, 0xb5183fb9},
    {"mpr-13449.a11", 0x20000, 0x8000, 0xd3d9aaf9},
};
inline const std::vector<RomEntry> kAurailTiles = {
    {"mpr-13450.a14", 0x20000, 0x00000, 0x0fc4a7a8},
    {"mpr-13465.b14", 0x20000, 0x20000, 0xe08135e0},
    {"mpr-13451.a15", 0x20000, 0x40000, 0x1c49852f},
    {"mpr-13466.b15", 0x20000, 0x60000, 0xe14c6684},
    {"mpr-13452.a16", 0x20000, 0x80000, 0x047bde5e},
    {"mpr-13467.b16", 0x20000, 0xa0000, 0x6309fec4},
};
inline const std::vector<RomEntry> kAurailSprites = {
    {"mpr-13453.b1", 0x20000, 0x00001, 0x5fa0a9f8},
    {"mpr-13457.b5", 0x20000, 0x00000, 0x0d1b54da},
    {"mpr-13454.b2", 0x20000, 0x40001, 0x5f6b33b1},
    {"mpr-13458.b6", 0x20000, 0x40000, 0xbad340c3},
    {"mpr-13455.b3", 0x20000, 0x80001, 0x4e80520b},
    {"mpr-13459.b7", 0x20000, 0x80000, 0x7e9165ac},
    {"mpr-13456.b4", 0x20000, 0xc0001, 0x5733c428},
    {"mpr-13460.b8", 0x20000, 0xc0000, 0x66b8f9b3},
    {"mpr-13440.a1", 0x20000, 0x100001, 0x4f370b2b},
    {"mpr-13461.b10", 0x20000, 0x100000, 0xf76014bf},
    {"mpr-13441.a2", 0x20000, 0x140001, 0x37cf9cb4},
    {"mpr-13462.b11", 0x20000, 0x140000, 0x1061e7da},
    {"mpr-13442.a3", 0x20000, 0x180001, 0x049698ef},
    {"mpr-13463.b12", 0x20000, 0x180000, 0x7dbcfbf1},
    {"mpr-13443.a4", 0x20000, 0x1c0001, 0x77a8989e},
    {"mpr-13464.b13", 0x20000, 0x1c0000, 0x551df422},
};

// Riot City (sprites remapped by driver like Golden Axe)
inline const std::vector<RomEntry> kRiotcityMain = {
    {"epr-14612.a7", 0x20000, 0x00000, 0xa1b331ec},
    {"epr-14610.a5", 0x20000, 0x00001, 0xcd4f2c50},
    {"epr-14613.a8", 0x20000, 0x40000, 0x0659df4c},
    {"epr-14611.a6", 0x20000, 0x40001, 0xd9e6f80b},
};
inline const std::vector<RomEntry> kRiotcitySound = {
    {"epr-14614.a10", 0x10000, 0x0000, 0xc65cc69a},
    {"epr-14615.a11", 0x20000, 0x10000, 0x46653db1},
};
inline const std::vector<RomEntry> kRiotcityTiles = {
    {"epr-14616.a14", 0x20000, 0x00000, 0x46d30368},
    {"epr-14625.b14", 0x20000, 0x20000, 0xabfb80fe},
    {"epr-14617.a15", 0x20000, 0x40000, 0x884e40f9},
    {"epr-14626.b15", 0x20000, 0x60000, 0x4ef55846},
    {"epr-14618.a16", 0x20000, 0x80000, 0x00eb260e},
    {"epr-14627.b16", 0x20000, 0xa0000, 0x961e5f82},
};
inline const std::vector<RomEntry> kRiotcitySprites = {
    {"epr-14619.b1", 0x40000, 0x00001, 0x6f2b5ef7},
    {"epr-14622.b5", 0x40000, 0x00000, 0x7ca7e40d},
    {"epr-14620.b2", 0x40000, 0x80001, 0x66183333},
    {"epr-14623.b6", 0x40000, 0x80000, 0x98630049},
    {"epr-14621.b3", 0x40000, 0x100001, 0xc0f2820e},
    {"epr-14624.b7", 0x40000, 0x100000, 0xd1a68448},
};

// SDI (sdib Pascal set, sdib.zip names with MAME sdi alternates)
inline const std::vector<RomEntry> kSdiMain = {
    {"epr-10986a.a4", 0x8000, 0x00000, 0x3e136215},
    {"epr-10984a.a1", 0x8000, 0x00001, 0x44bf3cf5},
    {"epr-10987a.a5", 0x8000, 0x10000, 0xcfd79404},
    {"epr-10985a.a2", 0x8000, 0x10001, 0x1c21a03f},
    {"epr-10829.a6", 0x8000, 0x20000, 0xa431ab08},
    {"epr-10826.a3", 0x8000, 0x20001, 0x2ed8e4b7},
};
inline const std::vector<RomEntry> kSdiKey = {
    {"317-0028.key", 0x2000, 0x0000, 0x1514662f},
};
inline const std::vector<RomEntry> kSdiSound = {
    {"epr-10775.a7|10775.a7", 0x8000, 0x0000, 0x4cbd55a8},
};
inline const std::vector<RomEntry> kSdiTiles = {
    {"epr-10772.b9", 0x10000, 0x00000, 0x182b6301},
    {"epr-10773.b10", 0x10000, 0x10000, 0x8f7129a2},
    {"epr-10774.b11", 0x10000, 0x20000, 0x4409411f},
};
inline const std::vector<RomEntry> kSdiSprites = {
    {"epr-10760.b1|10760.b1", 0x10000, 0x00001, 0x70de327b},
    {"epr-10763.b5|10763.b5", 0x10000, 0x00000, 0x99ec5cb5},
    {"epr-10761.b2|10761.b2", 0x10000, 0x20001, 0x4e80f80d},
    {"epr-10764.b6|10764.b6", 0x10000, 0x20000, 0x602da5d5},
    {"epr-10762.b3|10762.b3", 0x10000, 0x40001, 0x464b5f78},
    {"epr-10765.b7|10765.b7", 0x10000, 0x40000, 0x0a73a057},
};

// Cotton (FD1094 MAME main ROMs, Pascal gfx/sound)
inline const std::vector<RomEntry> kCottonMain = {
    {"epr-13921a.a7", 0x20000, 0x00000, 0xf047a037},
    {"epr-13919a.a5", 0x20000, 0x00001, 0x651108b1},
    {"epr-13922a.a8", 0x20000, 0x40000, 0x1ca248c5},
    {"epr-13920a.a6", 0x20000, 0x40001, 0xfa3610f9},
};
inline const std::vector<RomEntry> kCottonKey = {
    {"317-0181a.key", 0x2000, 0x0000, 0x5c419b36},
};
inline const std::vector<RomEntry> kCottonSound = {
    {"epr-13892.a10", 0x8000, 0x0000, 0xfdfbe6ad},
    {"opr-13893.a11", 0x20000, 0x8000, 0x384233df},
};
inline const std::vector<RomEntry> kCottonTiles = {
    {"opr-13862.a14", 0x20000, 0x00000, 0xa47354b6},
    {"opr-13877.b14", 0x20000, 0x20000, 0xd38424b5},
    {"opr-13863.a15", 0x20000, 0x40000, 0x8c990026},
    {"opr-13878.b15", 0x20000, 0x60000, 0x21c15b8a},
    {"opr-13864.a16", 0x20000, 0x80000, 0xd2b175bf},
    {"opr-13879.b16", 0x20000, 0xa0000, 0xb9d62531},
};
inline const std::vector<RomEntry> kCottonSprites = {
    {"opr-13865.b1", 0x20000, 0x00001, 0x7024f404},
    {"opr-13869.b5", 0x20000, 0x00000, 0xab4b3468},
    {"opr-13866.b2", 0x20000, 0x40001, 0x6169bba4},
    {"opr-13870.b6", 0x20000, 0x40000, 0x69b41ac3},
    {"opr-13867.b3", 0x20000, 0x80001, 0xb014f02d},
    {"opr-13871.b7", 0x20000, 0x80000, 0x0801cf02},
    {"opr-13868.b4", 0x20000, 0xc0001, 0xe62a7cd6},
    {"opr-13872.b8", 0x20000, 0xc0000, 0xf066f315},
    {"opr-13852.a1", 0x20000, 0x100001, 0x943aba8b},
    {"opr-13873.b10", 0x20000, 0x100000, 0x1bd145f3},
    {"opr-13853.a2", 0x20000, 0x140001, 0x7ea93200},
    {"opr-13874.b11", 0x20000, 0x140000, 0x4fd59bff},
    {"opr-13891.a3", 0x20000, 0x180001, 0xc6b3c414},
    {"opr-13894.b12", 0x20000, 0x180000, 0xe3d0bee2},
    {"opr-13855.a4", 0x20000, 0x1c0001, 0x856f3ee2},
    {"opr-13876.b13", 0x20000, 0x1c0000, 0x1c5ffad8},
};

// Bay Route (FD1094)
inline const std::vector<RomEntry> kBayrouteMain = {
    {"epr-12517.a7", 0x20000, 0x00000, 0x436728a9},
    {"epr-12516.a5", 0x20000, 0x00001, 0x4ff0353f},
    {"epr-12458.a8", 0x20000, 0x40000, 0xe7c7476a},
    {"epr-12456.a6", 0x20000, 0x40001, 0x25dc2eaf},
};
inline const std::vector<RomEntry> kBayrouteKey = {
    {"317-0116.key", 0x2000, 0x0000, 0x8778ee49},
};
inline const std::vector<RomEntry> kBayrouteSound = {
    {"epr-12459.a10", 0x8000, 0x0000, 0x3e1d29d0},
    {"mpr-12460.a11", 0x20000, 0x8000, 0x0bae570d},
    {"mpr-12461.a12", 0x20000, 0x28000, 0xb03b8b46},
};
inline const std::vector<RomEntry> kBayrouteTiles = {
    {"opr-12462.a14", 0x10000, 0x00000, 0xa19943b5},
    {"opr-12463.a15", 0x10000, 0x10000, 0x62f8200d},
    {"opr-12464.a16", 0x10000, 0x20000, 0xc8c59703},
};
inline const std::vector<RomEntry> kBayrouteSprites = {
    {"mpr-12465.b1", 0x20000, 0x00001, 0x11d61b45},
    {"mpr-12467.b5", 0x20000, 0x00000, 0xc3b4e4c0},
    {"mpr-12466.b2", 0x20000, 0x40001, 0xa57f236f},
    {"mpr-12468.b6", 0x20000, 0x40000, 0xd89c77de},
};

// Sonic Boom (FD1094)
inline const std::vector<RomEntry> kSonicbomMain = {
    {"epr-11342.a4", 0x10000, 0x00000, 0x454693f1},
    {"epr-11340.a1", 0x10000, 0x00001, 0x03ba3fed},
    {"epr-11343.a5", 0x10000, 0x20000, 0xedfeb7d4},
    {"epr-11341.a2", 0x10000, 0x20001, 0x0338f771},
};
inline const std::vector<RomEntry> kSonicbomKey = {
    {"317-0053.key", 0x2000, 0x0000, 0x91c80c88},
};
inline const std::vector<RomEntry> kSonicbomSound = {
    {"epr-11347.a7", 0x8000, 0x0000, 0xb41f0ced},
    {"epr-11348.a8", 0x8000, 0x8000, 0x89924588},
    {"epr-11349.a9", 0x8000, 0x10000, 0x8e4b6204},
};
inline const std::vector<RomEntry> kSonicbomTiles = {
    {"opr-11344.b9", 0x10000, 0x00000, 0x59a9f940},
    {"opr-11345.b10", 0x10000, 0x10000, 0xb44c068b},
    {"opr-11346.b11", 0x10000, 0x20000, 0xe5ada66c},
};
inline const std::vector<RomEntry> kSonicbomSprites = {
    {"opr-11350.b1", 0x10000, 0x00001, 0x525ba1df},
    {"opr-11354.b5", 0x10000, 0x00000, 0x793fa3ac},
    {"opr-11351.b2", 0x10000, 0x20001, 0x63b1f1ca},
    {"opr-11355.b6", 0x10000, 0x20000, 0xfe0fa332},
    {"opr-11352.b3", 0x10000, 0x40001, 0x047fa4b0},
    {"opr-11356.b7", 0x10000, 0x40000, 0xaea3c39d},
    {"opr-11353.b4", 0x10000, 0x60001, 0x4e0791f8},
    {"opr-11357.b8", 0x10000, 0x60000, 0xa7c5ea41},
};

// Time Scanner (matches Pascal)
inline const std::vector<RomEntry> kTimescanMain = {
    {"epr-10853.a4", 0x8000, 0x00000, 0x24d7c5fb},
    {"epr-10850.a1", 0x8000, 0x00001, 0xf1575732},
    {"epr-10854.a5", 0x8000, 0x10000, 0x82d0b237},
    {"epr-10851.a2", 0x8000, 0x10001, 0xf5ce271b},
    {"epr-10855.a6", 0x8000, 0x20000, 0x63e95a53},
    {"epr-10852.a3", 0x8000, 0x20001, 0x7cd1382b},
};
inline const std::vector<RomEntry> kTimescanSound = {
    {"epr-10562.a7", 0x8000, 0x0000, 0x3f5028bf},
    {"epr-10563.a8", 0x8000, 0x10000, 0x9db7eddf},
};
inline const std::vector<RomEntry> kTimescanTiles = {
    {"epr-10543.b9", 0x8000, 0x0000, 0x07dccc37},
    {"epr-10544.b10", 0x8000, 0x8000, 0x84fb9a3a},
    {"epr-10545.b11", 0x8000, 0x10000, 0xc8694bc0},
};
inline const std::vector<RomEntry> kTimescanSprites = {
    {"epr-10548.b1", 0x8000, 0x00001, 0xaa150735},
    {"epr-10552.b5", 0x8000, 0x00000, 0x6fcbb9f7},
    {"epr-10549.b2", 0x8000, 0x20001, 0x2f59f067},
    {"epr-10553.b6", 0x8000, 0x20000, 0x8a220a9f},
    {"epr-10550.b3", 0x8000, 0x40001, 0xf05069ff},
    {"epr-10554.b7", 0x8000, 0x40000, 0xdc64f809},
    {"epr-10551.b4", 0x8000, 0x60001, 0x435d811f},
    {"epr-10555.b8", 0x8000, 0x60000, 0x2143c471},
};

}  // namespace system16_roms
}  // namespace dsp
