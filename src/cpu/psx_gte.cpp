// Portions derived from ProjectPSX GTE.cs (MIT License)
// Copyright (c) 2019 Pedro Cortés
// See PROJECTPSX_LICENSE / https://github.com/BluestormDNA/ProjectPSX

#include "cpu/psx_gte.h"

#include <algorithm>

namespace dsp {

namespace {

// ProjectPSX UNR reciprocal approximation table (257 entries).
constexpr uint8_t kUnrTable[257] = {
    0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA, 0xE8, 0xE6, 0xE4, 0xE3,
    0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5, 0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB, 0xC9, 0xC8,
    0xC6, 0xC5, 0xC3, 0xC1, 0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8, 0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xB0,
    0xAE, 0xAD, 0xAB, 0xAA, 0xA9, 0xA7, 0xA6, 0xA4, 0xA3, 0xA2, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A,
    0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8F, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x87, 0x86,
    0x85, 0x84, 0x83, 0x82, 0x81, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x75, 0x74,
    0x73, 0x72, 0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
    0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58, 0x57, 0x56, 0x55,
    0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D, 0x4D, 0x4C, 0x4B, 0x4A, 0x49, 0x48, 0x48,
    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3F, 0x3F, 0x3E, 0x3D, 0x3C, 0x3C, 0x3B,
    0x3A, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2F,
    0x2E, 0x2E, 0x2D, 0x2C, 0x2C, 0x2B, 0x2A, 0x2A, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1B, 0x1B, 0x1A,
    0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
    0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x09, 0x09, 0x08, 0x08,
    0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
    0x00};

}  // namespace

void PsxGte::reset() {
  *this = PsxGte{};
}

void PsxGte::execute(uint32_t command) {
  current_command_ = command;
  sf_ = static_cast<int>((command & 0x80000u) >> 19) * 12;
  lm_ = ((command >> 10) & 0x1u) != 0;
  FLAG_ = 0;

  switch (command & 0x3Fu) {
    case 0x01: rtps(0, true); break;
    case 0x06: nclip(); break;
    case 0x0C: op(); break;
    case 0x10: dpcs(false); break;
    case 0x11: intpl(); break;
    case 0x12: mvmva(); break;
    case 0x13: ncds(0); break;
    case 0x14: cdp(); break;
    case 0x16: ncdt(); break;
    case 0x1B: nccs(0); break;
    case 0x1C: cc(); break;
    case 0x1E: ncs(0); break;
    case 0x20: nct(); break;
    case 0x28: sqr(); break;
    case 0x29: dcpl(); break;
    case 0x2A: dcpt(); break;
    case 0x2D: avsz3(); break;
    case 0x2E: avsz4(); break;
    case 0x30: rtpt(); break;
    case 0x3D: gpf(); break;
    case 0x3E: gpl(); break;
    case 0x3F: ncct(); break;
    default: break;
  }

  if ((FLAG_ & 0x7F87E000u) != 0) {
    FLAG_ |= 0x80000000u;
  }
}

void PsxGte::cdp() {
  MAC1_ = static_cast<int32_t>(
      set_mac(1, set_mac(1, set_mac(1, static_cast<int64_t>(RBK_) * 0x1000 + LRGB_.v1.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v1.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v1.z) * IR_[3]) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, set_mac(2, set_mac(2, static_cast<int64_t>(GBK_) * 0x1000 + LRGB_.v2.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v2.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v2.z) * IR_[3]) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, set_mac(3, set_mac(3, static_cast<int64_t>(BBK_) * 0x1000 + LRGB_.v3.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v3.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v3.z) * IR_[3]) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  MAC1_ = static_cast<int32_t>(set_mac(1, static_cast<int64_t>(RGBC_.r) * IR_[1]) << 4);
  MAC2_ = static_cast<int32_t>(set_mac(2, static_cast<int64_t>(RGBC_.g) * IR_[2]) << 4);
  MAC3_ = static_cast<int32_t>(set_mac(3, static_cast<int64_t>(RGBC_.b) * IR_[3]) << 4);

  interpolate_color(MAC1_, MAC2_, MAC3_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;
}

void PsxGte::cc() {
  MAC1_ = static_cast<int32_t>(
      set_mac(1, set_mac(1, set_mac(1, static_cast<int64_t>(RBK_) * 0x1000 + LRGB_.v1.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v1.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v1.z) * IR_[3]) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, set_mac(2, set_mac(2, static_cast<int64_t>(GBK_) * 0x1000 + LRGB_.v2.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v2.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v2.z) * IR_[3]) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, set_mac(3, set_mac(3, static_cast<int64_t>(BBK_) * 0x1000 + LRGB_.v3.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v3.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v3.z) * IR_[3]) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  MAC1_ = static_cast<int32_t>(set_mac(1, static_cast<int64_t>(RGBC_.r) * IR_[1]) << 4);
  MAC2_ = static_cast<int32_t>(set_mac(2, static_cast<int64_t>(RGBC_.g) * IR_[2]) << 4);
  MAC3_ = static_cast<int32_t>(set_mac(3, static_cast<int64_t>(RGBC_.b) * IR_[3]) << 4);

  MAC1_ = static_cast<int32_t>(set_mac(1, MAC1_) >> sf_);
  MAC2_ = static_cast<int32_t>(set_mac(2, MAC2_) >> sf_);
  MAC3_ = static_cast<int32_t>(set_mac(3, MAC3_) >> sf_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);
}

void PsxGte::dcpt() {
  dpcs(true);
  dpcs(true);
  dpcs(true);
}

void PsxGte::dcpl() {
  MAC1_ = static_cast<int32_t>(set_mac(1, RGBC_.r * IR_[1]) << 4);
  MAC2_ = static_cast<int32_t>(set_mac(2, RGBC_.g * IR_[2]) << 4);
  MAC3_ = static_cast<int32_t>(set_mac(3, RGBC_.b * IR_[3]) << 4);

  interpolate_color(MAC1_, MAC2_, MAC3_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;
}

void PsxGte::nccs(int r) {
  MAC1_ = static_cast<int32_t>(
      set_mac(1, static_cast<int64_t>(LM_.v1.x) * V_[r].x + LM_.v1.y * V_[r].y + LM_.v1.z * V_[r].z) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, static_cast<int64_t>(LM_.v2.x) * V_[r].x + LM_.v2.y * V_[r].y + LM_.v2.z * V_[r].z) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, static_cast<int64_t>(LM_.v3.x) * V_[r].x + LM_.v3.y * V_[r].y + LM_.v3.z * V_[r].z) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  MAC1_ = static_cast<int32_t>(
      set_mac(1, set_mac(1, set_mac(1, static_cast<int64_t>(RBK_) * 0x1000 + LRGB_.v1.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v1.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v1.z) * IR_[3]) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, set_mac(2, set_mac(2, static_cast<int64_t>(GBK_) * 0x1000 + LRGB_.v2.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v2.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v2.z) * IR_[3]) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, set_mac(3, set_mac(3, static_cast<int64_t>(BBK_) * 0x1000 + LRGB_.v3.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v3.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v3.z) * IR_[3]) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  MAC1_ = static_cast<int32_t>(set_mac(1, (RGBC_.r * IR_[1]) << 4));
  MAC2_ = static_cast<int32_t>(set_mac(2, (RGBC_.g * IR_[2]) << 4));
  MAC3_ = static_cast<int32_t>(set_mac(3, (RGBC_.b * IR_[3]) << 4));

  MAC1_ = static_cast<int32_t>(set_mac(1, MAC1_ >> sf_));
  MAC2_ = static_cast<int32_t>(set_mac(2, MAC2_ >> sf_));
  MAC3_ = static_cast<int32_t>(set_mac(3, MAC3_ >> sf_));

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);
}

void PsxGte::ncct() {
  nccs(0);
  nccs(1);
  nccs(2);
}

void PsxGte::dpcs(bool dpct) {
  uint8_t r = RGBC_.r;
  uint8_t g = RGBC_.g;
  uint8_t b = RGBC_.b;
  if (dpct) {
    r = RGB_[0].r;
    g = RGB_[0].g;
    b = RGB_[0].b;
  }

  MAC1_ = static_cast<int32_t>(set_mac(1, r) << 16);
  MAC2_ = static_cast<int32_t>(set_mac(2, g) << 16);
  MAC3_ = static_cast<int32_t>(set_mac(3, b) << 16);

  interpolate_color(MAC1_, MAC2_, MAC3_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;
}

void PsxGte::intpl() {
  MAC1_ = static_cast<int32_t>(set_mac(1, static_cast<int64_t>(IR_[1]) << 12));
  MAC2_ = static_cast<int32_t>(set_mac(2, static_cast<int64_t>(IR_[2]) << 12));
  MAC3_ = static_cast<int32_t>(set_mac(3, static_cast<int64_t>(IR_[3]) << 12));

  interpolate_color(MAC1_, MAC2_, MAC3_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;
}

void PsxGte::nct() {
  ncs(0);
  ncs(1);
  ncs(2);
}

void PsxGte::ncs(int r) {
  MAC1_ = static_cast<int32_t>(
      set_mac(1, static_cast<int64_t>(LM_.v1.x) * V_[r].x + LM_.v1.y * V_[r].y + LM_.v1.z * V_[r].z) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, static_cast<int64_t>(LM_.v2.x) * V_[r].x + LM_.v2.y * V_[r].y + LM_.v2.z * V_[r].z) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, static_cast<int64_t>(LM_.v3.x) * V_[r].x + LM_.v3.y * V_[r].y + LM_.v3.z * V_[r].z) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  MAC1_ = static_cast<int32_t>(
      set_mac(1, set_mac(1, set_mac(1, static_cast<int64_t>(RBK_) * 0x1000 + LRGB_.v1.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v1.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v1.z) * IR_[3]) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, set_mac(2, set_mac(2, static_cast<int64_t>(GBK_) * 0x1000 + LRGB_.v2.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v2.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v2.z) * IR_[3]) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, set_mac(3, set_mac(3, static_cast<int64_t>(BBK_) * 0x1000 + LRGB_.v3.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v3.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v3.z) * IR_[3]) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);
}

void PsxGte::mvmva() {
  const uint32_t mx_index = (current_command_ >> 17) & 0x3u;
  const uint32_t mv_index = (current_command_ >> 15) & 0x3u;
  const uint32_t tv_index = (current_command_ >> 13) & 0x3u;

  Matrix mx{};
  Vector3 vx{};
  int64_t tx = 0, ty = 0, tz = 0;

  if (mx_index == 0) {
    mx = RT_;
  } else if (mx_index == 1) {
    mx = LM_;
  } else if (mx_index == 2) {
    mx = LRGB_;
  } else {
    mx.v1.x = static_cast<int16_t>(-(RGBC_.r << 4));
    mx.v1.y = static_cast<int16_t>(RGBC_.r << 4);
    mx.v1.z = IR_[0];
    mx.v2.x = mx.v2.y = mx.v2.z = RT_.v1.z;
    mx.v3.x = mx.v3.y = mx.v3.z = RT_.v2.y;
  }

  if (mv_index == 0) {
    vx = V_[0];
  } else if (mv_index == 1) {
    vx = V_[1];
  } else if (mv_index == 2) {
    vx = V_[2];
  } else {
    vx.x = IR_[1];
    vx.y = IR_[2];
    vx.z = IR_[3];
  }

  if (tv_index == 0) {
    tx = TRX_;
    ty = TRY_;
    tz = TRZ_;
  } else if (tv_index == 1) {
    tx = RBK_;
    ty = GBK_;
    tz = BBK_;
  } else if (tv_index == 2) {
    tx = RFC_;
    ty = GFC_;
    tz = BFC_;

    int64_t mac1 = set_mac(1, tx * 0x1000 + mx.v1.x * vx.x);
    int64_t mac2 = set_mac(2, ty * 0x1000 + mx.v2.x * vx.x);
    int64_t mac3 = set_mac(3, tz * 0x1000 + mx.v3.x * vx.x);

    set_ir(1, static_cast<int>(mac1 >> sf_), false);
    set_ir(2, static_cast<int>(mac2 >> sf_), false);
    set_ir(3, static_cast<int>(mac3 >> sf_), false);

    mac1 = set_mac(1, set_mac(1, static_cast<int64_t>(mx.v1.y) * vx.y) +
                          static_cast<int64_t>(mx.v1.z) * vx.z);
    mac2 = set_mac(2, set_mac(2, static_cast<int64_t>(mx.v2.y) * vx.y) +
                          static_cast<int64_t>(mx.v2.z) * vx.z);
    mac3 = set_mac(3, set_mac(3, static_cast<int64_t>(mx.v3.y) * vx.y) +
                          static_cast<int64_t>(mx.v3.z) * vx.z);

    MAC1_ = static_cast<int32_t>(mac1 >> sf_);
    MAC2_ = static_cast<int32_t>(mac2 >> sf_);
    MAC3_ = static_cast<int32_t>(mac3 >> sf_);

    IR_[1] = set_ir(1, MAC1_, lm_);
    IR_[2] = set_ir(2, MAC2_, lm_);
    IR_[3] = set_ir(3, MAC3_, lm_);
    return;
  }

  MAC1_ = static_cast<int32_t>(
      set_mac(1, set_mac(1, set_mac(1, tx * 0x1000 + mx.v1.x * vx.x) +
                                    static_cast<int64_t>(mx.v1.y) * vx.y) +
                        static_cast<int64_t>(mx.v1.z) * vx.z) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, set_mac(2, set_mac(2, ty * 0x1000 + mx.v2.x * vx.x) +
                                    static_cast<int64_t>(mx.v2.y) * vx.y) +
                        static_cast<int64_t>(mx.v2.z) * vx.z) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, set_mac(3, set_mac(3, tz * 0x1000 + mx.v3.x * vx.x) +
                                    static_cast<int64_t>(mx.v3.y) * vx.y) +
                        static_cast<int64_t>(mx.v3.z) * vx.z) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);
}

void PsxGte::gpl() {
  const int64_t mac1 = static_cast<int64_t>(MAC1_) << sf_;
  const int64_t mac2 = static_cast<int64_t>(MAC2_) << sf_;
  const int64_t mac3 = static_cast<int64_t>(MAC3_) << sf_;

  MAC1_ = static_cast<int32_t>(set_mac(1, IR_[1] * IR_[0] + mac1) >> sf_);
  MAC2_ = static_cast<int32_t>(set_mac(2, IR_[2] * IR_[0] + mac2) >> sf_);
  MAC3_ = static_cast<int32_t>(set_mac(3, IR_[3] * IR_[0] + mac3) >> sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;
}

void PsxGte::gpf() {
  // Match ProjectPSX cast/shift precedence: (int)setMAC(...) >> sf
  MAC1_ = static_cast<int32_t>(set_mac(1, IR_[1] * IR_[0])) >> sf_;
  MAC2_ = static_cast<int32_t>(set_mac(2, IR_[2] * IR_[0])) >> sf_;
  MAC3_ = static_cast<int32_t>(set_mac(3, IR_[3] * IR_[0])) >> sf_;

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;
}

void PsxGte::ncdt() {
  ncds(0);
  ncds(1);
  ncds(2);
}

void PsxGte::op() {
  const int16_t d1 = RT_.v1.x;
  const int16_t d2 = RT_.v2.y;
  const int16_t d3 = RT_.v3.z;

  MAC1_ = static_cast<int32_t>(set_mac(1, ((IR_[3] * d2) - (IR_[2] * d3)) >> sf_));
  MAC2_ = static_cast<int32_t>(set_mac(2, ((IR_[1] * d3) - (IR_[3] * d1)) >> sf_));
  MAC3_ = static_cast<int32_t>(set_mac(3, ((IR_[2] * d1) - (IR_[1] * d2)) >> sf_));

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);
}

void PsxGte::sqr() {
  MAC1_ = static_cast<int32_t>(set_mac(1, (IR_[1] * IR_[1]) >> sf_));
  MAC2_ = static_cast<int32_t>(set_mac(2, (IR_[2] * IR_[2]) >> sf_));
  MAC3_ = static_cast<int32_t>(set_mac(3, (IR_[3] * IR_[3]) >> sf_));

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);
}

void PsxGte::avsz3() {
  const int64_t avsz3 = static_cast<int64_t>(ZSF3_) * (SZ_[1] + SZ_[2] + SZ_[3]);
  MAC0_ = static_cast<int32_t>(set_mac0(avsz3));
  OTZ_ = set_sz3(avsz3 >> 12);
}

void PsxGte::avsz4() {
  const int64_t avsz4 = static_cast<int64_t>(ZSF4_) * (SZ_[0] + SZ_[1] + SZ_[2] + SZ_[3]);
  MAC0_ = static_cast<int32_t>(set_mac0(avsz4));
  OTZ_ = set_sz3(avsz4 >> 12);
}

void PsxGte::ncds(int r) {
  MAC1_ = static_cast<int32_t>(
      set_mac(1, static_cast<int64_t>(LM_.v1.x) * V_[r].x + LM_.v1.y * V_[r].y + LM_.v1.z * V_[r].z) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, static_cast<int64_t>(LM_.v2.x) * V_[r].x + LM_.v2.y * V_[r].y + LM_.v2.z * V_[r].z) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, static_cast<int64_t>(LM_.v3.x) * V_[r].x + LM_.v3.y * V_[r].y + LM_.v3.z * V_[r].z) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  MAC1_ = static_cast<int32_t>(
      set_mac(1, set_mac(1, set_mac(1, static_cast<int64_t>(RBK_) * 0x1000 + LRGB_.v1.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v1.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v1.z) * IR_[3]) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, set_mac(2, set_mac(2, static_cast<int64_t>(GBK_) * 0x1000 + LRGB_.v2.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v2.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v2.z) * IR_[3]) >>
      sf_);
  MAC3_ = static_cast<int32_t>(
      set_mac(3, set_mac(3, set_mac(3, static_cast<int64_t>(BBK_) * 0x1000 + LRGB_.v3.x * IR_[1]) +
                                    static_cast<int64_t>(LRGB_.v3.y) * IR_[2]) +
                        static_cast<int64_t>(LRGB_.v3.z) * IR_[3]) >>
      sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);

  MAC1_ = static_cast<int32_t>(set_mac(1, (static_cast<int64_t>(RGBC_.r) * IR_[1]) << 4));
  MAC2_ = static_cast<int32_t>(set_mac(2, (static_cast<int64_t>(RGBC_.g) * IR_[2]) << 4));
  MAC3_ = static_cast<int32_t>(set_mac(3, (static_cast<int64_t>(RGBC_.b) * IR_[3]) << 4));

  interpolate_color(MAC1_, MAC2_, MAC3_);

  RGB_[0] = RGB_[1];
  RGB_[1] = RGB_[2];
  RGB_[2].r = set_rgb(1, MAC1_ >> 4);
  RGB_[2].g = set_rgb(2, MAC2_ >> 4);
  RGB_[2].b = set_rgb(3, MAC3_ >> 4);
  RGB_[2].c = RGBC_.c;
}

void PsxGte::interpolate_color(int mac1, int mac2, int mac3) {
  MAC1_ = static_cast<int32_t>(set_mac(1, (static_cast<int64_t>(RFC_) << 12) - mac1) >> sf_);
  MAC2_ = static_cast<int32_t>(set_mac(2, (static_cast<int64_t>(GFC_) << 12) - mac2) >> sf_);
  MAC3_ = static_cast<int32_t>(set_mac(3, (static_cast<int64_t>(BFC_) << 12) - mac3) >> sf_);

  IR_[1] = set_ir(1, MAC1_, false);
  IR_[2] = set_ir(2, MAC2_, false);
  IR_[3] = set_ir(3, MAC3_, false);

  MAC1_ = static_cast<int32_t>(set_mac(1, (static_cast<int64_t>(IR_[1]) * IR_[0]) + mac1) >> sf_);
  MAC2_ = static_cast<int32_t>(set_mac(2, (static_cast<int64_t>(IR_[2]) * IR_[0]) + mac2) >> sf_);
  MAC3_ = static_cast<int32_t>(set_mac(3, (static_cast<int64_t>(IR_[3]) * IR_[0]) + mac3) >> sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  IR_[3] = set_ir(3, MAC3_, lm_);
}

void PsxGte::nclip() {
  MAC0_ = static_cast<int32_t>(set_mac0(
      static_cast<int64_t>(SXY_[0].x) * SXY_[1].y + static_cast<int64_t>(SXY_[1].x) * SXY_[2].y +
      static_cast<int64_t>(SXY_[2].x) * SXY_[0].y - static_cast<int64_t>(SXY_[0].x) * SXY_[2].y -
      static_cast<int64_t>(SXY_[1].x) * SXY_[0].y - static_cast<int64_t>(SXY_[2].x) * SXY_[1].y));
}

void PsxGte::rtpt() {
  rtps(0, false);
  rtps(1, false);
  rtps(2, true);
}

void PsxGte::rtps(int r, bool set_mac0_flag) {
  MAC1_ = static_cast<int32_t>(
      set_mac(1, set_mac(1, set_mac(1, static_cast<int64_t>(TRX_) * 0x1000 + RT_.v1.x * V_[r].x) +
                                    static_cast<int64_t>(RT_.v1.y) * V_[r].y) +
                        static_cast<int64_t>(RT_.v1.z) * V_[r].z) >>
      sf_);
  MAC2_ = static_cast<int32_t>(
      set_mac(2, set_mac(2, set_mac(2, static_cast<int64_t>(TRY_) * 0x1000 + RT_.v2.x * V_[r].x) +
                                    static_cast<int64_t>(RT_.v2.y) * V_[r].y) +
                        static_cast<int64_t>(RT_.v2.z) * V_[r].z) >>
      sf_);
  const int64_t mac3 =
      set_mac(3, set_mac(3, set_mac(3, static_cast<int64_t>(TRZ_) * 0x1000 + RT_.v3.x * V_[r].x) +
                                    static_cast<int64_t>(RT_.v3.y) * V_[r].y) +
                        static_cast<int64_t>(RT_.v3.z) * V_[r].z);
  MAC3_ = static_cast<int32_t>(mac3 >> sf_);

  IR_[1] = set_ir(1, MAC1_, lm_);
  IR_[2] = set_ir(2, MAC2_, lm_);
  set_ir(3, static_cast<int>(mac3 >> 12), false);
  IR_[3] = static_cast<int16_t>(
      std::clamp(MAC3_, lm_ ? 0 : -0x8000, 0x7FFF));

  SZ_[0] = SZ_[1];
  SZ_[1] = SZ_[2];
  SZ_[2] = SZ_[3];
  SZ_[3] = set_sz3(mac3 >> 12);

  int64_t n;
  if (H_ < SZ_[3] * 2u) {
    // BitOperations.LeadingZeroCount on the zero-extended ushort.
    const int z = (SZ_[3] == 0) ? 16 : (__builtin_clz(static_cast<unsigned>(SZ_[3])) - 16);
    n = static_cast<int64_t>(H_) << z;
    uint32_t d = static_cast<uint32_t>(SZ_[3]) << z;
    uint16_t u = static_cast<uint16_t>(kUnrTable[static_cast<int>((d - 0x7FC0u) >> 7)] + 0x101);
    d = (0x2000080u - (d * u)) >> 8;
    d = (0x0000080u + (d * u)) >> 8;
    n = static_cast<int>(std::min<int64_t>(0x1FFFF, ((n * d) + 0x8000) >> 16));
  } else {
    FLAG_ |= 1u << 17;
    n = 0x1FFFF;
  }

  const int x = static_cast<int>(set_mac0(n * IR_[1] + OFX_) >> 16);
  const int y = static_cast<int>(set_mac0(n * IR_[2] + OFY_) >> 16);

  SXY_[0] = SXY_[1];
  SXY_[1] = SXY_[2];
  SXY_[2].x = set_sxy(1, x);
  SXY_[2].y = set_sxy(2, y);

  if (set_mac0_flag) {
    const int64_t mac0 = set_mac0(n * DQA_ + DQB_);
    MAC0_ = static_cast<int32_t>(mac0);
    IR_[0] = set_ir0(mac0 >> 12);
  }
}

int16_t PsxGte::set_ir0(int64_t value) {
  if (value < 0) {
    FLAG_ |= 0x1000;
    return 0;
  }
  if (value > 0x1000) {
    FLAG_ |= 0x1000;
    return 0x1000;
  }
  return static_cast<int16_t>(value);
}

int16_t PsxGte::set_sxy(int i, int value) {
  if (value < -0x400) {
    FLAG_ |= static_cast<uint32_t>(0x4000 >> (i - 1));
    return -0x400;
  }
  if (value > 0x3FF) {
    FLAG_ |= static_cast<uint32_t>(0x4000 >> (i - 1));
    return 0x3FF;
  }
  return static_cast<int16_t>(value);
}

uint16_t PsxGte::set_sz3(int64_t value) {
  if (value < 0) {
    FLAG_ |= 0x40000;
    return 0;
  }
  if (value > 0xFFFF) {
    FLAG_ |= 0x40000;
    return 0xFFFF;
  }
  return static_cast<uint16_t>(value);
}

uint8_t PsxGte::set_rgb(int i, int value) {
  if (value < 0) {
    FLAG_ |= static_cast<uint32_t>(0x200000) >> (i - 1);
    return 0;
  }
  if (value > 0xFF) {
    FLAG_ |= static_cast<uint32_t>(0x200000) >> (i - 1);
    return 0xFF;
  }
  return static_cast<uint8_t>(value);
}

int16_t PsxGte::set_ir(int i, int value, bool lm_flag) {
  if (lm_flag && value < 0) {
    FLAG_ |= static_cast<uint32_t>(0x1000000 >> (i - 1));
    return 0;
  }
  if (!lm_flag && value < -0x8000) {
    FLAG_ |= static_cast<uint32_t>(0x1000000 >> (i - 1));
    return -0x8000;
  }
  if (value > 0x7FFF) {
    FLAG_ |= static_cast<uint32_t>(0x1000000 >> (i - 1));
    return 0x7FFF;
  }
  return static_cast<int16_t>(value);
}

int64_t PsxGte::set_mac0(int64_t value) {
  if (value < -0x80000000LL) {
    FLAG_ |= 0x8000;
  } else if (value > 0x7FFFFFFFLL) {
    FLAG_ |= 0x10000;
  }
  return value;
}

int64_t PsxGte::set_mac(int i, int64_t value) {
  if (value < -0x80000000000LL) {
    FLAG_ |= static_cast<uint32_t>(0x8000000 >> (i - 1));
  } else if (value > 0x7FFFFFFFFFFLL) {
    FLAG_ |= static_cast<uint32_t>(0x40000000 >> (i - 1));
  }
  return (value << 20) >> 20;
}

uint8_t PsxGte::saturate_rgb(int value) {
  if (value < 0x00) return 0x00;
  if (value > 0x1F) return 0x1F;
  return static_cast<uint8_t>(value);
}

int PsxGte::leading_count(uint32_t v) {
  const uint32_t sign = v >> 31;
  int count = 0;
  for (int i = 0; i < 32; ++i) {
    if ((v >> 31) != sign) break;
    ++count;
    v <<= 1;
  }
  return count;
}

uint32_t PsxGte::read_data(uint32_t fs) const {
  switch (fs) {
    case 0: return V_[0].XY();
    case 1: return static_cast<uint32_t>(static_cast<int32_t>(V_[0].z));
    case 2: return V_[1].XY();
    case 3: return static_cast<uint32_t>(static_cast<int32_t>(V_[1].z));
    case 4: return V_[2].XY();
    case 5: return static_cast<uint32_t>(static_cast<int32_t>(V_[2].z));
    case 6: return RGBC_.val();
    case 7: return OTZ_;
    case 8: return static_cast<uint32_t>(static_cast<int32_t>(IR_[0]));
    case 9: return static_cast<uint32_t>(static_cast<int32_t>(IR_[1]));
    case 10: return static_cast<uint32_t>(static_cast<int32_t>(IR_[2]));
    case 11: return static_cast<uint32_t>(static_cast<int32_t>(IR_[3]));
    case 12: return SXY_[0].val();
    case 13: return SXY_[1].val();
    case 14:
    case 15: return SXY_[2].val();
    case 16: return SZ_[0];
    case 17: return SZ_[1];
    case 18: return SZ_[2];
    case 19: return SZ_[3];
    case 20: return RGB_[0].val();
    case 21: return RGB_[1].val();
    case 22: return RGB_[2].val();
    case 23: return RES1_;
    case 24: return static_cast<uint32_t>(MAC0_);
    case 25: return static_cast<uint32_t>(MAC1_);
    case 26: return static_cast<uint32_t>(MAC2_);
    case 27: return static_cast<uint32_t>(MAC3_);
    case 28:
    case 29: {
      // ProjectPSX assigns IRGB on read; keep const-correct by computing.
      return static_cast<uint16_t>(
          (saturate_rgb(IR_[3] / 0x80) << 10) | (saturate_rgb(IR_[2] / 0x80) << 5) |
          saturate_rgb(IR_[1] / 0x80));
    }
    case 30: return static_cast<uint32_t>(LZCS_);
    case 31: return static_cast<uint32_t>(LZCR_);
    default: return 0xFFFFFFFFu;
  }
}

void PsxGte::write_data(uint32_t fs, uint32_t v) {
  switch (fs) {
    case 0: V_[0].set_XY(v); break;
    case 1: V_[0].z = static_cast<int16_t>(v); break;
    case 2: V_[1].set_XY(v); break;
    case 3: V_[1].z = static_cast<int16_t>(v); break;
    case 4: V_[2].set_XY(v); break;
    case 5: V_[2].z = static_cast<int16_t>(v); break;
    case 6: RGBC_.set_val(v); break;
    case 7: OTZ_ = static_cast<uint16_t>(v); break;
    case 8: IR_[0] = static_cast<int16_t>(v); break;
    case 9: IR_[1] = static_cast<int16_t>(v); break;
    case 10: IR_[2] = static_cast<int16_t>(v); break;
    case 11: IR_[3] = static_cast<int16_t>(v); break;
    case 12: SXY_[0].set_val(v); break;
    case 13: SXY_[1].set_val(v); break;
    case 14: SXY_[2].set_val(v); break;
    case 15:
      SXY_[0] = SXY_[1];
      SXY_[1] = SXY_[2];
      SXY_[2].set_val(v);
      break;
    case 16: SZ_[0] = static_cast<uint16_t>(v); break;
    case 17: SZ_[1] = static_cast<uint16_t>(v); break;
    case 18: SZ_[2] = static_cast<uint16_t>(v); break;
    case 19: SZ_[3] = static_cast<uint16_t>(v); break;
    case 20: RGB_[0].set_val(v); break;
    case 21: RGB_[1].set_val(v); break;
    case 22: RGB_[2].set_val(v); break;
    case 23: RES1_ = v; break;
    case 24: MAC0_ = static_cast<int32_t>(v); break;
    case 25: MAC1_ = static_cast<int32_t>(v); break;
    case 26: MAC2_ = static_cast<int32_t>(v); break;
    case 27: MAC3_ = static_cast<int32_t>(v); break;
    case 28:
      IRGB_ = static_cast<uint16_t>(v & 0x7FFF);
      IR_[1] = static_cast<int16_t>((v & 0x1F) * 0x80);
      IR_[2] = static_cast<int16_t>(((v >> 5) & 0x1F) * 0x80);
      IR_[3] = static_cast<int16_t>(((v >> 10) & 0x1F) * 0x80);
      break;
    case 29: break;
    case 30:
      LZCS_ = static_cast<int32_t>(v);
      LZCR_ = leading_count(v);
      break;
    case 31: break;
    default: break;
  }
}

uint32_t PsxGte::read_control(uint32_t fs) const {
  switch (fs) {
    case 0: return RT_.v1.XY();
    case 1: return static_cast<uint16_t>(RT_.v1.z) | (static_cast<uint32_t>(RT_.v2.x) << 16);
    case 2: return static_cast<uint16_t>(RT_.v2.y) | (static_cast<uint32_t>(RT_.v2.z) << 16);
    case 3: return RT_.v3.XY();
    case 4: return static_cast<uint32_t>(static_cast<int32_t>(RT_.v3.z));
    case 5: return static_cast<uint32_t>(TRX_);
    case 6: return static_cast<uint32_t>(TRY_);
    case 7: return static_cast<uint32_t>(TRZ_);
    case 8: return LM_.v1.XY();
    case 9: return static_cast<uint16_t>(LM_.v1.z) | (static_cast<uint32_t>(LM_.v2.x) << 16);
    case 10: return static_cast<uint16_t>(LM_.v2.y) | (static_cast<uint32_t>(LM_.v2.z) << 16);
    case 11: return LM_.v3.XY();
    case 12: return static_cast<uint32_t>(static_cast<int32_t>(LM_.v3.z));
    case 13: return static_cast<uint32_t>(RBK_);
    case 14: return static_cast<uint32_t>(GBK_);
    case 15: return static_cast<uint32_t>(BBK_);
    case 16: return LRGB_.v1.XY();
    case 17: return static_cast<uint16_t>(LRGB_.v1.z) | (static_cast<uint32_t>(LRGB_.v2.x) << 16);
    case 18: return static_cast<uint16_t>(LRGB_.v2.y) | (static_cast<uint32_t>(LRGB_.v2.z) << 16);
    case 19: return LRGB_.v3.XY();
    case 20: return static_cast<uint32_t>(static_cast<int32_t>(LRGB_.v3.z));
    case 21: return static_cast<uint32_t>(RFC_);
    case 22: return static_cast<uint32_t>(GFC_);
    case 23: return static_cast<uint32_t>(BFC_);
    case 24: return static_cast<uint32_t>(OFX_);
    case 25: return static_cast<uint32_t>(OFY_);
    case 26: return static_cast<uint32_t>(static_cast<int16_t>(H_));
    case 27: return static_cast<uint32_t>(DQA_);
    case 28: return static_cast<uint32_t>(DQB_);
    case 29: return static_cast<uint32_t>(ZSF3_);
    case 30: return static_cast<uint32_t>(ZSF4_);
    case 31: return FLAG_;
    default: return 0xFFFFFFFFu;
  }
}

void PsxGte::write_control(uint32_t fs, uint32_t v) {
  switch (fs) {
    case 0: RT_.v1.set_XY(v); break;
    case 1:
      RT_.v1.z = static_cast<int16_t>(v);
      RT_.v2.x = static_cast<int16_t>(v >> 16);
      break;
    case 2:
      RT_.v2.y = static_cast<int16_t>(v);
      RT_.v2.z = static_cast<int16_t>(v >> 16);
      break;
    case 3: RT_.v3.set_XY(v); break;
    case 4: RT_.v3.z = static_cast<int16_t>(v); break;
    case 5: TRX_ = static_cast<int32_t>(v); break;
    case 6: TRY_ = static_cast<int32_t>(v); break;
    case 7: TRZ_ = static_cast<int32_t>(v); break;
    case 8: LM_.v1.set_XY(v); break;
    case 9:
      LM_.v1.z = static_cast<int16_t>(v);
      LM_.v2.x = static_cast<int16_t>(v >> 16);
      break;
    case 10:
      LM_.v2.y = static_cast<int16_t>(v);
      LM_.v2.z = static_cast<int16_t>(v >> 16);
      break;
    case 11: LM_.v3.set_XY(v); break;
    case 12: LM_.v3.z = static_cast<int16_t>(v); break;
    case 13: RBK_ = static_cast<int32_t>(v); break;
    case 14: GBK_ = static_cast<int32_t>(v); break;
    case 15: BBK_ = static_cast<int32_t>(v); break;
    case 16: LRGB_.v1.set_XY(v); break;
    case 17:
      LRGB_.v1.z = static_cast<int16_t>(v);
      LRGB_.v2.x = static_cast<int16_t>(v >> 16);
      break;
    case 18:
      LRGB_.v2.y = static_cast<int16_t>(v);
      LRGB_.v2.z = static_cast<int16_t>(v >> 16);
      break;
    case 19: LRGB_.v3.set_XY(v); break;
    case 20: LRGB_.v3.z = static_cast<int16_t>(v); break;
    case 21: RFC_ = static_cast<int32_t>(v); break;
    case 22: GFC_ = static_cast<int32_t>(v); break;
    case 23: BFC_ = static_cast<int32_t>(v); break;
    case 24: OFX_ = static_cast<int32_t>(v); break;
    case 25: OFY_ = static_cast<int32_t>(v); break;
    case 26: H_ = static_cast<uint16_t>(v); break;
    case 27: DQA_ = static_cast<int16_t>(v); break;
    case 28: DQB_ = static_cast<int32_t>(v); break;
    case 29: ZSF3_ = static_cast<int16_t>(v); break;
    case 30: ZSF4_ = static_cast<int16_t>(v); break;
    case 31:
      FLAG_ = v & 0x7FFFF000u;
      if ((FLAG_ & 0x7F87E000u) != 0) {
        FLAG_ |= 0x80000000u;
      }
      break;
    default: break;
  }
}

}  // namespace dsp
