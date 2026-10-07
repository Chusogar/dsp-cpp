// PlayStation CD-ROM controller + disc image loader.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "machine/psx_cdrom.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <fstream>
#include <filesystem>

namespace dsp {
namespace {

constexpr int kCpuClock = 33868800;

bool ends_with_ci(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) !=
            std::tolower(static_cast<unsigned char>(ext[i]))) {
            return false;
        }
    }
    return true;
}

bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        if (error) *error = "empty file " + path;
        return false;
    }
    out.resize(size_t(size));
    in.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

}  // namespace

void PsxCdrom::reset() {
    param_.clear();
    response_.clear();
    current_sector_.clear();
    last_read_sector_.clear();
    busy_ = false;
    ie_ = 0;
    if_ = 0;
    index_ = 0;
    stat_ = 0;
    seek_loc_ = 0;
    read_loc_ = 0;
    double_speed_ = false;
    xa_adpcm_ = false;
    sector_raw_ = false;
    ignore_bit_ = false;
    xa_filter_ = false;
    report_ = false;
    auto_pause_ = false;
    cdda_ = false;
    muted_ = false;
    lid_open_ = tracks_.empty();
    mode_ = Mode::Idle;
    counter_ = 0;
    irq_queue_.clear();
}

bool PsxCdrom::load_disc(const std::string& path, std::string* error) {
    namespace fs = std::filesystem;
    tracks_.clear();
    track_data_.clear();

    if (ends_with_ci(path, ".bin")) {
        std::vector<uint8_t> data;
        if (!read_file_bytes(path, data, error)) return false;
        PsxCdTrack t;
        t.file = path;
        t.size = int64_t(data.size());
        t.number = 1;
        t.lba = int(data.size() / kBytesPerSector);
        t.lba_start = 150;
        t.lba_end = t.lba;
        t.is_audio = false;
        tracks_.push_back(t);
        track_data_.push_back(std::move(data));
    } else if (ends_with_ci(path, ".cue")) {
        std::ifstream cue(path);
        if (!cue) {
            if (error) *error = "cannot open " + path;
            return false;
        }
        const fs::path dir = fs::path(path).parent_path();
        std::string line;
        int lba_counter = 0;
        uint8_t number = 0;
        while (std::getline(cue, line)) {
            // Trim
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.rfind("FILE", 0) != 0) continue;
            const auto q1 = line.find('"');
            const auto q2 = line.find('"', q1 == std::string::npos ? 0 : q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) continue;
            const std::string fname = line.substr(q1 + 1, q2 - q1 - 1);
            const std::string full = (dir / fname).string();
            std::vector<uint8_t> data;
            if (!read_file_bytes(full, data, error)) return false;
            const int lba = int(data.size() / kBytesPerSector);
            int lba_start = lba_counter + 150;
            number++;
            if (!tracks_.empty()) lba_start += 150;
            const int lba_end = lba_counter + lba;
            lba_counter += lba;
            std::string track_line;
            std::getline(cue, track_line);
            const bool audio = track_line.find("AUDIO") != std::string::npos;
            PsxCdTrack t;
            t.file = full;
            t.size = int64_t(data.size());
            t.number = number;
            t.lba = lba;
            t.lba_start = lba_start;
            t.lba_end = lba_end;
            t.is_audio = audio;
            tracks_.push_back(t);
            track_data_.push_back(std::move(data));
        }
        if (tracks_.empty()) {
            if (error) *error = "no tracks in cue " + path;
            return false;
        }
    } else {
        if (error) *error = "CD image must be .bin or .cue";
        return false;
    }
    lid_open_ = false;
    reset();
    lid_open_ = false;
    return true;
}

void PsxCdrom::enqueue_irq(uint8_t irq, int delay) {
    irq_queue_.push_back(DelayedIrq{delay, irq});
}

uint8_t PsxCdrom::dec_to_bcd(uint8_t v) { return uint8_t(v + 6 * (v / 10)); }
int PsxCdrom::bcd_to_dec(uint8_t v) { return v - 6 * (v >> 4); }

void PsxCdrom::lba_to_msf(int lba, uint8_t& mm, uint8_t& ss, uint8_t& ff) {
    ff = uint8_t(lba % 75);
    lba /= 75;
    ss = uint8_t(lba % 60);
    lba /= 60;
    mm = uint8_t(lba);
}

const PsxCdTrack& PsxCdrom::track_from_loc(int loc) const {
    for (const auto& t : tracks_) {
        if (t.lba_end >= loc) return t;
    }
    return tracks_[0];
}

int PsxCdrom::total_lba() const {
    int lba = 150;
    for (const auto& t : tracks_) lba += t.lba;
    return lba;
}

bool PsxCdrom::is_audio_cd() const {
    return !tracks_.empty() && tracks_[0].is_audio;
}

bool PsxCdrom::read_sector(int loc, uint8_t out[kBytesPerSector]) {
    if (tracks_.empty()) {
        std::memset(out, 0, kBytesPerSector);
        return false;
    }
    const PsxCdTrack& track = track_from_loc(loc);
    track_change_ = (loc == track.lba_end);
    int position = loc - track.lba_start;
    if (position < 0) position = 0;
    const size_t idx = size_t(track.number - 1);
    const auto& data = track_data_[idx];
    const size_t off = size_t(position) * kBytesPerSector;
    if (off + kBytesPerSector > data.size()) {
        std::memset(out, 0, kBytesPerSector);
        return false;
    }
    std::memcpy(out, data.data() + off, kBytesPerSector);
    return true;
}

bool PsxCdrom::tick(int cycles) {
    counter_ += cycles;
    if (!irq_queue_.empty()) {
        irq_queue_.front().delay -= cycles;
    }
    if (!irq_queue_.empty() && if_ == 0 && irq_queue_.front().delay <= 0) {
        if_ |= irq_queue_.front().interrupt;
        irq_queue_.pop_front();
    }
    if ((if_ & ie_) != 0) {
        busy_ = false;
        return true;
    }

    switch (mode_) {
        case Mode::Idle:
            counter_ = 0;
            return false;
        case Mode::Seek:
            if (counter_ < kCpuClock / 3 || !irq_queue_.empty()) return false;
            counter_ = 0;
            mode_ = Mode::Idle;
            stat_ = uint8_t(stat_ & ~0x40);
            response_.push_back(stat_);
            enqueue_irq(2);
            break;
        case Mode::Read:
        case Mode::Play: {
            const int period = kCpuClock / (double_speed_ ? 150 : 75);
            if (counter_ < period || !irq_queue_.empty()) return false;
            counter_ = 0;
            uint8_t sector[kBytesPerSector];
            read_sector(read_loc_++, sector);
            if (mode_ == Mode::Play) {
                // CDDA: audio path stubbed; optional report interrupt.
                if (auto_pause_ && track_change_) {
                    response_.push_back(stat_);
                    enqueue_irq(4, 1);
                    stat_ = 0x2;
                    mode_ = Mode::Idle;
                }
                return false;
            }
            hdr_mm_ = sector[12];
            hdr_ss_ = sector[13];
            hdr_ff_ = sector[14];
            hdr_mode_ = sector[15];
            sub_file_ = sector[16];
            sub_channel_ = sector[17];
            sub_mode_ = sector[18];
            sub_coding_ = sector[19];
            // Skip XA realtime audio delivery (SPU stub).
            const bool form2 = (sub_mode_ & 0x20) != 0;
            const bool is_audio = (sub_mode_ & 0x4) != 0;
            const bool realtime = (sub_mode_ & 0x40) != 0;
            if (xa_adpcm_ && form2 && realtime && is_audio) {
                if (xa_filter_ && (filter_file_ != sub_file_ || filter_channel_ != sub_channel_)) {
                    return false;
                }
                return false;
            }
            if (!sector_raw_) {
                last_read_sector_.fill(sector + 24, 0x800);
            } else {
                last_read_sector_.fill(sector + 12, kBytesPerSector - 12);
            }
            response_.push_back(stat_);
            enqueue_irq(1);
            break;
        }
        case Mode::Toc: {
            const int period = kCpuClock / (double_speed_ ? 150 : 75);
            if (counter_ < period || !irq_queue_.empty()) return false;
            mode_ = Mode::Idle;
            response_.push_back(stat_);
            enqueue_irq(2);
            counter_ = 0;
            break;
        }
    }
    return false;
}

uint8_t PsxCdrom::status_reg() const {
    int stat = 0;
    // ProjectPSX has a precedence bug (`isBusy ? 1 : 0 << 7`); use correct busy bit.
    if (busy_) stat |= 1 << 7;
    if (current_sector_.has_data()) stat |= 1 << 6;
    if (!response_.empty()) stat |= 1 << 5;
    if (param_.size() < 16) stat |= 1 << 4;
    if (param_.empty()) stat |= 1 << 3;
    if (xa_adpcm_) stat |= 1 << 2;
    stat |= index_;
    return uint8_t(stat);
}

uint32_t PsxCdrom::load(uint32_t addr) {
    switch (addr) {
        case 0x1F801800: return status_reg();
        case 0x1F801801:
            if (!response_.empty()) {
                const uint8_t b = response_.front();
                response_.pop_front();
                return b;
            }
            return 0xFF;
        case 0x1F801802:
            return current_sector_.has_data() ? current_sector_.read_byte() : 0;
        case 0x1F801803:
            if (index_ == 0 || index_ == 2) return uint32_t(0xE0 | ie_);
            if (index_ == 1 || index_ == 3) return uint32_t(0xE0 | if_);
            return 0;
        default: return 0;
    }
}

void PsxCdrom::write(uint32_t addr, uint32_t value) {
    switch (addr) {
        case 0x1F801800:
            index_ = uint8_t(value & 3);
            break;
        case 0x1F801801:
            if (index_ == 0) execute_command(value);
            break;
        case 0x1F801802:
            if (index_ == 0) param_.push_back(uint8_t(value));
            else if (index_ == 1) ie_ = uint8_t(value & 0x1F);
            break;
        case 0x1F801803:
            if (index_ == 0) {
                if ((value & 0x80) != 0) {
                    if (current_sector_.has_data()) return;
                    current_sector_.fill(last_read_sector_.bytes(), last_read_sector_.byte_size());
                } else {
                    current_sector_.clear();
                }
            } else if (index_ == 1) {
                if_ &= uint8_t(~(value & 0x1F));
                if (!irq_queue_.empty() && irq_queue_.front().delay <= 0) {
                    if_ |= irq_queue_.front().interrupt;
                    irq_queue_.pop_front();
                }
                if ((value & 0x40) == 0x40) param_.clear();
            }
            break;
        default:
            break;
    }
}

void PsxCdrom::dma_read(uint32_t* dest, int words) {
    current_sector_.read_words(dest, words);
}

void PsxCdrom::execute_command(uint32_t value) {
    irq_queue_.clear();
    response_.clear();
    busy_ = true;
    switch (value) {
        case 0x01: cmd_getstat(); break;
        case 0x02: cmd_setloc(); break;
        case 0x03: cmd_play(); break;
        case 0x06: cmd_readn(); break;
        case 0x07: cmd_motor_on(); break;
        case 0x08: cmd_stop(); break;
        case 0x09: cmd_pause(); break;
        case 0x0A: cmd_init(); break;
        case 0x0B: cmd_mute(); break;
        case 0x0C: cmd_demute(); break;
        case 0x0D: cmd_setfilter(); break;
        case 0x0E: cmd_setmode(); break;
        case 0x10: cmd_getlocl(); break;
        case 0x11: cmd_getlocp(); break;
        case 0x12: cmd_setsession(); break;
        case 0x13: cmd_gettn(); break;
        case 0x14: cmd_gettd(); break;
        case 0x15: cmd_seekl(); break;
        case 0x16: cmd_seekp(); break;
        case 0x19: cmd_test(); break;
        case 0x1A: cmd_getid(); break;
        case 0x1B: cmd_reads(); break;
        case 0x1E: cmd_readtoc(); break;
        case 0x1F: cmd_videocd(); break;
        default:
            if (value >= 0x50 && value <= 0x57) enqueue_irq(5);
            else busy_ = false;
            break;
    }
}

void PsxCdrom::cmd_getstat() {
    if (!lid_open_) {
        stat_ = uint8_t(stat_ & ~0x18);
        stat_ |= 0x2;
    }
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_setloc() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    const uint8_t mm = param_.front(); param_.pop_front();
    const uint8_t ss = param_.front(); param_.pop_front();
    const uint8_t ff = param_.front(); param_.pop_front();
    seek_loc_ = bcd_to_dec(ff) + bcd_to_dec(ss) * 75 + bcd_to_dec(mm) * 60 * 75;
    if (seek_loc_ < 0) seek_loc_ = 0;
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_play() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    if (!param_.empty() && param_.front() != 0) {
        const int track = bcd_to_dec(param_.front());
        param_.pop_front();
        if (is_audio_cd()) read_loc_ = seek_loc_ = tracks_[size_t(track - 1)].lba_start;
        else if (track < int(tracks_.size())) read_loc_ = seek_loc_ = tracks_[size_t(track)].lba_start;
    } else {
        read_loc_ = seek_loc_;
    }
    stat_ = 0x82;
    mode_ = Mode::Play;
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_readn() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    read_loc_ = seek_loc_;
    stat_ = 0x22;
    response_.push_back(stat_);
    enqueue_irq(3);
    mode_ = Mode::Read;
}

void PsxCdrom::cmd_motor_on() {
    stat_ = 0x2;
    response_.push_back(stat_);
    enqueue_irq(3);
    response_.push_back(stat_);
    enqueue_irq(2);
}

void PsxCdrom::cmd_stop() {
    stat_ = 0x2;
    response_.push_back(stat_);
    enqueue_irq(3);
    stat_ = 0;
    response_.push_back(stat_);
    enqueue_irq(2);
    mode_ = Mode::Idle;
}

void PsxCdrom::cmd_pause() {
    response_.push_back(stat_);
    enqueue_irq(3);
    stat_ = 0x2;
    mode_ = Mode::Idle;
    response_.push_back(stat_);
    enqueue_irq(2);
}

void PsxCdrom::cmd_init() {
    stat_ = 0x2;
    response_.push_back(stat_);
    enqueue_irq(3);
    response_.push_back(stat_);
    enqueue_irq(2);
}

void PsxCdrom::cmd_mute() {
    muted_ = true;
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_demute() {
    muted_ = false;
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_setfilter() {
    filter_file_ = param_.front(); param_.pop_front();
    filter_channel_ = param_.front(); param_.pop_front();
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_setmode() {
    const uint8_t mode = param_.front(); param_.pop_front();
    double_speed_ = ((mode >> 7) & 1) != 0;
    xa_adpcm_ = ((mode >> 6) & 1) != 0;
    sector_raw_ = ((mode >> 5) & 1) != 0;
    ignore_bit_ = ((mode >> 4) & 1) != 0;
    xa_filter_ = ((mode >> 3) & 1) != 0;
    report_ = ((mode >> 2) & 1) != 0;
    auto_pause_ = ((mode >> 1) & 1) != 0;
    cdda_ = (mode & 1) != 0;
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_getlocl() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    response_.push_back(hdr_mm_);
    response_.push_back(hdr_ss_);
    response_.push_back(hdr_ff_);
    response_.push_back(hdr_mode_);
    response_.push_back(sub_file_);
    response_.push_back(sub_channel_);
    response_.push_back(sub_mode_);
    response_.push_back(sub_coding_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_getlocp() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    const PsxCdTrack& track = track_from_loc(read_loc_);
    uint8_t mm, ss, ff, amm, ass, aff;
    lba_to_msf(read_loc_ - track.lba_start, mm, ss, ff);
    lba_to_msf(read_loc_, amm, ass, aff);
    response_.push_back(track.number);
    response_.push_back(1);
    response_.push_back(dec_to_bcd(mm));
    response_.push_back(dec_to_bcd(ss));
    response_.push_back(dec_to_bcd(ff));
    response_.push_back(dec_to_bcd(amm));
    response_.push_back(dec_to_bcd(ass));
    response_.push_back(dec_to_bcd(aff));
    enqueue_irq(3);
}

void PsxCdrom::cmd_setsession() {
    param_.clear();
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    stat_ = 0x42;
    response_.push_back(stat_);
    enqueue_irq(3);
    response_.push_back(stat_);
    enqueue_irq(2);
}

void PsxCdrom::cmd_gettn() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    response_.push_back(stat_);
    response_.push_back(1);
    response_.push_back(dec_to_bcd(uint8_t(tracks_.size())));
    enqueue_irq(3);
}

void PsxCdrom::cmd_gettd() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    const int track = bcd_to_dec(param_.front());
    param_.pop_front();
    uint8_t mm, ss, ff;
    if (track == 0) lba_to_msf(total_lba(), mm, ss, ff);
    else lba_to_msf(tracks_[size_t(track - 1)].lba_start, mm, ss, ff);
    response_.push_back(stat_);
    response_.push_back(dec_to_bcd(mm));
    response_.push_back(dec_to_bcd(ss));
    enqueue_irq(3);
}

void PsxCdrom::cmd_seekl() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    read_loc_ = seek_loc_;
    stat_ = 0x42;
    mode_ = Mode::Seek;
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_seekp() { cmd_seekl(); }

void PsxCdrom::cmd_test() {
    const uint8_t command = param_.front();
    param_.pop_front();
    response_.clear();
    switch (command) {
        case 0x04:
            stat_ = 0x2;
            response_.push_back(stat_);
            enqueue_irq(3);
            break;
        case 0x05:
            response_.push_back(0);
            response_.push_back(0);
            enqueue_irq(3);
            break;
        case 0x20:
            response_.push_back(0x94);
            response_.push_back(0x09);
            response_.push_back(0x19);
            response_.push_back(0xC0);
            enqueue_irq(3);
            break;
        case 0x22: {
            const char* s = "for US/AEP";
            for (const char* p = s; *p; ++p) response_.push_back(uint8_t(*p));
            enqueue_irq(3);
            break;
        }
        case 0x60:
            response_.push_back(0);
            enqueue_irq(3);
            break;
        default:
            break;
    }
}

void PsxCdrom::cmd_getid() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    stat_ = 0x42;
    response_.push_back(stat_);
    enqueue_irq(3);
    if (is_audio_cd()) {
        const uint8_t r[] = {0x0A, 0x90, 0, 0, 0, 0, 0, 0};
        for (uint8_t b : r) response_.push_back(b);
        enqueue_irq(5);
        return;
    }
    // Region from license sector (LBA 4): Amer/Euro/Japa → SCEA/SCEE/SCEI.
    // Falls back to SCEA when the sector cannot be read.
    uint8_t region = 'A';
    uint8_t sector[kBytesPerSector];
    if (read_sector(4 + 150, sector)) {
        // Mode2 Form1 user data at offset 24.
        const char* text = reinterpret_cast<const char*>(sector + 24);
        if (std::strstr(text, "Europe") != nullptr) region = 'E';
        else if (std::strstr(text, "Japan") != nullptr) region = 'I';
        else if (std::strstr(text, "Amer") != nullptr) region = 'A';
    }
    const uint8_t r[] = {0x02, 0x00, 0x20, 0x00, 0x53, 0x43, 0x45, region};
    for (uint8_t b : r) response_.push_back(b);
    enqueue_irq(2);
}

void PsxCdrom::cmd_reads() { cmd_readn(); }

void PsxCdrom::cmd_readtoc() {
    if (lid_open_) {
        response_.push_back(0x11); response_.push_back(0x80);
        enqueue_irq(5); return;
    }
    mode_ = Mode::Toc;
    response_.push_back(stat_);
    enqueue_irq(3);
}

void PsxCdrom::cmd_videocd() {
    response_.push_back(0x11);
    response_.push_back(0x40);
    enqueue_irq(5);
}

}  // namespace dsp
