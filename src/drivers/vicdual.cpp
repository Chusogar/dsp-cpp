#include "drivers/vicdual.h"
#include "core/rom_loader.h"
#include <algorithm>

namespace dsp {
namespace {

const uint32_t kPens[8] = {
    0xff000000u, 0xff00ff00u, 0xff0000ffu, 0xff00ffffu,
    0xffff0000u, 0xffffff00u, 0xffff00ffu, 0xffffffffu,
};

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

}  // namespace

VicDual::VicDual(Game game)
    : game_(game), cpu_(kCpuClock),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0xff000000u) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_io_handlers(
        [this](uint16_t p) { return read_port(p); },
        [this](uint16_t p, uint8_t v) { write_port(p, v); });
}

VicDual::Layout VicDual::layout() const {
    switch (game_) {
        case Game::DepthCharge: return Layout::DualGame;
        case Game::Safari: return Layout::Safari;
        case Game::Frogs: return Layout::DualGame;
        case Game::SpaceAttack: return Layout::HeadOn;
        case Game::SpaceAttackHeadOn: return Layout::DualGame;
        case Game::HeadOn: return Layout::HeadOn;
        case Game::HeadOn2: return Layout::HeadOn2;
        case Game::HeadOn2Slim: return Layout::DualGame;
        case Game::InvincoHeadOn2: return Layout::DualGame;
        case Game::NSub: return Layout::VramC000;
        case Game::Samurai: return Layout::DualGame;
        case Game::Invinco: return Layout::VramC000;
        case Game::InvincoDeepScan: return Layout::DualGame;
        case Game::TranqGun: return Layout::DualGame;
        case Game::SpaceTrek: return Layout::DualGame;
        case Game::Carnival: return Layout::DualGame;
        case Game::Borderline: return Layout::DualGame;
        case Game::Digger: return Layout::HeadOn2;
        case Game::Pulsar: return Layout::DualGame;
        case Game::Heiankyo: return Layout::DualGame;
        case Game::AlphaFighter: return Layout::DualGame;
    }
    return Layout::DualGame;
}

bool VicDual::is_color() const {
    return has_prom_ || layout() == Layout::DualGame;
}

const char* VicDual::title() const {
    switch (game_) {
        case Game::DepthCharge: return "Depth Charge";
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
    return "Vic Dual";
}

bool VicDual::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> rom(0x4000, 0);
    const std::vector<RomEntry>* cpu = nullptr;
    const std::vector<RomEntry>* prom = nullptr;
    size_t rom_bytes = 0x4000;
    switch (game_) {
    case Game::DepthCharge:
        cpu = &kDepthChargeCpu; rom_bytes = 0x1800;
        
        break;
    case Game::Safari:
        cpu = &kSafariCpu; rom_bytes = 0x2800;
        
        break;
    case Game::Frogs:
        cpu = &kFrogsCpu; rom_bytes = 0x2000;
        
        break;
    case Game::SpaceAttack:
        cpu = &kSpaceAttackCpu; rom_bytes = 0x2000;
        prom = &kSpaceAttackProm;
        break;
    case Game::SpaceAttackHeadOn:
        cpu = &kSpaceAttackHeadOnCpu; rom_bytes = 0x4000;
        prom = &kSpaceAttackHeadOnProm;
        break;
    case Game::HeadOn:
        cpu = &kHeadOnCpu; rom_bytes = 0x1c00;
        prom = &kHeadOnProm;
        break;
    case Game::HeadOn2:
        cpu = &kHeadOn2Cpu; rom_bytes = 0x2000;
        prom = &kHeadOn2Prom;
        break;
    case Game::HeadOn2Slim:
        cpu = &kHeadOn2SlimCpu; rom_bytes = 0x2000;
        prom = &kHeadOn2SlimProm;
        break;
    case Game::InvincoHeadOn2:
        cpu = &kInvincoHeadOn2Cpu; rom_bytes = 0x4000;
        prom = &kInvincoHeadOn2Prom;
        break;
    case Game::NSub:
        cpu = &kNSubCpu; rom_bytes = 0x4000;
        prom = &kNSubProm;
        break;
    case Game::Samurai:
        cpu = &kSamuraiCpu; rom_bytes = 0x3800;
        prom = &kSamuraiProm;
        break;
    case Game::Invinco:
        cpu = &kInvincoCpu; rom_bytes = 0x2400;
        prom = &kInvincoProm;
        break;
    case Game::InvincoDeepScan:
        cpu = &kInvincoDeepScanCpu; rom_bytes = 0x4000;
        prom = &kInvincoDeepScanProm;
        break;
    case Game::TranqGun:
        cpu = &kTranqGunCpu; rom_bytes = 0x4000;
        prom = &kTranqGunProm;
        break;
    case Game::SpaceTrek:
        cpu = &kSpaceTrekCpu; rom_bytes = 0x4000;
        prom = &kSpaceTrekProm;
        break;
    case Game::Carnival:
        cpu = &kCarnivalCpu; rom_bytes = 0x4000;
        prom = &kCarnivalProm;
        break;
    case Game::Borderline:
        cpu = &kBorderlineCpu; rom_bytes = 0x4000;
        prom = &kBorderlineProm;
        break;
    case Game::Digger:
        cpu = &kDiggerCpu; rom_bytes = 0x2000;
        prom = &kDiggerProm;
        break;
    case Game::Pulsar:
        cpu = &kPulsarCpu; rom_bytes = 0x4000;
        prom = &kPulsarProm;
        break;
    case Game::Heiankyo:
        cpu = &kHeiankyoCpu; rom_bytes = 0x4000;
        prom = &kHeiankyoProm;
        break;
    case Game::AlphaFighter:
        cpu = &kAlphaFighterCpu; rom_bytes = 0x4000;
        
        break;
    }
    if (!cpu || !loader.load(*cpu, rom, error)) return false;
    std::copy(rom.begin(), rom.begin() + static_cast<std::ptrdiff_t>(rom_bytes), memory_.begin());
    const auto lay = layout();
    if (lay == Layout::HeadOn) {
        for (int a = 0x2000; a < 0x8000; ++a) memory_[size_t(a)] = memory_[size_t(a & 0x1fff)];
    } else {
        for (int a = 0x4000; a < 0x8000; ++a) memory_[size_t(a)] = memory_[size_t(a & 0x3fff)];
    }
    has_prom_ = false;
    if (prom) {
        std::vector<uint8_t> p(0x20, 0);
        if (loader.load(*prom, p, error)) {
            std::copy(p.begin(), p.end(), color_prom_.begin());
            has_prom_ = true;
        } else if (error) error->clear();
    }
    warnings_ = loader.warnings();
    return true;
}

bool VicDual::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void VicDual::reset() {
    cpu_.reset();
    videoram_.fill(0);
    characterram_.fill(0);
    coin_status_ = 0;
    coin_clear_counter_ = 0;
    palette_bank_ = 0;
    scanline_ = 0;
    frame_count_ = 0;
    audio_.clear();
    in0_ = in1_ = in2_ = in3_ = 0xff;
    audio_port1_ = audio_port2_ = 0;
    for (auto& v : voices_) v = Voice{};
    lfsr_ = 0x1ffff;
}


void VicDual::set_inputs(const MachineInputs& inputs) {
    in0_ = in1_ = in2_ = in3_ = 0xff;
    if (inputs.coin1) {
        coin_status_ = 1;
        coin_clear_counter_ = int(kFramesPerSecond / 4);
    }
    // Generic active-low mapping covering dualgame + invinco-style boards
    if (inputs.player1.up) in0_ &= ~0x20;
    if (inputs.player1.down) in0_ &= ~0x10;
    if (inputs.player1.left) in1_ &= ~0x10;
    if (inputs.player1.right) in1_ &= ~0x20;
    if (inputs.player1.start) in2_ &= ~0x10;
    if (inputs.player1.button1) in2_ &= ~0x20;
    if (inputs.player2.start) in3_ &= ~0x20;
    // Head On / Invinco style on IN0
    if (game_ == Game::HeadOn || game_ == Game::HeadOn2 || game_ == Game::Digger ||
        game_ == Game::SpaceAttack) {
        in0_ = 0xff;
        if (inputs.player1.left) in0_ &= ~0x40;
        if (inputs.player1.right) in0_ &= ~0x10;
        if (inputs.player1.up) in0_ &= ~0x80;
        if (inputs.player1.down) in0_ &= ~0x20;
        if (inputs.player1.button1) in0_ &= ~0x08;
        if (inputs.player1.start) in0_ &= ~0x01;
        if (inputs.player2.start) in0_ &= ~0x02;
    }
    if (game_ == Game::Invinco) {
        in0_ = 0xff;
        if (inputs.player1.start) in0_ &= ~0x01;
        if (inputs.player2.start) in0_ &= ~0x02;
        if (inputs.player1.button1) in0_ &= ~0x08;
        if (inputs.player1.right) in0_ &= ~0x10;
        if (inputs.player1.left) in0_ &= ~0x40;
        in1_ = uint8_t(0xfc | (dsw_ & 3));
    }
}

void VicDual::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

bool VicDual::timer_value() const {
    return ((frame_count_ * 500 / int(kFramesPerSecond + 0.5)) & 1) != 0;
}

uint8_t VicDual::read_byte(uint16_t address) {
    const auto lay = layout();
    if (lay == Layout::DualGame) {
        if (address <= 0x7fff) return memory_[address & 0x3fff];
        if (address >= 0x8000) {
            const uint16_t base = uint16_t(address & ~uint16_t(0x7000));
            if (base <= 0x83ff) return videoram_[base & 0x3ff];
            if (base <= 0x87ff) return memory_[0x8400 + (base & 0x3ff)];
            if (base <= 0x8fff) return characterram_[base & 0x7ff];
        }
        return 0xff;
    }
    // HeadOn / HeadOn2 / VramC000 / Safari: character/video at $c000
    if (lay == Layout::HeadOn) {
        if (address <= 0x7fff) return memory_[address & 0x1fff];
    } else {
        if (address <= 0x7fff) return memory_[address & 0x3fff];
    }
    if (lay == Layout::Safari && address >= 0x8000 && address <= 0xbfff)
        return memory_[0x8000 + (address & 0xfff)];
    if ((address & 0xc000) == 0xc000) {
        const uint16_t off = address & 0xfff;
        if (off <= 0x3ff) return videoram_[off];
        if (off <= 0x7ff) return memory_[0xc400 + (off & 0x3ff)];
        return characterram_[off & 0x7ff];
    }
    return 0xff;
}

void VicDual::write_byte(uint16_t address, uint8_t value) {
    const auto lay = layout();
    if (lay == Layout::DualGame) {
        if (address <= 0x7fff) return;
        if (address >= 0x8000) {
            const uint16_t base = uint16_t(address & ~uint16_t(0x7000));
            if (base <= 0x83ff) { videoram_[base & 0x3ff] = value; return; }
            if (base <= 0x87ff) { memory_[0x8400 + (base & 0x3ff)] = value; return; }
            if (base <= 0x8fff) { characterram_[base & 0x7ff] = value; return; }
        }
        return;
    }
    if (address <= 0x7fff) return;
    if (lay == Layout::Safari && address >= 0x8000 && address <= 0xbfff) {
        memory_[0x8000 + (address & 0xfff)] = value;
        return;
    }
    if ((address & 0xc000) == 0xc000) {
        const uint16_t off = address & 0xfff;
        if (off <= 0x3ff) { videoram_[off] = value; return; }
        if (off <= 0x7ff) { memory_[0xc400 + (off & 0x3ff)] = value; return; }
        characterram_[off & 0x7ff] = value;
    }
}

uint8_t VicDual::read_port(uint16_t port) {
    const auto lay = layout();
    // Dual-game 4-port boards
    if (lay == Layout::DualGame) {
        const uint8_t o = uint8_t(port & 0x03);
        uint8_t data = 0xff;
        switch (o) {
            case 0: data = in0_; break;
            case 1:
                data = in1_;
                if (scanline_ < kScreenHeight) data |= 0x08;
                else data &= ~0x08;
                break;
            case 2:
                data = in2_;
                if (timer_value()) data |= 0x08;
                else data &= ~0x08;
                break;
            case 3:
                data = in3_;
                if (coin_status_) data |= 0x08;
                else data &= ~0x08;
                break;
        }
        return data;
    }
    // Invinco-style
    if (game_ == Game::Invinco || game_ == Game::NSub) {
        const uint8_t o = uint8_t(port & 0x0f);
        uint8_t data = 0xff;
        if (o & 0x01) data &= in0_;
        if (o & 0x04) data &= in1_;
        if (o & 0x08) {
            uint8_t p2 = 0x7e;
            if (scanline_ < kScreenHeight) p2 |= 0x01;
            if (coin_status_) p2 |= 0x80;
            data &= p2;
        }
        return data;
    }
    // Head On family
    const uint8_t o = uint8_t(port & 0x0f);
    uint8_t data = 0xff;
    if (o & 0x01) data &= in0_;
    if (o & 0x08) data &= in1_;
    if (o & 0x04) data &= in2_;
    return data;
}

void VicDual::write_port(uint16_t port, uint8_t value) {
    const auto lay = layout();
    if (lay == Layout::DualGame) {
        const uint8_t o = uint8_t(port & 0x7f);
        if (o & 0x01) audio_port1_w(value);
        if (o & 0x02) audio_port2_w(value);
        if (o & 0x08) { coin_status_ = 1; coin_clear_counter_ = int(kFramesPerSecond / 4); }
        if (game_ == Game::Borderline) {
            if (o & 0x02) palette_bank_ = value & 3;
        } else if (o & 0x40) {
            palette_bank_ = value & 3;
        }
        // Borderline uses bit1 for palette; still route low nibble as tone when not borderline-only
        if (game_ != Game::Borderline && (o & 0x02) == 0 && (o & 0x01) == 0) {
            // no-op
        }
        return;
    }
    const uint8_t o = uint8_t(port & 0x1f);
    if (o & 0x01) { coin_status_ = 1; coin_clear_counter_ = int(kFramesPerSecond / 4); }
    if (o & 0x02) audio_port1_w(value);
    if (o & 0x04) palette_bank_ = value & 3;
    if (o & 0x08) audio_port2_w(value);
}

void VicDual::update_video() {
    const bool color = has_prom_;
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            const int offs = ((y >> 3) << 5) | (x >> 3);
            const uint8_t code = videoram_[size_t(offs & 0x3ff)];
            const uint8_t line = characterram_[size_t((code << 3) | (y & 7))];
            const bool on = ((line >> (7 - (x & 7))) & 1) != 0;
            uint32_t pix;
            if (!color) {
                pix = on ? 0xffffffffu : 0xff000000u;
            } else {
                const int po = (code >> 5) | (palette_bank_ << 3);
                const uint8_t pr = color_prom_[size_t(po & 0x1f)];
                pix = on ? kPens[(pr >> 5) & 7] : kPens[(pr >> 1) & 7];
            }
            framebuffer_[size_t(y * kScreenWidth + x)] = pix;
        }
    }
}

void VicDual::run_frame() {
    const int cpl = int(double(kCpuClock) / (kFramesPerSecond * kVTotal) + 0.5);
    for (scanline_ = 0; scanline_ < kVTotal; ++scanline_) cpu_.run(cpl);
    update_video();
    if (coin_clear_counter_ > 0 && --coin_clear_counter_ == 0) coin_status_ = 0;
    ++frame_count_;
    mix_audio(int(kSampleRate / kFramesPerSecond + 0.5));
}

void VicDual::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}


void VicDual::audio_port1_w(uint8_t data) {
    const uint8_t changed = uint8_t(audio_port1_ ^ data);
    const uint8_t gone_high = uint8_t(changed & data);
    const uint8_t gone_low = uint8_t(changed & ~data);
    audio_port1_ = data;

    // Map control bits onto discrete voices.
    // Active bit = sustain tone; falling/rising edge = short one-shot (shots/explosions).
    auto trigger_shot = [&](int idx, double hz, float amp, int samples) {
        auto& v = voices_[size_t(idx)];
        v.freq_hz = hz;
        v.target_amp = amp;
        v.amp = amp;
        v.oneshot = samples;
        v.noise = false;
    };
    auto set_sustain = [&](int idx, bool on, double hz, float amp) {
        auto& v = voices_[size_t(idx)];
        v.freq_hz = hz;
        v.noise = false;
        if (on) {
            v.target_amp = amp;
            if (v.amp < 0.05f) v.amp = amp;
            v.oneshot = 0;
        } else if (v.oneshot == 0) {
            v.target_amp = 0;
        }
    };
    auto trigger_noise = [&](int idx, float amp, int samples) {
        auto& v = voices_[size_t(idx)];
        v.noise = true;
        v.freq_hz = 0;
        v.target_amp = amp;
        v.amp = amp;
        v.oneshot = samples;
    };

    switch (game_) {
        case Game::HeadOn:
        case Game::HeadOn2:
        case Game::HeadOn2Slim:
        case Game::Digger:
        case Game::SpaceAttack:
        case Game::AlphaFighter:
            // headon discrete bits (approximate MAME HEADON_* enables)
            set_sustain(0, data & 0x01, 90.0, 0.25f);    // car engine low
            set_sustain(1, data & 0x02, 180.0, 0.22f);   // hi-speed
            if (gone_high & 0x04) trigger_noise(2, 0.45f, int(0.35 * kSampleRate));  // crash
            set_sustain(3, data & 0x08, 1200.0, 0.12f);  // screech1
            set_sustain(4, data & 0x10, 900.0, 0.12f);   // screech2
            if (gone_high & 0x20) trigger_shot(5, 880.0, 0.3f, int(0.12 * kSampleRate));  // bonus
            set_sustain(6, data & 0x40, 60.0, 0.15f);
            break;

        case Game::Carnival:
            if (gone_low & 0x01) trigger_shot(0, 1800.0, 0.4f, int(0.08 * kSampleRate));  // rifle
            if (gone_low & 0x02) trigger_shot(1, 600.0, 0.35f, int(0.15 * kSampleRate));   // clang
            set_sustain(2, data & 0x04, 440.0, 0.12f);   // duck1
            set_sustain(3, data & 0x08, 520.0, 0.12f);   // duck2
            set_sustain(4, data & 0x10, 620.0, 0.12f);   // duck3
            if (gone_low & 0x20) trigger_shot(5, 300.0, 0.3f, int(0.1 * kSampleRate));
            if (gone_low & 0x40) trigger_shot(6, 1000.0, 0.25f, int(0.1 * kSampleRate));
            if (gone_low & 0x80) trigger_shot(7, 1200.0, 0.25f, int(0.1 * kSampleRate));
            break;

        case Game::DepthCharge:
            if (gone_high & 0x01) trigger_noise(0, 0.5f, int(0.5 * kSampleRate));
            if (gone_high & 0x02) trigger_noise(1, 0.4f, int(0.2 * kSampleRate));
            set_sustain(2, data & 0x04, 200.0, 0.15f);  // spray
            if (gone_high & 0x08) trigger_shot(3, 880.0, 0.3f, int(0.15 * kSampleRate));
            set_sustain(4, data & 0x10, 40.0, 0.2f);    // sonar
            break;

        case Game::Invinco:
        case Game::InvincoHeadOn2:
        case Game::InvincoDeepScan:
            if (gone_high & 0x01) trigger_shot(0, 400.0, 0.3f, int(0.1 * kSampleRate));
            if (gone_high & 0x02) trigger_noise(1, 0.4f, int(0.25 * kSampleRate));
            set_sustain(2, data & 0x04, 220.0, 0.15f);
            set_sustain(3, data & 0x08, 330.0, 0.12f);
            if (gone_high & 0x10) trigger_shot(4, 660.0, 0.25f, int(0.12 * kSampleRate));
            set_sustain(5, data & 0x20, 110.0, 0.1f);
            break;

        case Game::Pulsar:
            if (gone_high & 0x01) trigger_shot(0, 500.0, 0.3f, int(0.08 * kSampleRate));
            if (gone_high & 0x02) trigger_shot(1, 700.0, 0.25f, int(0.08 * kSampleRate));
            if (gone_high & 0x04) trigger_noise(2, 0.35f, int(0.2 * kSampleRate));
            set_sustain(3, data & 0x08, 150.0, 0.15f);
            set_sustain(4, data & 0x10, 300.0, 0.12f);
            if (gone_high & 0x20) trigger_shot(5, 1000.0, 0.3f, int(0.15 * kSampleRate));
            set_sustain(6, data & 0x40, 80.0, 0.1f);
            set_sustain(7, data & 0x80, 55.0, 0.12f);
            break;

        case Game::Borderline:
        case Game::TranqGun:
        case Game::SpaceTrek:
        case Game::Samurai:
        case Game::Heiankyo:
        case Game::Frogs:
        case Game::Safari:
        case Game::NSub:
        case Game::SpaceAttackHeadOn:
        default:
            // Generic: each set bit sustains a harmonic; edges fire noise
            for (int b = 0; b < 8; ++b) {
                const bool on = (data & (1 << b)) != 0;
                const double hz = 80.0 * (b + 1);
                if (gone_high & (1 << b)) {
                    if (b >= 5) trigger_noise(b, 0.3f, int(0.15 * kSampleRate));
                    else trigger_shot(b, hz * 2, 0.25f, int(0.1 * kSampleRate));
                } else {
                    set_sustain(b, on, hz, 0.12f);
                }
            }
            break;
    }
    (void)gone_low;
}

void VicDual::audio_port2_w(uint8_t data) {
    const uint8_t changed = uint8_t(audio_port2_ ^ data);
    const uint8_t gone_low = uint8_t(changed & ~data);
    const uint8_t gone_high = uint8_t(changed & data);
    audio_port2_ = data;

    if (game_ == Game::Carnival) {
        if (gone_low & 0x01) {  // bear
            voices_[5].freq_hz = 200.0;
            voices_[5].amp = voices_[5].target_amp = 0.35f;
            voices_[5].oneshot = int(0.3 * kSampleRate);
            voices_[5].noise = false;
        }
        if (gone_low & 0x02) {
            voices_[6].freq_hz = 1500.0;
            voices_[6].amp = voices_[6].target_amp = 0.3f;
            voices_[6].oneshot = int(0.2 * kSampleRate);
        }
        // bit4 music MCU reset ignored (no i8035)
    } else if (game_ == Game::Pulsar) {
        for (int b = 0; b < 8; ++b) {
            if (gone_high & (1 << b)) {
                auto& v = voices_[size_t(b)];
                v.freq_hz = 250.0 * (b + 1);
                v.amp = v.target_amp = 0.2f;
                v.oneshot = int(0.1 * kSampleRate);
                v.noise = (b & 1) != 0;
            }
        }
    } else {
        // secondary port: soft tones
        for (int b = 0; b < 4; ++b) {
            auto& v = voices_[size_t(4 + b)];
            if (data & (1 << b)) {
                v.freq_hz = 100.0 * (b + 2);
                v.target_amp = 0.1f;
                if (v.amp < 0.05f) v.amp = 0.1f;
                v.oneshot = 0;
            } else if (v.oneshot == 0) {
                v.target_amp = 0;
            }
        }
    }
    (void)gone_high;
}

void VicDual::mix_audio(int samples) {
    audio_.reserve(audio_.size() + size_t(samples));
    for (int i = 0; i < samples; ++i) {
        float mix = 0.f;
        // LFSR noise (MM5837-ish 17-bit)
        const uint32_t bit = ((lfsr_ >> 13) ^ (lfsr_ >> 16)) & 1;
        lfsr_ = ((lfsr_ << 1) | bit) & 0x1ffff;
        const float noise = (bit ? 1.f : -1.f);

        for (auto& v : voices_) {
            if (v.amp < 0.001f && v.target_amp < 0.001f && v.oneshot == 0) continue;
            float s;
            if (v.noise) {
                s = noise;
            } else {
                v.phase += v.freq_hz / double(kSampleRate);
                if (v.phase >= 1.0) v.phase -= 1.0;
                s = (v.phase < 0.5) ? 1.f : -1.f;
            }
            mix += s * v.amp;

            if (v.oneshot > 0) {
                if (--v.oneshot == 0) v.target_amp = 0;
            }
            // simple envelope toward target
            if (v.amp < v.target_amp) v.amp = std::min(v.target_amp, v.amp + 0.002f);
            else if (v.amp > v.target_amp) v.amp = std::max(v.target_amp, v.amp - 0.001f);
        }
        mix = std::max(-1.f, std::min(1.f, mix * 0.35f));
        audio_.push_back(int16_t(mix * 28000.f));
    }
}

}  // namespace dsp
