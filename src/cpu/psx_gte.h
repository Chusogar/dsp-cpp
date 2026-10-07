// Portions derived from ProjectPSX GTE.cs (MIT License)
// Copyright (c) 2019 Pedro Cortés
// See PROJECTPSX_LICENSE / https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <cstdint>

namespace dsp {

// PlayStation Geometry Transformation Engine (COP2). Faithful port of
// ProjectPSX's GTE.cs: data/control registers, all documented commands,
// MAC/IR/RGB/SZ/SXY flag saturation, and the UNR reciprocal table used by
// RTPS/RTPT perspective divide.
class PsxGte {
public:
  void execute(uint32_t command);

  uint32_t read_data(uint32_t fs) const;
  void write_data(uint32_t fs, uint32_t value);
  uint32_t read_control(uint32_t fs) const;
  void write_control(uint32_t fs, uint32_t value);

  uint32_t load_data(uint32_t fs) const { return read_data(fs); }
  uint32_t load_control(uint32_t fs) const { return read_control(fs); }

  void reset();

private:
  struct Vector3 {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;

    uint32_t XY() const {
      return static_cast<uint32_t>(static_cast<uint16_t>(x)) |
             (static_cast<uint32_t>(static_cast<uint16_t>(y)) << 16);
    }
    void set_XY(uint32_t v) {
      x = static_cast<int16_t>(v);
      y = static_cast<int16_t>(v >> 16);
    }
  };

  struct Vector2 {
    int16_t x = 0;
    int16_t y = 0;

    uint32_t val() const {
      return static_cast<uint32_t>(static_cast<uint16_t>(x)) |
             (static_cast<uint32_t>(static_cast<uint16_t>(y)) << 16);
    }
    void set_val(uint32_t v) {
      x = static_cast<int16_t>(v);
      y = static_cast<int16_t>(v >> 16);
    }
  };

  struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t c = 0;

    uint32_t val() const {
      return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
             (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 24);
    }
    void set_val(uint32_t v) {
      r = static_cast<uint8_t>(v);
      g = static_cast<uint8_t>(v >> 8);
      b = static_cast<uint8_t>(v >> 16);
      c = static_cast<uint8_t>(v >> 24);
    }
  };

  struct Matrix {
    Vector3 v1{};
    Vector3 v2{};
    Vector3 v3{};
  };

  void cdp();
  void cc();
  void dcpt();
  void dcpl();
  void nccs(int r);
  void ncct();
  void dpcs(bool dpct);
  void intpl();
  void nct();
  void ncs(int r);
  void mvmva();
  void gpl();
  void gpf();
  void ncdt();
  void op();
  void sqr();
  void avsz3();
  void avsz4();
  void ncds(int r);
  void interpolate_color(int mac1, int mac2, int mac3);
  void nclip();
  void rtpt();
  void rtps(int r, bool set_mac0);

  int16_t set_ir0(int64_t value);
  int16_t set_sxy(int i, int value);
  uint16_t set_sz3(int64_t value);
  uint8_t set_rgb(int i, int value);
  int16_t set_ir(int i, int value, bool lm_flag);
  int64_t set_mac0(int64_t value);
  int64_t set_mac(int i, int64_t value);

  static uint8_t saturate_rgb(int value);
  static int leading_count(uint32_t v);

  Vector3 V_[3]{};
  Color RGBC_{};
  uint16_t OTZ_ = 0;
  int16_t IR_[4]{};
  Vector2 SXY_[4]{};
  uint16_t SZ_[4]{};
  Color RGB_[3]{};
  uint32_t RES1_ = 0;
  int32_t MAC0_ = 0;
  int32_t MAC1_ = 0, MAC2_ = 0, MAC3_ = 0;
  uint16_t IRGB_ = 0;
  int32_t LZCS_ = 0, LZCR_ = 0;

  Matrix RT_{}, LM_{}, LRGB_{};
  int32_t TRX_ = 0, TRY_ = 0, TRZ_ = 0;
  int32_t RBK_ = 0, GBK_ = 0, BBK_ = 0;
  int32_t RFC_ = 0, GFC_ = 0, BFC_ = 0;
  int32_t OFX_ = 0, OFY_ = 0, DQB_ = 0;
  uint16_t H_ = 0;
  int16_t ZSF3_ = 0, ZSF4_ = 0, DQA_ = 0;
  uint32_t FLAG_ = 0;

  int sf_ = 0;
  bool lm_ = false;
  uint32_t current_command_ = 0;
};

}  // namespace dsp
