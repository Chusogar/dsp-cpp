#include "cpu/upd7801.h"

#include <algorithm>
#include <utility>

namespace dsp {

const uint8_t Upd7801::kOpSize[256] = {
      1,   1,   1,   1,   3,   3,   1,   2,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   3,   3,   2,   2,   1,   1,   1,   1,   1,   1,   1,   1,
      2,   1,   1,   1,   3,   3,   2,   2,   2,   1,   1,   1,   1,   1,   1,   1,
      2,   1,   1,   1,   3,   3,   2,   2,   2,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   3,   3,   2,   2,   1,   2,   2,   2,   1,   1,   2,   2,
      1,   1,   1,   1,   3,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      1,   1,   1,   2,   1,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      1,   3,   1,   1,   1,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
};

const uint8_t Upd7801::kOpCycles[256] = {
      4,   6,   7,   7,  10,  16,   4,   7,  11,   4,   4,   4,   4,   4,   4,   4,
      4,   4,   7,   7,  10,  16,   7,   7,  11,   4,   4,   4,   4,   4,   4,   4,
     13,  19,   7,   7,  10,  13,   7,   7,  10,   7,   7,   7,   7,   7,   7,   7,
     13,  13,   7,   7,  10,  13,   7,   7,  10,   7,   7,   7,   7,   7,   7,   7,
      4,   4,   4,   4,  16,  13,   7,   7,   0,  10,  10,  10,   0,   0,  17,  17,
      4,   4,   4,   4,  10,  13,   7,   7,  10,  10,  10,  10,  10,  10,  10,  10,
      0,   4,  15,  13,   0,  13,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,
      0,  13,  19,   4,   0,  13,   7,   7,  16,  16,  16,  16,  16,  16,  16,  16,
     19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,
     19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,
     19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,
     19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,
     13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,
     13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,
     13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,
     13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,
};

const uint8_t Upd7801::kOp48Size[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
};

const uint8_t Upd7801::kOp48Cycles[256] = {
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  17,  15,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  17,  15,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  11,  11,  17,  15,
      8,   8,   8,   8,   8,   8,   8,   8,  17,  17,   8,   8,  11,   8,  17,  15,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
};

const uint8_t Upd7801::kOp4cSize[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
};

const uint8_t Upd7801::kOp4cCycles[256] = {
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,   8,   8,   8,   8,  10,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
};

const uint8_t Upd7801::kOp4dSize[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
};

const uint8_t Upd7801::kOp4dCycles[256] = {
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
};

const uint8_t Upd7801::kOp64Size[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
};

const uint8_t Upd7801::kOp64Cycles[256] = {
      8,   8,   8,   8,   8,   8,   8,   8,  11,   1,   1,  11,  11,  11,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
      8,   8,   8,   8,   8,   8,   8,   8,  17,  17,  17,  17,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  17,  17,  17,  17,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
};

const uint8_t Upd7801::kOp70Size[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   4,   4,   4,   4,   4,   4,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   4,   4,   4,   4,   4,   4,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
};

const uint8_t Upd7801::kOp70Cycles[256] = {
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  20,  20,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  20,  20,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  20,  20,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  20,  20,
      8,  11,  11,  11,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,  17,  17,  17,  17,  17,  17,  17,  17,
      8,   8,   8,   8,   8,   8,   8,   8,  17,  17,  17,  17,  17,  17,  17,  17,
      8,   8,   8,   8,   8,   8,   8,   8,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
};

const uint8_t Upd7801::kOp74Size[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
};

const uint8_t Upd7801::kOp74Cycles[256] = {
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
};

Upd7801::Upd7801(uint32_t clock) : clock_(clock / 2) {}

void Upd7801::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void Upd7801::set_port_in(PortInHandler port_b, PortInHandler port_c) {
    port_b_in_ = std::move(port_b);
    port_c_in_ = std::move(port_c);
}

void Upd7801::set_port_out(PortOutHandler port_a, PortOutHandler port_c) {
    port_a_out_ = std::move(port_a);
    port_c_out_ = std::move(port_c);
}

void Upd7801::reset() {
    pc_ = ppc_ = sp_ = 0;
    v_ = a_ = b_ = c_ = d_ = e_ = h_ = l_ = 0;
    v2_ = a2_ = b2_ = c2_ = d2_ = e2_ = h2_ = l2_ = 0;
    ea_ = ea2_ = 0;
    zf_ = cy_ = sk_ = hc_ = l0_ = l1_ = f1_ = f7_ = false;
    iff_ = iff_pending_ = false;
    int1_ = int2_ = 0;
    irr_ = 0;
    mkl_ = 0xFF;
    tm_ = 0;
    ovc0_ = 0;
    to_ = 0;
    ma_ = 0;
    mb_ = 0xFF;
    mc_ = 0xFF;
    mcc_ = 0;
    pa_in_ = pb_in_ = pc_in_ = 0;
    pa_out_ = pb_out_ = pc_out_ = 0;
    txd_ = rdx_ = sck_ = ci_ = co0_ = co1_ = 0;
    iram.fill(0);
}

uint8_t Upd7801::psw() const {
    uint8_t v = 0;
    if (cy_) v |= 0x01;
    if (f1_) v |= 0x02;
    if (l0_) v |= 0x04;
    if (l1_) v |= 0x08;
    if (hc_) v |= 0x10;
    if (sk_) v |= 0x20;
    if (zf_) v |= 0x40;
    if (f7_) v |= 0x80;
    return v;
}

void Upd7801::set_psw(uint8_t value) {
    cy_ = (value & 0x01) != 0;
    f1_ = (value & 0x02) != 0;
    l0_ = (value & 0x04) != 0;
    l1_ = (value & 0x08) != 0;
    hc_ = (value & 0x10) != 0;
    sk_ = (value & 0x20) != 0;
    zf_ = (value & 0x40) != 0;
    f7_ = (value & 0x80) != 0;
}

void Upd7801::zhc_add(uint16_t after, uint16_t before, bool carry) {
    zf_ = (after == 0);
    if (after == before) cy_ = carry;
    else cy_ = after < before;
    hc_ = (after & 15) < (before & 15);
}

void Upd7801::zhc_sub(uint16_t after, uint16_t before, bool carry) {
    zf_ = (after == 0);
    if (before == after) cy_ = carry;
    else cy_ = after > before;
    hc_ = (after & 15) > (before & 15);
}

uint8_t Upd7801::read_port(int port) {
    uint8_t value = 0xFF;
    if (port == kPortA) {
        value = (pa_in_ & ma_) | (pa_out_ & uint8_t(~ma_));
    } else if (port == kPortB) {
        if (mb_ != 0 && port_b_in_) pb_in_ = port_b_in_(mb_);
        value = (pb_in_ & mb_) | (pb_out_ & uint8_t(~mb_));
    } else if (port == kPortC) {
        if (mc_ != 0 && port_c_in_) pc_in_ = port_c_in_(mc_);
        value = (pc_in_ & mc_) | (pc_out_ & uint8_t(~mc_));
        auto overlay = [&](uint8_t bit, uint8_t src) {
            if (mcc_ & bit) {
                value = uint8_t(value & ~bit);
                if (src & 1) value = uint8_t(value | bit);
            }
        };
        overlay(0x01, txd_);
        overlay(0x02, rdx_);
        overlay(0x04, sck_);
        overlay(0x08, int2_);
        overlay(0x10, to_);
        overlay(0x20, ci_);
        overlay(0x40, co0_);
        overlay(0x80, co1_);
    }
    return value;
}

void Upd7801::write_port(int port, uint8_t value) {
    if (port == kPortA) {
        pa_out_ = value;
        value = uint8_t((value & ~ma_) | ma_);
        if (port_a_out_) port_a_out_(value);
    } else if (port == kPortB) {
        pb_out_ = value;
    } else if (port == kPortC) {
        pc_out_ = value;
        value = uint8_t((value & ~mc_) | mc_);
        auto overlay = [&](uint8_t bit, uint8_t src) {
            if (mcc_ & bit) {
                value = uint8_t(value & ~bit);
                if (src & 1) value = uint8_t(value | bit);
            }
        };
        overlay(0x01, txd_);
        overlay(0x02, rdx_);
        overlay(0x04, sck_);
        overlay(0x08, int2_);
        overlay(0x10, to_);
        overlay(0x20, ci_);
        overlay(0x40, co0_);
        overlay(0x80, co1_);
        if (port_c_out_) port_c_out_(value);
    }
}

void Upd7801::set_input_line(int irqline, IrqLine state) {
    const uint8_t asserted = (state == IrqLine::Clear) ? 0 : 1;
    if (irqline == kIntf0) {
        if (asserted) irr_ |= kIntf0Bit;
        else irr_ = uint16_t(irr_ & ~kIntf0Bit);
    } else if (irqline == kIntf1) {
        if (int1_ == 0 && asserted) irr_ |= kIntf1Bit;
        int1_ = asserted;
    } else if (irqline == kIntf2) {
        if (mkl_ & 0x20) {
            if (int2_ == 0 && asserted) irr_ |= kIntf2Bit;
        } else if (int2_ == 1 && asserted == 0) {
            irr_ |= kIntf2Bit;
        }
        int2_ = asserted;
    }
}

void Upd7801::take_irq() {
    if (!iff_) return;
    uint16_t vector = 0;
    if ((irr_ & kIntf0Bit) && (mkl_ & 0x01) == 0) {
        vector = 0x0004;
        irr_ = uint16_t(irr_ & ~kIntf0Bit);
    }
    if ((irr_ & kIntFt0Bit) && (mkl_ & 0x02) == 0) {
        vector = 0x0008;
        irr_ = uint16_t(irr_ & ~kIntFt0Bit);
    }
    if ((irr_ & kIntf1Bit) && (mkl_ & 0x04) == 0) {
        vector = 0x0010;
        irr_ = uint16_t(irr_ & ~kIntf1Bit);
    }
    if ((irr_ & kIntf2Bit) && (mkl_ & 0x08) == 0) {
        vector = 0x0020;
        irr_ = uint16_t(irr_ & ~kIntf2Bit);
    }
    if ((irr_ & kIntfStBit) && (mkl_ & 0x10) == 0) {
        vector = 0x0040;
        irr_ = uint16_t(irr_ & ~kIntfStBit);
    }
    if (vector == 0) return;
    wr(--sp_, psw());
    wr(--sp_, uint8_t(pc_ >> 8));
    wr(--sp_, uint8_t(pc_));
    iff_ = iff_pending_ = false;
    sk_ = l0_ = l1_ = false;
    pc_ = vector;
}

void Upd7801::handle_timers(int cycles) {
    if (ovc0_ == 0) return;
    ovc0_ -= cycles;
    if (ovc0_ <= 0) {
        irr_ |= kIntFt0Bit;
        to_ = 0;
        ovc0_ = 8 * int(tm_ & 0x0FFF);
    }
}

uint8_t& Upd7801::reg_by_index(int index) {
    switch (index & 7) {
        case 0: return v_;
        case 1: return a_;
        case 2: return b_;
        case 3: return c_;
        case 4: return d_;
        case 5: return e_;
        case 6: return h_;
        default: return l_;
    }
}

void Upd7801::ani(uint8_t& r) {
    r = uint8_t(r & fetch());
    zf_ = r == 0;
}
void Upd7801::xri(uint8_t& r) {
    r = uint8_t(r ^ fetch());
    zf_ = r == 0;
}
void Upd7801::ori(uint8_t& r) {
    r = uint8_t(r | fetch());
    zf_ = r == 0;
}
void Upd7801::adi(uint8_t& r) {
    const uint8_t before = r;
    r = uint8_t(r + fetch());
    zhc_add(r, before, false);
}
void Upd7801::aci(uint8_t& r) {
    const uint8_t before = r;
    r = uint8_t(r + fetch() + uint8_t(cy_));
    zhc_add(r, before, cy_);
}
void Upd7801::sui(uint8_t& r) {
    const uint8_t before = r;
    r = uint8_t(r - fetch());
    zhc_sub(r, before, false);
}
void Upd7801::sbi(uint8_t& r) {
    const uint8_t before = r;
    const bool carry = cy_;
    r = uint8_t(r - fetch() - uint8_t(carry));
    zhc_sub(r, before, carry);
}
void Upd7801::adinc(uint8_t& r) {
    adi(r);
    if (!cy_) sk_ = true;
}
void Upd7801::suinb(uint8_t& r) {
    sui(r);
    if (!cy_) sk_ = true;
}
void Upd7801::eqi(uint8_t& r) {
    const uint8_t before = r;
    const uint8_t after = uint8_t(r - fetch());
    zhc_sub(after, before, false);
    if (zf_) sk_ = true;
}
void Upd7801::nei(uint8_t& r) {
    const uint8_t before = r;
    const uint8_t after = uint8_t(r - fetch());
    zhc_sub(after, before, false);
    if (!zf_) sk_ = true;
}
void Upd7801::lti(uint8_t& r) {
    const uint8_t before = r;
    const uint8_t after = uint8_t(r - fetch());
    zhc_sub(after, before, false);
    if (cy_) sk_ = true;
}
void Upd7801::gti(uint8_t& r) {
    const uint16_t after = uint16_t(r - fetch() - 1);
    zhc_sub(after, r, false);
    if (!cy_) sk_ = true;
}
void Upd7801::oni(uint8_t& r) {
    if ((r & fetch()) != 0) sk_ = true;
}
void Upd7801::offi(uint8_t& r) {
    if ((r & fetch()) == 0) sk_ = true;
}

void Upd7801::add_a(uint8_t r) {
    const uint8_t before = a_;
    a_ = uint8_t(a_ + r);
    zhc_add(a_, before, false);
}
void Upd7801::adc_a(uint8_t r) {
    const uint8_t before = a_;
    a_ = uint8_t(a_ + r + uint8_t(cy_));
    zhc_add(a_, before, cy_);
}
void Upd7801::sub_a(uint8_t r) {
    const uint8_t before = a_;
    a_ = uint8_t(a_ - r);
    zhc_sub(a_, before, false);
}
void Upd7801::sbb_a(uint8_t r) {
    const uint8_t before = a_;
    const bool carry = cy_;
    a_ = uint8_t(a_ - r - uint8_t(carry));
    zhc_sub(a_, before, carry);
}
void Upd7801::addnc_a(uint8_t r) {
    add_a(r);
    if (!cy_) sk_ = true;
}
void Upd7801::subnb_a(uint8_t r) {
    sub_a(r);
    if (!cy_) sk_ = true;
}
void Upd7801::eqa_a(uint8_t r) {
    const uint8_t after = uint8_t(a_ - r);
    zhc_sub(after, a_, false);
    if (zf_) sk_ = true;
}
void Upd7801::nea_a(uint8_t r) {
    const uint8_t after = uint8_t(a_ - r);
    zhc_sub(after, a_, false);
    if (!zf_) sk_ = true;
}
void Upd7801::lta_a(uint8_t r) {
    const uint8_t after = uint8_t(a_ - r);
    zhc_sub(after, a_, false);
    if (cy_) sk_ = true;
}
void Upd7801::gta_a(uint8_t r) {
    const uint16_t after = uint16_t(a_ - r - 1);
    zhc_sub(after, a_, false);
    if (!cy_) sk_ = true;
}
void Upd7801::ona_a(uint8_t r) {
    if ((a_ & r) != 0) {
        zf_ = false;
        sk_ = true;
    } else {
        zf_ = true;
    }
}
void Upd7801::offa_a(uint8_t r) {
    if ((a_ & r) != 0) zf_ = false;
    else {
        zf_ = true;
        sk_ = true;
    }
}
void Upd7801::ora_a(uint8_t r) {
    a_ = uint8_t(a_ | r);
    zf_ = a_ == 0;
}
void Upd7801::xra_a(uint8_t r) {
    a_ = uint8_t(a_ ^ r);
    zf_ = a_ == 0;
}

void Upd7801::add_x_a(uint8_t& r) {
    const uint8_t before = r;
    r = uint8_t(r + a_);
    zhc_add(r, before, false);
}
void Upd7801::adc_x_a(uint8_t& r) {
    const uint8_t before = r;
    r = uint8_t(r + a_ + uint8_t(cy_));
    zhc_add(r, before, cy_);
}
void Upd7801::sub_x_a(uint8_t& r) {
    const uint8_t before = r;
    r = uint8_t(r - a_);
    zhc_sub(r, before, false);
}
void Upd7801::sbb_x_a(uint8_t& r) {
    const uint8_t before = r;
    const bool carry = cy_;
    r = uint8_t(r - a_ - uint8_t(carry));
    zhc_sub(r, before, carry);
}
void Upd7801::addnc_x_a(uint8_t& r) {
    add_x_a(r);
    if (!cy_) sk_ = true;
}
void Upd7801::subnb_x_a(uint8_t& r) {
    sub_x_a(r);
    if (!cy_) sk_ = true;
}
void Upd7801::eqa_x_a(uint8_t& r) {
    const uint8_t after = uint8_t(r - a_);
    zhc_sub(after, r, false);
    if (zf_) sk_ = true;
}
void Upd7801::nea_x_a(uint8_t& r) {
    const uint8_t after = uint8_t(r - a_);
    zhc_sub(after, r, false);
    if (!zf_) sk_ = true;
}
void Upd7801::lta_x_a(uint8_t& r) {
    const uint8_t after = uint8_t(r - a_);
    zhc_sub(after, r, false);
    if (cy_) sk_ = true;
}
void Upd7801::gta_x_a(uint8_t& r) {
    const uint16_t after = uint16_t(r - a_ - 1);
    zhc_sub(after, r, false);
    if (!cy_) sk_ = true;
}
void Upd7801::ana_x_a(uint8_t& r) {
    r = uint8_t(r & a_);
    zf_ = r == 0;
}
void Upd7801::ora_x_a(uint8_t& r) {
    r = uint8_t(r | a_);
    zf_ = r == 0;
}
void Upd7801::xra_x_a(uint8_t& r) {
    a_ = uint8_t(r ^ a_);
    zf_ = r == 0;
}

int Upd7801::prefix_size(uint8_t op, uint8_t op2) const {
    switch (op) {
        case 0x48: return kOp48Size[op2];
        case 0x4C: return kOp4cSize[op2];
        case 0x4D: return kOp4dSize[op2];
        case 0x60: return 2;
        case 0x64: return kOp64Size[op2];
        case 0x70: return kOp70Size[op2];
        case 0x74: return kOp74Size[op2];
        default: return kOpSize[op];
    }
}

int Upd7801::prefix_cycles(uint8_t op, uint8_t op2) const {
    int cycles = 0;
    switch (op) {
        case 0x48: cycles = kOp48Cycles[op2]; break;
        case 0x4C: cycles = kOp4cCycles[op2]; break;
        case 0x4D: cycles = kOp4dCycles[op2]; break;
        case 0x60: cycles = 8; break;
        case 0x64: cycles = kOp64Cycles[op2]; break;
        case 0x70: cycles = kOp70Cycles[op2]; break;
        case 0x74: cycles = kOp74Cycles[op2]; break;
        default: cycles = kOpCycles[op]; break;
    }
    return cycles ? cycles : 8;
}

void Upd7801::skip_rest(uint8_t op) {
    if (op == 0x48 || op == 0x4C || op == 0x4D || op == 0x60 || op == 0x64 ||
        op == 0x70 || op == 0x74) {
        const uint8_t op2 = fetch();
        const int extra = prefix_size(op, op2) - 2;
        for (int i = 0; i < extra; i++) fetch();
        return;
    }
    const int extra = kOpSize[op] - 1;
    for (int i = 0; i < extra; i++) fetch();
}

void Upd7801::op_48() {
    const uint8_t op = fetch();
    switch (op) {
        case 0x00:
            if (irr_ & kIntf0Bit) sk_ = true;
            irr_ = uint16_t(irr_ & ~kIntf0Bit);
            break;
        case 0x01:
            if (irr_ & kIntFt0Bit) sk_ = true;
            irr_ = uint16_t(irr_ & ~kIntFt0Bit);
            break;
        case 0x02:
            if (irr_ & kIntf1Bit) sk_ = true;
            irr_ = uint16_t(irr_ & ~kIntf1Bit);
            break;
        case 0x03:
            if (irr_ & kIntf2Bit) sk_ = true;
            irr_ = uint16_t(irr_ & ~kIntf2Bit);
            break;
        case 0x04:
            if (irr_ & kIntfStBit) sk_ = true;
            irr_ = uint16_t(irr_ & ~kIntfStBit);
            break;
        case 0x0A:
            if (cy_) sk_ = true;
            break;
        case 0x0C:
            if (zf_) sk_ = true;
            break;
        case 0x0E:
            wr(--sp_, v_);
            wr(--sp_, a_);
            break;
        case 0x0F:
            a_ = rd(sp_++);
            v_ = rd(sp_++);
            break;
        case 0x1A:
            if (!cy_) sk_ = true;
            break;
        case 0x1C:
            if (!zf_) sk_ = true;
            break;
        case 0x1E:
            wr(--sp_, b_);
            wr(--sp_, c_);
            break;
        case 0x1F:
            c_ = rd(sp_++);
            b_ = rd(sp_++);
            break;
        case 0x20:
            iff_pending_ = true;
            break;
        case 0x24:
            iff_ = iff_pending_ = false;
            break;
        case 0x2A:
            cy_ = false;
            break;
        case 0x2B:
            cy_ = true;
            break;
        case 0x2E:
            wr(--sp_, d_);
            wr(--sp_, e_);
            break;
        case 0x2F:
            e_ = rd(sp_++);
            d_ = rd(sp_++);
            break;
        case 0x30: {
            const uint8_t carry = uint8_t(cy_);
            cy_ = (a_ & 0x80) != 0;
            a_ = uint8_t((a_ << 1) | carry);
            break;
        }
        case 0x31: {
            const uint8_t carry = uint8_t(cy_) << 7;
            cy_ = (a_ & 1) != 0;
            a_ = uint8_t((a_ >> 1) | carry);
            break;
        }
        case 0x32: {
            const uint8_t carry = uint8_t(cy_);
            cy_ = (c_ & 0x80) != 0;
            c_ = uint8_t((c_ << 1) | carry);
            break;
        }
        case 0x33: {
            const uint8_t carry = uint8_t(cy_) << 7;
            cy_ = (c_ & 1) != 0;
            c_ = uint8_t((c_ >> 1) | carry);
            break;
        }
        case 0x34:
            cy_ = (a_ & 0x80) != 0;
            a_ = uint8_t(a_ << 1);
            break;
        case 0x35:
            cy_ = (a_ & 1) != 0;
            a_ = uint8_t(a_ >> 1);
            break;
        case 0x36:
            cy_ = (c_ & 0x80) != 0;
            c_ = uint8_t(c_ << 1);
            break;
        case 0x37:
            cy_ = (c_ & 1) != 0;
            c_ = uint8_t(c_ >> 1);
            break;
        case 0x38: {
            const uint8_t m = rd(hl());
            const uint8_t n = uint8_t((m << 4) | (a_ & 0x0F));
            a_ = uint8_t((a_ & 0xF0) | (m >> 4));
            wr(hl(), n);
            break;
        }
        case 0x39: {
            const uint8_t m = rd(hl());
            const uint8_t n = uint8_t((a_ << 4) | (m >> 4));
            a_ = uint8_t((a_ & 0xF0) | (m & 0x0F));
            wr(hl(), n);
            break;
        }
        case 0x3E:
            wr(--sp_, h_);
            wr(--sp_, l_);
            break;
        case 0x3F:
            l_ = rd(sp_++);
            h_ = rd(sp_++);
            break;
        default:
            break;
    }
}

void Upd7801::op_4c() {
    const uint8_t op = fetch();
    switch (op) {
        case 0xC0: a_ = read_port(kPortA); break;
        case 0xC1: a_ = read_port(kPortB); break;
        case 0xC2: a_ = read_port(kPortC); break;
        default: break;
    }
}

void Upd7801::op_4d() {
    const uint8_t op = fetch();
    switch (op) {
        case 0xC0: write_port(kPortA, a_); break;
        case 0xC1: write_port(kPortB, a_); break;
        case 0xC2: write_port(kPortC, a_); break;
        case 0xC3: mkl_ = a_; break;
        case 0xC4:
            if (mb_ != a_) {
                mb_ = a_;
                write_port(kPortB, pb_out_);
            }
            break;
        case 0xC5: mc_ = uint8_t(0x84 | (a_ & 0x03)); break;
        case 0xC6: tm_ = uint16_t((tm_ & 0xFF00) | a_); break;
        case 0xC7: tm_ = uint16_t((tm_ & 0x00FF) | (uint16_t(a_) << 8)); break;
        default: break;
    }
}

void Upd7801::op_60() {
    const uint8_t op = fetch();
    if (op >= 0x08 && op <= 0x0F) ana_x_a(reg_by_index(op));
    else if (op == 0x10 || op == 0x11 || op == 0x91) xra_x_a(reg_by_index(op == 0x91 ? 1 : op));
    else if (op >= 0x12 && op <= 0x17) xra_x_a(reg_by_index(op));
    else if (op >= 0x18 && op <= 0x1F) ora_x_a(reg_by_index(op));
    else if (op >= 0x20 && op <= 0x27) addnc_x_a(reg_by_index(op));
    else if (op >= 0x28 && op <= 0x2F) gta_x_a(reg_by_index(op));
    else if (op >= 0x30 && op <= 0x37) subnb_x_a(reg_by_index(op));
    else if (op >= 0x38 && op <= 0x3F) lta_x_a(reg_by_index(op));
    else if ((op >= 0x40 && op <= 0x47) || op == 0xC1) add_x_a(reg_by_index(op == 0xC1 ? 1 : op));
    else if (op >= 0x50 && op <= 0x57) adc_x_a(reg_by_index(op));
    else if (op >= 0x60 && op <= 0x67) sub_x_a(reg_by_index(op));
    else if (op >= 0x68 && op <= 0x6F) nea_x_a(reg_by_index(op));
    else if (op >= 0x70 && op <= 0x77) sbb_x_a(reg_by_index(op));
    else if (op >= 0x78 && op <= 0x7F) eqa_x_a(reg_by_index(op));
    else if (op >= 0x88 && op <= 0x8F) {
        a_ = uint8_t(a_ & reg_by_index(op));
        zf_ = a_ == 0;
    } else if (op == 0x90 || (op >= 0x92 && op <= 0x97)) xra_a(reg_by_index(op));
    else if (op >= 0x98 && op <= 0x9F) ora_a(reg_by_index(op));
    else if (op >= 0xA0 && op <= 0xA7) addnc_a(reg_by_index(op));
    else if (op >= 0xA8 && op <= 0xAF) gta_a(reg_by_index(op));
    else if (op >= 0xB0 && op <= 0xB7) subnb_a(reg_by_index(op));
    else if (op >= 0xB8 && op <= 0xBF) lta_a(reg_by_index(op));
    else if (op == 0xC0 || (op >= 0xC2 && op <= 0xC7)) add_a(reg_by_index(op));
    else if (op >= 0xC8 && op <= 0xCF) ona_a(reg_by_index(op));
    else if (op >= 0xD0 && op <= 0xD7) adc_a(reg_by_index(op));
    else if (op >= 0xD8 && op <= 0xDF) offa_a(reg_by_index(op));
    else if (op >= 0xE0 && op <= 0xE7) sub_a(reg_by_index(op));
    else if (op >= 0xE8 && op <= 0xEF) nea_a(reg_by_index(op));
    else if (op >= 0xF0 && op <= 0xF7) sbb_a(reg_by_index(op));
    else if (op >= 0xF8 && op <= 0xFF) eqa_a(reg_by_index(op));
}

void Upd7801::op_64() {
    const uint8_t op = fetch();
    if (op >= 0x08 && op <= 0x0F) ani(reg_by_index(op));
    else if (op >= 0x10 && op <= 0x17) xri(reg_by_index(op));
    else if (op >= 0x18 && op <= 0x1F) ori(reg_by_index(op));
    else if (op >= 0x20 && op <= 0x27) adinc(reg_by_index(op));
    else if (op >= 0x28 && op <= 0x2F) gti(reg_by_index(op));
    else if (op >= 0x30 && op <= 0x37) suinb(reg_by_index(op));
    else if (op >= 0x38 && op <= 0x3F) lti(reg_by_index(op));
    else if (op >= 0x40 && op <= 0x47) adi(reg_by_index(op));
    else if (op >= 0x48 && op <= 0x4F) oni(reg_by_index(op));
    else if (op >= 0x50 && op <= 0x57) aci(reg_by_index(op));
    else if (op >= 0x58 && op <= 0x5F) offi(reg_by_index(op));
    else if (op >= 0x60 && op <= 0x67) sui(reg_by_index(op));
    else if (op >= 0x68 && op <= 0x6F) nei(reg_by_index(op));
    else if (op >= 0x70 && op <= 0x77) sbi(reg_by_index(op));
    else if (op >= 0x78 && op <= 0x7F) eqi(reg_by_index(op));
    else if (op == 0x8A) {
        const uint8_t temp = uint8_t(read_port(kPortC) & fetch());
        write_port(kPortC, temp);
        zf_ = temp == 0;
    } else if (op == 0x8B) {
        mkl_ = uint8_t(mkl_ & fetch());
        zf_ = mkl_ == 0;
    } else if (op == 0x93) {
        mkl_ = uint8_t(mkl_ ^ fetch());
        zf_ = mkl_ == 0;
    } else if (op == 0x9A) {
        const uint8_t temp = uint8_t(read_port(kPortC) | fetch());
        write_port(kPortC, temp);
        zf_ = temp == 0;
    } else if (op == 0x9B) {
        mkl_ = uint8_t(mkl_ | fetch());
        zf_ = mkl_ == 0;
    } else if (op == 0xA8) {
        const uint8_t port = read_port(kPortA);
        const uint16_t after = uint16_t(port - fetch() - 1);
        zhc_sub(after, port, false);
        if (!cy_) sk_ = true;
    } else if (op == 0xCA) {
        const uint8_t port = read_port(kPortC);
        if ((port & fetch()) != 0) sk_ = true;
    } else if (op == 0xCB) {
        if ((mkl_ & fetch()) != 0) sk_ = true;
    } else if (op == 0xDA) {
        const uint8_t port = read_port(kPortC);
        if ((port & fetch()) == 0) sk_ = true;
    } else if (op == 0xDB) {
        offi(mkl_);
    }
}

void Upd7801::op_70() {
    const uint8_t op = fetch();
    auto imm16 = [this]() { return fetch16(); };
    auto store16 = [this](uint16_t addr, uint16_t value) {
        wr(addr, uint8_t(value));
        wr(uint16_t(addr + 1), uint8_t(value >> 8));
    };
    auto load16 = [this](uint16_t addr) {
        return uint16_t(rd(addr) | (rd(uint16_t(addr + 1)) << 8));
    };
    switch (op) {
        case 0x0E: store16(imm16(), sp_); break;
        case 0x0F: sp_ = load16(imm16()); break;
        case 0x1E: store16(imm16(), bc()); break;
        case 0x1F: set_bc(load16(imm16())); break;
        case 0x2E: store16(imm16(), de()); break;
        case 0x2F: set_de(load16(imm16())); break;
        case 0x3E: store16(imm16(), hl()); break;
        case 0x3F: set_hl(load16(imm16())); break;
        case 0x68: v_ = rd(imm16()); break;
        case 0x69: a_ = rd(imm16()); break;
        case 0x6A: b_ = rd(imm16()); break;
        case 0x6B: c_ = rd(imm16()); break;
        case 0x6C: d_ = rd(imm16()); break;
        case 0x6D: e_ = rd(imm16()); break;
        case 0x6E: h_ = rd(imm16()); break;
        case 0x6F: l_ = rd(imm16()); break;
        case 0x78: wr(imm16(), v_); break;
        case 0x79: wr(imm16(), a_); break;
        case 0x7A: wr(imm16(), b_); break;
        case 0x7B: wr(imm16(), c_); break;
        case 0x7C: wr(imm16(), d_); break;
        case 0x7D: wr(imm16(), e_); break;
        case 0x7E: wr(imm16(), h_); break;
        case 0x7F: wr(imm16(), l_); break;
        default: {
            auto ptr = [this, op]() -> uint16_t {
                switch (op & 7) {
                    case 1: return bc();
                    case 2: return de();
                    case 3: return hl();
                    case 4: {
                        const uint16_t a = de();
                        set_de(uint16_t(a + 1));
                        return a;
                    }
                    case 5: {
                        const uint16_t a = hl();
                        set_hl(uint16_t(a + 1));
                        return a;
                    }
                    case 6: {
                        const uint16_t a = de();
                        set_de(uint16_t(a - 1));
                        return a;
                    }
                    case 7: {
                        const uint16_t a = hl();
                        set_hl(uint16_t(a - 1));
                        return a;
                    }
                    default: return 0;
                }
            };
                if (op == 0x89 || op == 0x8A || op == 0x8B || op == 0x8D) {
                uint16_t addr = 0;
                if (op == 0x89) addr = bc();
                else if (op == 0x8A) addr = de();
                else if (op == 0x8B) addr = hl();
                else {
                    addr = hl();
                    set_hl(uint16_t(hl() + 1));
                }
                a_ = uint8_t(a_ & rd(addr));
                zf_ = a_ == 0;
            } else if (op == 0x91 || op == 0x92 || op == 0x93) {
                const uint16_t addr = (op == 0x91) ? bc() : (op == 0x92) ? de() : hl();
                a_ = uint8_t(a_ ^ rd(addr));
                zf_ = a_ == 0;
            } else if (op >= 0x99 && op <= 0x9F && op != 0x98) {
                const uint16_t addr = ptr();
                a_ = uint8_t(a_ | rd(addr));
                zf_ = a_ == 0;
            } else if (op == 0xA2 || op == 0xA3) {
                const uint8_t m = rd(op == 0xA2 ? de() : hl());
                const uint8_t before = a_;
                a_ = uint8_t(a_ + m);
                zhc_add(a_, before, false);
                if (!cy_) sk_ = true;
            } else if (op == 0xA9 || op == 0xAA || op == 0xAB || op == 0xAC) {
                uint16_t addr = 0;
                if (op == 0xA9) addr = bc();
                else if (op == 0xAA) addr = de();
                else if (op == 0xAB) addr = hl();
                else {
                    addr = de();
                    set_de(uint16_t(de() + 1));
                }
                const uint16_t after = uint16_t(a_ - rd(addr) - 1);
                zhc_sub(after, a_, false);
                if (!cy_) sk_ = true;
            } else if (op >= 0xB1 && op <= 0xB7) {
                const uint16_t addr = ptr();
                const uint8_t before = a_;
                a_ = uint8_t(a_ - rd(addr));
                zhc_sub(a_, before, false);
                if (!cy_) sk_ = true;
            } else if (op == 0xB9 || op == 0xBA || op == 0xBB || op == 0xBC) {
                uint16_t addr = 0;
                if (op == 0xB9) addr = bc();
                else if (op == 0xBA) addr = de();
                else if (op == 0xBB) addr = hl();
                else {
                    addr = de();
                    set_de(uint16_t(de() + 1));
                }
                const uint8_t after = uint8_t(a_ - rd(addr));
                zhc_sub(after, a_, false);
                if (cy_) sk_ = true;
            } else if (op >= 0xC1 && op <= 0xC7) {
                const uint16_t addr = ptr();
                const uint8_t before = a_;
                a_ = uint8_t(a_ + rd(addr));
                zhc_add(a_, before, false);
            } else if (op == 0xCA || op == 0xCB) {
                const uint8_t m = rd(op == 0xCA ? de() : hl());
                ona_a(m);
            } else if (op == 0xD1 || op == 0xD2 || op == 0xD3 || op == 0xD5 ||
                       op == 0xD6 || op == 0xD7) {
                uint16_t addr = 0;
                if (op == 0xD1) addr = bc();
                else if (op == 0xD2) addr = de();
                else if (op == 0xD3) addr = hl();
                else if (op == 0xD5) {
                    addr = hl();
                    set_hl(uint16_t(hl() + 1));
                } else if (op == 0xD6) {
                    addr = de();
                    set_de(uint16_t(de() - 1));
                } else {
                    addr = hl();
                    set_hl(uint16_t(hl() - 1));
                }
                const uint8_t before = a_;
                a_ = uint8_t(a_ + rd(addr) + uint8_t(cy_));
                zhc_add(a_, before, false);
            } else if (op == 0xD9 || op == 0xDA || op == 0xDB) {
                const uint16_t addr = (op == 0xD9) ? bc() : (op == 0xDA) ? de() : hl();
                offa_a(rd(addr));
            } else if (op >= 0xE1 && op <= 0xE7) {
                const uint16_t addr = ptr();
                const uint8_t before = a_;
                a_ = uint8_t(a_ - rd(addr));
                zhc_sub(a_, before, false);
            } else if (op >= 0xE9 && op <= 0xED) {
                uint16_t addr = 0;
                if (op == 0xE9) addr = bc();
                else if (op == 0xEA) addr = de();
                else if (op == 0xEB) addr = hl();
                else if (op == 0xEC) {
                    addr = de();
                    set_de(uint16_t(de() + 1));
                } else {
                    addr = hl();
                    set_hl(uint16_t(hl() + 1));
                }
                const uint8_t after = uint8_t(a_ - rd(addr));
                zhc_sub(after, a_, false);
                if (!zf_) sk_ = true;
            } else if (op == 0xF2 || op == 0xF3 || op == 0xF4 || op == 0xF7) {
                uint16_t addr = 0;
                if (op == 0xF2) addr = de();
                else if (op == 0xF3) addr = hl();
                else if (op == 0xF4) {
                    addr = de();
                    set_de(uint16_t(de() + 1));
                } else {
                    addr = hl();
                    set_hl(uint16_t(hl() - 1));
                }
                const uint8_t before = a_;
                const bool carry = cy_;
                a_ = uint8_t(a_ - rd(addr) - uint8_t(carry));
                zhc_sub(a_, before, carry);
            } else if (op >= 0xF9 && op <= 0xFF) {
                const uint16_t addr = ptr();
                const uint8_t after = uint8_t(a_ - rd(addr));
                zhc_sub(after, a_, false);
                if (zf_) sk_ = true;
            }
            break;
        }
    }
}

void Upd7801::op_74() {
    const uint8_t op = fetch();
    auto wa_mem = [this]() {
        return rd(uint16_t((v_ << 8) | fetch()));
    };
    switch (op) {
        case 0x88:
            a_ = uint8_t(a_ & wa_mem());
            zf_ = a_ == 0;
            break;
        case 0x90:
            a_ = uint8_t(a_ ^ wa_mem());
            zf_ = a_ == 0;
            break;
        case 0x98:
            a_ = uint8_t(a_ | wa_mem());
            zf_ = a_ == 0;
            break;
        case 0xA0: addnc_a(wa_mem()); break;
        case 0xA8: gta_a(wa_mem()); break;
        case 0xB0: subnb_a(wa_mem()); break;
        case 0xB8: lta_a(wa_mem()); break;
        case 0xC0: add_a(wa_mem()); break;
        case 0xC8: ona_a(wa_mem()); break;
        case 0xD0: adc_a(wa_mem()); break;
        case 0xD8: offa_a(wa_mem()); break;
        case 0xE0: sub_a(wa_mem()); break;
        case 0xE8: nea_a(wa_mem()); break;
        case 0xF8: eqa_a(wa_mem()); break;
        default:
            break;
    }
}

int Upd7801::execute_one() {
    ppc_ = pc_;
    const uint8_t op = fetch();
    int cycles = kOpCycles[op] ? kOpCycles[op] : 4;
    const bool keep_l0 = (op == 0x34 || op == 0x6F);
    const bool keep_l1 = (op == 0x69);

    if (sk_ && op != 0x72) {
        sk_ = false;
        skip_rest(op);
        if (op == 0x48 || op == 0x4C || op == 0x4D || op == 0x60 || op == 0x64 ||
            op == 0x70 || op == 0x74) {
            // skip_rest already consumed the prefix byte; cycles from main table are 0.
            cycles = 8;
        }
        if (!keep_l0) l0_ = false;
        if (!keep_l1) l1_ = false;
        return cycles;
    }
    sk_ = false;

    auto wa = [this]() { return uint16_t((v_ << 8) | fetch()); };
    auto push_pc = [this]() {
        wr(--sp_, uint8_t(pc_ >> 8));
        wr(--sp_, uint8_t(pc_));
    };

    switch (op) {
        case 0x00: break;
        case 0x01:
            pc_--;
            cycles = 6;
            break;
        case 0x02: sp_++; break;
        case 0x04: sp_ = fetch16(); break;
        case 0x05: {
            const uint16_t addr = wa();
            const uint8_t v = uint8_t(rd(addr) & fetch());
            wr(addr, v);
            zf_ = v == 0;
            break;
        }
        case 0x07: ani(a_); break;
        case 0x08:
            pc_ = uint16_t(rd(sp_) | (rd(uint16_t(sp_ + 1)) << 8));
            sp_ = uint16_t(sp_ + 2);
            break;
        case 0x0A: a_ = b_; break;
        case 0x0B: a_ = c_; break;
        case 0x0C: a_ = d_; break;
        case 0x0D: a_ = e_; break;
        case 0x0E: a_ = h_; break;
        case 0x0F: a_ = l_; break;
        case 0x10:
            std::swap(ea_, ea2_);
            std::swap(v_, v2_);
            std::swap(a_, a2_);
            break;
        case 0x11:
            std::swap(b_, b2_);
            std::swap(c_, c2_);
            std::swap(d_, d2_);
            std::swap(e_, e2_);
            std::swap(h_, h2_);
            std::swap(l_, l2_);
            break;
        case 0x12: set_bc(uint16_t(bc() + 1)); break;
        case 0x13: set_bc(uint16_t(bc() - 1)); break;
        case 0x14: set_bc(fetch16()); break;
        case 0x15: {
            const uint16_t addr = wa();
            const uint8_t v = uint8_t(rd(addr) | fetch());
            wr(addr, v);
            zf_ = v == 0;
            break;
        }
        case 0x16: xri(a_); break;
        case 0x17: ori(a_); break;
        case 0x18:
            pc_ = uint16_t(rd(sp_) | (rd(uint16_t(sp_ + 1)) << 8));
            sp_ = uint16_t(sp_ + 2);
            sk_ = true;
            break;
        case 0x19:
            to_ = 1;
            ovc0_ = 8 * int(tm_ & 0x0FFF);
            break;
        case 0x1A: b_ = a_; break;
        case 0x1B: c_ = a_; break;
        case 0x1C: d_ = a_; break;
        case 0x1D: e_ = a_; break;
        case 0x1E: h_ = a_; break;
        case 0x1F: l_ = a_; break;
        case 0x20: {
            const bool saved = cy_;
            const uint16_t addr = wa();
            const uint8_t before = rd(addr);
            const uint8_t after = uint8_t(before + 1);
            zhc_add(after, before, false);
            wr(addr, after);
            if (cy_) sk_ = true;
            cy_ = saved;
            break;
        }
        case 0x21: {
            const uint16_t addr = uint16_t(pc_ + a_ + 1);
            c_ = rd(addr);
            b_ = rd(uint16_t(addr + 1));
            break;
        }
        case 0x22: set_de(uint16_t(de() + 1)); break;
        case 0x23: set_de(uint16_t(de() - 1)); break;
        case 0x24: set_de(fetch16()); break;
        case 0x25: {
            const uint16_t addr = wa();
            const uint8_t l = rd(addr);
            const uint16_t after = uint16_t(l - fetch() - 1);
            zhc_sub(after, l, false);
            if (!cy_) sk_ = true;
            break;
        }
        case 0x26: adinc(a_); break;
        case 0x27: gti(a_); break;
        case 0x28: a_ = rd(wa()); break;
        case 0x29: a_ = rd(bc()); break;
        case 0x2A: a_ = rd(de()); break;
        case 0x2B: a_ = rd(hl()); break;
        case 0x2C:
            a_ = rd(de());
            set_de(uint16_t(de() + 1));
            break;
        case 0x2D:
            a_ = rd(hl());
            set_hl(uint16_t(hl() + 1));
            break;
        case 0x2E:
            a_ = rd(de());
            set_de(uint16_t(de() - 1));
            break;
        case 0x2F:
            a_ = rd(hl());
            set_hl(uint16_t(hl() - 1));
            break;
        case 0x30: {
            const bool saved = cy_;
            const uint16_t addr = wa();
            const uint8_t before = rd(addr);
            const uint8_t after = uint8_t(before - 1);
            zhc_sub(after, before, false);
            wr(addr, after);
            if (cy_) sk_ = true;
            cy_ = saved;
            break;
        }
        case 0x31:
            wr(de(), rd(hl()));
            set_de(uint16_t(de() + 1));
            set_hl(uint16_t(hl() + 1));
            c_ = uint8_t(c_ - 1);
            if (c_ == 0xFF) cy_ = true;
            else {
                cy_ = false;
                pc_--;
            }
            break;
        case 0x32: set_hl(uint16_t(hl() + 1)); break;
        case 0x33: set_hl(uint16_t(hl() - 1)); break;
        case 0x34:
            if (l0_) pc_ = uint16_t(pc_ + 2);
            else {
                set_hl(fetch16());
                l0_ = true;
            }
            break;
        case 0x35: {
            const uint16_t addr = wa();
            const uint8_t l = rd(addr);
            const uint8_t after = uint8_t(l - fetch());
            zhc_sub(after, l, false);
            if (cy_) sk_ = true;
            break;
        }
        case 0x36:
            sui(a_);
            if (!cy_) sk_ = true;
            break;
        case 0x37: lti(a_); break;
        case 0x38: wr(wa(), a_); break;
        case 0x39: wr(bc(), a_); break;
        case 0x3A: wr(de(), a_); break;
        case 0x3B: wr(hl(), a_); break;
        case 0x3C:
            wr(de(), a_);
            set_de(uint16_t(de() + 1));
            break;
        case 0x3D:
            wr(hl(), a_);
            set_hl(uint16_t(hl() + 1));
            break;
        case 0x3E:
            wr(de(), a_);
            set_de(uint16_t(de() - 1));
            break;
        case 0x3F:
            wr(hl(), a_);
            set_hl(uint16_t(hl() - 1));
            break;
        case 0x41: {
            const bool saved = cy_;
            const uint8_t before = a_;
            a_ = uint8_t(a_ + 1);
            zhc_add(a_, before, false);
            if (cy_) sk_ = true;
            cy_ = saved;
            break;
        }
        case 0x42: {
            const bool saved = cy_;
            const uint8_t before = b_;
            b_ = uint8_t(b_ + 1);
            zhc_add(b_, before, false);
            if (cy_) sk_ = true;
            cy_ = saved;
            break;
        }
        case 0x43: {
            const bool saved = cy_;
            const uint8_t before = c_;
            c_ = uint8_t(c_ + 1);
            zhc_add(c_, before, false);
            if (cy_) sk_ = true;
            cy_ = saved;
            break;
        }
        case 0x44: {
            const uint16_t target = fetch16();
            push_pc();
            pc_ = target;
            break;
        }
        case 0x45: {
            const uint16_t addr = wa();
            const uint8_t mask = fetch();
            sk_ = (rd(addr) & mask) != 0;
            break;
        }
        case 0x46: adi(a_); break;
        case 0x47: oni(a_); break;
        case 0x48:
            op_48();
            cycles = prefix_cycles(0x48, rd(uint16_t(ppc_ + 1)));
            break;
        case 0x49: wr(bc(), fetch()); break;
        case 0x4A: wr(de(), fetch()); break;
        case 0x4B: wr(hl(), fetch()); break;
        case 0x4C:
            op_4c();
            cycles = prefix_cycles(0x4C, rd(uint16_t(ppc_ + 1)));
            break;
        case 0x4D:
            op_4d();
            cycles = prefix_cycles(0x4D, rd(uint16_t(ppc_ + 1)));
            break;
        case 0x4E: {
            const uint8_t d = fetch();
            pc_ = uint16_t(pc_ + d);
            break;
        }
        case 0x4F: {
            const uint8_t d = fetch();
            pc_ = uint16_t(pc_ - (256 - d));
            break;
        }
        case 0x51: {
            const bool saved = cy_;
            const uint8_t before = a_;
            a_ = uint8_t(a_ - 1);
            zhc_sub(a_, before, false);
            if (cy_) sk_ = true;
            cy_ = saved;
            break;
        }
        case 0x52: {
            const bool saved = cy_;
            const uint8_t before = b_;
            b_ = uint8_t(b_ - 1);
            zhc_sub(b_, before, false);
            if (cy_) sk_ = true;
            cy_ = saved;
            break;
        }
        case 0x53: {
            const bool saved = cy_;
            const uint8_t before = c_;
            c_ = uint8_t(c_ - 1);
            zhc_sub(c_, before, false);
            if (cy_) sk_ = true;
            cy_ = saved;
            break;
        }
        case 0x54: pc_ = fetch16(); break;
        case 0x55: {
            const uint16_t addr = wa();
            const uint8_t mask = fetch();
            sk_ = (rd(addr) & mask) == 0;
            break;
        }
        case 0x56: aci(a_); break;
        case 0x57: offi(a_); break;
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
            const uint16_t addr = wa();
            sk_ = (rd(addr) & (1u << (op & 7))) != 0;
            break;
        }
        case 0x60:
            op_60();
            cycles = 8;
            break;
        case 0x61: {
            const uint8_t l = a_ & 0x0F;
            const uint8_t h = uint8_t(a_ >> 4);
            uint8_t adj = 0;
            const bool saved = cy_;
            if (!hc_) {
                if (l < 10) {
                    if (!((h < 10) && !cy_)) adj = 0x60;
                } else {
                    adj = ((h < 9) && !cy_) ? 0x06 : 0x66;
                }
            } else if (l < 3) {
                adj = ((h < 10) && !cy_) ? 0x06 : 0x66;
            }
            const uint8_t before = a_;
            a_ = uint8_t(a_ + adj);
            zhc_add(a_, before, cy_);
            cy_ = cy_ || saved;
            break;
        }
        case 0x62:
            pc_ = uint16_t(rd(sp_) | (rd(uint16_t(sp_ + 1)) << 8));
            set_psw(rd(uint16_t(sp_ + 2)));
            sp_ = uint16_t(sp_ + 3);
            break;
        case 0x63:
            push_pc();
            pc_ = bc();
            break;
        case 0x64:
            op_64();
            cycles = prefix_cycles(0x64, rd(uint16_t(ppc_ + 1)));
            break;
        case 0x65: {
            const uint16_t addr = wa();
            const uint8_t l = rd(addr);
            const uint8_t after = uint8_t(l - fetch());
            zhc_sub(after, l, false);
            if (!zf_) sk_ = true;
            break;
        }
        case 0x66: sui(a_); break;
        case 0x67: nei(a_); break;
        case 0x68: v_ = fetch(); break;
        case 0x69:
            if (l1_) pc_++;
            else {
                a_ = fetch();
                l1_ = true;
            }
            break;
        case 0x6A: b_ = fetch(); break;
        case 0x6B: c_ = fetch(); break;
        case 0x6C: d_ = fetch(); break;
        case 0x6D: e_ = fetch(); break;
        case 0x6E: h_ = fetch(); break;
        case 0x6F:
            if (l0_) pc_++;
            else {
                l_ = fetch();
                l0_ = true;
            }
            break;
        case 0x70:
            op_70();
            cycles = prefix_cycles(0x70, rd(uint16_t(ppc_ + 1)));
            break;
        case 0x71: {
            const uint16_t addr = wa();
            wr(addr, fetch());
            break;
        }
        case 0x73: pc_ = bc(); break;
        case 0x74:
            op_74();
            cycles = prefix_cycles(0x74, rd(uint16_t(ppc_ + 1)));
            break;
        case 0x75: {
            const uint16_t addr = wa();
            const uint8_t l = rd(addr);
            const uint8_t after = uint8_t(l - fetch());
            zhc_sub(after, l, false);
            if (zf_) sk_ = true;
            break;
        }
        case 0x76: sbi(a_); break;
        case 0x77: eqi(a_); break;
        default:
            if (op >= 0x78 && op <= 0x7F) {
                const uint16_t target = uint16_t(((0x08 + (op & 7)) << 8) | fetch());
                push_pc();
                pc_ = target;
            } else if (op >= 0x80 && op <= 0xBF) {
                const uint16_t vector = uint16_t(0x80 + 2 * (op & 0x3F));
                push_pc();
                pc_ = uint16_t(rd(vector) | (rd(uint16_t(vector + 1)) << 8));
            } else if (op >= 0xC0) {
                pc_ = uint16_t(pc_ + (int8_t(op << 2) >> 2));
            }
            break;
    }

    if (!keep_l0) l0_ = false;
    if (!keep_l1) l1_ = false;
    return cycles;
}

int Upd7801::run(int cycles) {
    int done = 0;
    while (done < cycles) {
        const int remaining = cycles - done;
        const int c = execute_one();
        const int used = (c > 0) ? c : 4;
        handle_timers(used);
        take_irq();
        iff_ = iff_pending_;
        done += used;
        if (cycle_handler_) cycle_handler_(used);
        if (pc_ == ppc_ && used == 6) {
            const int rest = remaining - used;
            if (rest > 0) {
                handle_timers(rest);
                take_irq();
                iff_ = iff_pending_;
                done += rest;
                if (cycle_handler_) cycle_handler_(rest);
            }
        }
    }
    return done;
}

}  // namespace dsp
