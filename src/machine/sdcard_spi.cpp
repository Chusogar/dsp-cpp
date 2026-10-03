#include "machine/sdcard_spi.h"

#include <cstring>

namespace dsp {

SdCardSpi::~SdCardSpi() { close(); }

bool SdCardSpi::open(const std::string& path, bool read_only, std::string* error) {
    close();
    file_.open(path, read_only ? (std::ios::in | std::ios::binary)
                               : (std::ios::in | std::ios::out | std::ios::binary));
    if (!file_.is_open() && !read_only) {
        // Read-only media: fall back to an in-memory write overlay.
        file_.clear();
        file_.open(path, std::ios::in | std::ios::binary);
        read_only = true;
    }
    if (!file_.is_open()) {
        if (error) *error = "cannot open SD card image " + path;
        return false;
    }
    file_.seekg(0, std::ios::end);
    size_ = uint64_t(file_.tellg());
    file_.seekg(0);
    if (size_ < 512) {
        if (error) *error = "SD card image too small: " + path;
        close();
        return false;
    }
    path_ = path;
    read_only_ = read_only;
    idle_ = true;
    state_ = State::Idle;
    out_.clear();
    return true;
}

void SdCardSpi::close() {
    if (file_.is_open()) file_.close();
    overlay_.clear();
    path_.clear();
    size_ = 0;
}

void SdCardSpi::select(bool selected) {
    if (selected == selected_) return;
    selected_ = selected;
    if (!selected) {
        // Deselecting aborts a command being received; pending output is
        // dropped (a multi-block read stops being clocked out).
        if (state_ == State::Command || state_ == State::MultiRead) state_ = State::Idle;
        cmd_len_ = 0;
        out_.clear();
    }
}

bool SdCardSpi::read_sector(uint32_t lba, uint8_t* out) {
    auto it = overlay_.find(lba);
    if (it != overlay_.end()) {
        std::memcpy(out, it->second.data(), 512);
        return true;
    }
    const uint64_t pos = uint64_t(lba) * 512;
    if (!file_.is_open() || pos + 512 > size_) {
        std::memset(out, 0, 512);
        return false;
    }
    file_.clear();
    file_.seekg(std::streamoff(pos));
    file_.read(reinterpret_cast<char*>(out), 512);
    ++sectors_read_;
    return bool(file_);
}

bool SdCardSpi::write_sector(uint32_t lba, const uint8_t* data) {
    const uint64_t pos = uint64_t(lba) * 512;
    if (!file_.is_open() || pos + 512 > size_) return false;
    ++sectors_written_;
    if (read_only_) {
        overlay_[lba].assign(data, data + 512);
        return true;
    }
    file_.clear();
    file_.seekp(std::streamoff(pos));
    file_.write(reinterpret_cast<const char*>(data), 512);
    file_.flush();
    return bool(file_);
}

void SdCardSpi::queue_data_block(const uint8_t* p, size_t n) {
    out_.push_back(0xff);
    out_.push_back(0xfe);  // start block token
    queue_bytes(p, n);
    out_.push_back(0xff);  // CRC16 (not checked by the host)
    out_.push_back(0xff);
}

void SdCardSpi::queue_read_block(uint32_t lba) {
    uint8_t buf[512];
    if (!read_sector(lba, buf)) {
        out_.push_back(0x08);  // data error token: out of range
        return;
    }
    queue_data_block(buf, 512);
}

uint8_t SdCardSpi::exchange(uint8_t mosi) {
    if (!selected_ || !file_.is_open()) return 0xff;

    uint8_t miso = 0xff;
    if (!out_.empty()) {
        miso = out_.front();
        out_.pop_front();
    }

    switch (state_) {
        case State::WriteToken:
            if (mosi == 0xfe || (multi_write_ && mosi == 0xfc)) {
                write_buf_.clear();
                state_ = State::WriteData;
            } else if (multi_write_ && mosi == 0xfd) {
                // Stop transmission token: card is busy for a moment.
                out_.push_back(0xff);
                out_.push_back(0x00);
                state_ = State::Idle;
            }
            return miso;
        case State::WriteData:
            if (write_skip_ > 0) {  // CRC bytes after the block
                if (--write_skip_ == 0) {
                    const bool ok = write_sector(rw_lba_, write_buf_.data());
                    out_.push_back(ok ? 0x05 : 0x0d);  // data accepted / write error
                    out_.push_back(0x00);               // busy
                    out_.push_back(0x00);
                    if (multi_write_ && ok) {
                        ++rw_lba_;
                        state_ = State::WriteToken;
                    } else {
                        state_ = State::Idle;
                    }
                }
                return miso;
            }
            write_buf_.push_back(mosi);
            if (write_buf_.size() == 512) write_skip_ = 2;
            return miso;
        default:
            break;
    }

    if (state_ == State::Command) {
        cmd_[cmd_len_++] = mosi;
        if (cmd_len_ == 6) {
            state_ = State::Idle;
            execute_command();
        }
        return miso;
    }

    if ((mosi & 0xc0) == 0x40) {
        // Start of a command frame (a CMD12 can interrupt a multi-block read).
        if (state_ == State::MultiRead) {
            out_.clear();
            miso = 0xff;
        }
        cmd_[0] = mosi;
        cmd_len_ = 1;
        state_ = State::Command;
        return miso;
    }

    if (state_ == State::MultiRead && out_.size() < 4) {
        ++rw_lba_;
        queue_read_block(rw_lba_);
    }
    return miso;
}

void SdCardSpi::execute_command() {
    const uint8_t cmd = cmd_[0] & 0x3f;
    const uint32_t arg = (uint32_t(cmd_[1]) << 24) | (uint32_t(cmd_[2]) << 16) |
                         (uint32_t(cmd_[3]) << 8) | cmd_[4];
    const bool app = app_cmd_;
    app_cmd_ = false;
    out_.clear();
    out_.push_back(0xff);  // NCR: one byte before the response

    if (app) {
        switch (cmd) {
            case 41:  // SD_SEND_OP_COND
                idle_ = false;
                queue_r1(0x00);
                return;
            case 13:  // SD_STATUS
                queue_r1(r1_status());
                out_.push_back(0x00);
                {
                    uint8_t status[64] = {};
                    queue_data_block(status, sizeof(status));
                }
                return;
            case 51: {  // SEND_SCR
                queue_r1(r1_status());
                const uint8_t scr[8] = {0x02, 0x35, 0x80, 0x00, 0, 0, 0, 0};
                queue_data_block(scr, sizeof(scr));
                return;
            }
            default:
                break;  // falls through to the normal command set
        }
    }

    switch (cmd) {
        case 0:  // GO_IDLE_STATE
            idle_ = true;
            queue_r1(0x01);
            break;
        case 1:  // SEND_OP_COND (MMC)
            idle_ = false;
            queue_r1(0x00);
            break;
        case 8: {  // SEND_IF_COND
            const uint8_t r7[5] = {r1_status(), 0x00, 0x00, uint8_t(cmd_[3] & 0x0f), cmd_[4]};
            queue_bytes(r7, sizeof(r7));
            break;
        }
        case 9: {  // SEND_CSD (version 2.0, SDHC)
            queue_r1(r1_status());
            const uint32_t c_size = uint32_t(size_ / (512 * 1024)) - 1;
            const uint8_t csd[16] = {0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00,
                                     uint8_t((c_size >> 16) & 0x3f), uint8_t(c_size >> 8),
                                     uint8_t(c_size), 0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01};
            queue_data_block(csd, sizeof(csd));
            break;
        }
        case 10: {  // SEND_CID
            queue_r1(r1_status());
            const uint8_t cid[16] = {0x03, 'D', 'S', 'D', 'S', 'P', 'C', 'P',
                                     0x10, 0x01, 0x23, 0x45, 0x67, 0x01, 0x6a, 0x01};
            queue_data_block(cid, sizeof(cid));
            break;
        }
        case 12:  // STOP_TRANSMISSION
            state_ = State::Idle;
            out_.push_back(0xff);  // stuff byte
            queue_r1(0x00);
            out_.push_back(0x00);  // busy
            break;
        case 13:  // SEND_STATUS (R2)
            queue_r1(r1_status());
            out_.push_back(0x00);
            break;
        case 16:  // SET_BLOCKLEN
            queue_r1(arg == 512 ? r1_status() : uint8_t(r1_status() | 0x40));
            break;
        case 17:  // READ_SINGLE_BLOCK
        case 18:  // READ_MULTIPLE_BLOCK
            if (uint64_t(arg) * 512 >= size_) {
                queue_r1(0x40);  // parameter error
                break;
            }
            queue_r1(0x00);
            rw_lba_ = arg;
            queue_read_block(rw_lba_);
            if (cmd == 18) state_ = State::MultiRead;
            break;
        case 24:  // WRITE_BLOCK
        case 25:  // WRITE_MULTIPLE_BLOCK
            if (uint64_t(arg) * 512 >= size_) {
                queue_r1(0x40);
                break;
            }
            queue_r1(0x00);
            rw_lba_ = arg;
            multi_write_ = cmd == 25;
            write_skip_ = 0;
            state_ = State::WriteToken;
            break;
        case 55:  // APP_CMD
            app_cmd_ = true;
            queue_r1(r1_status());
            break;
        case 58: {  // READ_OCR: powered up, SDHC (CCS)
            const uint8_t r3[5] = {r1_status(), 0xc0, 0xff, 0x80, 0x00};
            queue_bytes(r3, sizeof(r3));
            break;
        }
        case 59:  // CRC_ON_OFF
            queue_r1(r1_status());
            break;
        default:
            queue_r1(uint8_t(r1_status() | 0x04));  // illegal command
            break;
    }
}

}  // namespace dsp
