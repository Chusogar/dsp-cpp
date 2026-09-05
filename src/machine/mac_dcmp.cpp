#include "machine/mac_dcmp.h"

#include <cstring>

namespace dsp {
namespace {

constexpr uint8_t kDcmp0Table[] = {
    0x00, 0x00, 0x4e, 0xba, 0x00, 0x08, 0x4e, 0x75, 0x00, 0x0c, 0x4e, 0xad, 0x20, 0x53, 0x2f, 0x0b,
    0x61, 0x00, 0x00, 0x10, 0x70, 0x00, 0x2f, 0x00, 0x48, 0x6e, 0x20, 0x50, 0x20, 0x6e, 0x2f, 0x2e,
    0xff, 0xfc, 0x48, 0xe7, 0x3f, 0x3c, 0x00, 0x04, 0xff, 0xf8, 0x2f, 0x0c, 0x20, 0x06, 0x4e, 0xed,
    0x4e, 0x56, 0x20, 0x68, 0x4e, 0x5e, 0x00, 0x01, 0x58, 0x8f, 0x4f, 0xef, 0x00, 0x02, 0x00, 0x18,
    0x60, 0x00, 0xff, 0xff, 0x50, 0x8f, 0x4e, 0x90, 0x00, 0x06, 0x26, 0x6e, 0x00, 0x14, 0xff, 0xf4,
    0x4c, 0xee, 0x00, 0x0a, 0x00, 0x0e, 0x41, 0xee, 0x4c, 0xdf, 0x48, 0xc0, 0xff, 0xf0, 0x2d, 0x40,
    0x00, 0x12, 0x30, 0x2e, 0x70, 0x01, 0x2f, 0x28, 0x20, 0x54, 0x67, 0x00, 0x00, 0x20, 0x00, 0x1c,
    0x20, 0x5f, 0x18, 0x00, 0x26, 0x6f, 0x48, 0x78, 0x00, 0x16, 0x41, 0xfa, 0x30, 0x3c, 0x28, 0x40,
    0x72, 0x00, 0x28, 0x6e, 0x20, 0x0c, 0x66, 0x00, 0x20, 0x6b, 0x2f, 0x07, 0x55, 0x8f, 0x00, 0x28,
    0xff, 0xfe, 0xff, 0xec, 0x22, 0xd8, 0x20, 0x0b, 0x00, 0x0f, 0x59, 0x8f, 0x2f, 0x3c, 0xff, 0x00,
    0x01, 0x18, 0x81, 0xe1, 0x4a, 0x00, 0x4e, 0xb0, 0xff, 0xe8, 0x48, 0xc7, 0x00, 0x03, 0x00, 0x22,
    0x00, 0x07, 0x00, 0x1a, 0x67, 0x06, 0x67, 0x08, 0x4e, 0xf9, 0x00, 0x24, 0x20, 0x78, 0x08, 0x00,
    0x66, 0x04, 0x00, 0x2a, 0x4e, 0xd0, 0x30, 0x28, 0x26, 0x5f, 0x67, 0x04, 0x00, 0x30, 0x43, 0xee,
    0x3f, 0x00, 0x20, 0x1f, 0x00, 0x1e, 0xff, 0xf6, 0x20, 0x2e, 0x42, 0xa7, 0x20, 0x07, 0xff, 0xfa,
    0x60, 0x02, 0x3d, 0x40, 0x0c, 0x40, 0x66, 0x06, 0x00, 0x26, 0x2d, 0x48, 0x2f, 0x01, 0x70, 0xff,
    0x60, 0x04, 0x18, 0x80, 0x4a, 0x40, 0x00, 0x40, 0x00, 0x2c, 0x2f, 0x08, 0x00, 0x11, 0xff, 0xe4,
    0x21, 0x40, 0x26, 0x40, 0xff, 0xf2, 0x42, 0x6e, 0x4e, 0xb9, 0x3d, 0x7c, 0x00, 0x38, 0x00, 0x0d,
    0x60, 0x06, 0x42, 0x2e, 0x20, 0x3c, 0x67, 0x0c, 0x2d, 0x68, 0x66, 0x08, 0x4a, 0x2e, 0x4a, 0xae,
    0x00, 0x2e, 0x48, 0x40, 0x22, 0x5f, 0x22, 0x00, 0x67, 0x0a, 0x30, 0x07, 0x42, 0x67, 0x00, 0x32,
    0x20, 0x28, 0x00, 0x09, 0x48, 0x7a, 0x02, 0x00, 0x2f, 0x2b, 0x00, 0x05, 0x22, 0x6e, 0x66, 0x02,
    0xe5, 0x80, 0x67, 0x0e, 0x66, 0x0a, 0x00, 0x50, 0x3e, 0x00, 0x66, 0x0c, 0x2e, 0x00, 0xff, 0xee,
    0x20, 0x6d, 0x20, 0x40, 0xff, 0xe0, 0x53, 0x40, 0x60, 0x08, 0x04, 0x80, 0x00, 0x68, 0x0b, 0x7c,
    0x44, 0x00, 0x41, 0xe8, 0x48, 0x41,
};

class Cursor {
public:
    Cursor(const uint8_t* p, size_t n) : p_(p), n_(n), i_(0) {}
    bool get(uint8_t& b) {
        if (i_ >= n_) return false;
        b = p_[i_++];
        return true;
    }
    bool read(uint8_t* dest, size_t n) {
        if (i_ + n > n_) return false;
        std::memcpy(dest, p_ + i_, n);
        i_ += n;
        return true;
    }
    bool varint(int32_t& v) {
        uint8_t head = 0;
        if (!get(head)) return false;
        if (head == 0xff) {
            uint8_t b[4];
            if (!read(b, 4)) return false;
            v = int32_t((uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) |
                        b[3]);
            return true;
        }
        if (head >= 0x80) {
            uint8_t lo = 0;
            if (!get(lo)) return false;
            const uint16_t w = uint16_t((uint16_t((head - 0xc0) & 0xff) << 8) | lo);
            v = int16_t(w);
            return true;
        }
        v = int8_t(head);
        return true;
    }

private:
    const uint8_t* p_;
    size_t n_;
    size_t i_;
};

bool dcmp0(Cursor& in, std::vector<uint8_t>& out, uint32_t expect) {
    std::vector<std::vector<uint8_t>> prev;
    auto add = [&](const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); };
    auto add_vec = [&](const std::vector<uint8_t>& v) { add(v.data(), v.size()); };

    for (;;) {
        uint8_t byte = 0;
        if (!in.get(byte)) return false;
        if (byte < 0x20) {
            uint8_t count_div2 = byte & 0x0f;
            if (byte == 0x00 || byte == 0x10) {
                if (!in.get(count_div2)) return false;
            }
            const size_t n = size_t(count_div2) * 2;
            std::vector<uint8_t> lit(n);
            if (n && !in.read(lit.data(), n)) return false;
            if (byte >= 0x10) prev.push_back(lit);
            add_vec(lit);
        } else if (byte == 0x20 || byte == 0x21) {
            uint8_t next = 0;
            if (!in.get(next)) return false;
            const size_t idx = 0x28 + (size_t(byte - 0x20) << 8 | next);
            if (idx >= prev.size()) return false;
            add_vec(prev[idx]);
        } else if (byte == 0x22) {
            uint8_t b[2];
            if (!in.read(b, 2)) return false;
            const size_t idx = 0x28 + ((size_t(b[0]) << 8) | b[1]);
            if (idx >= prev.size()) return false;
            add_vec(prev[idx]);
        } else if (byte >= 0x23 && byte <= 0x4a) {
            const size_t idx = size_t(byte - 0x23);
            if (idx >= prev.size()) return false;
            add_vec(prev[idx]);
        } else if (byte >= 0x4b && byte <= 0xfd) {
            add(kDcmp0Table + size_t(byte - 0x4b) * 2, 2);
        } else if (byte == 0xfe) {
            uint8_t kind = 0;
            if (!in.get(kind)) return false;
            if (kind == 0x00) {
                int32_t seg = 0, count = 0, cur = 0;
                if (!in.varint(seg) || !in.varint(count) || !in.varint(cur)) return false;
                if (count <= 0) return false;
                uint8_t tail[6] = {0x3f, 0x3c, uint8_t(seg >> 8), uint8_t(seg), 0xa9, 0xf0};
                add(tail, 6);
                uint8_t entry[8] = {uint8_t(cur >> 8), uint8_t(cur), 0x3f, 0x3c, uint8_t(seg >> 8),
                                    uint8_t(seg), 0xa9, 0xf0};
                add(entry, 8);
                for (int32_t n = 1; n < count; n++) {
                    int32_t diff = 0;
                    if (!in.varint(diff)) return false;
                    cur = (cur + (diff - 6)) & 0xffff;
                    entry[0] = uint8_t(cur >> 8);
                    entry[1] = uint8_t(cur);
                    add(entry, 8);
                }
            } else if (kind == 0x02 || kind == 0x03) {
                const int n = kind == 0x02 ? 1 : 2;
                int32_t val = 0, count = 0;
                if (!in.varint(val) || !in.varint(count)) return false;
                count += 1;
                if (count <= 0) return false;
                uint8_t bytes[2] = {uint8_t(n == 2 ? (val >> 8) : val), uint8_t(val)};
                const uint8_t* p = n == 2 ? bytes : bytes + 1;
                for (int32_t i = 0; i < count; i++) add(p, size_t(n));
            } else if (kind == 0x04) {
                int32_t initial = 0, count = 0;
                if (!in.varint(initial) || !in.varint(count)) return false;
                if (count < 0) return false;
                out.push_back(uint8_t(initial >> 8));
                out.push_back(uint8_t(initial));
                uint32_t cur = uint32_t(initial) & 0xffffu;
                for (int32_t i = 0; i < count; i++) {
                    uint8_t d = 0;
                    if (!in.get(d)) return false;
                    cur = (cur + uint32_t(int8_t(d))) & 0xffffu;
                    out.push_back(uint8_t(cur >> 8));
                    out.push_back(uint8_t(cur));
                }
            } else if (kind == 0x06) {
                int32_t initial = 0, count = 0;
                if (!in.varint(initial) || !in.varint(count)) return false;
                out.push_back(uint8_t(initial >> 24));
                out.push_back(uint8_t(initial >> 16));
                out.push_back(uint8_t(initial >> 8));
                out.push_back(uint8_t(initial));
                uint32_t cur = uint32_t(initial);
                for (int32_t i = 0; i < count; i++) {
                    int32_t diff = 0;
                    if (!in.varint(diff)) return false;
                    cur += uint32_t(diff);
                    out.push_back(uint8_t(cur >> 24));
                    out.push_back(uint8_t(cur >> 16));
                    out.push_back(uint8_t(cur >> 8));
                    out.push_back(uint8_t(cur));
                }
            } else {
                return false;
            }
        } else if (byte == 0xff) {
            break;
        } else {
            return false;
        }
    }
    if ((expect & 1u) && out.size() == expect + 1) out.pop_back();
    (void)expect;
    return true;
}

}  // namespace

std::vector<uint8_t> mac_decompress_resource(const uint8_t* data, size_t size) {
    std::vector<uint8_t> empty;
    if (!data || size < 18) return empty;
    if (data[0] != 0xa8 || data[1] != 0x9f || data[2] != 0x65 || data[3] != 0x72) return empty;
    const uint16_t hdr_len = uint16_t((uint16_t(data[4]) << 8) | data[5]);
    const uint16_t ctype = uint16_t((uint16_t(data[6]) << 8) | data[7]);
    const uint32_t expect =
        (uint32_t(data[8]) << 24) | (uint32_t(data[9]) << 16) | (uint32_t(data[10]) << 8) | data[11];
    if (hdr_len != 0x12 || ctype != 0x0801) return empty;
    const int16_t dcmp_id = int16_t((uint16_t(data[14]) << 8) | data[15]);
    if (dcmp_id != 0) return empty;
    Cursor in(data + 18, size - 18);
    std::vector<uint8_t> out;
    out.reserve(expect);
    if (!dcmp0(in, out, expect)) return empty;
    if (out.size() > expect) out.resize(expect);
    if (out.size() != expect && !((expect & 1u) && out.size() + 1 == expect)) return empty;
    return out;
}

}  // namespace dsp
