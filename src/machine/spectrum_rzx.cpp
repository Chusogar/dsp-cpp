#include "machine/spectrum_rzx.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <zlib.h>

namespace dsp {
namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24));
}

}  // namespace

bool SpectrumRzx::inflate(const uint8_t* src, size_t src_len, size_t expected, std::vector<uint8_t>& out,
                          std::string* error) {
    out.assign(expected ? expected : src_len * 4, 0);
    uLongf dest_len = uLongf(out.size());
    int z = ::uncompress(out.data(), &dest_len, src, uLongf(src_len));
    if (z == Z_BUF_ERROR && expected == 0) {
        out.resize(out.size() * 4);
        dest_len = uLongf(out.size());
        z = ::uncompress(out.data(), &dest_len, src, uLongf(src_len));
    }
    if (z != Z_OK) {
        // Raw inflate (no zlib header) — some block writers omit it.
        z_stream strm{};
        strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
        strm.avail_in = uInt(src_len);
        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
            if (error) *error = "RZX zlib init failed";
            return false;
        }
        out.assign(expected ? expected : src_len * 8, 0);
        strm.next_out = out.data();
        strm.avail_out = uInt(out.size());
        z = ::inflate(&strm, Z_FINISH);
        const size_t got = size_t(strm.total_out);
        inflateEnd(&strm);
        if (z != Z_STREAM_END && z != Z_OK) {
            if (error) *error = "RZX decompress failed";
            return false;
        }
        out.resize(got);
        return true;
    }
    out.resize(size_t(dest_len));
    return true;
}

bool SpectrumRzx::load(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open RZX: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) {
        if (error) *error = "empty RZX";
        return false;
    }
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    if (!f) {
        if (error) *error = "failed reading RZX";
        return false;
    }
    return load_bytes(buf.data(), buf.size(), error);
}

bool SpectrumRzx::load_bytes(const uint8_t* data, size_t size, std::string* error) {
    stop();
    frames_.clear();
    snap_ok_ = false;
    finished_ = false;
    return parse(data, size, error);
}

bool SpectrumRzx::parse(const uint8_t* data, size_t size, std::string* error) {
    if (!data || size < 10 || std::memcmp(data, "RZX!", 4) != 0) {
        if (error) *error = "not an RZX file";
        return false;
    }
    // major/minor at 4/5; flags at 6 — encryption bit not supported for full-file crypto.
    if (rd32(data + 6) & 1) {
        if (error) *error = "encrypted RZX not supported";
        return false;
    }

    size_t o = 10;
    bool got_input = false;
    while (o + 5 <= size) {
        const uint8_t id = data[o];
        const uint32_t blen = rd32(data + o + 1);
        if (blen < 5 || o + blen > size) {
            if (error) *error = "corrupt RZX block length";
            return false;
        }
        const uint8_t* block = data + o;
        if (id == 0x30) {
            // Snapshot block
            if (blen < 17) {
                if (error) *error = "RZX snapshot block too small";
                return false;
            }
            const uint32_t flags = rd32(block + 5);
            if (flags & 1) {
                if (error) *error = "RZX external snapshot not supported";
                return false;
            }
            char ext[5] = {};
            std::memcpy(ext, block + 9, 4);
            const uint32_t usl = rd32(block + 13);
            const uint8_t* snap_data = block + 17;
            const size_t snap_raw = blen - 17;
            std::vector<uint8_t> decoded;
            const uint8_t* use = snap_data;
            size_t use_len = snap_raw;
            if (flags & 2) {
                if (!inflate(snap_data, snap_raw, usl, decoded, error)) return false;
                use = decoded.data();
                use_len = decoded.size();
            }
            if (!spectrum_snap_from_bytes(use, use_len, ext, snap_, error)) return false;
            snap_ok_ = true;
        } else if (id == 0x80) {
            if (blen < 18) {
                if (error) *error = "RZX input block too small";
                return false;
            }
            const uint32_t nframes = rd32(block + 5);
            start_tstates_ = rd32(block + 10);
            const uint32_t flags = rd32(block + 14);
            if (flags & 1) {
                if (error) *error = "protected/encrypted RZX input not supported";
                return false;
            }
            const uint8_t* fdata = block + 18;
            size_t flen = blen - 18;
            std::vector<uint8_t> decoded;
            if (flags & 2) {
                // Unknown uncompressed size — inflate without expected length.
                if (!inflate(fdata, flen, 0, decoded, error)) return false;
                fdata = decoded.data();
                flen = decoded.size();
            }
            size_t p = 0;
            frames_.clear();
            frames_.reserve(nframes);
            for (uint32_t i = 0; i < nframes; ++i) {
                if (p + 4 > flen) {
                    if (error) *error = "RZX frame data truncated";
                    return false;
                }
                Frame fr;
                fr.fetch_count = rd16(fdata + p);
                const uint16_t in_count = rd16(fdata + p + 2);
                p += 4;
                if (in_count == 0xffff) {
                    fr.repeat = true;
                } else {
                    if (p + in_count > flen) {
                        if (error) *error = "RZX IN bytes truncated";
                        return false;
                    }
                    fr.ins.assign(fdata + p, fdata + p + in_count);
                    p += in_count;
                }
                frames_.push_back(std::move(fr));
            }
            got_input = true;
        }
        // Creator (0x10), security, etc. ignored.
        o += blen;
    }

    if (!snap_ok_) {
        if (error) *error = "RZX has no embedded snapshot";
        return false;
    }
    if (!got_input || frames_.empty()) {
        if (error) *error = "RZX has no input frames";
        return false;
    }
    return true;
}

void SpectrumRzx::start() {
    if (!ok()) return;
    playing_ = true;
    finished_ = false;
    frame_index_ = 0;
    fetches_left_ = 0;
    in_pos_ = 0;
    last_ins_.clear();
    cur_ins_.clear();
}

void SpectrumRzx::stop() {
    playing_ = false;
    finished_ = false;
    fetches_left_ = 0;
    in_pos_ = 0;
}

bool SpectrumRzx::begin_frame() {
    if (!playing_ || finished_) return false;
    if (frame_index_ >= frames_.size()) {
        playing_ = false;
        finished_ = true;
        return false;
    }
    const Frame& fr = frames_[frame_index_];
    fetches_left_ = fr.fetch_count;
    if (fr.repeat) {
        cur_ins_ = last_ins_;
    } else {
        cur_ins_ = fr.ins;
        last_ins_ = fr.ins;
    }
    in_pos_ = 0;
    ++frame_index_;
    return true;
}

void SpectrumRzx::on_m1() {
    if (!playing_ || fetches_left_ == 0) return;
    --fetches_left_;
}

uint8_t SpectrumRzx::next_in() {
    if (!playing_) return 0xff;
    if (in_pos_ < cur_ins_.size()) return cur_ins_[in_pos_++];
    // Over-read: return idle bus.
    return 0xff;
}

}  // namespace dsp
