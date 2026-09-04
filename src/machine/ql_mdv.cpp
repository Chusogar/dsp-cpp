#include "machine/ql_mdv.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <zlib.h>

namespace dsp {
namespace {

constexpr int kHdrOff = 12;
constexpr int kBlockPreamble = 28;
constexpr uint16_t kQdosExtra = 0xfb4a;
const char kQdosInline[] = "]!QDOS File Header";

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void put_be16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
void put_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}
void put_le16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}

uint16_t mdv_sum(const uint8_t* p, int len) {
    uint16_t v = 0x0f0f;
    for (int i = 0; i < len; i++) v = uint16_t(v + p[i]);
    return v;
}

std::string to_lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ends_with(const std::string& s, const char* ext) {
    const std::string e = ext;
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n <= 0) return false;
    in.seekg(0);
    out.resize(size_t(n));
    in.read(reinterpret_cast<char*>(out.data()), n);
    return bool(in);
}

struct ZipLocal {
    uint32_t data_off = 0;
    uint32_t comp = 0;
    uint32_t raw = 0;
    uint16_t method = 0;
    uint16_t extra_id = 0;
    std::vector<uint8_t> extra;
    std::string name;
};

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool inflate_raw(const uint8_t* src, uint32_t slen, std::vector<uint8_t>& dest) {
    dest.assign(dest.size(), 0);
    z_stream st{};
    st.next_in = const_cast<Bytef*>(src);
    st.avail_in = slen;
    st.next_out = dest.data();
    st.avail_out = uInt(dest.size());
    if (inflateInit2(&st, -MAX_WBITS) != Z_OK) return false;
    const int rc = inflate(&st, Z_FINISH);
    inflateEnd(&st);
    return rc == Z_STREAM_END;
}

bool parse_zip(const std::vector<uint8_t>& zip, std::vector<ZipLocal>& files, std::string* error) {
    if (zip.size() < 22) {
        if (error) *error = "qlpak is too small";
        return false;
    }
    size_t eocd = 0;
    bool found = false;
    const size_t limit = std::min<size_t>(zip.size(), 0xffff + 22);
    for (size_t i = 22; i <= limit; i++) {
        const size_t pos = zip.size() - i;
        if (u32(&zip[pos]) == 0x06054b50) {
            eocd = pos;
            found = true;
            break;
        }
    }
    if (!found) {
        if (error) *error = "qlpak is not a zip archive";
        return false;
    }
    uint16_t entries = u16(&zip[eocd + 10]);
    uint32_t cd = u32(&zip[eocd + 16]);
    size_t pos = cd;
    files.clear();
    for (uint16_t n = 0; n < entries; n++) {
        if (pos + 46 > zip.size() || u32(&zip[pos]) != 0x02014b50) {
            if (error) *error = "qlpak central directory is corrupt";
            return false;
        }
        ZipLocal z;
        z.method = u16(&zip[pos + 10]);
        z.comp = u32(&zip[pos + 20]);
        z.raw = u32(&zip[pos + 24]);
        const uint16_t nlen = u16(&zip[pos + 28]);
        const uint16_t elen = u16(&zip[pos + 30]);
        const uint16_t clen = u16(&zip[pos + 32]);
        const uint32_t local = u32(&zip[pos + 42]);
        z.name.assign(reinterpret_cast<const char*>(&zip[pos + 46]), nlen);
        const uint8_t* extra = &zip[pos + 46 + nlen];
        for (int off = 0; off + 4 <= elen;) {
            const uint16_t id = u16(extra + off);
            const uint16_t sz = u16(extra + off + 2);
            if (id == kQdosExtra) {
                z.extra_id = id;
                z.extra.assign(extra + off + 4, extra + off + 4 + std::min<int>(sz, elen - off - 4));
            }
            off += 4 + sz;
        }
        if (local + 30 <= zip.size() && u32(&zip[local]) == 0x04034b50) {
            const uint16_t ln = u16(&zip[local + 26]);
            const uint16_t le = u16(&zip[local + 28]);
            z.data_off = local + 30u + ln + le;
        }
        files.push_back(std::move(z));
        pos += 46u + nlen + elen + clen;
    }
    return true;
}

bool extract_zip_file(const std::vector<uint8_t>& zip, const ZipLocal& z, std::vector<uint8_t>& out) {
    if (z.data_off + z.comp > zip.size()) return false;
    if (z.method == 0) {
        out.assign(zip.begin() + z.data_off, zip.begin() + z.data_off + z.comp);
        return true;
    }
    if (z.method != 8) return false;
    out.resize(z.raw);
    return inflate_raw(&zip[z.data_off], z.comp, out);
}

void format_qlay(std::vector<uint8_t>& image, const char* medium) {
    image.assign(kMdvImageBytes, 0);
    char name[10];
    std::memset(name, ' ', 10);
    const int n = int(std::min<size_t>(std::strlen(medium), 10));
    std::memcpy(name, medium, size_t(n));
    const uint16_t rnd = 0x1234;
    for (int s = 0; s < kMdvSectors; s++) {
        uint8_t* sec = &image[size_t(s) * kMdvSectorBytes];
        sec[10] = 0xff;
        sec[11] = 0xff;
        sec[kHdrOff] = 0xff;
        sec[kHdrOff + 1] = uint8_t(s);
        std::memcpy(sec + kHdrOff + 2, name, 10);
        put_le16(sec + kHdrOff + 12, rnd);
        put_le16(sec + kHdrOff + 14, mdv_sum(sec + kHdrOff, 14));
        sec[kBlockPreamble + 10] = 0xff;
        sec[kBlockPreamble + 11] = 0xff;
        sec[40] = 0xfd;
        sec[41] = 0x00;
        put_le16(sec + 42, mdv_sum(sec + 40, 2));
        sec[44 + 6] = 0xff;
        sec[44 + 7] = 0xff;
        std::memset(sec + 52, 0, 512);
        put_le16(sec + 564, mdv_sum(sec + 52, 512));
        std::memset(sec + 566, 0x5a, 120);
    }
    uint8_t* map = &image[52];
    for (int i = 0; i < 256; i++) {
        map[2 * i] = 0xfd;
        map[2 * i + 1] = 0x00;
    }
    map[0] = 0xf8;
    map[2] = 0x00;
    image[40] = 0x80;
    put_le16(&image[42], mdv_sum(&image[40], 2));
    put_le16(&image[564], mdv_sum(&image[52], 512));

    uint8_t* dir = &image[kMdvSectorBytes + 52];
    image[kMdvSectorBytes + 40] = 0x00;
    image[kMdvSectorBytes + 41] = 0x00;
    put_le16(&image[kMdvSectorBytes + 42], mdv_sum(&image[kMdvSectorBytes + 40], 2));
    put_be32(dir, 64);
    put_le16(dir + 512, mdv_sum(dir, 512));
}

uint8_t* sector_by_number(std::vector<uint8_t>& image, int sno) {
    for (int i = 0; i < kMdvSectors; i++) {
        uint8_t* sec = &image[size_t(i) * kMdvSectorBytes];
        if (sec[kHdrOff + 1] == uint8_t(sno)) return sec;
    }
    return nullptr;
}

int mapping(const std::vector<uint8_t>& image, int sno) {
    const uint8_t* map = &image[52];
    return (int(map[2 * sno]) << 8) | map[2 * sno + 1];
}

void set_mapping(std::vector<uint8_t>& image, int sno, int file, int block) {
    uint8_t* map = &image[52];
    map[2 * sno] = uint8_t(file);
    map[2 * sno + 1] = uint8_t(block);
    put_le16(&image[564], mdv_sum(map, 512));
}

uint8_t* alloc_sector(std::vector<uint8_t>& image, int file, int block, int hint) {
    for (int i = 0; i < kMdvSectors; i++) {
        int sno = hint - 13 - i;
        if (sno < 0) sno += kMdvSectors;
        if ((mapping(image, sno) & 0xff00) != 0xfd00) continue;
        set_mapping(image, sno, file, block);
        uint8_t* sec = sector_by_number(image, sno);
        if (!sec) continue;
        sec[40] = uint8_t(file);
        sec[41] = uint8_t(block);
        put_le16(sec + 42, mdv_sum(sec + 40, 2));
        return sec;
    }
    return nullptr;
}

void write_dir_length(std::vector<uint8_t>& image, uint32_t bytes) {
    uint8_t* dir0 = &image[kMdvSectorBytes + 52];
    put_be32(dir0, bytes);
    put_le16(dir0 + 512, mdv_sum(dir0, 512));
}

uint32_t dir_length(const std::vector<uint8_t>& image) { return be32(&image[kMdvSectorBytes + 52]); }

bool add_ql_file(std::vector<uint8_t>& image, const std::string& name, const uint8_t* data,
                 uint32_t size, uint8_t type, uint32_t dataspace) {
    std::string fname = name;
    if (const size_t slash = fname.find_last_of("/\\"); slash != std::string::npos) {
        fname = fname.substr(slash + 1);
    }
    for (char& c : fname) {
        if (c == '.') c = '_';
    }
    if (fname.empty() || fname[0] == '.' || ends_with(to_lower(fname), ".qcf")) return true;

    uint8_t header[64]{};
    put_be32(header, size + 64);
    header[5] = type;
    put_be32(header + 6, dataspace);
    const uint16_t nlen = uint16_t(std::min<size_t>(fname.size(), 36));
    put_be16(header + 14, nlen);
    std::memcpy(header + 16, fname.data(), nlen);

    uint32_t entries = dir_length(image) / 64;
    if ((entries & 7) == 7) {
        if (!alloc_sector(image, 0, int(entries / 8) + 1, int(entries / 8))) return false;
    }
    const int file_index = int(entries);
    const int dir_sec = file_index / 8;
    uint8_t* dsec = nullptr;
    if (dir_sec == 0) {
        dsec = &image[kMdvSectorBytes];
    } else {
        for (int s = 0; s < kMdvSectors; s++) {
            uint8_t* sec = &image[size_t(s) * kMdvSectorBytes];
            if (sec[40] == 0 && sec[41] == uint8_t(dir_sec)) {
                dsec = sec;
                break;
            }
        }
    }
    if (!dsec) return false;
    std::memcpy(dsec + 52 + (file_index & 7) * 64, header, 64);
    put_le16(dsec + 564, mdv_sum(dsec + 52, 512));
    write_dir_length(image, (entries + 1) * 64);

    uint32_t remain = size + 64;
    const uint8_t* src = data;
    int last = 0;
    int block = 0;
    while (remain) {
        const int room = block == 0 ? 512 - 64 : 512;
        const int chunk = int(std::min<uint32_t>(uint32_t(room), remain));
        uint8_t* sec = alloc_sector(image, file_index, block, last);
        if (!sec) return false;
        std::memset(sec + 52, 0, 512);
        if (block == 0) {
            std::memcpy(sec + 52, header, 64);
            std::memcpy(sec + 52 + 64, src, size_t(std::min(chunk, int(size))));
            src += std::min(chunk, int(size));
        } else {
            std::memcpy(sec + 52, src, size_t(chunk));
            src += chunk;
        }
        put_le16(sec + 564, mdv_sum(sec + 52, 512));
        last = sec[kHdrOff + 1];
        remain -= uint32_t(chunk);
        block++;
    }
    return true;
}

// Q-emuLator prepends "]!QDOS File Header" + reserved + length-in-words.
// 15 words (30 bytes) store QDOS header bytes 4-13; 22 words (44 bytes)
// add a 14-byte microdrive sector header used by copy-protection.
bool strip_qemul_header(std::vector<uint8_t>& body, uint8_t& type, uint32_t& dataspace) {
    const size_t ilen = std::strlen(kQdosInline);
    if (body.size() < 30 || std::memcmp(body.data(), kQdosInline, ilen) != 0) return false;
    const int words = body[19];
    const int total = words * 2;
    if ((words != 15 && words != 22) || body.size() < size_t(total)) return false;
    type = body[21];
    dataspace = be32(body.data() + 22);
    body.erase(body.begin(), body.begin() + total);
    return true;
}

void rewrite_flp_to_mdv(std::vector<uint8_t>& body) {
    int ascii = 0;
    for (uint8_t b : body) {
        if (b == '\n' || b == '\r' || (b >= 0x20 && b < 0x7f)) ascii++;
    }
    if (body.empty() || ascii * 10 < int(body.size()) * 8) return;
    auto replace = [&](const char* from, const char* to) {
        const size_t n = std::strlen(from);
        for (size_t i = 0; i + n <= body.size(); i++) {
            bool match = true;
            for (size_t k = 0; k < n; k++) {
                const char c = char(std::tolower(static_cast<unsigned char>(body[i + k])));
                if (c != from[k]) {
                    match = false;
                    break;
                }
            }
            if (!match) continue;
            for (size_t k = 0; k < n; k++) body[i + k] = uint8_t(to[k]);
        }
    };
    replace("flp1_", "mdv1_");
    replace("flp2_", "mdv2_");
}

bool qlpak_to_qlay(const std::vector<uint8_t>& zip, std::vector<uint8_t>& qlay, std::string* error) {
    std::vector<ZipLocal> files;
    if (!parse_zip(zip, files, error)) return false;
    format_qlay(qlay, "QLPAK");
    int stored = 0;
    for (const ZipLocal& z : files) {
        if (z.name.empty() || z.name.back() == '/') continue;
        std::vector<uint8_t> body;
        if (!extract_zip_file(zip, z, body)) continue;

        uint8_t type = 0;
        uint32_t dataspace = 0;
        if (!strip_qemul_header(body, type, dataspace) && z.extra.size() >= 72) {
            const uint8_t* fh = z.extra.data() + 8;
            type = fh[5];
            dataspace = be32(fh + 6);
            if (be32(fh) > 0 && be32(fh) <= uint32_t(body.size())) {
                body.resize(be32(fh));
            }
        }
        rewrite_flp_to_mdv(body);

        if (!add_ql_file(qlay, z.name, body.data(), uint32_t(body.size()), type, dataspace)) {
            if (error) *error = "qlpak does not fit on a 255-sector cartridge";
            return false;
        }
        stored++;
    }
    if (stored == 0) {
        if (error) *error = "qlpak contains no QL files";
        return false;
    }
    return true;
}

}  // namespace

QlMicrodrive::QlMicrodrive() { reset(); }

void QlMicrodrive::ensure_tape() {
    if (!left_.empty()) return;
    left_.assign(kMdvTapePairs, 0);
    right_.assign(kMdvTapePairs, 0);
    erased_.assign(kMdvTapePairs, 1);
}

void QlMicrodrive::reset() {
    ensure_tape();
    clk_ = false;
    comms_in_ = false;
    comms_out_ = false;
    erase_ = false;
    write_ = false;
    motor_ = false;
}

void QlMicrodrive::tape_from_qlay(const uint8_t* image) {
    left_.assign(kMdvTapePairs, 0);
    right_.assign(kMdvTapePairs, 0);
    erased_.assign(kMdvTapePairs, 1);
    for (int sector = 0; sector < kMdvSectors; sector++) {
        const uint8_t* sec = image + sector * kMdvSectorBytes;
        const int base = sector * kMdvPairsPerSector;
        if (sec[10] != 0xff || sec[11] != 0xff) continue;
        int pos = base;
        for (int i = 0; i < kMdvPairsHeader; i++, pos++) {
            left_[size_t(pos)] = sec[2 * i];
            right_[size_t(pos)] = sec[2 * i + 1];
            erased_[size_t(pos)] = 0;
        }
        if (sec[kBlockPreamble + 10] != 0xff || sec[kBlockPreamble + 11] != 0xff) continue;
        pos = base + kMdvPairsHeader + kMdvPairsBlockGap;
        for (int i = 0; i < kMdvPairsBlock; i++, pos++) {
            left_[size_t(pos)] = sec[kBlockPreamble + 2 * i];
            right_[size_t(pos)] = sec[kBlockPreamble + 2 * i + 1];
            erased_[size_t(pos)] = 0;
        }
    }
    loaded_ = true;
    bit_ = 0;
    pair_ = 0;
}

bool QlMicrodrive::load_image(const std::vector<uint8_t>& qlay, std::string* error) {
    if (qlay.size() < kMdvImageBytes) {
        if (error) *error = "microdrive image is not a 174930-byte QLAY cartridge";
        return false;
    }
    tape_from_qlay(qlay.data());
    return true;
}

bool QlMicrodrive::load_file(const std::string& path, std::string* error) {
    std::vector<uint8_t> qlay;
    if (!load_ql_cartridge(path, qlay, error)) return false;
    return load_image(qlay, error);
}

void QlMicrodrive::clk_w(int state) {
    const bool level = state != 0;
    if (clk_ && !level) {
        comms_out_ = comms_in_;
        motor_ = comms_out_ && loaded_;
    }
    clk_ = level;
}

void QlMicrodrive::tick_bit() {
    if (!motor_) return;
    if (write_) {
        if (bit_ == 0) {
            const uint16_t pair = tx_pop_cb_ ? tx_pop_cb_() : uint16_t(0);
            left_[size_t(pair_)] = uint8_t(pair >> 8);
            right_[size_t(pair_)] = uint8_t(pair);
            erased_[size_t(pair_)] = 0;
        }
    } else if (erase_) {
        erased_[size_t(pair_)] = 1;
        left_[size_t(pair_)] = 0;
        right_[size_t(pair_)] = 0;
    }
    bit_++;
    if (bit_ == 8) {
        bit_ = 0;
        pair_++;
        if (pair_ >= kMdvTapePairs) pair_ = 0;
    }
}

int QlMicrodrive::data1() const {
    if (!comms_out_ || write_ || erased_[size_t(pair_)]) return 0;
    return (left_[size_t(pair_)] >> (7 - bit_)) & 1;
}

int QlMicrodrive::data2() const {
    if (!comms_out_ || write_ || erased_[size_t(pair_)]) return 0;
    return (right_[size_t(pair_)] >> (7 - bit_)) & 1;
}

int QlMicrodrive::gap() const {
    if (!comms_out_) return 1;
    if (!loaded_) return 1;
    return erased_[size_t(pair_)] ? 1 : 0;
}

bool load_ql_cartridge(const std::string& path, std::vector<uint8_t>& qlay, std::string* error) {
    std::vector<uint8_t> raw;
    if (!read_file(path, raw)) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    if (raw.size() >= 2 && raw[0] == 'P' && raw[1] == 'K') {
        return qlpak_to_qlay(raw, qlay, error);
    }
    if (raw.size() >= kMdvImageBytes && raw.size() % kMdvSectorBytes == 0) {
        qlay.assign(raw.begin(), raw.begin() + kMdvImageBytes);
        return true;
    }
    if (error) *error = path + " is not a QLAY .mdv or ZIP .qlpak";
    return false;
}

}  // namespace dsp
