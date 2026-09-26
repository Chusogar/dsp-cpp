#include "drivers/arcade/vicdual.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "core/rom_loader.h"

namespace dsp {
namespace {

constexpr double kPi = 3.14159265358979323846;

const uint32_t kPens[8] = {
    0xff000000u, 0xff00ff00u, 0xff0000ffu, 0xff00ffffu,
    0xffff0000u, 0xffffff00u, 0xffff00ffu, 0xffffffffu,
};

const std::vector<RomEntry> kCarnivalMusic = {{"epr-412.u5", 0x0400, 0x0000, 0x0dbaa2b0}};

const std::vector<RomEntry> kDepthChargeCpu = {
    {"50a", 0x0400, 0x0000, 0x56c5ffed},
    {"51a", 0x0400, 0x0400, 0x695eb81f},
    {"52", 0x0400, 0x0800, 0xaed0ba1b},
    {"53", 0x0400, 0x0c00, 0x2ccbd2d0},
    {"54a", 0x0400, 0x1000, 0x1b7f6a43},
    {"55a", 0x0400, 0x1400, 0x9fc2eb41},
};

// DepthCharge: no color PROM

const std::vector<RomEntry> kSafariCpu = {
    {"316-0066.u48", 0x0400, 0x0000, 0x2a26b098},
    {"316-0065.u47", 0x0400, 0x0400, 0xb776f7db},
    {"316-0064.u46", 0x0400, 0x0800, 0x19d8c196},
    {"316-0063.u45", 0x0400, 0x0c00, 0x028bad25},
    {"316-0062.u44", 0x0400, 0x1000, 0x504e0575},
    {"316-0061.u43", 0x0400, 0x1400, 0xd4c528e0},
    {"316-0060.u42", 0x0400, 0x1800, 0x48c7b0cc},
    {"316-0059.u41", 0x0400, 0x1c00, 0x3f7baaff},
    {"316-0058.u40", 0x0400, 0x2000, 0x0d5058f1},
    {"316-0057.u39", 0x0400, 0x2400, 0x298e8c41},
};

// Safari: no color PROM

const std::vector<RomEntry> kFrogsCpu = {
    {"316-119a.u48", 0x0400, 0x0000, 0xb1d1fce4},
    {"316-118a.u47", 0x0400, 0x0400, 0x12fdcc05},
    {"316-117a.u46", 0x0400, 0x0800, 0x8a5be424},
    {"316-116b.u45", 0x0400, 0x0c00, 0x09b82619},
    {"316-115a.u44", 0x0400, 0x1000, 0x3d4e4fa8},
    {"316-114a.u43", 0x0400, 0x1400, 0x04a21853},
    {"316-113a.u42", 0x0400, 0x1800, 0x02786692},
    {"316-112a.u41", 0x0400, 0x1c00, 0x0be2a058},
};

// Frogs: no color PROM

const std::vector<RomEntry> kSpaceAttackCpu = {
    {"155.u27", 0x0400, 0x0000, 0xba7bb86f},
    {"156.u26", 0x0400, 0x0400, 0x0b3a491c},
    {"157.u25", 0x0400, 0x0800, 0x3d3fac3b},
    {"158.u24", 0x0400, 0x0c00, 0x843b80f6},
    {"159.u23", 0x0400, 0x1000, 0x1eacf60d},
    {"160.u22", 0x0400, 0x1400, 0xe61d482f},
    {"161.u21", 0x0400, 0x1800, 0xeb5e0993},
    {"162.u20", 0x0400, 0x1c00, 0x5f84d550},
};

const std::vector<RomEntry> kSpaceAttackProm = {{"316-0138.u44", 0x0020, 0x0000, 0x67104ea9}};

const std::vector<RomEntry> kSpaceAttackHeadOnCpu = {
    {"epr-0001.bin", 0x0800, 0x0000, 0xba62f57a},
    {"epr-0002.bin", 0x0800, 0x0800, 0x94b3c59c},
    {"epr-0003.bin", 0x0800, 0x1000, 0xdf13aef2},
    {"epr-0004.bin", 0x0800, 0x1800, 0x8431e15e},
    {"epr-0005.bin", 0x0800, 0x2000, 0xeec2b6e7},
    {"epr-0006.bin", 0x0800, 0x2800, 0x780e47ed},
    {"epr-0007.bin", 0x0800, 0x3000, 0x8189a2fa},
    {"epr-0008.bin", 0x0800, 0x3800, 0x34a64a80},
};

const std::vector<RomEntry> kSpaceAttackHeadOnProm = {{"316-0138.u44", 0x0020, 0x0000, 0x67104ea9}};

const std::vector<RomEntry> kHeadOnCpu = {
    {"316-163a.u27", 0x0400, 0x0000, 0x4bb51259},
    {"316-164a.u26", 0x0400, 0x0400, 0xaeac8c5f},
    {"316-165a.u25", 0x0400, 0x0800, 0xf1a0cb72},
    {"316-166c.u24", 0x0400, 0x0c00, 0x65d12951},
    {"316-167c.u23", 0x0400, 0x1000, 0x2280831e},
    {"316-192a.u22", 0x0400, 0x1400, 0xed4666f2},
    {"316-193a.u21", 0x0400, 0x1800, 0x37a1df4c},
};

const std::vector<RomEntry> kHeadOnProm = {{"316-0138.u44", 0x0020, 0x0000, 0x67104ea9}};

const std::vector<RomEntry> kHeadOn2Cpu = {
    {"u27.bin", 0x0400, 0x0000, 0xfa47d2fb},
    {"u26.bin", 0x0400, 0x0400, 0x61c47b15},
    {"u25.bin", 0x0400, 0x0800, 0xbb16db92},
    {"u24.bin", 0x0400, 0x0c00, 0x17a09f24},
    {"u23.bin", 0x0400, 0x1000, 0x0024895e},
    {"u22.bin", 0x0400, 0x1400, 0xf798304d},
    {"u21.bin", 0x0400, 0x1800, 0x4c19dd40},
    {"u20.bin", 0x0400, 0x1c00, 0x25887ff2},
};

const std::vector<RomEntry> kHeadOn2Prom = {{"316-0138.u44", 0x0020, 0x0000, 0x67104ea9}};

const std::vector<RomEntry> kHeadOn2SlimCpu = {
    {"epr-170.u33", 0x0400, 0x0000, 0xc108625d},
    {"epr-171.u32", 0x0400, 0x0400, 0x05814307},
    {"epr-172.u31", 0x0400, 0x0800, 0x77108d24},
    {"epr-173.u30", 0x0400, 0x0c00, 0x3d711e00},
    {"epr-174.u29", 0x0400, 0x1000, 0x89f98392},
    {"epr-175.u28", 0x0400, 0x1400, 0xfd9034c5},
    {"epr-176.u27", 0x0400, 0x1800, 0x319d3465},
    {"epr-177.u26", 0x0400, 0x1c00, 0xf43a9846},
};

const std::vector<RomEntry> kHeadOn2SlimProm = {{"316-0138.u49", 0x0020, 0x0000, 0x67104ea9}};

const std::vector<RomEntry> kInvincoHeadOn2Cpu = {
    {"271b.u33", 0x0400, 0x0000, 0x44356a73},
    {"272b.u32", 0x0400, 0x0400, 0xbd251265},
    {"273b.u31", 0x0400, 0x0800, 0x2fc80cd9},
    {"274b.u30", 0x0400, 0x0c00, 0x4fac4210},
    {"275b.u29", 0x0400, 0x1000, 0x85af508e},
    {"276b.u28", 0x0400, 0x1400, 0xe305843a},
    {"277b.u27", 0x0400, 0x1800, 0xb6b4221e},
    {"278b.u26", 0x0400, 0x1c00, 0x74d42250},
    {"279b.u8", 0x0400, 0x2000, 0x8d30a3e0},
    {"280b.u7", 0x0400, 0x2400, 0xb5ee60ec},
    {"281b.u6", 0x0400, 0x2800, 0x21a6d4f2},
    {"282b.u5", 0x0400, 0x2c00, 0x07d54f8a},
    {"283b.u4", 0x0400, 0x3000, 0xbdbe7ec1},
    {"284b.u3", 0x0400, 0x3400, 0xae9e9f16},
    {"285b.u2", 0x0400, 0x3800, 0x8dc3ec34},
    {"286b.u1", 0x0400, 0x3c00, 0x4bab9ba2},
};

const std::vector<RomEntry> kInvincoHeadOn2Prom = {{"316-0287.u49", 0x0020, 0x0000, 0xd4374b01}};

const std::vector<RomEntry> kNSubCpu = {
    {"epr-268.u48", 0x0800, 0x0000, 0x485b4704},
    {"epr-269.u47", 0x0800, 0x0800, 0x32774ac9},
    {"epr-270.u46", 0x0800, 0x1000, 0xaf7ca40a},
    {"epr-271.u45", 0x0800, 0x1800, 0x3f9c180b},
    {"epr-272.u44", 0x0800, 0x2000, 0xd818aa51},
    {"epr-273.u43", 0x0800, 0x2800, 0x03a6f12a},
    {"epr-274.u42", 0x0800, 0x3000, 0xd69eb098},
    {"epr-275.u41", 0x0800, 0x3800, 0x1c7d90cc},
};

const std::vector<RomEntry> kNSubProm = {{"pr-69.u11", 0x0020, 0x0000, 0xc94dd091}};

const std::vector<RomEntry> kSamuraiCpu = {
    {"epr-1217.u33", 0x0400, 0x0000, 0xa1a9cb03},
    {"epr-1218.u32", 0x0400, 0x0400, 0x4b45d07d},
    {"epr-1219.u31", 0x0400, 0x0800, 0x9fd4b195},
    {"epr-1220.u30", 0x0400, 0x0c00, 0x90370e13},
    {"epr-1221.u29", 0x0400, 0x1000, 0xdcc47158},
    {"epr-1222.u28", 0x0400, 0x1400, 0xd2fab27a},
    {"epr-1223.u27", 0x0400, 0x1800, 0xf7e2ad95},
    {"epr-1224.u26", 0x0400, 0x1c00, 0xd46e306b},
    {"epr-1225.u8", 0x0400, 0x2000, 0x3dd5c41f},
    {"epr-1226.u7", 0x0400, 0x2400, 0x7c3561b1},
    {"epr-1227.u6", 0x0400, 0x2800, 0xe72c71a4},
    {"epr-1228.u5", 0x0400, 0x2c00, 0xd76f4a56},
    {"epr-1229.u4", 0x0400, 0x3000, 0xe0d40395},
    {"epr-1230.u3", 0x0400, 0x3400, 0x55e9a5c4},
};

const std::vector<RomEntry> kSamuraiProm = {{"pr55.clr", 0x0020, 0x0000, 0x975f5fb0}};

const std::vector<RomEntry> kInvincoCpu = {
    {"310a.u27", 0x0400, 0x0000, 0xe3931365},
    {"311a.u26", 0x0400, 0x0400, 0xde1a6c4a},
    {"312a.u25", 0x0400, 0x0800, 0xe3c08f39},
    {"313a.u24", 0x0400, 0x0c00, 0xb680b306},
    {"314a.u23", 0x0400, 0x1000, 0x790f07d9},
    {"315a.u22", 0x0400, 0x1400, 0x0d13bed2},
    {"316a.u21", 0x0400, 0x1800, 0x88d7eab8},
    {"317a.u20", 0x0400, 0x1c00, 0x75389463},
    {"318a.uxx", 0x0400, 0x2000, 0x0780721d},
};

const std::vector<RomEntry> kInvincoProm = {{"316-0246.u44", 0x0020, 0x0000, 0xfe4406cb}};

const std::vector<RomEntry> kInvincoDeepScanCpu = {
    {"367.u33", 0x0400, 0x0000, 0xe6a33eae},
    {"368.u32", 0x0400, 0x0400, 0x421554a8},
    {"369.u31", 0x0400, 0x0800, 0x531e917a},
    {"370.u30", 0x0400, 0x0c00, 0x2ad68f8c},
    {"371.u29", 0x0400, 0x1000, 0x1b98dc5c},
    {"372.u28", 0x0400, 0x1400, 0x3a72190a},
    {"373.u27", 0x0400, 0x1800, 0x3d361520},
    {"374.u26", 0x0400, 0x1c00, 0xe606e7d9},
    {"375.u8", 0x0400, 0x2000, 0xadbe8d32},
    {"376.u7", 0x0400, 0x2400, 0x79409a46},
    {"377.u6", 0x0400, 0x2800, 0x3f021a71},
    {"378.u5", 0x0400, 0x2c00, 0x49a542b0},
    {"379.u4", 0x0400, 0x3000, 0xee140e49},
    {"380.u3", 0x0400, 0x3400, 0x688ba831},
    {"381.u2", 0x0400, 0x3800, 0x798ba0c7},
    {"382.u1", 0x0400, 0x3c00, 0x8d195c24},
};

const std::vector<RomEntry> kInvincoDeepScanProm = {{"316-0246.u44", 0x0020, 0x0000, 0xfe4406cb}};

const std::vector<RomEntry> kTranqGunCpu = {
    {"u33.bin", 0x0400, 0x0000, 0x6d50e902},
    {"u32.bin", 0x0400, 0x0400, 0xf0ba0e60},
    {"u31.bin", 0x0400, 0x0800, 0x9fe440d3},
    {"u30.bin", 0x0400, 0x0c00, 0x1041608e},
    {"u29.bin", 0x0400, 0x1000, 0xfb5de95f},
    {"u28.bin", 0x0400, 0x1400, 0x03fd8727},
    {"u27.bin", 0x0400, 0x1800, 0x3d93239b},
    {"u26.bin", 0x0400, 0x1c00, 0x20f64a7f},
    {"u8.bin", 0x0400, 0x2000, 0x5121c695},
    {"u7.bin", 0x0400, 0x2400, 0xb13d21f7},
    {"u6.bin", 0x0400, 0x2800, 0x603cee59},
    {"u5.bin", 0x0400, 0x2c00, 0x7f25475f},
    {"u4.bin", 0x0400, 0x3000, 0x57dc3123},
    {"u3.bin", 0x0400, 0x3400, 0x7aa7829b},
    {"u2.bin", 0x0400, 0x3800, 0xa9b10df5},
    {"u1.bin", 0x0400, 0x3c00, 0x431a7449},
};

const std::vector<RomEntry> kTranqGunProm = {{"u49.bin", 0x0020, 0x0000, 0x6481445b}};

const std::vector<RomEntry> kSpaceTrekCpu = {
    {"u33.bin", 0x0400, 0x0000, 0x9033fe50},
    {"u32.bin", 0x0400, 0x0400, 0x08f61f0d},
    {"u31.bin", 0x0400, 0x0800, 0x1088a8c4},
    {"u30.bin", 0x0400, 0x0c00, 0x55560cc8},
    {"u29.bin", 0x0400, 0x1000, 0x71713958},
    {"u28.bin", 0x0400, 0x1400, 0x7bcf5ca3},
    {"u27.bin", 0x0400, 0x1800, 0xad7a2065},
    {"u26.bin", 0x0400, 0x1c00, 0x6060fe77},
    {"u8.bin", 0x0400, 0x2000, 0x75a90624},
    {"u7.bin", 0x0400, 0x2400, 0x7b31a2ab},
    {"u6.bin", 0x0400, 0x2800, 0x94135b33},
    {"u5.bin", 0x0400, 0x2c00, 0xcfbf2538},
    {"u4.bin", 0x0400, 0x3000, 0xb4b95129},
    {"u3.bin", 0x0400, 0x3400, 0x03ca1d70},
    {"u2.bin", 0x0400, 0x3800, 0xa968584b},
    {"u1.bin", 0x0400, 0x3c00, 0xe6e300e8},
};

const std::vector<RomEntry> kSpaceTrekProm = {{"u49.bin", 0x0020, 0x0000, 0xaabae4cd}};

const std::vector<RomEntry> kCarnivalCpu = {
    {"epr-651.u33", 0x0400, 0x0000, 0x9f2736e6},
    {"epr-652.u32", 0x0400, 0x0400, 0xa1f58beb},
    {"epr-653.u31", 0x0400, 0x0800, 0x67b17922},
    {"epr-654.u30", 0x0400, 0x0c00, 0xbefb09a5},
    {"epr-655.u29", 0x0400, 0x1000, 0x623fcdad},
    {"epr-656.u28", 0x0400, 0x1400, 0x53040332},
    {"epr-657.u27", 0x0400, 0x1800, 0xf2537467},
    {"epr-658.u26", 0x0400, 0x1c00, 0xfcc3854e},
    {"epr-659.u8", 0x0400, 0x2000, 0x28be8d69},
    {"epr-660.u7", 0x0400, 0x2400, 0x3873ccdb},
    {"epr-661.u6", 0x0400, 0x2800, 0xd9a96dff},
    {"epr-662.u5", 0x0400, 0x2c00, 0xd893ca72},
    {"epr-663.u4", 0x0400, 0x3000, 0xdf8c63c5},
    {"epr-664.u3", 0x0400, 0x3400, 0x689a73e8},
    {"epr-665.u2", 0x0400, 0x3800, 0x28e7b2b6},
    {"epr-666.u1", 0x0400, 0x3c00, 0x4eec7fae},
};

const std::vector<RomEntry> kCarnivalProm = {{"316-0633.u49", 0x0020, 0x0000, 0xf0084d80}};

const std::vector<RomEntry> kBorderlineCpu = {
    {"b1.bin", 0x0400, 0x0000, 0xdf182769},
    {"b2.bin", 0x0400, 0x0400, 0xe1d1c4ce},
    {"b3.bin", 0x0400, 0x0800, 0x4ec4afa2},
    {"b4.bin", 0x0400, 0x0c00, 0x88de95f6},
    {"b5.bin", 0x0400, 0x1000, 0x2e4e13b9},
    {"b6.bin", 0x0400, 0x1400, 0xc181e87a},
    {"b7.bin", 0x0400, 0x1800, 0x21180015},
    {"b8.bin", 0x0400, 0x1c00, 0x56a7fee0},
    {"b9.bin", 0x0400, 0x2000, 0xbb532e63},
    {"b10.bin", 0x0400, 0x2400, 0x64793709},
    {"b11.bin", 0x0400, 0x2800, 0x2ae2f928},
    {"b12.bin", 0x0400, 0x2c00, 0xe14cfaf5},
    {"b13.bin", 0x0400, 0x3000, 0x605e0d27},
    {"b14.bin", 0x0400, 0x3400, 0x93f5714f},
    {"b15.bin", 0x0400, 0x3800, 0x2f8a9b1c},
    {"b16.bin", 0x0400, 0x3c00, 0xcc138bed},
};

const std::vector<RomEntry> kBorderlineProm = {{"borderc.49", 0x0020, 0x0000, 0xbc6be94e}};

const std::vector<RomEntry> kDiggerCpu = {
    {"684.u27", 0x0400, 0x0000, 0xbba0d7c2},
    {"685.u26", 0x0400, 0x0400, 0x85210d8b},
    {"686.u25", 0x0400, 0x0800, 0x2d87238c},
    {"687.u24", 0x0400, 0x0c00, 0x0dd0604e},
    {"688.u23", 0x0400, 0x1000, 0x2f649667},
    {"689.u22", 0x0400, 0x1400, 0x89fd63d9},
    {"690.u21", 0x0400, 0x1800, 0xa86622a6},
    {"691.u20", 0x0400, 0x1c00, 0x8aca72d8},
};

const std::vector<RomEntry> kDiggerProm = {{"316-507", 0x0020, 0x0000, 0xfdb22e8f}};

const std::vector<RomEntry> kPulsarCpu = {
    {"790.u33", 0x0400, 0x0000, 0x5e3816da},
    {"791.u32", 0x0400, 0x0400, 0xce0aee83},
    {"792.u31", 0x0400, 0x0800, 0x72d78cf1},
    {"793.u30", 0x0400, 0x0c00, 0x42155dd4},
    {"794.u29", 0x0400, 0x1000, 0x11c7213a},
    {"795.u28", 0x0400, 0x1400, 0xd2f02e29},
    {"796.u27", 0x0400, 0x1800, 0x67737a2e},
    {"797.u26", 0x0400, 0x1c00, 0xec250b24},
    {"798.u8", 0x0400, 0x2000, 0x1d34912d},
    {"799.u7", 0x0400, 0x2400, 0xf5695e4c},
    {"800.u6", 0x0400, 0x2800, 0xbf91ad92},
    {"801.u5", 0x0400, 0x2c00, 0x1e9721dc},
    {"802.u4", 0x0400, 0x3000, 0xd32d2192},
    {"803.u3", 0x0400, 0x3400, 0x3ede44d5},
    {"804.u2", 0x0400, 0x3800, 0x62847b01},
    {"805.u1", 0x0400, 0x3c00, 0xab418e86},
};

const std::vector<RomEntry> kPulsarProm = {{"316-0789.u49", 0x0020, 0x0000, 0x7fc1861f}};

const std::vector<RomEntry> kHeiankyoCpu = {
    {"ha16.u33", 0x0400, 0x0000, 0x1eec8b36},
    {"ha15.u32", 0x0400, 0x0400, 0xc1b9a1a5},
    {"ha14.u31", 0x0400, 0x0800, 0x5b7b582e},
    {"ha13.u30", 0x0400, 0x0c00, 0x4aa67e01},
    {"ha12.u29", 0x0400, 0x1000, 0x75889ca6},
    {"ha11.u28", 0x0400, 0x1400, 0xd469226a},
    {"ha10.u27", 0x0400, 0x1800, 0x4e203074},
    {"ha9.u26", 0x0400, 0x1c00, 0x9c3a3dd2},
    {"ha8.u8", 0x0400, 0x2000, 0x6cc64878},
    {"ha7.u7", 0x0400, 0x2400, 0x6d2f9527},
    {"ha6.u6", 0x0400, 0x2800, 0xe467c353},
    {"ha3.u3", 0x0400, 0x2c00, 0x6a55eda8},
    {"ha2.u2", 0x0400, 0x3800, 0x056b3b8b},
    {"ha1.u1", 0x0400, 0x3c00, 0xb8da2b5e},
};

const std::vector<RomEntry> kHeiankyoProm = {{"316-138.u49", 0x0010, 0x0010, 0x67104ea9}};

const std::vector<RomEntry> kAlphaFighterCpu = {
    {"c0.bin", 0x0400, 0x0000, 0xdb774c23},
    {"c1.bin", 0x0400, 0x0400, 0xb63f4695},
    {"c2.bin", 0x0400, 0x0800, 0x4ebf0ba4},
    {"c3.bin", 0x0400, 0x0c00, 0x126f17ec},
    {"c4.bin", 0x0400, 0x1000, 0x52798c61},
    {"c5.bin", 0x0400, 0x1400, 0x4827cb36},
    {"c6.bin", 0x0400, 0x1800, 0x8b2ff47e},
    {"c7.bin", 0x0400, 0x1c00, 0x44921df4},
    {"c8.bin", 0x0400, 0x2000, 0x9fb12fca},
    {"c9.bin", 0x0400, 0x2400, 0xe5f622f7},
    {"ca.bin", 0x0400, 0x2800, 0x82b28e77},
    {"cb.bin", 0x0400, 0x2c00, 0x94fba0ad},
    {"cc.bin", 0x0400, 0x3000, 0xde338b6d},
    {"cd.bin", 0x0400, 0x3400, 0xbe76baac},
    {"ce.bin", 0x0400, 0x3800, 0x3c409d57},
    {"cf.bin", 0x0400, 0x3c00, 0xd03c5a09},
};

// AlphaFighter: no color PROM

// ---------------------------------------------------------------------------
// Synthesized stand-ins for MAME's sample WAVs (used when the samples are not
// available).  Each one imitates the kind of sound the original sample holds.

using Wave = std::vector<float>;

int sec(double s) { return int(s * VicDual::kSampleRate); }

struct Rng {
    uint32_t s = 0x12345678u;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(int32_t(s)) / 2147483648.0f;
    }
};

// Low-passed noise with an exponential decay (explosions, hits).
Wave noise_burst(double len, double decay, double cutoff, double gain = 0.9) {
    Wave w(size_t(sec(len)));
    Rng r;
    const double a = 1.0 - std::exp(-2.0 * kPi * cutoff / VicDual::kSampleRate);
    double lp = 0, lp2 = 0;
    for (size_t i = 0; i < w.size(); i++) {
        const double t = double(i) / VicDual::kSampleRate;
        lp += a * (r.next() - lp);
        lp2 += a * (lp - lp2);
        w[i] = float(lp2 * gain * 3.0 * std::exp(-t / decay));
    }
    return w;
}

// Square/triangle-ish tone with a frequency sweep and decay.
Wave sweep(double len, double f0, double f1, double decay, bool square = true, double gain = 0.6) {
    Wave w(size_t(sec(len)));
    double ph = 0;
    for (size_t i = 0; i < w.size(); i++) {
        const double t = double(i) / VicDual::kSampleRate;
        const double f = f0 + (f1 - f0) * (t / len);
        ph += f / VicDual::kSampleRate;
        ph -= std::floor(ph);
        const double v = square ? (ph < 0.5 ? 1.0 : -1.0) : std::sin(2 * kPi * ph);
        w[i] = float(v * gain * std::exp(-t / decay));
    }
    return w;
}

Wave notes(const std::vector<double>& freqs, double each, double gain = 0.5) {
    Wave w;
    for (double f : freqs) {
        Wave n = sweep(each, f, f, each * 0.9, true, gain);
        w.insert(w.end(), n.begin(), n.end());
    }
    return w;
}

// Inharmonic partials: bells, clangs, pings.
Wave metallic(double len, std::initializer_list<double> freqs, double decay, double gain = 0.5) {
    Wave w(size_t(sec(len)));
    for (size_t i = 0; i < w.size(); i++) {
        const double t = double(i) / VicDual::kSampleRate;
        double v = 0;
        int k = 0;
        for (double f : freqs) v += std::sin(2 * kPi * f * t) / (1 + k++);
        w[i] = float(v * gain * std::exp(-t / decay));
    }
    return w;
}

// Periodic sound followed by silence, for looping samples (sonar, beats).
Wave with_gap(Wave w, double total) {
    w.resize(std::max(w.size(), size_t(sec(total))), 0.0f);
    return w;
}

Wave mix(const Wave& a, const Wave& b) {
    Wave w(std::max(a.size(), b.size()), 0.0f);
    for (size_t i = 0; i < a.size(); i++) w[i] += a[i];
    for (size_t i = 0; i < b.size(); i++) w[i] += b[i];
    return w;
}

Wave warble(double len, double f, double depth, double rate, double gain = 0.45) {
    Wave w(size_t(sec(len)));
    double ph = 0;
    for (size_t i = 0; i < w.size(); i++) {
        const double t = double(i) / VicDual::kSampleRate;
        ph += (f + depth * std::sin(2 * kPi * rate * t)) / VicDual::kSampleRate;
        ph -= std::floor(ph);
        w[i] = float((ph < 0.5 ? 1.0 : -1.0) * gain);
    }
    return w;
}

Wave synth_sample(const std::string& name) {
    // Depthcharge
    if (name == "longex") return noise_burst(1.8, 0.6, 350);
    if (name == "shortex") return noise_burst(0.7, 0.2, 700);
    if (name == "spray") return noise_burst(0.35, 0.12, 5000, 0.5);
    if (name == "bonus") return notes({523, 659, 784, 1047, 1319}, 0.07);
    if (name == "sonar") return with_gap(metallic(0.5, {1200, 1203}, 0.12, 0.7), 1.3);
    // Invinco
    if (name == "saucer") return warble(0.5, 700, 200, 9);
    if (name == "move1") return sweep(0.07, 110, 110, 0.05, true, 0.5);
    if (name == "move2") return sweep(0.07, 98, 98, 0.05, true, 0.5);
    if (name == "move3") return sweep(0.07, 87, 87, 0.05, true, 0.5);
    if (name == "move4") return sweep(0.07, 82, 82, 0.05, true, 0.5);
    if (name == "fire") return sweep(0.22, 1600, 300, 0.12, true, 0.4);
    if (name == "invhit") return mix(noise_burst(0.35, 0.1, 2500, 0.6), sweep(0.2, 900, 200, 0.08, true, 0.3));
    if (name == "shiphit") return noise_burst(1.3, 0.45, 600);
    // Pulsar
    if (name == "clang") return metallic(0.5, {523, 1397, 2311, 3217}, 0.12);
    if (name == "key") return sweep(0.09, 2200, 2200, 0.05, true, 0.4);
    if (name == "alienhit") return noise_burst(0.45, 0.15, 1500);
    if (name == "phit") return noise_burst(1.1, 0.4, 500);
    if (name == "ashoot") return sweep(0.25, 900, 200, 0.12, true, 0.4);
    if (name == "pshoot") return sweep(0.16, 2200, 600, 0.08, true, 0.4);
    if (name == "sizzle") return noise_burst(0.5, 0.2, 7000, 0.5);
    if (name == "gate") return warble(0.5, 120, 30, 16, 0.4);
    if (name == "birth") return sweep(0.6, 200, 1400, 0.5, true, 0.4);
    if (name == "hbeat") {
        Wave a = sweep(0.09, 55, 45, 0.05, false, 0.9);
        Wave b = sweep(0.09, 50, 40, 0.05, false, 0.8);
        Wave w(size_t(sec(0.85)), 0.0f);
        for (size_t i = 0; i < a.size(); i++) w[i] += a[i];
        for (size_t i = 0; i < b.size(); i++) w[size_t(sec(0.2)) + i] += b[i];
        return w;
    }
    if (name == "movmaze") return warble(0.4, 90, 25, 6, 0.35);
    // Carnival
    if (name == "bear") return mix(sweep(0.5, 95, 70, 0.3, true, 0.4), noise_burst(0.5, 0.3, 400, 0.4));
    if (name == "bonus1") return notes({523, 659, 784, 1047}, 0.08);
    if (name == "bonus2") return notes({1047, 784, 659, 523}, 0.08);
    if (name == "duck1") return with_gap(sweep(0.08, 720, 620, 0.05, true, 0.35), 0.28);
    if (name == "duck2") return with_gap(sweep(0.08, 820, 700, 0.05, true, 0.35), 0.26);
    if (name == "duck3") return with_gap(sweep(0.08, 940, 800, 0.05, true, 0.35), 0.24);
    if (name == "pipehit") return metallic(0.25, {2500, 3700}, 0.06, 0.6);
    if (name == "ranking") return notes({523, 587, 659, 698, 784, 1047}, 0.1);
    if (name == "rifle") return mix(noise_burst(0.15, 0.04, 4000, 0.9), sweep(0.05, 1500, 400, 0.02, true, 0.3));
    // N-Sub
    if (name == "SND_EXPL_L0") return noise_burst(0.8, 2.0, 300);
    if (name == "SND_EXPL_L1") return noise_burst(1.6, 0.5, 300);
    if (name == "SND_SONAR") return with_gap(metallic(0.5, {1100, 1104}, 0.12, 0.7), 1.2);
    if (name == "SND_LAUNCH0") return warble(0.5, 300, 60, 12, 0.35);
    if (name == "SND_LAUNCH1") return sweep(0.4, 300, 900, 0.2, true, 0.4);
    if (name == "SND_WARNING0") return with_gap(sweep(0.15, 880, 880, 0.2, true, 0.35), 0.3);
    if (name == "SND_WARNING1") return sweep(0.15, 880, 880, 0.08, true, 0.35);
    if (name == "SND_EXPL_S0") return noise_burst(0.5, 2.0, 800);
    if (name == "SND_EXPL_S1") return noise_burst(0.8, 0.25, 800);
    if (name == "SND_BONUS0") return with_gap(notes({659, 784, 988}, 0.07), 0.35);
    if (name == "SND_BONUS1") return notes({988, 1319}, 0.08);
    if (name == "SND_CODE") return with_gap(sweep(0.05, 1800, 1800, 0.04, true, 0.3), 0.12);
    if (name == "SND_BOAT") return warble(0.5, 70, 10, 5, 0.3);
    return {};
}

// 8/16-bit PCM WAV at any rate -> mono float at kSampleRate.
bool decode_wav(const std::vector<uint8_t>& f, Wave& out) {
    auto u16 = [&](size_t o) { return unsigned(f[o] | (f[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return uint32_t(u16(o) | (u16(o + 2) << 16)); };
    if (f.size() < 44 || std::memcmp(f.data(), "RIFF", 4) != 0 || std::memcmp(f.data() + 8, "WAVE", 4) != 0)
        return false;
    unsigned channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t data = 0, size = 0;
    for (size_t o = 12; o + 8 <= f.size();) {
        const uint32_t len = u32(o + 4);
        if (std::memcmp(f.data() + o, "fmt ", 4) == 0 && o + 24 <= f.size()) {
            if (u16(o + 8) != 1) return false;
            channels = u16(o + 10);
            rate = u32(o + 12);
            bits = u16(o + 22);
        } else if (std::memcmp(f.data() + o, "data", 4) == 0) {
            data = o + 8;
            size = std::min<size_t>(len, f.size() - data);
            break;
        }
        o += 8 + len + (len & 1);
    }
    if (!data || !rate || !channels || (bits != 8 && bits != 16)) return false;
    const size_t frame = channels * bits / 8;
    const size_t frames = size / frame;
    Wave in(frames);
    for (size_t i = 0; i < frames; i++) {
        const size_t o = data + i * frame;
        in[i] = bits == 8 ? (float(f[o]) - 128.0f) / 128.0f : float(int16_t(u16(o))) / 32768.0f;
    }
    const double step = double(rate) / VicDual::kSampleRate;
    out.resize(size_t(double(frames) / step));
    for (size_t i = 0; i < out.size(); i++) {
        const double p = double(i) * step;
        const size_t k = size_t(p);
        const double fr = p - double(k);
        const float a = in[std::min(k, frames - 1)], b = in[std::min(k + 1, frames - 1)];
        out[i] = float(a + (b - a) * fr);
    }
    return true;
}

const std::vector<std::string>& sample_names(VicDual::Game game, std::string* set) {
    static const std::vector<std::string> depthch = {"longex", "shortex", "spray", "bonus", "sonar"};
    static const std::vector<std::string> invinco = {"saucer", "move1", "move2", "fire", "invhit", "shiphit", "move3", "move4"};
    static const std::vector<std::string> pulsar = {"clang", "key", "alienhit", "phit", "ashoot", "pshoot",
                                                    "bonus", "sizzle", "gate", "birth", "hbeat", "movmaze"};
    static const std::vector<std::string> carnival = {"bear", "bonus1", "bonus2", "clang", "duck1",
                                                      "duck2", "duck3", "pipehit", "ranking", "rifle"};
    static const std::vector<std::string> nsub = {"SND_EXPL_L0", "SND_EXPL_L1", "SND_SONAR", "SND_LAUNCH0",
                                                  "SND_LAUNCH1", "SND_WARNING0", "SND_WARNING1", "SND_EXPL_S0",
                                                  "SND_EXPL_S1", "SND_BONUS0", "SND_BONUS1", "SND_CODE", "SND_BOAT"};
    static const std::vector<std::string> none;
    switch (game) {
        case VicDual::Game::DepthCharge: *set = "depthch"; return depthch;
        case VicDual::Game::Invinco:
        case VicDual::Game::InvincoHeadOn2:
        case VicDual::Game::InvincoDeepScan: *set = "invinco"; return invinco;
        case VicDual::Game::Pulsar: *set = "pulsar"; return pulsar;
        case VicDual::Game::Carnival: *set = "carnival"; return carnival;
        case VicDual::Game::NSub: *set = "nsub"; return nsub;
        default: set->clear(); return none;
    }
}

}  // namespace

VicDual::VicDual(Game game)
    : game_(game), cpu_(kCpuClock),
      native_(size_t(kNativeWidth * kNativeHeight), 0xff000000u),
      output_(size_t(kNativeWidth * kNativeHeight), 0xff000000u) {
    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    configure();
}

VicDual::~VicDual() = default;

void VicDual::configure() {
    switch (game_) {
        case Game::HeadOn:
        case Game::HeadOn2:
        case Game::HeadOn2Slim:
        case Game::SpaceAttackHeadOn:
            sound_ = Audio::HeadOn;
            break;
        case Game::InvincoHeadOn2:  // Invinco samples + Head On discrete
        case Game::DepthCharge:
        case Game::Invinco:
        case Game::InvincoDeepScan:
        case Game::Pulsar:
        case Game::NSub:
            sound_ = Audio::Samples;
            break;
        case Game::Carnival:
            sound_ = Audio::Carnival;
            break;
        case Game::Frogs:
            sound_ = Audio::Frogs;
            break;
        case Game::Borderline:
        case Game::TranqGun:
            sound_ = Audio::Borderline;
            break;
        default:
            sound_ = Audio::None;
            break;
    }
    if (game_ == Game::Carnival) {
        music_cpu_ = std::make_unique<Mcs48>(3579545, Mcs48::Chip::I8035);
        psg_ = std::make_unique<AY8910>(3579545 / 3, 1.0f);
        music_cpu_->set_io_handlers(
            [this](uint16_t port) -> uint8_t {
                // T1: comms from audio port 2 d3.
                if (port == MCS48_PORT_T1) return uint8_t((~port2_state_ >> 3) & 1);
                return 0xff;
            },
            [this](uint16_t port, uint8_t data) {
                if (port == MCS48_PORT_P1) {
                    music_data_ = data;  // AY8912 d0-d7
                } else if (port == MCS48_PORT_P2) {
                    music_bus_ = uint8_t((data >> 6) & 3);  // d6 BDIR, d7 BC1
                } else {
                    return;
                }
                if (music_bus_ & 1) {
                    if (music_bus_ & 2) psg_->control(music_data_);
                    else {
                        psg_->write(music_data_);
                        psg_writes_++;
                    }
                }
            });
    }
}

bool VicDual::rotated() const {
    switch (game_) {
        case Game::DepthCharge:
        case Game::Safari:
        case Game::Frogs:
        case Game::HeadOn:
        case Game::HeadOn2:
            return false;  // ROT0
        default:
            return true;   // ROT270
    }
}

VicDual::Map VicDual::map() const {
    switch (game_) {
        case Game::Safari: return Map::Safari;
        case Game::SpaceAttack:
        case Game::HeadOn:
        case Game::HeadOn2:
        case Game::Digger: return Map::HeadOn;
        case Game::Invinco:
        case Game::NSub: return Map::Invinco;
        case Game::Samurai: return Map::Vid8000Samurai;
        default: return Map::Vid8000;
    }
}

VicDual::IoRead VicDual::io_read_type() const {
    switch (game_) {
        case Game::DepthCharge:
        case Game::Safari:
        case Game::Frogs:
        case Game::HeadOn:
        case Game::NSub: return IoRead::Logic18;
        case Game::SpaceAttack:
        case Game::HeadOn2:
        case Game::Digger:
        case Game::Invinco: return IoRead::Logic148;
        default: return IoRead::Dual4;
    }
}

uint8_t VicDual::io_mask() const {
    switch (game_) {
        case Game::TranqGun:
        case Game::Borderline:
        case Game::Heiankyo: return 0x0f;
        default: return io_read_type() == IoRead::Dual4 ? 0x7f : 0x1f;
    }
}

const char* VicDual::title() const {
    switch (game_) {
        case Game::DepthCharge: return "Depthcharge";
        case Game::Safari: return "Safari";
        case Game::Frogs: return "Frogs";
        case Game::SpaceAttack: return "Space Attack";
        case Game::SpaceAttackHeadOn: return "Space Attack / Head On";
        case Game::HeadOn: return "Head On";
        case Game::HeadOn2: return "Head On 2";
        case Game::HeadOn2Slim: return "Head On 2 (Slimline)";
        case Game::InvincoHeadOn2: return "Invinco / Head On 2";
        case Game::NSub: return "N-Sub";
        case Game::Samurai: return "Samurai";
        case Game::Invinco: return "Invinco";
        case Game::InvincoDeepScan: return "Invinco / Deep Scan";
        case Game::TranqGun: return "Tranquillizer Gun";
        case Game::SpaceTrek: return "Space Trek";
        case Game::Carnival: return "Carnival";
        case Game::Borderline: return "Borderline";
        case Game::Digger: return "Digger";
        case Game::Pulsar: return "Pulsar";
        case Game::Heiankyo: return "Heiankyo Alien";
        case Game::AlphaFighter: return "Alpha Fighter / Head On";
    }
    return "VIC Dual";
}

bool VicDual::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> rom(0x4000, 0);
    const std::vector<RomEntry>* cpu = nullptr;
    const std::vector<RomEntry>* prom = nullptr;
    switch (game_) {
        case Game::DepthCharge: cpu = &kDepthChargeCpu; break;
        case Game::Safari: cpu = &kSafariCpu; break;
        case Game::Frogs: cpu = &kFrogsCpu; break;
        case Game::SpaceAttack: cpu = &kSpaceAttackCpu; prom = &kSpaceAttackProm; break;
        case Game::SpaceAttackHeadOn: cpu = &kSpaceAttackHeadOnCpu; prom = &kSpaceAttackHeadOnProm; break;
        case Game::HeadOn: cpu = &kHeadOnCpu; prom = &kHeadOnProm; break;
        case Game::HeadOn2: cpu = &kHeadOn2Cpu; prom = &kHeadOn2Prom; break;
        case Game::HeadOn2Slim: cpu = &kHeadOn2SlimCpu; prom = &kHeadOn2SlimProm; break;
        case Game::InvincoHeadOn2: cpu = &kInvincoHeadOn2Cpu; prom = &kInvincoHeadOn2Prom; break;
        case Game::NSub: cpu = &kNSubCpu; prom = &kNSubProm; break;
        case Game::Samurai: cpu = &kSamuraiCpu; prom = &kSamuraiProm; break;
        case Game::Invinco: cpu = &kInvincoCpu; prom = &kInvincoProm; break;
        case Game::InvincoDeepScan: cpu = &kInvincoDeepScanCpu; prom = &kInvincoDeepScanProm; break;
        case Game::TranqGun: cpu = &kTranqGunCpu; prom = &kTranqGunProm; break;
        case Game::SpaceTrek: cpu = &kSpaceTrekCpu; prom = &kSpaceTrekProm; break;
        case Game::Carnival: cpu = &kCarnivalCpu; prom = &kCarnivalProm; break;
        case Game::Borderline: cpu = &kBorderlineCpu; prom = &kBorderlineProm; break;
        case Game::Digger: cpu = &kDiggerCpu; prom = &kDiggerProm; break;
        case Game::Pulsar: cpu = &kPulsarCpu; prom = &kPulsarProm; break;
        case Game::Heiankyo: cpu = &kHeiankyoCpu; prom = &kHeiankyoProm; break;
        case Game::AlphaFighter: cpu = &kAlphaFighterCpu; break;
    }
    if (!cpu || !loader.load(*cpu, rom, error)) return false;
    std::copy(rom.begin(), rom.end(), rom_.begin());
    has_prom_ = false;
    color_prom_.fill(0);
    if (prom) {
        std::vector<uint8_t> p(0x20, 0);
        std::string perr;
        if (loader.load(*prom, p, &perr)) {
            std::copy(p.begin(), p.end(), color_prom_.begin());
            has_prom_ = true;
        }
    }
    if (game_ == Game::Carnival) {
        std::vector<uint8_t> music(0x400, 0);
        std::string merr;
        std::fill(music_cpu_->rom(), music_cpu_->rom() + Mcs48::kRomSize, 0);
        if (loader.load(kCarnivalMusic, music, &merr)) {
            std::copy(music.begin(), music.end(), music_cpu_->rom());
        } else {
            warnings_.push_back("Carnival music ROM epr-412.u5 not found: no music");
        }
    }
    const auto w = loader.warnings();
    warnings_.insert(warnings_.end(), w.begin(), w.end());
    return true;
}

void VicDual::load_samples(const std::string& rom_path) {
    std::string set;
    const auto& names = sample_names(game_, &set);
    samples_.assign(names.size(), Sample{});
    samples_from_files_ = false;
    if (names.empty()) return;
    // MAME keeps samples in a "samples" folder next to the ROM folder.
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::path(rom_path);
    if (!fs::is_directory(base, ec)) base = base.parent_path();
    std::vector<fs::path> candidates = {base / "samples" / (set + ".zip"), base / "samples" / set,
                                        base.parent_path() / "samples" / (set + ".zip"),
                                        base.parent_path() / "samples" / set, base / (set + "_samples.zip")};
    int loaded = 0;
    for (const fs::path& c : candidates) {
        if (!fs::exists(c, ec)) continue;
        RomLoader loader;
        std::string err;
        if (!loader.open(c.string(), &err)) continue;
        for (size_t i = 0; i < names.size(); i++) {
            std::vector<uint8_t> wav;
            if (loader.try_read(names[i] + ".wav", wav) && decode_wav(wav, samples_[i].data)) loaded++;
        }
        if (loaded) break;
    }
    samples_from_files_ = loaded > 0;
    for (size_t i = 0; i < names.size(); i++) {
        if (samples_[i].data.empty()) samples_[i].data = synth_sample(names[i]);
    }
    if (!samples_from_files_) {
        warnings_.push_back("MAME samples for '" + set +
                            "' not found (samples/" + set + ".zip): using synthesized sound effects");
    }
}

bool VicDual::init(const std::string& rom_path, std::string* error) {
    warnings_.clear();
    if (!load_roms(rom_path, error)) return false;
    load_samples(rom_path);
    reset();
    return true;
}

void VicDual::reset() {
    cpu_.reset();
    videoram_.fill(0);
    ram_.fill(0);
    safari_ram_.fill(0);
    characterram_.fill(0);
    coin_status_ = 0;
    coin_clear_at_ = -1;
    // Head On 2 powers up with palette bank 3 (MAME machine_reset).
    palette_bank_ = game_ == Game::HeadOn2 ? 3 : 0;
    scanline_ = 0;
    hblank_ = false;
    cycles_ = 0;
    samurai_protection_ = 0;
    tranqgun_prot_ = 0;
    nsub_play_counter_ = 0;
    nsub_next_pulse_ = 0;
    port1_state_ = 0;
    port2_state_ = game_ == Game::NSub ? 0xff : 0;
    for (auto& c : channels_) c = Channel{};
    audio_.clear();
    audio_frac_ = 0;
    hp_in_ = hp_out_ = 0;
    ho_ = HeadOn{};
    for (auto& v : net_) v = NetVoice{};
    game_select_ = false;
    if (music_cpu_) {
        music_cpu_->reset();
        music_cpu_->set_reset_line(IrqLine::Assert);  // held until port 2 d4 goes high
        psg_->reset();
        music_data_ = music_bus_ = 0;
        music_cycle_acc_ = 0;
    }
    std::fill(native_.begin(), native_.end(), 0xff000000u);
    rotate_output();
}

// ---------------------------------------------------------------------------
// Memory

uint8_t VicDual::read_byte(uint16_t address) {
    switch (map()) {
        case Map::Vid8000:
        case Map::Vid8000Samurai:
            if (address < 0x8000) {
                if (game_ == Game::TranqGun && address >= 0x4000) {
                    return (address - 0x4000) == 0x3800 ? tranqgun_prot_ : 0x00;
                }
                return rom_[address & 0x3fff];
            }
            break;
        case Map::HeadOn:
            if (address < 0x8000) return rom_[address & 0x1fff];
            if (address < 0xc000) return 0;
            break;
        case Map::Invinco:
            if (address < 0x8000) return rom_[address & 0x3fff];
            if (address < 0xc000) return 0;
            break;
        case Map::Safari:
            if (address < 0x4000) return rom_[address];
            if (address < 0x8000) return 0;
            if (address < 0xc000) return safari_ram_[address & 0xfff];
            break;
    }
    // Video area: $8000 (mirror $7000) or $C000 (mirror $3000).
    const uint16_t off = address & 0x0fff;
    if (off < 0x400) return videoram_[off];
    if (off < 0x800) return ram_[off & 0x3ff];
    return characterram_[off & 0x7ff];
}

void VicDual::write_byte(uint16_t address, uint8_t value) {
    switch (map()) {
        case Map::Vid8000:
            if (address < 0x8000) {
                if (game_ == Game::TranqGun && address == 0x4000) {
                    if (value == 0xd8) tranqgun_prot_ = 0x02;
                    else if (value == 0x3a) tranqgun_prot_ = 0x01;
                    else if (value == 0x6a) tranqgun_prot_ = 0x06;
                }
                return;
            }
            break;
        case Map::Vid8000Samurai:
            if (address < 0x8000) {
                samurai_protection_ = value;
                return;
            }
            break;
        case Map::HeadOn:
        case Map::Invinco:
            if (address < 0xc000) return;
            break;
        case Map::Safari:
            if (address < 0x8000) return;
            if (address < 0xc000) {
                safari_ram_[address & 0xfff] = value;
                return;
            }
            break;
    }
    const uint16_t off = address & 0x0fff;
    if (off < 0x400) videoram_[off] = value;
    else if (off < 0x800) ram_[off & 0x3ff] = value;
    else characterram_[off & 0x7ff] = value;
}

// ---------------------------------------------------------------------------
// Inputs (MAME INPUT_PORTS per game).  Bits are active low unless noted.

bool VicDual::timer_value() const {
    // 500 Hz square wave: toggles every 2 ms of emulated time.
    return ((cycles_ * 500 / int64_t(kCpuClock)) & 1) != 0;
}

uint8_t VicDual::input_port(int n) {
    const InputState& p1 = host_.player1;
    const InputState& p2 = host_.player2;
    const bool v64 = ((vcounter() >> 6) & 1) != 0;
    const bool vblank_comp = vcounter() < kVBlankStart;
    const bool cblank_comp = vblank_comp && !hblank_;
    const bool timer = timer_value();
    const bool coin = coin_status_ != 0;
    uint8_t v = 0xff;
    // Active-low control: clear the bit while pressed.
    auto low = [&v](uint8_t mask, bool pressed) { if (pressed) v = uint8_t(v & ~mask); };
    // Active-high signal or fixed level.
    auto set = [&v](uint8_t mask, bool level) { v = level ? uint8_t(v | mask) : uint8_t(v & ~mask); };

    switch (game_) {
        case Game::DepthCharge:
            if (n == 0) { low(0x01, p1.button2); low(0x02, p1.button1); low(0x04, p1.right); low(0x08, p1.left); }
            else { set(0x01, v64); set(0x80, coin); }
            break;
        case Game::Safari:
            if (n == 0) {
                low(0x01, p1.up); low(0x02, p1.down); low(0x04, p1.right); low(0x08, p1.left);
                low(0x10, p1.button2); low(0x20, p1.button3); low(0x80, p1.button1);
            } else { set(0x01, v64); set(0x80, coin); }
            break;
        case Game::Frogs:
            if (n == 0) { low(0x01, p1.right); low(0x02, p1.up); low(0x04, p1.left); low(0x80, p1.button1); }
            else { set(0x01, v64); set(0x80, coin); }
            break;
        case Game::HeadOn:
            if (n == 0) {
                v = uint8_t(v & ~0x07);  // lives / demo sounds DIPs at their defaults
                low(0x08, p1.button1 || p1.start); low(0x10, p1.right); low(0x20, p1.down);
                low(0x40, p1.left); low(0x80, p1.up);
            } else { set(0x01, v64); set(0x80, coin); }
            break;
        case Game::SpaceAttack:
            if (n == 0) {
                low(0x01, p1.right); low(0x02, p1.button1); low(0x04, p1.start); low(0x08, p2.start);
                low(0x10, p2.button1); low(0x20, p2.right); low(0x40, p2.left); low(0x80, p1.left);
            } else if (n == 1) {
                v = 0x6e;
            } else { set(0x01, timer); set(0x80, coin); }
            break;
        case Game::HeadOn2:
            if (n == 0) {
                low(0x01, p1.start); low(0x02, p2.start); low(0x08, p1.button1); low(0x10, p1.right);
                low(0x20, p1.down); low(0x40, p1.left); low(0x80, p1.up);
            } else if (n == 2) { set(0x80, coin); }
            break;
        case Game::Digger:
            if (n == 0) {
                low(0x01, p1.start); low(0x02, p2.start); low(0x04, p1.button1); low(0x08, p1.button2);
                low(0x10, p1.right); low(0x20, p1.down); low(0x40, p1.left); low(0x80, p1.up);
            } else if (n == 1) { v = 0x63; }
            else { set(0x01, cblank_comp); set(0x80, coin); }
            break;
        case Game::NSub:
            if (n == 0) {
                low(0x01, p1.start); low(0x02, p2.start); low(0x04, p1.button1); low(0x08, p1.button2);
                low(0x10, p1.right); low(0x20, p1.down); low(0x40, p1.left); low(0x80, p1.up);
            } else { set(0x01, cblank_comp); set(0x80, coin); }
            break;
        case Game::Invinco:
            if (n == 0) {
                low(0x01, p1.start); low(0x02, p2.start); low(0x08, p1.button1); low(0x10, p1.right); low(0x40, p1.left);
            } else if (n == 1) { v = 0x60; }
            else { set(0x01, cblank_comp); set(0x80, coin); }
            break;
        // ---- dual game boards: IN0-IN3 on ports 0-3 ----
        case Game::InvincoHeadOn2:
        case Game::SpaceAttackHeadOn:
        case Game::AlphaFighter:
            // 0x04: lives DIPs (MAME defaults read 0), 0x08: SW1:5 = 0.
            if (n == 0) { set(0x0c, false); low(0x10, p1.down); low(0x20, p1.up); if (game_ != Game::InvincoHeadOn2) { low(0x01, p2.up); low(0x02, p2.button1); } }
            if (n == 1) { set(0x04, false); set(0x08, cblank_comp); low(0x10, p1.left); low(0x20, p1.right); if (game_ != Game::InvincoHeadOn2) low(0x01, p2.right); }
            if (n == 2) { set(0x04, false); set(0x08, timer); low(0x10, p1.start); low(0x20, p1.button1); if (game_ != Game::InvincoHeadOn2) low(0x01, p2.down); }
            if (n == 3) { set(0x04, false); set(0x08, coin); low(0x10, game_select_); low(0x20, p2.start); if (game_ != Game::InvincoHeadOn2) low(0x01, p2.left); }
            break;
        case Game::InvincoDeepScan:
            if (n == 0) { set(0x0c, false); low(0x20, p1.button1); }
            if (n == 1) { set(0x04, false); set(0x08, cblank_comp); low(0x10, p1.left); low(0x20, p1.right); }
            if (n == 2) { set(0x04, false); set(0x08, timer); low(0x10, p1.start); low(0x20, p1.button2); }
            if (n == 3) { set(0x04, false); set(0x08, coin); low(0x10, game_select_); low(0x20, p2.start); }
            break;
        case Game::TranqGun:
            if (n == 0) { set(0x0c, false); low(0x01, p2.up); low(0x02, p2.button1); low(0x10, p1.down); low(0x20, p1.up); }
            if (n == 1) { set(0x04, false); set(0x08, vblank_comp); low(0x01, p2.right); low(0x10, p1.left); low(0x20, p1.right); }
            if (n == 2) { set(0x04, false); set(0x08, timer); low(0x01, p2.down); low(0x10, p1.start); low(0x20, p1.button1); }
            if (n == 3) { set(0x04, false); set(0x08, coin); low(0x01, p2.left); low(0x20, p2.start); }
            break;
        case Game::SpaceTrek:
            if (n == 0) { set(0x08, false); low(0x10, p1.right); low(0x20, p1.left); }
            if (n == 1) { set(0x04, false); set(0x08, cblank_comp); low(0x10, p1.down); low(0x20, p1.up); }
            if (n == 2) { set(0x02, false); set(0x08, timer); low(0x10, p1.start); low(0x20, p1.button1); }
            if (n == 3) { set(0x06, false); set(0x08, coin); low(0x10, p1.button2); low(0x20, p2.start); }
            break;
        case Game::Carnival:
            if (n == 0) set(0x1c, false);
            if (n == 1) { set(0x04, false); set(0x08, cblank_comp); low(0x10, p1.left); low(0x20, p1.right); }
            if (n == 2) { set(0x04, false); set(0x08, timer); low(0x10, p1.start); low(0x20, p1.button1); }
            if (n == 3) { set(0x04, false); set(0x08, coin); low(0x20, p2.start); }
            break;
        case Game::Borderline:
            if (n == 0) { set(0x0c, false); low(0x01, p2.up); low(0x02, p2.button1); low(0x10, p1.down); low(0x20, p1.up); }
            if (n == 1) { set(0x08, vblank_comp); low(0x01, p2.right); low(0x10, p1.left); low(0x20, p1.right); }
            if (n == 2) { set(0x08, v64); low(0x01, p2.down); low(0x10, p1.start); low(0x20, p1.button1); }
            if (n == 3) { set(0x08, !coin); low(0x01, p2.left); low(0x20, p2.start); }  // coin is active low here
            break;
        case Game::Pulsar:
            if (n == 0) { set(0x08, false); low(0x10, p1.down); low(0x20, p1.up); }
            if (n == 1) { set(0x04, false); set(0x08, cblank_comp); low(0x10, p1.left); low(0x20, p1.right); }
            if (n == 2) { set(0x04, false); set(0x08, timer); low(0x10, p1.start); low(0x20, p1.button1); }
            if (n == 3) { set(0x04, false); set(0x08, coin); low(0x20, p2.start); }
            break;
        case Game::Heiankyo:
            if (n == 0) { set(0x08, false); low(0x01, p2.up); low(0x02, p2.button1); low(0x10, p1.up); low(0x20, p1.button1); }
            if (n == 1) { set(0x08, cblank_comp); low(0x01, p2.right); low(0x02, p2.button2); low(0x10, p1.right); low(0x20, p1.button2); }
            if (n == 2) { set(0x22, false); set(0x08, timer); low(0x01, p2.down); low(0x10, p1.down); }
            if (n == 3) { set(0x04, false); set(0x08, coin); low(0x01, p2.left); low(0x02, p2.start); low(0x10, p1.left); low(0x20, p1.start); }
            break;
        case Game::HeadOn2Slim:
            if (n == 0) { set(0x08, false); low(0x01, p2.up); low(0x02, p2.button1); low(0x10, p1.down); low(0x20, p1.up); }
            if (n == 1) { low(0x01, p2.right); low(0x10, p1.left); low(0x20, p1.right); }
            if (n == 2) { low(0x01, p2.down); low(0x10, p1.start); low(0x20, p1.button1); }
            if (n == 3) { set(0x08, coin); low(0x01, p2.left); low(0x20, p2.start); }
            break;
        case Game::Samurai: {
            // Protection: $AB -> 0x02, $1D -> 0x0c; ports 1-3 read bits 1-3.
            const uint8_t answer = samurai_protection_ == 0xab ? 0x02 : samurai_protection_ == 0x1d ? 0x0c : 0x00;
            if (n == 0) { low(0x10, p1.down); low(0x20, p1.up); }
            if (n == 1) { set(0x02, (answer >> 1) & 1); set(0x04, false); set(0x08, cblank_comp); low(0x10, p1.left); low(0x20, p1.right); }
            if (n == 2) { set(0x02, (answer >> 2) & 1); set(0x04, false); set(0x08, timer); low(0x10, p1.start); low(0x20, p1.button1); }
            if (n == 3) { set(0x02, (answer >> 3) & 1); set(0x04, false); set(0x08, coin); low(0x20, p2.start); }
            break;
        }
    }
    return v;
}

uint8_t VicDual::read_port(uint16_t port) {
    const uint8_t o = uint8_t(port & io_mask());
    switch (io_read_type()) {
        case IoRead::Logic18: {
            uint8_t data = 0xff;
            if (o & 0x01) data &= input_port(0);
            if (o & 0x08) data &= input_port(1);
            return data;
        }
        case IoRead::Logic148: {
            uint8_t data = 0xff;
            if (o & 0x01) data &= input_port(0);
            if (o & 0x04) data &= input_port(1);
            if (o & 0x08) data &= input_port(2);
            return data;
        }
        case IoRead::Dual4:
            return input_port(o & 3);
    }
    return 0xff;
}

void VicDual::write_port(uint16_t port, uint8_t value) {
    const uint8_t o = uint8_t(port & io_mask());
    // No decoder, just logic gates: every selected bit acts.
    switch (game_) {
        case Game::DepthCharge:
            if (o & 0x01) coin_status_ = 1;
            if (o & 0x04) depthch_audio_w(value);
            break;
        case Game::Safari:
            if (o & 0x01) coin_status_ = 1;
            break;
        case Game::Frogs:
            if (o & 0x01) coin_status_ = 1;
            if (o & 0x02) netlist_audio_w(value);
            break;
        case Game::HeadOn:
        case Game::SpaceAttack:
        case Game::HeadOn2:
            if (o & 0x01) coin_status_ = 1;
            if ((o & 0x02) && game_ != Game::SpaceAttack) headon_audio_w(value);
            break;
        case Game::Digger:
            if (o & 0x01) coin_status_ = 1;
            if (o & 0x04) palette_bank_w(value & 3);
            break;
        case Game::NSub:
            if (o & 0x01) coin_status_ = 1;
            if (o & 0x02) nsub_audio_w(value);
            if (o & 0x04) palette_bank_w(value);
            break;
        case Game::Invinco:
            if (o & 0x01) coin_status_ = 1;
            if (o & 0x02) invinco_audio_w(value);
            if (o & 0x04) palette_bank_w(value);
            break;
        case Game::InvincoHeadOn2:
            if (o & 0x01) invho2_audio_w(value);
            if (o & 0x02) invinco_audio_w(value);
            if (o & 0x08) coin_status_ = 1;
            if (o & 0x40) palette_bank_w(value);
            break;
        case Game::InvincoDeepScan:
            if (o & 0x01) invinco_audio_w(value);
            if (o & 0x08) coin_status_ = 1;
            if (o & 0x40) palette_bank_w(value);
            break;
        case Game::SpaceAttackHeadOn:
            if (o & 0x01) invho2_audio_w(value);
            if (o & 0x08) coin_status_ = 1;
            if (o & 0x40) palette_bank_w(value);
            break;
        case Game::TranqGun:
        case Game::Borderline:
            if (o & 0x01) netlist_audio_w(value);
            if (o & 0x02) palette_bank_w(value);
            if (o & 0x08) coin_status_ = 1;
            break;
        case Game::SpaceTrek:
        case Game::AlphaFighter:
            if (o & 0x08) coin_status_ = 1;
            if (o & 0x40) palette_bank_w(value);
            break;
        case Game::Carnival:
            if (o & 0x01) carnival_audio_1_w(value);
            if (o & 0x02) carnival_audio_2_w(value);
            if (o & 0x08) coin_status_ = 1;
            if (o & 0x40) palette_bank_w(value);
            break;
        case Game::Pulsar:
            if (o & 0x01) pulsar_audio_1_w(value);
            if (o & 0x02) pulsar_audio_2_w(value);
            if (o & 0x08) coin_status_ = 1;
            if (o & 0x40) palette_bank_w(value);
            break;
        case Game::Heiankyo:
            if (o & 0x08) coin_status_ = 1;
            break;
        case Game::HeadOn2Slim:
            if (o & 0x01) invho2_audio_w(value);
            if (o & 0x02) palette_bank_w(uint8_t((value & 3) ^ 1));
            if (o & 0x08) coin_status_ = 1;
            break;
        case Game::Samurai:
            if (o & 0x02) palette_bank_w(value);
            if (o & 0x08) coin_status_ = 1;
            break;
    }
}

// The main CPU is reset when a coin is inserted; the coin switch stays
// closed for 70 ms, after which the coin status line clears.
void VicDual::coin_in() {
    cpu_.reset();
    coin_clear_at_ = cycles_ + int64_t(kCpuClock) * 70 / 1000;
}

void VicDual::set_inputs(const MachineInputs& inputs) {
    host_ = inputs;
    const bool coin = inputs.coin1 || inputs.coin2;
    if (coin && !prev_coin_) {
        if (game_ == Game::NSub) nsub_play_counter_++;  // 1 coin / 1 credit (default coinage)
        else coin_in();
    }
    prev_coin_ = coin;
    // "Game Select" toggle on the two-game boards.
    const bool select = game_ == Game::InvincoDeepScan ? inputs.player1.button3 : inputs.player1.button2;
    if (select && !prev_select_) game_select_ = !game_select_;
    prev_select_ = select;
}

void VicDual::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) {
        dsw_ = value;
        dsw_set_ = true;
    }
}

// ---------------------------------------------------------------------------
// Video

void VicDual::render_line(int y) {
    uint32_t* out = &native_[size_t(y * kNativeWidth)];
    const bool color = has_prom_;
    for (int col = 0; col < 32; col++) {
        const uint8_t code = videoram_[size_t(((y >> 3) << 5) | col)];
        const uint8_t bits = characterram_[size_t((code << 3) | (y & 7))];
        uint32_t fore = 0xffffffffu, back = 0xff000000u;
        if (color) {
            const uint8_t pr = color_prom_[size_t(((code >> 5) | (palette_bank_ << 3)) & 0x1f)];
            back = kPens[(pr >> 1) & 7];
            fore = kPens[(pr >> 5) & 7];
        }
        for (int b = 0; b < 8; b++) out[col * 8 + b] = ((bits << b) & 0x80) ? fore : back;
    }
}

void VicDual::rotate_output() {
    if (!rotated()) {
        output_ = native_;
        return;
    }
    // ROT270: output (x, y) = native (255 - y, x); 224 x 256.
    const int ow = kNativeHeight;
    for (int y = 0; y < kNativeWidth; y++) {
        for (int x = 0; x < ow; x++) {
            output_[size_t(y * ow + x)] = native_[size_t(x * kNativeWidth + (kNativeWidth - 1 - y))];
        }
    }
}

void VicDual::run_frame() {
    const double samples_per_line = double(kSampleRate) / (kFramesPerSecond * kVTotal);
    const double music_per_line =
        music_cpu_ ? double(music_cpu_->clock()) / (kFramesPerSecond * kVTotal) : 0.0;
    for (scanline_ = 0; scanline_ < kVTotal; ++scanline_) {
        hblank_ = false;
        cpu_.run(kVisibleCycles);
        hblank_ = true;
        cpu_.run(kCyclesPerLine - kVisibleCycles);
        cycles_ += kCyclesPerLine;
        if (scanline_ < kNativeHeight) render_line(scanline_);
        if (coin_clear_at_ >= 0 && cycles_ >= coin_clear_at_) {
            coin_status_ = 0;
            coin_clear_at_ = -1;
        }
        if (game_ == Game::NSub && cycles_ >= nsub_next_pulse_) {
            // Play-counter 555: one coin pulse every 150 ms while credits are due.
            nsub_next_pulse_ = cycles_ + int64_t(kCpuClock) * 150 / 1000;
            if (nsub_play_counter_ > 0) {
                nsub_play_counter_--;
                coin_in();
            }
        }
        if (music_cpu_) {
            music_cycle_acc_ += music_per_line;
            const int n = int(music_cycle_acc_);
            music_cycle_acc_ -= n;
            if (n > 0) music_cpu_->run(n);
        }
        audio_frac_ += samples_per_line;
        const int n = int(audio_frac_);
        audio_frac_ -= n;
        if (n > 0) generate_audio(n);
    }
    rotate_output();
}

void VicDual::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Sound latches, as in MAME.

void VicDual::play(int channel, int sample, bool loop) {
    if (sample < 0 || size_t(sample) >= samples_.size() || samples_[size_t(sample)].data.empty()) return;
    Channel& c = channels_[size_t(channel)];
    c.sample = sample;
    c.pos = 0;
    c.loop = loop;
}

void VicDual::stop(int channel) { channels_[size_t(channel)].sample = -1; }

void VicDual::depthch_audio_w(uint8_t data) {
    const uint8_t changed = uint8_t(port1_state_ ^ data), high = uint8_t(changed & data), low = uint8_t(changed & ~data);
    port1_state_ = data;
    enum { LONGEX, SHORTEX, SPRAY, BONUS, SONAR };
    if (high & 0x01) play(LONGEX, LONGEX, false);
    if (high & 0x02) play(SHORTEX, SHORTEX, false);
    if (high & 0x04) play(SPRAY, SPRAY, false);
    if (high & 0x08) play(SONAR, SONAR, true);
    if (low & 0x08) {
        stop(SONAR);
        play(BONUS, BONUS, false);  // bonus sound on the same line as sonar
    }
}

void VicDual::invinco_audio_w(uint8_t data) {
    const uint8_t changed = uint8_t(port2_state_ ^ data), low = uint8_t(changed & ~data);
    port2_state_ = data;
    enum { SAUCER, MOVE1, MOVE2, FIRE, INVHIT, SHIPHIT };
    if (low & 0x04) play(SAUCER, SAUCER, false);
    if (low & 0x08) play(MOVE1, MOVE1, false);
    if (low & 0x10) play(MOVE2, MOVE2, false);
    if (low & 0x20) play(FIRE, FIRE, false);
    if (low & 0x40) play(INVHIT, INVHIT, false);
    if (low & 0x80) play(SHIPHIT, SHIPHIT, false);
}

void VicDual::pulsar_audio_1_w(uint8_t data) {
    const uint8_t changed = uint8_t(port1_state_ ^ data), low = uint8_t(changed & ~data);
    port1_state_ = data;
    enum { CLANG, KEY, ALIENHIT, PHIT, ASHOOT, PSHOOT, BONUS };
    if (low & 0x01) play(CLANG, CLANG, false);
    if (low & 0x02) play(KEY, KEY, false);
    if (low & 0x04) play(ALIENHIT, ALIENHIT, false);
    if (low & 0x08) play(PHIT, PHIT, false);
    if (low & 0x10) play(ASHOOT, ASHOOT, false);
    if (low & 0x20) play(PSHOOT, PSHOOT, false);
    if (low & 0x40) play(BONUS, BONUS, false);
}

void VicDual::pulsar_audio_2_w(uint8_t data) {
    const uint8_t changed = uint8_t(port2_state_ ^ data), high = uint8_t(changed & data), low = uint8_t(changed & ~data);
    port2_state_ = data;
    enum { CLANG = 0, SIZZLE = 7, GATE, BIRTH, HBEAT, MOVMAZE };
    if (low & 0x01) play(SIZZLE, SIZZLE, false);
    if (low & 0x02) play(CLANG, GATE, false);
    if (high & 0x02) stop(CLANG);
    if (low & 0x04) play(BIRTH, BIRTH, false);
    if (low & 0x08) play(HBEAT, HBEAT, true);
    if (high & 0x08) stop(HBEAT);
    if (low & 0x10) play(MOVMAZE, MOVMAZE, true);
    if (high & 0x10) stop(MOVMAZE);
}

void VicDual::carnival_audio_1_w(uint8_t data) {
    const uint8_t changed = uint8_t(port1_state_ ^ data), high = uint8_t(changed & data), low = uint8_t(changed & ~data);
    port1_state_ = data;
    enum { BEAR, BONUS1, BONUS2, CLANG, DUCK1, DUCK2, DUCK3, PIPEHIT, RANKING, RIFLE };
    if (low & 0x01) play(RIFLE, RIFLE, false);
    if (low & 0x02) play(CLANG, CLANG, false);
    if (low & 0x04) play(DUCK1, DUCK1, true);
    if (high & 0x04) stop(DUCK1);
    if (low & 0x08) play(DUCK2, DUCK2, true);
    if (high & 0x08) stop(DUCK2);
    if (low & 0x10) play(DUCK3, DUCK3, true);
    if (high & 0x10) stop(DUCK3);
    if (low & 0x20) play(PIPEHIT, PIPEHIT, false);
    if (low & 0x40) play(BONUS1, BONUS1, false);
    if (low & 0x80) play(BONUS2, BONUS2, false);
}

void VicDual::carnival_audio_2_w(uint8_t data) {
    const uint8_t changed = uint8_t(port2_state_ ^ data), low = uint8_t(changed & ~data);
    port2_state_ = data;
    enum { BEAR = 0, RANKING = 8 };
    if (low & 0x04) play(BEAR, BEAR, false);
    if (low & 0x20) play(RANKING, RANKING, false);
    // d4: music board MCU reset (active low).
    if (music_cpu_) music_cpu_->set_reset_line((data & 0x10) ? IrqLine::Clear : IrqLine::Assert);
}

void VicDual::nsub_audio_w(uint8_t data) {
    const uint8_t changed = uint8_t(port2_state_ ^ data), high = uint8_t(changed & data), low = uint8_t(changed & ~data);
    port2_state_ = data;
    enum { EXPL_L0, EXPL_L1, SONAR, LAUNCH0, LAUNCH1, WARNING0, WARNING1, EXPL_S0, EXPL_S1, BONUS0, BONUS1, CODE, BOAT };
    auto pair = [&](uint8_t bit, int on, int off) {
        if (low & bit) { play(on, on, true); stop(off); }
        else if (high & bit) { play(off, off, false); stop(on); }
    };
    pair(0x01, WARNING0, WARNING1);
    if (low & 0x02) play(SONAR, SONAR, true);
    else if (high & 0x02) stop(SONAR);
    pair(0x04, LAUNCH0, LAUNCH1);
    pair(0x08, EXPL_L0, EXPL_L1);
    pair(0x10, EXPL_S0, EXPL_S1);
    pair(0x20, BONUS0, BONUS1);
    if (low & 0x40) play(CODE, CODE, true);
    else if (high & 0x40) stop(CODE);
    if (low & 0x80) play(BOAT, BOAT, true);
    else if (high & 0x80) stop(BOAT);
}

void VicDual::headon_audio_w(uint8_t data) {
    ho_.hispeed_pc = data & 0x01;
    ho_.screech1 = data & 0x02;
    ho_.crash = data & 0x04;
    ho_.hispeed_cc = data & 0x08;
    ho_.screech2 = data & 0x10;
    ho_.bonus = data & 0x20;
    ho_.car_on = data & 0x40;
}

void VicDual::invho2_audio_w(uint8_t data) {
    ho_.hispeed_pc = data & 0x10;
    ho_.screech1 = data & 0x08;
    ho_.crash = data & 0x80;
    ho_.hispeed_cc = data & 0x40;
    ho_.screech2 = data & 0x04;
    ho_.bonus = data & 0x02;
    ho_.car_on = data & 0x20;
}

void VicDual::netlist_audio_w(uint8_t data) {
    const uint8_t changed = uint8_t(port1_state_ ^ data);
    port1_state_ = data;
    for (int b = 0; b < 8; b++) {
        const uint8_t m = uint8_t(1 << b);
        if (!(changed & m)) continue;
        NetVoice& v = net_[size_t(b)];
        if (sound_ == Audio::Borderline) {
            // Active-low triggers (the sound board inverts the latch).
            v.held = (data & m) == 0;
            if (v.held) v.t = 0;
        } else {
            // Frogs: the latch drives the 555 triggers directly.
            v.held = (data & m) != 0;
            if (v.held) v.t = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Head On discrete board (MAME headon_discrete), behavioural model.

float VicDual::headon_sample() {
    constexpr int kOver = 4;
    constexpr double dt = 1.0 / (kSampleRate * kOver);
    double acc = 0;
    for (int s = 0; s < kOver; s++) {
        // MM5837 noise at ~100 kHz.
        ho_.noise_acc += 100000.0 * dt;
        while (ho_.noise_acc >= 1.0) {
            ho_.noise_acc -= 1.0;
            const uint32_t bit = ((ho_.lfsr >> 13) ^ (ho_.lfsr >> 16)) & 1;
            ho_.lfsr = ((ho_.lfsr << 1) | (bit ^ 1)) & 0x1ffff;
            ho_.noise = (ho_.lfsr >> 16) & 1;
        }
        const double noise = ho_.noise ? 1.0 : 0.0;

        // Engines: ramp 12 V -> 10.8 V over 7 s while the car runs, extra
        // -2 V over 0.8 s at high speed; a 555 current-controlled oscillator
        // whose output feeds /2, /4 and /3 counters.
        auto car = [&](HeadOnCar& c, bool hispeed) -> double {
            const double car_target = ho_.car_on ? 10.8 : 12.0;
            const double car_rate = (12.0 - 10.8) / 7.0 * dt;
            if (c.ramp_car > car_target) c.ramp_car = std::max(car_target, c.ramp_car - car_rate);
            else c.ramp_car = std::min(car_target, c.ramp_car + car_rate);
            const double hi_target = hispeed ? -2.0 : 0.0;
            const double hi_rate = 2.0 / 0.8 * dt;
            if (c.ramp_hi > hi_target) c.ramp_hi = std::max(hi_target, c.ramp_hi - hi_rate);
            else c.ramp_hi = std::min(hi_target, c.ramp_hi + hi_rate);
            if (!ho_.car_on) {
                c.cap = 4.0;
                c.charging = true;
                return 0.0;
            }
            const double vin = c.ramp_car + c.ramp_hi;
            // Current source (PNP from +12 V): (12 - (Vin + Vbe)) / 10k; no
            // current, no oscillation, until the ramp has come down.
            const double i = std::max(12.0 - vin - 0.6, 0.0) / 10000.0;
            if (i <= 0.0) {
                c.cap -= c.cap * dt / 1.0;  // leakage only
            } else if (c.charging) {
                c.cap += i / 100e-9 * dt;
                if (c.cap >= 8.0) {
                    c.charging = false;
                }
            } else {
                // Discharge through 1k.
                c.cap -= (c.cap / (1000.0 * 100e-9)) * dt;
                if (c.cap <= 4.0) {
                    c.charging = true;
                    // Rising edge of the output clocks the counters.
                    c.div2 = (c.div2 + 1) % 2;
                    c.div4 = (c.div4 + 1) % 4;
                    c.div3 = (c.div3 + 1) % 3;
                }
            }
            const int level = c.div2 + (c.div4 > 1 ? 1 : 0) + (c.div3 == 2 ? 1 : 0);
            return level * 4.0;  // 0..12 V
        };
        const double player = car(ho_.player, ho_.hispeed_pc);
        const double computer = car(ho_.computer, ho_.hispeed_cc);

        // Screeches: CD4069 oscillators with the noise on their inputs.
        auto screech = [&](double& ph, double freq, bool on) -> double {
            if (!on) return 0.0;
            ph += freq * (0.8 + 0.4 * noise) * dt;
            ph -= std::floor(ph);
            return ph < 0.5 ? 12.0 : 0.0;
        };
        const double s1 = screech(ho_.screech_phase1, 1.0 / (2.2 * 10000.0 * 47e-9), ho_.screech1);
        const double s2 = screech(ho_.screech_phase2, 1.0 / (2.2 * 10000.0 * 57e-9), ho_.screech2);

        // Bonus: slow inverter oscillator switching a 555 between ~600 Hz
        // and ~375 Hz.
        double bonus = 0.0;
        if (ho_.bonus) {
            ho_.bonus_mod += dt / (2.2 * 1e6 * 470e-9);
            ho_.bonus_mod -= std::floor(ho_.bonus_mod);
            const double f = ho_.bonus_mod < 0.5 ? 600.0 : 375.0;
            ho_.bonus_phase += f * dt;
            ho_.bonus_phase -= std::floor(ho_.bonus_phase);
            bonus = ho_.bonus_phase < 0.5 ? 11.5 : 0.0;
        } else {
            ho_.bonus_mod = 0;
        }

        // Crash: two 555 monostables (0.52 s and 1.14 s) triggered while the
        // crash input is low gate the noise, through a 500 Hz band-pass and a
        // 71 Hz Sallen-Key low-pass.
        if (!ho_.crash) {
            ho_.crash1 = 1.1 * 470000.0 * 1e-6;
            ho_.crash2 = 1.1 * 470000.0 * 2.2e-6;
        }
        const double g1 = ho_.crash1 > 0 ? 1.0 : 0.0;
        const double g2 = ho_.crash2 > 0 ? 1.0 : 0.0;
        ho_.crash1 -= dt;
        ho_.crash2 -= dt;
        const double n1 = g1 * (noise * 12.0 - 6.0);
        const double n2 = g2 * (noise * 12.0 - 6.0);
        const double a_lp = 1.0 - std::exp(-2.0 * kPi * 500.0 * dt);
        ho_.bp_lp += a_lp * (n1 - ho_.bp_lp);
        const double bp = ho_.bp_lp - ho_.bp_hp;
        ho_.bp_hp += a_lp * (ho_.bp_lp - ho_.bp_hp);
        const double a_sk = 1.0 - std::exp(-2.0 * kPi * 71.0 * dt);
        ho_.sk_1 += a_sk * (n2 - ho_.sk_1);
        ho_.sk_2 += a_sk * 1.4 * (ho_.sk_1 - ho_.sk_2);
        const double crash = (bp * 2.0 + ho_.sk_2 * 10.0) * 0.5;

        // Resistor mixer (130k, 130k, 100k, 100k, 100k, 10k into 100k).
        const double g[6] = {1 / 130e3, 1 / 130e3, 1 / 100e3, 1 / 100e3, 1 / 100e3, 1 / 10e3};
        const double vin[6] = {player, computer, s1, s2, bonus, crash};
        double num = 0, den = 1 / 100e3;
        for (int k = 0; k < 6; k++) {
            num += vin[k] * g[k];
            den += g[k];
        }
        acc += num / den;
    }
    return float(acc / kOver * (37000.0 / 12.0) / 32768.0);
}

// Frogs / Borderline stand-ins: one synthesized voice per latch bit.
float VicDual::netlist_sample() {
    const double dt = 1.0 / kSampleRate;
    const uint32_t bit = ((noise_lfsr_ >> 13) ^ (noise_lfsr_ >> 16)) & 1;
    noise_lfsr_ = ((noise_lfsr_ << 1) | bit) & 0x1ffff;
    const double noise = bit ? 1.0 : -1.0;
    double out = 0;
    for (int b = 0; b < 8; b++) {
        NetVoice& v = net_[size_t(b)];
        if (v.t < 0) continue;
        const double t = v.t;
        double s = 0;
        bool done = false;
        auto sq = [&](double f) {
            v.phase += f * dt;
            v.phase -= std::floor(v.phase);
            return v.phase < 0.5 ? 1.0 : -1.0;
        };
        if (sound_ == Audio::Frogs) {
            switch (b) {
                case 0: s = sq(300 + 900 * t) * std::exp(-t / 0.08); done = t > 0.3; break;       // hop
                case 1: s = sq(200 + 1400 * t) * std::exp(-t / 0.2); done = t > 0.6; break;       // jump (boing)
                case 2: s = sq(1500 - 1200 * t) * std::exp(-t / 0.12); done = t > 0.35; break;    // tongue (zip)
                case 3: s = sq(120) * (0.6 + 0.4 * noise) * std::exp(-t / 0.15); done = t > 0.45; break;  // capture (croak)
                case 4: s = v.held ? sq(220 + 30 * std::sin(2 * kPi * 25 * t)) * 0.4 : 0; done = !v.held; break;  // fly buzz
                case 7: s = noise * std::exp(-t / 0.35); done = t > 1.0; break;                    // splash
                default: done = true; break;
            }
        } else {
            switch (b) {
                case 0: s = sq(1000 + 400 * std::sin(2 * kPi * 12 * t)) * std::exp(-t / 0.15); done = t > 0.4; break;  // point
                case 1: s = noise * std::exp(-t / 0.3); done = t > 0.8; break;                     // hit
                case 2: s = sq(90) * (std::fmod(t, 0.25) < 0.08 ? 1.0 : 0.0) * 0.6; done = !v.held && t > 0.25; break;  // walk
                case 3: s = sq(700 - 300 * t) * std::exp(-t / 0.3); done = t > 0.8; break;         // cry
                case 4: s = sq(150 + 60 * std::sin(2 * kPi * 3 * t)) * 0.5 * std::exp(-t / 0.5); done = t > 1.2; break;  // animal
                case 5: s = (noise * 0.7 + sq(1200 - 3000 * t) * 0.3) * std::exp(-t / 0.06); done = t > 0.2; break;  // gun
                case 6: s = v.held ? sq(60 + 8 * noise) * 0.35 : 0; done = !v.held; break;          // jeep
                case 7: s = sq(500 + 500 * std::sin(2 * kPi * 7 * t)) * std::exp(-t / 0.4); done = t > 1.0; break;  // emergency
                default: done = true; break;
            }
        }
        out += s;
        v.t += dt;
        if (done) v.t = -1;
    }
    return float(out * 0.25);
}

void VicDual::generate_audio(int n) {
    for (int i = 0; i < n; i++) {
        double mixv = 0;
        for (Channel& c : channels_) {
            if (c.sample < 0) continue;
            const Wave& w = samples_[size_t(c.sample)].data;
            const size_t k = size_t(c.pos);
            if (k >= w.size()) {
                if (c.loop && !w.empty()) {
                    c.pos = 0;
                } else {
                    c.sample = -1;
                    continue;
                }
            }
            mixv += 0.5 * w[size_t(c.pos)];
            c.pos += 1.0;
        }
        if (sound_ == Audio::HeadOn || game_ == Game::InvincoHeadOn2) mixv += headon_sample();
        if (sound_ == Audio::Frogs || sound_ == Audio::Borderline) mixv += netlist_sample();
        if (psg_) mixv += 0.25 * double(psg_->update()) / 16384.0;
        // Output coupling capacitor.
        hp_out_ = 0.998 * (hp_out_ + mixv - hp_in_);
        hp_in_ = mixv;
        const int32_t s = int32_t(std::lround(hp_out_ * 32767.0));
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

}  // namespace dsp
