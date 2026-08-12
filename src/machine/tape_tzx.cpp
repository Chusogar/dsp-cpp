#include "machine/tape_tzx.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <zlib.h>

namespace dsp {
namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd24(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16)); }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

uint32_t pause_tstates(uint16_t ms) {
    if (ms == 0) return 0;
    return uint32_t(ms) * 3500u;
}

}  // namespace

void TapeTzx::clear() {
    blocks_.clear();
    loaded_ = false;
    playing_ = false;
    paused_ = false;
    index_ = 0;
    level_ = 0;
    estado_ = 0;
    estados_left_ = 0;
}

bool TapeTzx::load_file(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open tape: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) {
        if (error) *error = "empty tape";
        return false;
    }
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
	f.read(reinterpret_cast<char*>(buf.data()), sz);
    return load_memory(buf.data(), buf.size(), error);
}

bool TapeTzx::load_memory(const uint8_t* data, size_t size, std::string* error) {
    clear();
    if (size >= 22 && std::memcmp(data, "Compressed Square Wave", 22) == 0) {
        if (!parse_csw(data, size, error)) return false;
    } else if (size >= 4 && std::memcmp(data, "PZXT", 4) == 0) {
        if (!parse_pzx(data, size, error)) return false;
    } else {
        if (!parse_tzx(data, size, error)) return false;
    }
    loaded_ = !blocks_.empty();
    if (!loaded_ && error) *error = "no usable tape blocks";
    return loaded_;
}

bool TapeTzx::load_csw(const uint8_t* data, size_t size, std::string* error) {
    clear();
    if (!parse_csw(data, size, error)) return false;
    loaded_ = !blocks_.empty();
    return loaded_;
}

bool TapeTzx::load_pzx(const uint8_t* data, size_t size, std::string* error) {
    clear();
    if (!parse_pzx(data, size, error)) return false;
    loaded_ = !blocks_.empty();
    return loaded_;
}

std::vector<uint8_t> TapeTzx::csw_rle_decode(const uint8_t* in, size_t in_len,
                                             uint8_t initial_polarity) {
    // Expand RLE pulse lengths into a packed bit stream (MSB first), matching
    // descomprimir_csw in tap_tzx.pas. Each pulse is `contador` samples at the
    // current polarity, then polarity flips.
    std::vector<uint8_t> out;
    uint8_t polarity = initial_polarity & 1;
    int pos_bit = 7;
    uint8_t byte_acc = 0;
    size_t i = 0;
    while (i < in_len) {
        uint32_t contador = in[i++];
        if (contador == 0) {
            if (i + 4 > in_len) break;
            contador = uint32_t(in[i] | (in[i + 1] << 8) | (in[i + 2] << 16) | (in[i + 3] << 24));
            i += 4;
        }
        for (uint32_t f = 0; f < contador; ++f) {
            byte_acc = uint8_t(byte_acc | (polarity << pos_bit));
            --pos_bit;
            if (pos_bit < 0) {
                pos_bit = 7;
                out.push_back(byte_acc);
                byte_acc = 0;
            }
        }
        polarity ^= 1;
    }
    if (pos_bit != 7) {
        // partial last byte
        out.push_back(byte_acc);
    }
    return out;
}

bool TapeTzx::parse_csw(const uint8_t* data, size_t size, std::string* error) {
    // Header: "Compressed Square Wave" (22) + 0x1A + major + minor  => 25 bytes
    if (size < 25) {
        if (error) *error = "CSW too small";
        return false;
    }
    if (std::memcmp(data, "Compressed Square Wave", 22) != 0) {
        if (error) *error = "not a CSW file";
        return false;
    }
    const uint8_t major = data[23];
    const uint8_t minor = data[24];
    (void)minor;

    uint32_t sample_rate = 44100;
    uint8_t initial_polarity = 0;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    uint8_t compression = 1;
    std::vector<uint8_t> zlib_buf;

    if (major == 1) {
        if (size < 32) {
            if (error) *error = "CSW v1 truncated header";
            return false;
        }
        sample_rate = uint32_t(data[25] | (data[26] << 8));
        compression = data[27];
        initial_polarity = data[28] & 1;
        payload = data + 32;  // 25 + 7
        payload_len = size - 32;
        if (compression != 1) {
            // v1 is always RLE in practice
            compression = 1;
        }
    } else if (major == 2) {
        // After 25-byte common header:
        // sample_rate:u32, pulse_total:u32, compression:u8, flags:u8, header:u8, app[16]
        // = 4+4+1+1+1+16 = 27, then `header` extra extension bytes
        if (size < 25 + 27) {
            if (error) *error = "CSW v2 truncated header";
            return false;
        }
        const uint8_t* p = data + 25;
        sample_rate = uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
        // pulse_total at p+4
        compression = p[8];
        initial_polarity = p[9] & 1;
        const uint8_t ext = p[10];
        const size_t hdr_skip = 25 + 27 + ext;
        if (size < hdr_skip) {
            if (error) *error = "CSW v2 header extension past EOF";
            return false;
        }
        payload = data + hdr_skip;
        payload_len = size - hdr_skip;
    } else {
        if (error) *error = "unsupported CSW version " + std::to_string(major);
        return false;
    }

    if (sample_rate == 0) sample_rate = 44100;

    std::vector<uint8_t> rle_src;
    if (compression == 2) {
        // zlib-wrapped RLE
        uLongf dest_len = uLongf(payload_len * 20 + 1024);
        zlib_buf.resize(dest_len);
        int z = Z_BUF_ERROR;
        while (z == Z_BUF_ERROR) {
            z = uncompress(zlib_buf.data(), &dest_len,
                           payload, uLong(payload_len));
            if (z == Z_BUF_ERROR) {
                dest_len *= 2;
                zlib_buf.resize(dest_len);
            }
        }
        if (z != Z_OK) {
            if (error) *error = "CSW zlib decompress failed (" + std::to_string(z) + ")";
            return false;
        }
        zlib_buf.resize(dest_len);
        rle_src = std::move(zlib_buf);
    } else {
        rle_src.assign(payload, payload + payload_len);
    }

    std::vector<uint8_t> samples = csw_rle_decode(rle_src.data(), rle_src.size(), initial_polarity);
    if (samples.empty()) {
        if (error) *error = "CSW produced no samples";
        return false;
    }

    // Convert to Direct Recording block ($15)
    Block b;
    b.type = BlockType::DirectRecording;
    // T-states per sample at 3.5 MHz: 3500000 / sample_rate
    b.sample_tstates = uint32_t((3500000.0 + sample_rate / 2) / sample_rate);
    if (b.sample_tstates == 0) b.sample_tstates = 1;
    b.one = uint16_t(std::min<uint32_t>(b.sample_tstates, 65535));
    b.used_bits = 8;
    b.pause_ms = 0;
    b.data = std::move(samples);
    blocks_.push_back(std::move(b));
    return true;
}

bool TapeTzx::parse_tzx(const uint8_t* data, size_t size, std::string* error) {
    if (size < 8) {
        if (error) *error = "file too small";
        return false;
    }

    // Plain TAP without TZX header
    if (std::memcmp(data, "ZXTape!", 7) != 0) {
        size_t pos = 0;
        while (pos + 2 <= size) {
            const uint16_t len = rd16(data + pos);
            pos += 2;
            if (len == 0 || pos + len > size) break;
            Block b;
            b.type = BlockType::Standard;
            b.pilot = 2168;
            b.sync1 = 667;
            b.sync2 = 735;
            b.zero = 855;
            b.one = 1710;
            b.pilot_pulses = (data[pos] & 0x80) ? 3223 : 8063;
            b.used_bits = 8;
            b.pause_ms = 1000;
            b.data.assign(data + pos, data + pos + len);
            blocks_.push_back(std::move(b));
            pos += len;
        }
        return !blocks_.empty();
    }

    size_t pos = 10;  // header + version
    while (pos < size) {
        const uint8_t id = data[pos++];
        Block b;
        b.type = BlockType(id);

        auto need = [&](size_t n) -> bool {
            if (pos + n > size) {
                if (error) *error = "truncated TZX block";
                return false;
            }
            return true;
        };

        switch (id) {
            case 0x10: {
                if (!need(4)) return false;
                b.pause_ms = rd16(data + pos); pos += 2;
                const uint16_t len = rd16(data + pos); pos += 2;
                if (!need(len)) return false;
                b.pilot = 2168; b.sync1 = 667; b.sync2 = 735;
                b.zero = 855; b.one = 1710;
                b.pilot_pulses = (len && (data[pos] & 0x80)) ? 3223 : 8063;
                b.used_bits = 8;
                b.data.assign(data + pos, data + pos + len);
                pos += len;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x11: {
                if (!need(18)) return false;
                b.pilot = rd16(data + pos); pos += 2;
                b.sync1 = rd16(data + pos); pos += 2;
                b.sync2 = rd16(data + pos); pos += 2;
                b.zero = rd16(data + pos); pos += 2;
                b.one = rd16(data + pos); pos += 2;
                b.pilot_pulses = rd16(data + pos); pos += 2;
                b.used_bits = data[pos++];
                b.pause_ms = rd16(data + pos); pos += 2;
                const uint32_t len = rd24(data + pos); pos += 3;
                if (!need(len)) return false;
                b.data.assign(data + pos, data + pos + len);
                pos += len;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x12: {
                if (!need(4)) return false;
                b.pilot = rd16(data + pos); pos += 2;
                b.pilot_pulses = rd16(data + pos); pos += 2;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x13: {
                if (!need(1)) return false;
                const uint8_t n = data[pos++];
                if (!need(size_t(n) * 2)) return false;
                for (uint8_t i = 0; i < n; ++i) {
                    b.pulses.push_back(rd16(data + pos));
                    pos += 2;
                }
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x14: {
                if (!need(10)) return false;
                b.zero = rd16(data + pos); pos += 2;
                b.one = rd16(data + pos); pos += 2;
                b.used_bits = data[pos++];
                b.pause_ms = rd16(data + pos); pos += 2;
                const uint32_t len = rd24(data + pos); pos += 3;
                if (!need(len)) return false;
                b.pilot_pulses = 0;
                b.data.assign(data + pos, data + pos + len);
                pos += len;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x15: {  // Direct recording
                if (!need(8)) return false;
                b.sample_tstates = rd16(data + pos); pos += 2;
                b.pause_ms = rd16(data + pos); pos += 2;
                b.used_bits = data[pos++];
                const uint32_t len = rd24(data + pos); pos += 3;
                if (!need(len)) return false;
                b.one = uint16_t(b.sample_tstates);  // reuse: duration per bit
                b.data.assign(data + pos, data + pos + len);
                pos += len;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x19: {  // Generalized Data Block
                if (!need(18)) return false;
                const uint32_t block_len = rd32(data + pos);  // not including this length field? Spec: length of following data
                // TZX: DWORD length of the rest of the block AFTER the length field itself.
                // Header fields after length: pause(2)+TOTP(4)+NPP(1)+ASP(1)+TOTD(4)+NPD(1)+ASD(1) = 14, already in first 18 with size.
                // Actually layout: size(4) pause(2) totp(4) npp(1) asp(1) totd(4) npd(1) asd(1) = 18 bytes from start of block body.
                b.pause_ms = rd16(data + pos + 4);
                b.totp = rd32(data + pos + 6);
                b.npp = data[pos + 10];
                b.asp = data[pos + 11];
                b.totd = rd32(data + pos + 12);
                b.npd = data[pos + 16];
                b.asd = data[pos + 17];
                pos += 18;

                const uint16_t asp = alphabet_size(uint8_t(b.asp > 255 ? 0 : b.asp));
                const uint16_t asd = alphabet_size(uint8_t(b.asd > 255 ? 0 : b.asd));
                // Reread asp/asd properly
                // asp/asd already stored; alphabet_size uses 0→256

                // Pilot symbols
                if (b.totp != 0) {
                    const uint16_t nsym = alphabet_size(uint8_t(b.asp));
                    for (uint16_t s = 0; s < nsym; ++s) {
                        if (!need(1 + size_t(b.npp) * 2)) return false;
                        Symbol sym;
                        sym.flags = data[pos++];
                        for (uint8_t p = 0; p < b.npp; ++p) {
                            const uint16_t v = rd16(data + pos);
                            pos += 2;
                            if (v != 0) sym.pulses.push_back(v);
                        }
                        b.pilot_symbols.push_back(std::move(sym));
                    }
                    // Pilot stream: TOTP entries of (symbol index u8, repeat u16)
                    for (uint32_t i = 0; i < b.totp; ++i) {
                        if (!need(3)) return false;
                        const uint8_t sym_i = data[pos++];
                        const uint16_t rep = rd16(data + pos);
                        pos += 2;
                        b.pilot_stream.push_back(sym_i);
                        b.pilot_stream.push_back(rep);
                    }
                }

                // Data symbols
                if (b.totd != 0) {
                    const uint16_t nsym = alphabet_size(uint8_t(b.asd));
                    for (uint16_t s = 0; s < nsym; ++s) {
                        if (!need(1 + size_t(b.npd) * 2)) return false;
                        Symbol sym;
                        sym.flags = data[pos++];
                        for (uint8_t p = 0; p < b.npd; ++p) {
                            const uint16_t v = rd16(data + pos);
                            pos += 2;
                            if (v != 0) sym.pulses.push_back(v);
                        }
                        b.symbols.push_back(std::move(sym));
                    }
                    // Data stream size: bit-packed for any alphabet size.
                    // ASD=2 → 1 bit/symbol; ASD=256 → 8 bits; else ceil(log2(ASD)).
                    const uint8_t bps = bits_for_alphabet(nsym);
                    const uint32_t data_bits = b.totd * bps;
                    const uint32_t data_bytes = (data_bits + 7) / 8;
                    if (!need(data_bytes)) return false;
                    b.data.assign(data + pos, data + pos + data_bytes);
                    pos += data_bytes;
                }

                (void)block_len;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x20: {
                if (!need(2)) return false;
                b.pause_ms = rd16(data + pos); pos += 2;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x21: {
                if (!need(1)) return false;
                const uint8_t n = data[pos++];
                if (!need(n)) return false;
                b.text.assign(reinterpret_cast<const char*>(data + pos), n);
                pos += n;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x22:
                blocks_.push_back(std::move(b));
                break;
            case 0x23: {
                if (!need(2)) return false;
                b.jump = int16_t(rd16(data + pos)); pos += 2;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x24: {
                if (!need(2)) return false;
                b.loop_count = rd16(data + pos); pos += 2;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x25:
                blocks_.push_back(std::move(b));
                break;
            case 0x28: {  // Select — skip (UI only)
                if (!need(2)) return false;
                const uint16_t n = rd16(data + pos); pos += 2;
                if (!need(n)) return false;
                pos += n;
                break;
            }
            case 0x2a: {
                if (!need(4)) return false;
                pos += 4;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x2b: {  // Set signal level
                if (!need(5)) return false;
                const uint32_t n = rd32(data + pos); pos += 4;
                if (!need(1)) return false;
                b.signal_level = data[pos++] ? 0x40 : 0x00;
                if (n > 1) {
                    if (!need(n - 1)) return false;
                    pos += n - 1;
                }
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x30: {
                if (!need(1)) return false;
                const uint8_t n = data[pos++];
                if (!need(n)) return false;
                b.text.assign(reinterpret_cast<const char*>(data + pos), n);
                pos += n;
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x31: {
                if (!need(2)) return false;
                pos += 1;
                const uint8_t n = data[pos++];
                if (!need(n)) return false;
                pos += n;
                break;
            }
            case 0x32: {
                if (!need(2)) return false;
                const uint16_t n = rd16(data + pos); pos += 2;
                if (!need(n)) return false;
                pos += n;
                break;
            }
            case 0x33: {
                if (!need(1)) return false;
                const uint8_t n = data[pos++];
                if (!need(size_t(n) * 3)) return false;
                pos += size_t(n) * 3;
                break;
            }
            case 0x35: {
                if (!need(20)) return false;
                pos += 16;
                const uint32_t n = rd32(data + pos); pos += 4;
                if (!need(n)) return false;
                pos += n;
                break;
            }
            case 0x4b: {  // MSX Kansas City (treat as turbo-like pure data with given timings)
                if (!need(16)) return false;
                // size(4) pause(2) pilot_pulse(2) pilot_len(2) zero(2) one(2) bit0_pulses(1)? — follow Pascal ttzx_type_4b
                const uint32_t blen = rd32(data + pos); pos += 4;
                b.pause_ms = rd16(data + pos); pos += 2;
                b.pilot = rd16(data + pos); pos += 2;
                b.pilot_pulses = rd16(data + pos); pos += 2;
                b.zero = rd16(data + pos); pos += 2;
                b.one = rd16(data + pos); pos += 2;
                // remaining payload in block
                const size_t rest = (blen > 12) ? (blen - 12) : 0;
                if (!need(rest)) return false;
                // first two bytes may be flags; take rest as data if present
                if (rest >= 2) {
                    b.used_bits = 8;
                    b.data.assign(data + pos + 2, data + pos + rest);
                }
                pos += rest;
                b.type = BlockType::Turbo;  // play like turbo with pilot+data
                blocks_.push_back(std::move(b));
                break;
            }
            case 0x5a: {
                if (!need(9)) return false;
                pos += 9;
                break;
            }
            default: {
                // Try generic: many blocks start with a length field.
                if (error) {
                    *error = "unsupported TZX block 0x" + std::to_string(id) +
                             " — stopping parse, keeping " + std::to_string(blocks_.size()) + " blocks";
                }
                return !blocks_.empty();
            }
        }
    }
    return !blocks_.empty();
}

void TapeTzx::play(bool restart) {
    if (!loaded_ || blocks_.empty()) return;
    if (restart || !playing_) {
        index_ = 0;
        level_ = 0;
        estado_ = 0;
        estados_left_ = 0;
        playing_ = true;
        paused_ = false;
        start_block();
    } else {
        // resume from pause
        paused_ = false;
        playing_ = true;
    }
}

void TapeTzx::pause() {
    if (playing_) paused_ = true;
}

void TapeTzx::stop() {
    playing_ = false;
    paused_ = false;
    estado_ = 0;
    estados_left_ = 0;
    index_ = 0;
}

void TapeTzx::next_block() {
    ++index_;
    if (index_ >= blocks_.size()) {
        stop();
        return;
    }
    start_block();
}

void TapeTzx::start_gen_symbol(const Symbol& sym) {
    const uint8_t pol = sym.flags & 3;
    if (pol == 1) level_ = 0;
    else if (pol == 2) level_ = 0x40;
    // 0 = xor each pulse, 3 = hold (no xor on first)
    sym_pulse_i_ = 0;
    if (sym.pulses.empty()) {
        estados_left_ = 1;
        return;
    }
    if (pol != 3) level_ ^= 0x40;
    estados_left_ = int(sym.pulses[0]);
}

uint8_t TapeTzx::bits_for_alphabet(uint16_t asd) {
    if (asd <= 2) return 1;
    if (asd <= 4) return 2;
    if (asd <= 8) return 3;
    if (asd <= 16) return 4;
    if (asd <= 32) return 5;
    if (asd <= 64) return 6;
    if (asd <= 128) return 7;
    return 8;
}

uint32_t TapeTzx::read_symbol_index(const Block& b, uint32_t symbol_i) const {
    const uint16_t asd = alphabet_size(uint8_t(b.asd));
    const uint8_t bps = bits_for_alphabet(asd);
    if (bps == 8 && asd == 256) {
        return symbol_i < b.data.size() ? b.data[symbol_i] : 0;
    }
    // MSB-first bit stream across data[]
    const uint32_t bit_pos = symbol_i * bps;
    uint32_t value = 0;
    for (uint8_t k = 0; k < bps; ++k) {
        const uint32_t abs_bit = bit_pos + k;
        const size_t byte_i = abs_bit / 8;
        const int bit = 7 - int(abs_bit % 8);
        uint8_t bitv = 0;
        if (byte_i < b.data.size()) bitv = (b.data[byte_i] >> bit) & 1;
        value = (value << 1) | bitv;
    }
    return value;
}

void TapeTzx::advance_gen_data() {
    const Block& b = blocks_[index_];
    if (b.symbols.empty() || data_pos_ >= b.totd) {
        estado_ = 5;
        const uint32_t p = pause_tstates(uint16_t(b.pause_ms));
        estados_left_ = p ? int(p) : 1;
        return;
    }
    uint32_t sym_i = read_symbol_index(b, uint32_t(data_pos_));
    if (sym_i >= b.symbols.size()) sym_i = 0;
    current_symbol_ = uint8_t(sym_i & 0xff);
    // For alphabets > 256 (shouldn't happen per TZX) clamp
    if (sym_i < b.symbols.size()) {
        start_gen_symbol(b.symbols[sym_i]);
    } else {
        start_gen_symbol(b.symbols[0]);
    }
    estado_ = 20;
}

void TapeTzx::start_block() {
    if (index_ >= blocks_.size()) {
        stop();
        return;
    }
    const Block& b = blocks_[index_];
    data_pos_ = 0;
    pulse_count_ = 0;
    pulse_index_ = 0;
    bit_mask_ = 0x80;
    last_bit_mask_ = 1;
    sym_pulse_i_ = 0;
    pilot_stream_i_ = 0;
    pilot_repeat_left_ = 0;
    if (!b.data.empty() && b.used_bits > 0 && b.used_bits < 8) {
        last_bit_mask_ = uint8_t(1u << (8 - b.used_bits));
    }

    switch (b.type) {
        case BlockType::Standard:
        case BlockType::Turbo:
            estado_ = 0;
            estados_left_ = int(b.pilot);
            pulse_count_ = b.pilot_pulses;
            break;
        case BlockType::PureTone:
            estado_ = 0;
            estados_left_ = int(b.pilot);
            pulse_count_ = b.pilot_pulses;
            break;
        case BlockType::PulseSeq:
            estado_ = 10;
            if (b.initial_level) level_ = b.initial_level;
            if (!b.pulses.empty()) {
                estados_left_ = int(b.pulses[0] > 0x7fffffff ? 0x7fffffff : b.pulses[0]);
                pulse_index_ = 0;
            } else {
                next_block();
            }
            break;
        case BlockType::PureData:
            estado_ = 3;
            if (!b.data.empty()) {
                estados_left_ = int((b.data[0] & 0x80) ? b.one : b.zero);
                bit_mask_ = 0x80;
            } else {
                next_block();
            }
            break;
        case BlockType::DirectRecording:
            estado_ = 15;
            if (!b.data.empty()) {
                bit_mask_ = 0x80;
                if (b.data.size() == 1) last_bit_mask_ = uint8_t(1u << (8 - b.used_bits));
                level_ = (b.data[0] & 0x80) ? 0x40 : 0;
                estados_left_ = int(b.sample_tstates ? b.sample_tstates : 1);
            } else {
                next_block();
            }
            break;
        case BlockType::Generalized:
            if (b.totp && !b.pilot_stream.empty() && !b.pilot_symbols.empty()) {
                estado_ = 18;  // pilot stream
                pilot_stream_i_ = 0;
                pilot_repeat_left_ = 0;
                // kick first symbol
                const uint8_t si = uint8_t(b.pilot_stream[0]);
                pilot_repeat_left_ = b.pilot_stream.size() > 1 ? b.pilot_stream[1] : 1;
                if (si < b.pilot_symbols.size()) {
                    current_symbol_ = si;
                    start_gen_symbol(b.pilot_symbols[si]);
                    estado_ = 19;  // pilot symbol pulses
                } else {
                    advance_gen_data();
                }
            } else if (b.totd) {
                advance_gen_data();
            } else {
                next_block();
            }
            break;
        case BlockType::Pause:
            if (b.pause_ms == 0 && b.sample_tstates == 0) {
                stop();
                return;
            }
            estado_ = 5;
            if (b.sample_tstates) {
                estados_left_ = int(b.sample_tstates > 0x7fffffffu ? 0x7fffffff : b.sample_tstates);
            } else {
                estados_left_ = int(pause_tstates(uint16_t(b.pause_ms)));
            }
            break;
        case BlockType::SetLevel:
            level_ = b.signal_level;
            next_block();
            break;
        case BlockType::Jump: {
            const int target = int(index_) + b.jump;
            if (target >= 0 && target < int(blocks_.size())) {
                index_ = size_t(target);
                start_block();
            } else {
                next_block();
            }
            break;
        }
        case BlockType::LoopStart:
            loop_start_ = index_ + 1;
            loop_remain_ = b.loop_count;
            next_block();
            break;
        case BlockType::LoopEnd:
            if (loop_remain_ > 1) {
                --loop_remain_;
                index_ = loop_start_;
                start_block();
            } else {
                next_block();
            }
            break;
        case BlockType::GroupStart:
        case BlockType::GroupEnd:
        case BlockType::Text:
        case BlockType::Stop48k:
            next_block();
            break;
        default:
            next_block();
            break;
    }
}

int TapeTzx::advance(int tstates) {
    if (!playing_ || paused_ || tstates <= 0) return (level_ & 0x40) ? 1 : 0;

    while (tstates > 0 && playing_) {
        if (estados_left_ > tstates) {
            estados_left_ -= tstates;
            tstates = 0;
            break;
        }
        tstates -= estados_left_;
        estados_left_ = 0;

        if (index_ >= blocks_.size()) {
            stop();
            break;
        }
        const Block& b = blocks_[index_];

        switch (estado_) {
            case 0:  // pilot
                level_ ^= 0x40;
                if (pulse_count_ > 1) {
                    --pulse_count_;
                    estados_left_ = int(b.pilot);
                } else {
                    estado_ = 1;
                    estados_left_ = int(b.sync1 ? b.sync1 : 1);
                }
                break;
            case 1:
                level_ ^= 0x40;
                estado_ = 2;
                estados_left_ = int(b.sync2 ? b.sync2 : 1);
                break;
            case 2:
                level_ ^= 0x40;
                estado_ = 3;
                data_pos_ = 0;
                bit_mask_ = 0x80;
                if (b.data.empty()) {
                    estado_ = 5;
                    estados_left_ = int(pause_tstates(uint16_t(b.pause_ms)));
                    break;
                }
                if (b.data.size() == 1) last_bit_mask_ = uint8_t(1u << (8 - b.used_bits));
                estados_left_ = int((b.data[0] & 0x80) ? b.one : b.zero);
                break;
            case 3:
                level_ ^= 0x40;
                estado_ = 4;
                {
                    const uint8_t byte = b.data[data_pos_];
                    estados_left_ = int((byte & bit_mask_) ? b.one : b.zero);
                }
                break;
            case 4: {
                level_ ^= 0x40;
                const bool last_byte = (data_pos_ + 1 >= b.data.size());
                if (bit_mask_ > last_bit_mask_) {
                    bit_mask_ >>= 1;
                    if (bit_mask_ != 0) {
                        estado_ = 3;
                        const uint8_t byte = b.data[data_pos_];
                        estados_left_ = int((byte & bit_mask_) ? b.one : b.zero);
                        break;
                    }
                }
                ++data_pos_;
                if (data_pos_ < b.data.size()) {
                    bit_mask_ = 0x80;
                    last_bit_mask_ = (data_pos_ + 1 == b.data.size())
                                         ? uint8_t(1u << (8 - b.used_bits))
                                         : uint8_t(1);
                    estado_ = 3;
                    estados_left_ = int((b.data[data_pos_] & 0x80) ? b.one : b.zero);
                } else {
                    estado_ = 5;
                    const uint32_t p = pause_tstates(uint16_t(b.pause_ms));
                    estados_left_ = p ? int(p) : 1;
                }
                break;
            }
            case 5:
                next_block();
                break;
            case 10:  // pulse sequence
                level_ ^= 0x40;
                ++pulse_index_;
                if (pulse_index_ < b.pulses.size()) {
                    estados_left_ = int(b.pulses[pulse_index_] > 0x7fffffffu ? 0x7fffffff : b.pulses[pulse_index_]);
                } else {
                    next_block();
                }
                break;
            case 15: {  // direct recording: level = bit, duration = sample_tstates
                // finished one sample; advance bit
                if (bit_mask_ > last_bit_mask_) {
                    bit_mask_ >>= 1;
                    level_ = (b.data[data_pos_] & bit_mask_) ? 0x40 : 0;
                    estados_left_ = int(b.sample_tstates ? b.sample_tstates : 1);
                } else {
                    ++data_pos_;
                    if (data_pos_ < b.data.size()) {
                        bit_mask_ = 0x80;
                        last_bit_mask_ = (data_pos_ + 1 == b.data.size())
                                             ? uint8_t(1u << (8 - b.used_bits))
                                             : uint8_t(1);
                        level_ = (b.data[data_pos_] & 0x80) ? 0x40 : 0;
                        estados_left_ = int(b.sample_tstates ? b.sample_tstates : 1);
                    } else {
                        estado_ = 5;
                        const uint32_t p = pause_tstates(uint16_t(b.pause_ms));
                        estados_left_ = p ? int(p) : 1;
                    }
                }
                break;
            }
            case 19: {  // generalized pilot symbol pulses
                const Symbol* sym = nullptr;
                if (current_symbol_ < b.pilot_symbols.size()) {
                    sym = &b.pilot_symbols[current_symbol_];
                }
                if (!sym || sym->pulses.empty()) {
                    // next pilot stream entry
                    estado_ = 18;
                    estados_left_ = 1;
                    break;
                }
                ++sym_pulse_i_;
                if (sym_pulse_i_ < sym->pulses.size()) {
                    if ((sym->flags & 3) != 3) level_ ^= 0x40;
                    else level_ ^= 0x40;  // still toggle for subsequent pulses
                    estados_left_ = int(sym->pulses[sym_pulse_i_]);
                } else {
                    // symbol done — repeats
                    if (pilot_repeat_left_ > 1) {
                        --pilot_repeat_left_;
                        start_gen_symbol(*sym);
                    } else {
                        pilot_stream_i_ += 2;
                        if (pilot_stream_i_ + 1 < b.pilot_stream.size()) {
                            const uint8_t si = uint8_t(b.pilot_stream[pilot_stream_i_]);
                            pilot_repeat_left_ = b.pilot_stream[pilot_stream_i_ + 1];
                            if (si < b.pilot_symbols.size()) {
                                current_symbol_ = si;
                                start_gen_symbol(b.pilot_symbols[si]);
                            } else {
                                advance_gen_data();
                            }
                        } else {
                            data_pos_ = 0;
                            advance_gen_data();
                        }
                    }
                }
                break;
            }
            case 20: {  // generalized data symbol pulses
                const Symbol* sym = nullptr;
                if (current_symbol_ < b.symbols.size()) sym = &b.symbols[current_symbol_];
                if (!sym || sym->pulses.empty()) {
                    ++data_pos_;
                    advance_gen_data();
                    break;
                }
                ++sym_pulse_i_;
                if (sym_pulse_i_ < sym->pulses.size()) {
                    level_ ^= 0x40;
                    estados_left_ = int(sym->pulses[sym_pulse_i_]);
                } else {
                    ++data_pos_;
                    advance_gen_data();
                }
                break;
            }
            case 18:
                // transient — should not linger
                advance_gen_data();
                break;
            default:
                next_block();
                break;
        }
    }
    return (level_ & 0x40) ? 1 : 0;
}

bool TapeTzx::parse_pzx(const uint8_t* data, size_t size, std::string* error) {
    if (size < 8 || std::memcmp(data, "PZXT", 4) != 0) {
        if (error) *error = "not a PZX file";
        return false;
    }
    size_t pos = 0;
    auto read_tag = [&](char name[5], uint32_t& tag_size) -> bool {
        if (pos + 8 > size) return false;
        std::memcpy(name, data + pos, 4);
        name[4] = 0;
        tag_size = rd32(data + pos + 4);
        pos += 8;
        if (pos + tag_size > size) return false;
        return true;
    };

    char tag[5];
    uint32_t tag_size = 0;
    if (!read_tag(tag, tag_size)) {
        if (error) *error = "truncated PZX header";
        return false;
    }
    // Skip major/minor info payload of first PZXT
    pos += tag_size;

    while (pos + 8 <= size) {
        const size_t tag_pos = pos;
        if (!read_tag(tag, tag_size)) break;
        const uint8_t* body = data + pos;
        pos += tag_size;

        if (std::strcmp(tag, "PULS") == 0) {
            Block b;
            b.type = BlockType::PulseSeq;
            size_t i = 0;
            uint8_t init_ear = 0;
            while (i + 2 <= tag_size) {
                uint32_t count = 1;
                uint32_t duration = rd16(body + i);
                i += 2;
                if (duration > 0x8000) {
                    count = duration & 0x7fff;
                    if (i + 2 > tag_size) break;
                    duration = rd16(body + i);
                    i += 2;
                }
                if (duration & 0x8000) {
                    if (i + 2 > tag_size) break;
                    duration = ((duration & 0x7fff) << 16) | rd16(body + i);
                    i += 2;
                }
                if (duration == 0) {
                    init_ear = 1;
                    continue;
                }
                for (uint32_t c = 0; c < count; ++c) b.pulses.push_back(duration);
            }
            if (init_ear) b.initial_level = 0x40;
            if (!b.pulses.empty()) blocks_.push_back(std::move(b));
        } else if (std::strcmp(tag, "PAUS") == 0) {
            if (tag_size < 4) continue;
            const uint32_t raw = rd32(body);
            const uint32_t tstates = raw & 0x7fffffffu;
            const bool high = (raw >> 31) != 0;
            if (high) {
                Block lvl;
                lvl.type = BlockType::SetLevel;
                lvl.signal_level = 0x40;
                blocks_.push_back(std::move(lvl));
            }
            Block b;
            b.type = BlockType::Pause;
            // Convert T-states to ms (approx) for our pause handler
            b.pause_ms = tstates / 3500u;
            if (b.pause_ms == 0 && tstates > 0) b.pause_ms = 1;
            // Store exact T-states in sample_tstates for better fidelity
            b.sample_tstates = tstates;
            blocks_.push_back(std::move(b));
        } else if (std::strcmp(tag, "DATA") == 0) {
            if (tag_size < 8) continue;
            uint32_t bit_count = rd32(body);
            const uint8_t initial = uint8_t((bit_count >> 31) & 1);
            bit_count &= 0x7fffffffu;
            const uint16_t tail = rd16(body + 4);
            const uint8_t p0 = body[6];
            const uint8_t p1 = body[7];
            size_t i = 8;
            Block b;
            b.type = BlockType::Generalized;
            b.asd = 2;
            b.npd = (p0 > p1) ? p0 : p1;
            b.totd = bit_count;
            b.used_bits = (bit_count % 8) ? uint8_t(bit_count % 8) : 8;
            if (initial) b.initial_level = 0x40;

            Symbol s0, s1;
            s0.flags = 0;
            s1.flags = 0;
            for (uint8_t k = 0; k < p0; ++k) {
                if (i + 2 > tag_size) break;
                s0.pulses.push_back(rd16(body + i));
                i += 2;
            }
            for (uint8_t k = 0; k < p1; ++k) {
                if (i + 2 > tag_size) break;
                s1.pulses.push_back(rd16(body + i));
                i += 2;
            }
            b.symbols.push_back(std::move(s0));
            b.symbols.push_back(std::move(s1));

            const uint32_t data_bytes = (bit_count + 7) / 8;
            if (i + data_bytes <= tag_size) {
                b.data.assign(body + i, body + i + data_bytes);
            }
            blocks_.push_back(std::move(b));

            if (tail) {
                Block t;
                t.type = BlockType::PulseSeq;
                t.pulses.push_back(tail);
                blocks_.push_back(std::move(t));
            }
        } else if (std::strcmp(tag, "BRWS") == 0) {
            Block b;
            b.type = BlockType::Text;
            b.text.assign(reinterpret_cast<const char*>(body), tag_size);
            blocks_.push_back(std::move(b));
        } else if (std::strcmp(tag, "STOP") == 0) {
            Block b;
            uint16_t flags = 0;
            if (tag_size >= 2) flags = rd16(body);
            if (flags == 1) {
                b.type = BlockType::Stop48k;
            } else {
                b.type = BlockType::Pause;
                b.pause_ms = 0;  // stop tape
            }
            blocks_.push_back(std::move(b));
        } else if (std::strcmp(tag, "PZXT") == 0) {
            // nested glue / version — ignore payload
        } else {
            // unknown tag — skip
            (void)tag_pos;
        }
    }
    if (blocks_.empty()) {
        if (error) *error = "PZX contained no playable blocks";
        return false;
    }
    return true;
}

}  // namespace dsp
