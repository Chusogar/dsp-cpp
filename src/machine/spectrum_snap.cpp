#include "machine/spectrum_snap.h"

#include <cstring>

namespace dsp {
namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }

void apply_regs_from_z80_header(SpectrumSnap& out, const uint8_t* h) {
    out.a = h[0];
    out.f = h[1];
    out.c = h[2];
    out.b = h[3];
    out.l = h[4];
    out.h = h[5];
    // PC at 6-7 for v1; ignored for v2/v3 (zero)
    out.sp = rd16(h + 8);
    out.i = h[10];
    out.r = uint8_t((h[11] & 0x7f) | ((h[12] & 1) << 7));
    uint8_t flags = h[12];
    if (flags == 0xff) flags = 1;
    out.border = uint8_t((flags >> 1) & 7);
    out.e = h[13];
    out.d = h[14];
    out.c2 = h[15];
    out.b2 = h[16];
    out.e2 = h[17];
    out.d2 = h[18];
    out.l2 = h[19];
    out.h2 = h[20];
    out.a2 = h[21];
    out.f2 = h[22];
    out.iy = rd16(h + 23);
    out.ix = rd16(h + 25);
    out.iff1 = h[27] != 0;
    out.iff2 = h[28] != 0;
    out.im = uint8_t(h[29] & 3);
}

// Classic .Z80 RLE: ED ED xx yy => yy repeated xx times. Optional end marker 00 ED ED 00.
bool decompress_z80_block(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len,
                          bool expect_end_marker, std::string* error) {
    size_t si = 0, di = 0;
    while (si < src_len && di < dst_len) {
        if (expect_end_marker && si + 4 <= src_len && src[si] == 0x00 && src[si + 1] == 0xed &&
            src[si + 2] == 0xed && src[si + 3] == 0x00) {
            si += 4;
            break;
        }
        if (si + 4 <= src_len && src[si] == 0xed && src[si + 1] == 0xed) {
            const uint8_t count = src[si + 2];
            const uint8_t value = src[si + 3];
            si += 4;
            for (uint8_t n = 0; n < count && di < dst_len; ++n) dst[di++] = value;
            continue;
        }
        dst[di++] = src[si++];
    }
    if (di != dst_len) {
        if (error) *error = "Z80 block decompressed to wrong size";
        return false;
    }
    return true;
}

void map_48k_page(SpectrumSnap& out, uint8_t page, const uint8_t* block) {
    uint16_t base = 0;
    switch (page) {
        case 8: base = 0x4000; break;
        case 4: base = 0x8000; break;
        case 5: base = 0xc000; break;
        default: return;
    }
    std::memcpy(out.ram48.data() + (base - 0x4000), block, 0x4000);
    // Also mirror into 128K bank layout used by 128K drivers when loading a 48K snap.
    const int bank = (base == 0x4000) ? 5 : (base == 0x8000) ? 2 : 0;
    std::memcpy(out.banks[size_t(bank)].data(), block, 0x4000);
}

}  // namespace

bool spectrum_snap_from_sna(const uint8_t* data, size_t size, SpectrumSnap& out, std::string* error) {
    // Classic 48K SNA is exactly 27 + 49152 = 49179 bytes.
    // 128K SNA is 49179 + 4 (PC, 7FFD, TR-DOS) + remaining banks (typically 131103).
    constexpr size_t kSna48 = 27 + 0xc000;       // 49179
    constexpr size_t kSna128Min = kSna48 + 4;    // need PC/7FFD/TR-DOS tail
    if (!data || size < kSna48) {
        if (error) *error = "SNA too small";
        return false;
    }
    out = SpectrumSnap{};
    const uint8_t* h = data;
    out.i = h[0];
    out.l2 = h[1];
    out.h2 = h[2];
    out.e2 = h[3];
    out.d2 = h[4];
    out.c2 = h[5];
    out.b2 = h[6];
    out.f2 = h[7];
    out.a2 = h[8];
    out.l = h[9];
    out.h = h[10];
    out.e = h[11];
    out.d = h[12];
    out.c = h[13];
    out.b = h[14];
    out.iy = rd16(h + 15);
    out.ix = rd16(h + 17);
    // Byte 19 bit 2 = IFF2 (EI/DI). RETN semantics: IFF1 ← IFF2.
    out.iff2 = (h[19] & 4) != 0;
    out.iff1 = out.iff2;
    out.r = h[20];
    out.f = h[21];
    out.a = h[22];
    out.sp = rd16(h + 23);
    out.im = uint8_t(h[25] & 3);
    out.border = uint8_t(h[26] & 7);

    if (size >= kSna128Min) {
        // 128K: banks 5, 2, then currently paged bank (7FFD bits 0..2), then
        // remaining banks in ascending order (skipping those already stored).
        // The paged bank is included even when it is 5 or 2 (duplicated).
        out.is_128 = true;
        const uint8_t* tail = data + kSna48;
        out.pc = rd16(tail);
        out.port_7ffd = tail[2];
        out.trdos_paged = tail[3] != 0;
        const int paged = int(out.port_7ffd & 7);

        std::memcpy(out.banks[5].data(), data + 27, 0x4000);
        std::memcpy(out.banks[2].data(), data + 27 + 0x4000, 0x4000);
        std::memcpy(out.banks[size_t(paged)].data(), data + 27 + 0x8000, 0x4000);

        size_t off = kSna128Min;
        for (int b = 0; b < 8; ++b) {
            if (b == 5 || b == 2 || b == paged) continue;
            if (off + 0x4000 > size) break;
            std::memcpy(out.banks[size_t(b)].data(), data + off, 0x4000);
            off += 0x4000;
        }
        // ram48 view = what a 48K machine would see (5 / 2 / paged-at-C000).
        std::memcpy(out.ram48.data(), out.banks[5].data(), 0x4000);
        std::memcpy(out.ram48.data() + 0x4000, out.banks[2].data(), 0x4000);
        std::memcpy(out.ram48.data() + 0x8000, out.banks[size_t(paged)].data(), 0x4000);
    } else {
        out.is_128 = false;
        std::memcpy(out.ram48.data(), data + 27, 0xc000);
        std::memcpy(out.banks[5].data(), out.ram48.data(), 0x4000);
        std::memcpy(out.banks[2].data(), out.ram48.data() + 0x4000, 0x4000);
        std::memcpy(out.banks[0].data(), out.ram48.data() + 0x8000, 0x4000);
        // PC is on the stack at SP (Mirage Microdriver RETN).
        const uint16_t sp = out.sp;
        if (sp < 0x4000 || sp + 1 > 0xffff) {
            if (error) *error = "SNA SP out of range";
            return false;
        }
        out.pc = rd16(out.ram48.data() + (sp - 0x4000));
        out.sp = uint16_t(sp + 2);
    }
    return true;
}

bool spectrum_snap_from_z80(const uint8_t* data, size_t size, SpectrumSnap& out, std::string* error) {
    if (!data || size < 30) {
        if (error) *error = "Z80 snapshot too small";
        return false;
    }
    out = SpectrumSnap{};
    apply_regs_from_z80_header(out, data);
    const uint16_t pc_v1 = rd16(data + 6);
    const bool compressed_v1 = (data[12] != 0xff) && ((data[12] & 0x20) != 0);

    if (pc_v1 != 0) {
        // Version 1
        out.pc = pc_v1;
        out.is_128 = false;
        if (size < 30) {
            if (error) *error = "Z80 v1 truncated";
            return false;
        }
        const uint8_t* payload = data + 30;
        const size_t plen = size - 30;
        if (!compressed_v1) {
            if (plen < 0xc000) {
                if (error) *error = "Z80 v1 uncompressed RAM short";
                return false;
            }
            std::memcpy(out.ram48.data(), payload, 0xc000);
        } else {
            if (!decompress_z80_block(payload, plen, out.ram48.data(), 0xc000, true, error)) return false;
        }
        std::memcpy(out.banks[5].data(), out.ram48.data(), 0x4000);
        std::memcpy(out.banks[2].data(), out.ram48.data() + 0x4000, 0x4000);
        std::memcpy(out.banks[0].data(), out.ram48.data() + 0x8000, 0x4000);
        return true;
    }

    // Version 2 / 3
    if (size < 32) {
        if (error) *error = "Z80 v2/v3 truncated header";
        return false;
    }
    const size_t ext_len = rd16(data + 30);
    if (size < 32 + ext_len) {
        if (error) *error = "Z80 extended header truncated";
        return false;
    }
    const uint8_t* ext = data + 32;
    out.pc = rd16(ext);
    const uint8_t hw = ext[2];
    // v2: 0/1=48K, 3/4=128K. v3: 0/1=48K, 4/5=128K, 3=48K+MGT.
    const bool v3 = (ext_len >= 54);
    if (v3) {
        out.is_128 = (hw == 4 || hw == 5 || hw == 6);
    } else {
        out.is_128 = (hw == 3 || hw == 4);
    }
    if (ext_len >= 6) out.port_7ffd = ext[3];
    if (ext_len >= 8) {
        out.ay_used = (ext[5] & 0x04) != 0;
    }
    if (ext_len >= 9) out.ay_latch = ext[6];
    if (ext_len >= 25) std::memcpy(out.ay_regs.data(), ext + 7, 16);
    if (ext_len >= 55) out.port_1ffd = ext[54];

    size_t off = 32 + ext_len;
    while (off + 3 <= size) {
        const uint16_t blk_len = rd16(data + off);
        const uint8_t page = data[off + 2];
        off += 3;
        std::array<uint8_t, 0x4000> block{};
        if (blk_len == 0xffff) {
            if (off + 0x4000 > size) {
                if (error) *error = "Z80 uncompressed page truncated";
                return false;
            }
            std::memcpy(block.data(), data + off, 0x4000);
            off += 0x4000;
        } else {
            if (off + blk_len > size) {
                if (error) *error = "Z80 compressed page truncated";
                return false;
            }
            if (!decompress_z80_block(data + off, blk_len, block.data(), 0x4000, false, error)) return false;
            off += blk_len;
        }

        if (out.is_128) {
            // Pages 3..10 => banks 0..7
            if (page >= 3 && page <= 10) {
                std::memcpy(out.banks[size_t(page - 3)].data(), block.data(), 0x4000);
            }
        } else {
            map_48k_page(out, page, block.data());
        }
    }

    if (!out.is_128) {
        // Ensure banks mirror for 128K hosts loading a 48K snap.
        std::memcpy(out.banks[5].data(), out.ram48.data(), 0x4000);
        std::memcpy(out.banks[2].data(), out.ram48.data() + 0x4000, 0x4000);
        std::memcpy(out.banks[0].data(), out.ram48.data() + 0x8000, 0x4000);
    } else {
        std::memcpy(out.ram48.data(), out.banks[5].data(), 0x4000);
        std::memcpy(out.ram48.data() + 0x4000, out.banks[2].data(), 0x4000);
        std::memcpy(out.ram48.data() + 0x8000, out.banks[0].data(), 0x4000);
    }
    return true;
}

bool spectrum_snap_from_bytes(const uint8_t* data, size_t size, const char* ext_hint, SpectrumSnap& out,
                              std::string* error) {
    std::string ext;
    if (ext_hint) {
        for (const char* p = ext_hint; *p; ++p) {
            char ch = *p;
            if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
            if (ch == 0) break;
            ext.push_back(ch);
        }
    }
    if (ext.find("sna") != std::string::npos) return spectrum_snap_from_sna(data, size, out, error);
    if (ext.find("z80") != std::string::npos) return spectrum_snap_from_z80(data, size, out, error);
    // Autodetect classic SNA sizes (48K = 49179, 128K = 131103 / 147487).
    if (size == 49179 || size == 131103 || size == 147487) {
        if (spectrum_snap_from_sna(data, size, out, error)) return true;
    }
    return spectrum_snap_from_z80(data, size, out, error);
}

}  // namespace dsp
