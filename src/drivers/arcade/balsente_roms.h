#pragma once

#include <cstdint>
#include <vector>

#include "core/rom_loader.h"

namespace dsp {
namespace balsente_roms {

// Sente Diagnostic Cartridge — MAME sentetst
inline const std::vector<RomEntry> kSentetstMain = {
    {"sdiagef.bin", 0x2000, 0x1e000, 0x2a39fc53},
};
inline const std::vector<RomEntry> kSentetstSprites = {
    {"sdiaggr0.bin", 0x2000, 0x0000, 0x5e0ff62a},
};

// Chicken Shift — MAME cshift
inline const std::vector<RomEntry> kCshiftMain = {
    {"chicken_shift_ab_0_11-23-84.u9a", 0x2000, 0x0000, 0xd2069e75},
    {"chicken_shift_ab_1_11-23-84.u8a", 0x2000, 0x2000, 0x198f25a8},
    {"chicken_shift_ab_2_11-23-84.u7a", 0x2000, 0x4000, 0x2e2b2b82},
    {"chicken_shift_ab_3_11-23-84.u6a", 0x2000, 0x6000, 0xb97fc520},
    {"chicken_shift_ab_4_11-23-84.u5a", 0x2000, 0x8000, 0xb4f0d673},
    {"chicken_shift_ab_5_11-23-84.u4a", 0x2000, 0xa000, 0xb1f8e589},
    {"chicken_shift_cd_11-23-84.u3a", 0x2000, 0x1c000, 0xf555a0b2},
    {"chicken_shift_ef_11-23-84.u2a", 0x2000, 0x1e000, 0x368b1ce3},
};
inline const std::vector<RomEntry> kCshiftSprites = {
    {"chicken_shift_gr-0_11-23-84.u9b", 0x2000, 0x0000, 0x67f9d3b3},
    {"chicken_shift_gr-1_11-23-84.u8b", 0x2000, 0x2000, 0x78973d50},
    {"chicken_shift_gr-2_11-23-84.u7b", 0x2000, 0x4000, 0x1784f939},
    {"chicken_shift_gr-3_11-23-84.u6b", 0x2000, 0x6000, 0xb43916a2},
    {"chicken_shift_gr-4_11-23-84.u5b", 0x2000, 0x8000, 0xa94cd35b},
};

// Hat Trick — MAME hattrick
inline const std::vector<RomEntry> kHattrickMain = {
    {"hat_trk_ab0_11-12-84.u9a", 0x2000, 0x0000, 0xf25c1b99},
    {"hat_trk_ab1_11-12-84.u8a", 0x2000, 0x2000, 0xc1df3d1f},
    {"hat_trk_ab2_11-12-84.u7a", 0x2000, 0x4000, 0xf6c41257},
    {"hat_trk_cd_11-12-84.u3a", 0x2000, 0x1c000, 0xfc44f36c},
    {"hat_trk_ef_11-12-84.u2a", 0x2000, 0x1e000, 0xd8f910fb},
};
inline const std::vector<RomEntry> kHattrickSprites = {
    {"hat_trk_gr0_11-12-84.u9b", 0x2000, 0x0000, 0x9f41baba},
    {"hat_trk_gr1_11-12-84.u8b", 0x2000, 0x2000, 0x951f08c9},
};

// Goalie Ghost — MAME gghost
inline const std::vector<RomEntry> kGghostMain = {
    {"ggh-ab0.u9a", 0x2000, 0x0000, 0xed0fdeac},
    {"ggh-ab1.u8a", 0x2000, 0x2000, 0x5bfbae58},
    {"ggh-ab2.u7a", 0x2000, 0x4000, 0xf0baf921},
    {"ggh-ab3.u6a", 0x2000, 0x6000, 0xed0fdeac},
    {"ggh-ab4.u5a", 0x2000, 0x8000, 0x5bfbae58},
    {"ggh-ab5.u4a", 0x2000, 0xa000, 0xf0baf921},
    {"ggh-cd.u3a", 0x2000, 0x1c000, 0xd3d75f84},
    {"ggh-ef.u2a", 0x2000, 0x1e000, 0xa02b4243},
};
inline const std::vector<RomEntry> kGghostSprites = {
    {"ggh-gr0.u8b", 0x2000, 0x0000, 0x03515526},
    {"ggh-gr1.u8b", 0x2000, 0x2000, 0xb4293435},
    {"ggh-gr2.u7b", 0x2000, 0x4000, 0xece0cb97},
    {"ggh-gr3.u6b", 0x2000, 0x6000, 0xdd7e25d0},
    {"ggh-gr4.u5b", 0x2000, 0x8000, 0xb4293435},
    {"ggh-gr5.u4b", 0x2000, 0xa000, 0xd3da0093},
};

// Off the Wall — MAME otwalls
inline const std::vector<RomEntry> kOtwallsMain = {
    {"off_the_wall_ab0_10-16-84.u9a", 0x2000, 0x0000, 0x474441c7},
    {"off_the_wall_ab1_10-16-84.u8a", 0x2000, 0x2000, 0x2e9e9411},
    {"off_the_wall_ab2_10-16-84.u7a", 0x2000, 0x4000, 0xba092128},
    {"off_the_wall_ab3_10-16-84.u6a", 0x2000, 0x6000, 0x74bc479d},
    {"off_the_wall_ab4_10-16-84.u5a", 0x2000, 0x8000, 0xf5f67619},
    {"off_the_wall_ab5_10-16-84.u4a", 0x2000, 0xa000, 0xf5f67619},
    {"off_the_wall_cd_10-16-84.u3a", 0x2000, 0x1c000, 0x8e2d15ab},
    {"off_the_wall_ef_10-16-84.u2a", 0x2000, 0x1e000, 0x57eab299},
};
inline const std::vector<RomEntry> kOtwallsSprites = {
    {"off_the_wall_gr0_10-16-84.u9b", 0x2000, 0x0000, 0x210bad3c},
    {"off_the_wall_gr1_10-16-84.u8b", 0x2000, 0x2000, 0x13e6aaa5},
    {"off_the_wall_gr2_10-16-84.u7b", 0x2000, 0x4000, 0x5cfefee5},
    {"off_the_wall_gr3_10-16-84.u6b", 0x2000, 0x6000, 0x6b17e4a9},
    {"off_the_wall_gr4_10-16-84.u5b", 0x2000, 0x8000, 0x15985c8c},
    {"off_the_wall_gr5_10-16-84.u4b", 0x2000, 0xa000, 0x448f7e3c},
};

// Snake Pit — MAME snakepit
inline const std::vector<RomEntry> kSnakepitMain = {
    {"spit-ab0.u9a", 0x2000, 0x0000, 0x5aa86081},
    {"spit-ab1.u8a", 0x2000, 0x2000, 0x588228b8},
    {"spit-ab2.u7a", 0x2000, 0x4000, 0x60173ab6},
    {"spit-ab3.u6a", 0x2000, 0x6000, 0x56cb51a8},
    {"spit-ab4.u5a", 0x2000, 0x8000, 0x40ba61e0},
    {"spit-ab5.u4a", 0x2000, 0xa000, 0x2a1d9d8f},
    {"spit-cd.u3a", 0x2000, 0x1c000, 0x54095cbb},
    {"spit-ef.u2a", 0x2000, 0x1e000, 0x5f836a66},
};
inline const std::vector<RomEntry> kSnakepitSprites = {
    {"spit-gr0.u9b", 0x2000, 0x0000, 0xf77fd85d},
    {"spit-gr1.u8b", 0x2000, 0x2000, 0x3ad10334},
    {"spit-gr2.u7b", 0x2000, 0x4000, 0x24887703},
    {"spit-gr3.u6b", 0x2000, 0x6000, 0xc6703ec2},
    {"spit-gr4.u5b", 0x2000, 0x8000, 0xb4293435},
    {"spit-gr5.u4b", 0x2000, 0xa000, 0xdc27c970},
};

// Trivial Pursuit — MAME triviag1
inline const std::vector<RomEntry> kTriviag1Main = {
    {"t.prst_u9a_2-12-85.u9a", 0x2000, 0x0000, 0x79fd3ac3},
    {"t.prst_u8a_2-12-85.u8a", 0x2000, 0x2000, 0x0ff677e9},
    {"t.prst_u7a_2-12-85.u7a", 0x2000, 0x4000, 0x3b4d03e7},
    {"t.prst_u6a_2-12-85.u6a", 0x2000, 0x6000, 0x2c6c0651},
    {"t.prst_u5a_2-12-85.u5a", 0x2000, 0x8000, 0x397529e7},
    {"t.prst_u4a_2-12-85.u4a", 0x2000, 0xa000, 0x499773a4},
    {"t.prst_u3a_2-12-85.u3a", 0x2000, 0x1c000, 0x35c9b9c2},
    {"t.prst_u2a_2-12-85.u2a", 0x2000, 0x1e000, 0x64878342},
};
inline const std::vector<RomEntry> kTriviag1Sprites = {
    {"t.prst_u9b_2-12-85.u9b", 0x2000, 0x0000, 0x20c9217a},
    {"t.prst_u8b_2-12-85.u8b", 0x2000, 0x2000, 0xd7f44504},
    {"t.prst_u7b_2-12-85.u7b", 0x2000, 0x4000, 0x4e59a15d},
    {"t.prst_u6b_2-12-85.u6b", 0x2000, 0x6000, 0x323a8640},
    {"t.prst_u5b_2-12-85.u5b", 0x2000, 0x8000, 0x673acf42},
    {"t.prst_u4b_2-12-85.u4b", 0x2000, 0xa000, 0x067bfd66},
};

// Snacks'n Jaxson — MAME snakjack
inline const std::vector<RomEntry> kSnakjackMain = {
    {"rom-ab0.u9a", 0x2000, 0x0000, 0xda2dd119},
    {"rom-ab1.u8a", 0x2000, 0x2000, 0x657ddf26},
    {"rom-ab2.u7a", 0x2000, 0x4000, 0x15333dcf},
    {"rom-ab3.u6a", 0x2000, 0x6000, 0x57671f6f},
    {"rom-ab4.u5a", 0x2000, 0x8000, 0xc16c5dc0},
    {"rom-ab5.u4a", 0x2000, 0xa000, 0xd7019747},
    {"rom-cd.u3a", 0x2000, 0x1c000, 0x7b44ca4c},
    {"rom-ef.u1a", 0x2000, 0x1e000, 0xf5309b38},
};
inline const std::vector<RomEntry> kSnakjackSprites = {
    {"rom-gr0.u9b", 0x2000, 0x0000, 0x3e64b5d5},
    {"rom-gr1.u8b", 0x2000, 0x2000, 0xb3b8baee},
    {"rom-gr2.u7b", 0x2000, 0x4000, 0xe9d89dac},
    {"rom-gr3.u6b", 0x2000, 0x6000, 0xb6602be8},
    {"rom-gr4.u5b", 0x2000, 0x8000, 0x3fbfa686},
    {"rom-gr5.u4b", 0x2000, 0xa000, 0x345f94fb},
};

// Stocker — MAME stocker
inline const std::vector<RomEntry> kStockerMain = {
    {"stocker_ab_01_3-19-85.u8a", 0x4000, 0x0000, 0x6a914d99},
    {"stocker_ab_23_3-19-85.u7a", 0x4000, 0x4000, 0x48e432c2},
    {"stocker_ef_3-19-85.u1a", 0x4000, 0x1c000, 0x83e6e5c9},
};
inline const std::vector<RomEntry> kStockerSprites = {
    {"stocker_gr_01_3-19-85.u6b", 0x4000, 0x0000, 0x2e66ac35},
    {"stocker_gr_23_3-19-85.u5b", 0x4000, 0x4000, 0x6fa43631},
};

// Trivial Pursuit — MAME triviabb
inline const std::vector<RomEntry> kTriviabbMain = {
    {"b.boomer_rom_ab01r_3-20-85.u8a", 0x4000, 0x0000, 0x1b7c439d},
    {"b.boomer_rom_ab23r_3-20-85.u7a", 0x4000, 0x4000, 0xe4f1e704},
    {"b.boomer_rom_ab45r_3-20-85.u6a", 0x4000, 0x8000, 0xdaa2d8bc},
    {"b.boomer_rom_ab67r_3-20-85.u5a", 0x4000, 0xc000, 0x3622c4f1},
    {"b.boomer_rom_cd45r_3-20-85.u2a", 0x4000, 0x18000, 0x07fd88ff},
    {"b.boomer_rom_cd6efr_3-20-85.u1a", 0x4000, 0x1c000, 0x2d03f241},
};
inline const std::vector<RomEntry> kTriviabbSprites = {
    {"b.boomer_gr01r_3-20-85.u6b", 0x4000, 0x0000, 0x6829de8e},
    {"b.boomer_gr23r_3-20-85.u5b", 0x4000, 0x4000, 0x89398700},
    {"b.boomer_gr45r_3-20-85.u4b", 0x4000, 0x8000, 0x92fb6fb1},
};

// Trivial Pursuit — MAME triviag2
inline const std::vector<RomEntry> kTriviag2Main = {
    {"genus_ii_ab01_r_3-22-85.u8a", 0x4000, 0x0000, 0x4fca20c5},
    {"genus_ii_ab23_r_3-22-85.u7a", 0x4000, 0x4000, 0x6cf2ddeb},
    {"genus_ii_ab45_r_3-22-85.u6a", 0x4000, 0x8000, 0xa7ff789c},
    {"genus_ii_ab67_r_3-22-85.u5a", 0x4000, 0xc000, 0xcc5c68ef},
    {"genus_ii_rom_cd45r_3-22-85.u2a", 0x4000, 0x18000, 0xfc9c752a},
    {"genus_ii_rom_d6efr_3-22-85.u1a", 0x4000, 0x1c000, 0x23b56fb8},
};
inline const std::vector<RomEntry> kTriviag2Sprites = {
    {"genus_ii_gr0_r_3-22-85.u6b", 0x4000, 0x0000, 0x6829de8e},
    {"genus_ii_gr1_r_3-22-85.u5b", 0x4000, 0x4000, 0x89398700},
    {"genus_ii_grz_r_3-22-85.u4b", 0x4000, 0x8000, 0x1e870293},
};

// Trivial Pursuit — MAME triviayp
inline const std::vector<RomEntry> kTriviaypMain = {
    {"young_rom_ab01_r_3-29-85.u8a", 0x4000, 0x0000, 0x97d35a85},
    {"young_rom_ab23_r_3-29-85.u7a", 0x4000, 0x4000, 0x2ff67c70},
    {"young_rom_ab45_r_3-29-85.u6a", 0x4000, 0x8000, 0x511a0fab},
    {"young_rom_ab67_r_3-29-85.u5a", 0x4000, 0xc000, 0xdf99d00c},
    {"young_rom_cd45_r_3-29-85.u2a", 0x4000, 0x18000, 0xac45809e},
    {"young_rom_cd6ef_r_3-29-85.u1a", 0x4000, 0x1c000, 0xa008059f},
};
inline const std::vector<RomEntry> kTriviaypSprites = {
    {"young_gr01_r_3-29-85.u6b", 0x4000, 0x0000, 0x6829de8e},
    {"young_gr23_r_3-29-85.u5b", 0x4000, 0x4000, 0x89398700},
    {"young_gr45_r_3-29-85.u4b", 0x4000, 0x8000, 0x1242033e},
};

// Trivial Pursuit — MAME triviasp
inline const std::vector<RomEntry> kTriviaspMain = {
    {"allsport.u8a", 0x4000, 0x0000, 0x54b7ff31},
    {"allsport.u7a", 0x4000, 0x4000, 0x59fae9d2},
    {"allsport.u6a", 0x4000, 0x8000, 0x237b6b95},
    {"allsport.u5a", 0x4000, 0xc000, 0xb64d7f61},
    {"allsport.u2a", 0x4000, 0x18000, 0xe45d09d6},
    {"allsport.u1a", 0x4000, 0x1c000, 0x8bb3e831},
};
inline const std::vector<RomEntry> kTriviaspSprites = {
    {"allsport.u6b", 0x4000, 0x0000, 0x6829de8e},
    {"allsport.u5b", 0x4000, 0x4000, 0x89398700},
    {"allsport.u4b", 0x4000, 0x8000, 0x7415a7fc},
};

// Gimme A Break — MAME gimeabrk
inline const std::vector<RomEntry> kGimeabrkMain = {
    {"gimmeabreak_ab01_7-7-85.u8a", 0x4000, 0x0000, 0x18cc53db},
    {"gimmeabreak_ab23_7-7-85.u7a", 0x4000, 0x4000, 0x6bd4190a},
    {"gimmeabreak_ab45_7-7-85.u6a", 0x4000, 0x8000, 0x5dca4f33},
    {"gimmeabreak_cd_6_ef_7-7-85.u1a", 0x4000, 0x1c000, 0x5e2b3510},
};
inline const std::vector<RomEntry> kGimeabrkSprites = {
    {"gimmeabreak_gr01_7-7-85.u6b", 0x4000, 0x0000, 0xe3cdc476},
    {"gimmeabreak_gr23_7-7-85.u5b", 0x4000, 0x4000, 0x0555d9c0},
};

// Mini Golf — MAME minigolf
inline const std::vector<RomEntry> kMinigolfMain = {
    {"ab01.u8a", 0x4000, 0x0000, 0x348f827f},
    {"ab23.u7a", 0x4000, 0x4000, 0x19a6ff47},
    {"ab45.u6a", 0x4000, 0x8000, 0x925d76eb},
    {"ab67.u5a", 0x4000, 0xc000, 0x6a311c9a},
    {"1a-ver2", 0x10000, 0x10000, 0x60b6cd58},
};
inline const std::vector<RomEntry> kMinigolfSprites = {
    {"gr01.u6b", 0x4000, 0x0000, 0x8e24d594},
    {"gr23.u5b", 0x4000, 0x4000, 0x3bf355ef},
    {"gr45.u4b", 0x4000, 0x8000, 0x8eb14921},
};

// Team Hat Trick — MAME teamht
inline const std::vector<RomEntry> kTeamhtMain = {
    {"hat_trk_ab-0_11-12-84.u8a", 0x4000, 0x0000, 0xcb746de8},
    {"hat_trk_ab-1_11-16-84.u7a", 0x4000, 0x4000, 0x5f2a0b24},
    {"hat_trk_cd_11-16-84.u1a", 0x4000, 0x1c000, 0x6c6cf2be},
};
inline const std::vector<RomEntry> kTeamhtSprites = {
    {"hat_trk_gr-0_11-16-84.u6b", 0x4000, 0x0000, 0x6e299728},
};

// Grudge Match — MAME grudge
inline const std::vector<RomEntry> kGrudgeMain = {
    {"ab0.8a.romab0", 0x8000, 0x0000, 0xeabeec2b},
    {"ab4.9a.romab4", 0x8000, 0x8000, 0x2dddb371},
    {"g.m._cd-0_9-21-87.13a.romcd0", 0x8000, 0x10000, 0xad168726},
    {"cd4.15a.romcd4", 0x8000, 0x18000, 0x1de8dd2e},
    {"cd12.18a.romcd12", 0x8000, 0x18000, 0x1de8dd2e},
};
inline const std::vector<RomEntry> kGrudgeSprites = {
    {"g.m._gr0_9-21-87.8a.gr0", 0x8000, 0x0000, 0xb9681f53},
};

// Trivial Pursuit — MAME triviaes
inline const std::vector<RomEntry> kTriviaesMain = {
    {"tp_a2.bin", 0x4000, 0x0000, 0xb4d69463},
    {"tp_a7.bin", 0x4000, 0x4000, 0xd78bd4b6},
    {"tp_a4.bin", 0x4000, 0x8000, 0x0de9e14d},
    {"tp_a5.bin", 0x4000, 0xc000, 0xe749adac},
    {"tp_a8.bin", 0x4000, 0x10000, 0x168ef5ed},
    {"tp_a1.bin", 0x4000, 0x14000, 0x1f6ef37f},
    {"tp_a6.bin", 0x4000, 0x18000, 0x421c1a29},
    {"tp_a3.bin", 0x4000, 0x1c000, 0xc6254f46},
};
inline const std::vector<RomEntry> kTriviaesSprites = {
    {"tp_gr3.bin", 0x4000, 0x0000, 0x6829de8e},
    {"tp_gr2.bin", 0x4000, 0x4000, 0x89398700},
    {"tp_gr1.bin", 0x4000, 0x8000, 0x1242033e},
};

// Toggle — MAME toggle
inline const std::vector<RomEntry> kToggleMain = {
    {"tgle-ab0.bin", 0x2000, 0x0000, 0x8c7b7fad},
    {"tgle-ab1.bin", 0x2000, 0x2000, 0x771e5434},
    {"tgle-ab2.bin", 0x2000, 0x4000, 0x9b4baa3f},
    {"tgle-ab3.bin", 0x2000, 0x6000, 0x35308a41},
    {"tgle-ab4.bin", 0x2000, 0x8000, 0xbaf5617b},
    {"tgle-ab5.bin", 0x2000, 0xa000, 0x88077dad},
    {"tgle-cd.bin", 0x2000, 0x1c000, 0x0a2bb949},
    {"tgle-ef.bin", 0x2000, 0x1e000, 0x3ec10804},
};
inline const std::vector<RomEntry> kToggleSprites = {
    {"tgle-gr0.bin", 0x2000, 0x0000, 0x0e0e5d0e},
    {"tgle-gr1.bin", 0x2000, 0x2000, 0x3b141ad2},
};

// Night Stocker — MAME nstocker
inline const std::vector<RomEntry> kNstockerMain = {
    {"night_stocker_ab_01_10-06-86.u8a", 0x4000, 0x0000, 0xa635f973},
    {"night_stocker_ab_23_10-06-86.u7a", 0x4000, 0x4000, 0x223acbb2},
    {"night_stocker_ab_45_10-06-86.u6a", 0x4000, 0x8000, 0x27a728b5},
    {"night_stocker_ab_67_10-06-86.u5a", 0x4000, 0xc000, 0x2999cdf2},
    {"night_stocker_cd_01_10-06-86.u4a", 0x4000, 0x10000, 0x75e9b51a},
    {"night_stocker_cd_23_10-06-86.u3a", 0x4000, 0x14000, 0x0a32e0a5},
    {"night_stocker_cd_45_10-06-86.u2a", 0x4000, 0x18000, 0x9bb292fe},
    {"night_stocker_cd_6_ef_10-06-86.u1a", 0x4000, 0x1c000, 0xe77c1aea},
};
inline const std::vector<RomEntry> kNstockerSprites = {
    {"night_stocker_gr_01_10-06-86.u4c", 0x4000, 0x0000, 0xfd0c38be},
    {"night_stocker_gr_23_10-06-86.u3c", 0x4000, 0x4000, 0x35d4433e},
    {"night_stocker_gr_45_10-06-86.u2c", 0x4000, 0x8000, 0x734b858a},
    {"night_stocker_gr_67_10-06-86.u1c", 0x4000, 0xc000, 0x3311f9c0},
};

// Street Football — MAME sfootbal
inline const std::vector<RomEntry> kSfootbalMain = {
    {"street_football_ab_01_11-12-86.u8a", 0x4000, 0x0000, 0x2a69803f},
    {"street_football_ab_23_11-12-86.u7a", 0x4000, 0x4000, 0x89f157c2},
    {"street_football_ab_45_11-12-86.u6a", 0x4000, 0x8000, 0x91ad42c5},
    {"street_football_cd_6_ef_11-12-86.u1a", 0x4000, 0x1c000, 0xbf80bb1a},
};
inline const std::vector<RomEntry> kSfootbalSprites = {
    {"street_football_gr_01_11-12-86.u4c", 0x4000, 0x0000, 0xe3108d35},
    {"street_football_gr_23_11-12-86.u3c", 0x4000, 0x4000, 0x5c5af726},
    {"street_football_gr_45_11-12-86.u2c", 0x4000, 0x8000, 0xe767251e},
    {"street_football_gr_67_11-12-86.u1c", 0x4000, 0xc000, 0x42452a7a},
};

// Spiker — MAME spiker
inline const std::vector<RomEntry> kSpikerMain = {
    {"spiker_u_r_ab01_5-05-86.u8a", 0x4000, 0x0000, 0x2d53d023},
    {"spiker_u_r_ab23_5-05-86.u7a", 0x4000, 0x4000, 0x3be87edf},
    {"spiker_u_r_cd_6_ef_6-09-86.u1a", 0x4000, 0x1c000, 0x5b5a6d86},
};
inline const std::vector<RomEntry> kSpikerSprites = {
    {"spiker_u_r_gr01_5-05-86.u4c", 0x4000, 0x0000, 0x0caa6e3e},
    {"spiker_u_r_gr23_5-05-86.u3c", 0x4000, 0x4000, 0x970c81f6},
    {"spiker_u_r_gr45_5-05-86.u2c", 0x4000, 0x8000, 0x90ddd737},
};

// Stompin' — MAME stompin
inline const std::vector<RomEntry> kStompinMain = {
    {"ab 01.u8a", 0x4000, 0x0000, 0x46f428c6},
    {"ab 23.u7a", 0x4000, 0x4000, 0x0e13132f},
    {"ab 45.u6a", 0x4000, 0x8000, 0x6ed26069},
    {"ab 67.u5a", 0x4000, 0xc000, 0x7f63b516},
    {"cd 23.u3a", 0x4000, 0x14000, 0x52b29048},
    {"cd 6 ef.u1a", 0x4000, 0x1c000, 0xb880961a},
};
inline const std::vector<RomEntry> kStompinSprites = {
    {"gr 01.u4c", 0x4000, 0x0000, 0x14ffdd1e},
    {"gr 23.u3c", 0x4000, 0x4000, 0x761abb80},
    {"gr 45.u2c", 0x4000, 0x8000, 0x0d2cf2e6},
    {"gr 67.u2c", 0x4000, 0xc000, 0x2bab2784},
};

// Name That Tune — MAME nametune
inline const std::vector<RomEntry> kNametuneMain = {
    {"namethattune_ur_ab_01_3-31-86.u8a", 0x4000, 0x0000, 0xf99054f1},
    {"namethattune_ur_ab_01_3-31-86.u8a", 0x4000, 0x20000, 0xf99054f1},
    {"namethattune_ur_ab_23_3-31-86.u7a", 0x4000, 0x4000, 0xf2b8f7fa},
    {"namethattune_ur_ab_23_3-31-86.u7a", 0x4000, 0x24000, 0xf2b8f7fa},
    {"namethattune_ur_ab_45_3-31-86.u6a", 0x4000, 0x8000, 0x89e1c769},
    {"namethattune_ur_ab_45_3-31-86.u6a", 0x4000, 0x28000, 0x89e1c769},
    {"namethattune_ur_ab_67_3-31-86.u5a", 0x4000, 0xc000, 0x7e5572a1},
    {"namethattune_ur_ab_67_3-31-86.u5a", 0x4000, 0x2c000, 0x7e5572a1},
    {"namethattune_ur_cd_01_3-31-86.u4a", 0x4000, 0x10000, 0xdb9d6154},
    {"namethattune_ur_cd_01_3-31-86.u4a", 0x4000, 0x30000, 0xdb9d6154},
    {"namethattune_ur_cd_23_3-31-86.u3a", 0x4000, 0x14000, 0x9d2e458f},
    {"namethattune_ur_cd_23_3-31-86.u3a", 0x4000, 0x34000, 0x9d2e458f},
    {"namethattune_ur_cd_45_3-31-86.u2a", 0x4000, 0x18000, 0x9a4b87aa},
    {"namethattune_ur_cd_45_3-31-86.u2a", 0x4000, 0x38000, 0x9a4b87aa},
    {"namethattune_ur_cd_6_ef_3-31-86.u1a", 0x4000, 0x1c000, 0x0459e6f8},
    {"namethattune_ur_cd_6_ef_3-31-86.u1a", 0x4000, 0x3c000, 0x0459e6f8},
};
inline const std::vector<RomEntry> kNametuneSprites = {
    {"namethattune_ur_gr_0_3-31-86.u3c", 0x8000, 0x0000, 0x6b75bb4b},
};

// Rescue Raider — MAME rescraid
inline const std::vector<RomEntry> kRescraidMain = {
    {"ab 1.a10", 0x8000, 0x0000, 0x33a76b47},
    {"ab 12.a12", 0x8000, 0x8000, 0x7c7a9f12},
    {"cd 8.a16", 0x8000, 0x10000, 0x90917a43},
    {"cd 12.a18", 0x8000, 0x18000, 0x0450e9d7},
};
inline const std::vector<RomEntry> kRescraidSprites = {
    {"gr 0.a5", 0x8000, 0x0000, 0xe0dfc133},
    {"gr 4.a7", 0x8000, 0x8000, 0x952ade30},
};

// Shrike Avenger — MAME shrike
inline const std::vector<RomEntry> kShrikeMain = {
    {"savgu35.bin", 0x2000, 0x0000, 0xdd2230a0},
    {"savgu20.bin", 0x2000, 0x2000, 0x3d140edc},
    {"savgu34.bin", 0x2000, 0x4000, 0x779eca9d},
    {"savgu19.bin", 0x2000, 0x6000, 0x9ec89a80},
    {"savgu33.bin", 0x2000, 0x8000, 0x20596f48},
    {"savgu18.bin", 0x2000, 0xa000, 0x7abc3f14},
    {"savgu32.bin", 0x2000, 0xc000, 0x807f0a3b},
    {"savgu17.bin", 0x2000, 0xe000, 0xe0dbf6ad},
    {"savgu21.bin", 0x2000, 0x1c000, 0xc22b93e1},
    {"savgu36.bin", 0x2000, 0x1e000, 0x28431c4a},
};
inline const std::vector<RomEntry> kShrikeSprites = {
    {"savgu8.bin", 0x2000, 0x0000, 0x499a1d06},
    {"savgu7.bin", 0x2000, 0x2000, 0xce0607f9},
    {"savgu6.bin", 0x2000, 0x4000, 0x01d1b31e},
    {"savgu5.bin", 0x2000, 0x6000, 0x8bc6d101},
    {"savgu4.bin", 0x2000, 0x8000, 0x72644753},
    {"savgu3.bin", 0x2000, 0xa000, 0x606a9cfd},
    {"savgu2.bin", 0x2000, 0xc000, 0x69f600f6},
    {"savgu1.bin", 0x2000, 0xe000, 0x303b8e7b},
    {"savgu16.bin", 0x2000, 0x10000, 0xb8f60607},
    {"savgu15.bin", 0x2000, 0x12000, 0x6b332a5d},
    {"savgu14.bin", 0x2000, 0x14000, 0x8d5117aa},
    {"savgu13.bin", 0x2000, 0x16000, 0xd3ce645e},
    {"savgu12.bin", 0x2000, 0x18000, 0xccdfedb1},
    {"savgu11.bin", 0x2000, 0x1a000, 0xdb11ff4c},
    {"savgu10.bin", 0x2000, 0x1c000, 0x6f3d9aa1},
};

// Shared sente6vb sound ROM (present in most balsente zips)
inline const std::vector<RomEntry> kSoundRom = {
    {"8002-10 9-25-84.5", 0x2000, 0x0000, 0x4dd0a525},
};

}  // namespace balsente_roms
}  // namespace dsp
