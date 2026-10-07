// PlayStation CD-ROM controller + MODE2/2352 disc image loader.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace dsp {

struct PsxCdTrack {
    std::string file;
    int64_t size = 0;
    uint8_t number = 1;
    int lba = 0;        // length in sectors
    int lba_start = 0;  // absolute LBA including 150 pregap
    int lba_end = 0;
    bool is_audio = false;
};

class PsxCdrom {
public:
    static constexpr int kBytesPerSector = 2352;

    void reset();

    // Load a raw .bin (MODE2/2352) or .cue referencing such bins.
    bool load_disc(const std::string& path, std::string* error);

    uint32_t load(uint32_t addr);
    void write(uint32_t addr, uint32_t value);

    // Advances controller; returns true when CDROM IRQ should fire.
    bool tick(int cycles);

    // DMA channel 3: copy `words` 32-bit words from data FIFO to caller buffer.
    void dma_read(uint32_t* dest, int words);

    // XA / CD-DA decoded samples → SPU CD input (interleaved stereo S16 @ 44100).
    using CdAudioCallback = std::function<void(const int16_t* samples, int count)>;
    void set_cd_audio_callback(CdAudioCallback cb) { cd_audio_cb_ = std::move(cb); }

    bool disc_loaded() const { return !tracks_.empty(); }
    const std::vector<PsxCdTrack>& tracks() const { return tracks_; }

    int debug_mode() const { return int(mode_); }
    int debug_read_loc() const { return read_loc_; }
    uint8_t debug_stat() const { return stat_; }
    uint8_t debug_if() const { return if_; }
    uint8_t debug_ie() const { return ie_; }
    bool debug_busy() const { return busy_; }
    size_t debug_irq_queue() const { return irq_queue_.size(); }

private:
    struct SectorBuf {
        std::vector<uint8_t> data;
        int pointer = 0;
        int size = 0;
        void clear() { pointer = 0; size = 0; }
        void fill(const uint8_t* src, int n) {
            if (int(data.size()) < n) data.resize(size_t(n));
            std::memcpy(data.data(), src, size_t(n));
            pointer = 0;
            size = n;
        }
        bool has_data() const { return pointer < size; }
        uint8_t read_byte() { return data[size_t(pointer++)]; }
        void read_words(uint32_t* dest, int words) {
            const int bytes = words * 4;
            std::memcpy(dest, data.data() + pointer, size_t(bytes));
            pointer += bytes;
        }
        const uint8_t* bytes() const { return data.data(); }
        int byte_size() const { return size; }
    };

    struct DelayedIrq {
        int delay = 0;
        uint8_t interrupt = 0;
    };

    enum class Mode { Idle, Seek, Read, Play, Toc };

    void execute_command(uint32_t value);
    void enqueue_irq(uint8_t irq, int delay = 50000);
    uint8_t status_reg() const;

    void cmd_getstat();
    void cmd_setloc();
    void cmd_play();
    void cmd_readn();
    void cmd_motor_on();
    void cmd_stop();
    void cmd_pause();
    void cmd_init();
    void cmd_mute();
    void cmd_demute();
    void cmd_setfilter();
    void cmd_setmode();
    void cmd_getlocl();
    void cmd_getlocp();
    void cmd_setsession();
    void cmd_gettn();
    void cmd_gettd();
    void cmd_seekl();
    void cmd_seekp();
    void cmd_test();
    void cmd_getid();
    void cmd_reads();
    void cmd_readtoc();
    void cmd_videocd();

    bool read_sector(int loc, uint8_t out[kBytesPerSector]);
    const PsxCdTrack& track_from_loc(int loc) const;
    int total_lba() const;
    bool is_audio_cd() const;

    static uint8_t dec_to_bcd(uint8_t v);
    static int bcd_to_dec(uint8_t v);
    static void lba_to_msf(int lba, uint8_t& mm, uint8_t& ss, uint8_t& ff);

    std::vector<PsxCdTrack> tracks_;
    // Entire disc image(s) kept in memory for simplicity (Hercules-sized OK).
    std::vector<std::vector<uint8_t>> track_data_;

    std::deque<uint8_t> param_;
    std::deque<uint8_t> response_;
    SectorBuf current_sector_;
    SectorBuf last_read_sector_;

    bool busy_ = false;
    uint8_t ie_ = 0;
    uint8_t if_ = 0;
    uint8_t index_ = 0;
    uint8_t stat_ = 0;

    int seek_loc_ = 0;
    int read_loc_ = 0;

    bool double_speed_ = false;
    bool xa_adpcm_ = false;
    bool sector_raw_ = false;
    bool ignore_bit_ = false;
    bool xa_filter_ = false;
    bool report_ = false;
    bool auto_pause_ = false;
    bool cdda_ = false;
    uint8_t filter_file_ = 0;
    uint8_t filter_channel_ = 0;
    bool muted_ = false;
    bool lid_open_ = false;

    uint8_t hdr_mm_ = 0, hdr_ss_ = 0, hdr_ff_ = 0, hdr_mode_ = 0;
    uint8_t sub_file_ = 0, sub_channel_ = 0, sub_mode_ = 0, sub_coding_ = 0;

    Mode mode_ = Mode::Idle;
    int counter_ = 0;
    std::deque<DelayedIrq> irq_queue_;
    bool track_change_ = false;
    CdAudioCallback cd_audio_cb_;
};

}  // namespace dsp
