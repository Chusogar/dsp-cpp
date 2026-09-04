#include "machine/ncr5380_hdd.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

constexpr uint8_t kIcRst = 0x80;
constexpr uint8_t kIcAck = 0x10;
constexpr uint8_t kIcBsy = 0x08;
constexpr uint8_t kIcSel = 0x04;
constexpr uint8_t kIcAtn = 0x02;
constexpr uint8_t kIcDbus = 0x01;

constexpr uint8_t kModeDma = 0x02;
constexpr uint8_t kModeArb = 0x01;

}  // namespace

void Ncr5380Hdd::reset() {
    odr_ = icr_ = mode_ = tcr_ = ser_ = idr_ = 0;
    rst_ = bsy_ = sel_ = req_ = ack_ = atn_ = false;
    drq_ = eop_ = irq_ = aip_ = dma_ = false;
    phase_ = kFree;
    cdb_len_ = cdb_pos_ = 0;
    status_ = message_ = 0;
    last_cmd_ = 0;
    xfer_.clear();
    xfer_pos_ = 0;
    xfer_done_ = 0;
    accesses_ = 0;
}

bool Ncr5380Hdd::load_file(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 1024 || (size & 511) != 0) {
        if (error) *error = path + ": SCSI disk must be a multiple of 512 bytes";
        return false;
    }
    image_.assign(static_cast<size_t>(size), 0);
    in.read(reinterpret_cast<char*>(image_.data()), size);
    if (!in) {
        if (error) *error = "cannot read " + path;
        image_.clear();
        return false;
    }
    blocks_ = uint32_t(image_.size() / 512);
    wrap_raw_hfs();
    loaded_ = true;
    return true;
}

void Ncr5380Hdd::wrap_raw_hfs() {
    if (image_.size() < 1024 || image_[0] != 'L' || image_[1] != 'K') return;

    const uint32_t hfs_blocks = blocks_;
    std::vector<uint8_t> hfs = std::move(image_);
    image_.assign(1024 + hfs.size(), 0);

    image_[0] = 'E';
    image_[1] = 'R';
    image_[2] = 0x02;
    image_[3] = 0x00;
    const uint32_t total = hfs_blocks + 2;
    image_[4] = uint8_t(total >> 24);
    image_[5] = uint8_t(total >> 16);
    image_[6] = uint8_t(total >> 8);
    image_[7] = uint8_t(total);
    image_[0x11] = 1;   // one driver
    image_[0x15] = 1;   // start block
    image_[0x17] = 1;   // length
    image_[0x19] = 1;   // Macintosh

    static const uint8_t kMacScsiDriver[512] = {
        0x48, 0xe7, 0xff, 0xfe, 0x41, 0xfa, 0x00, 0x22, 0x30, 0x3c, 0xff, 0xf8, 0xa0, 0x3d, 0x66, 0x00,
        0x00, 0x0e, 0x41, 0xfa, 0x01, 0xd0, 0x20, 0x3c, 0xff, 0xf8, 0x00, 0x08, 0xa0, 0x4e, 0x4c, 0xdf,
        0x7f, 0xff, 0x70, 0x00, 0x4e, 0x75, 0x00, 0x00, 0x4f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x22, 0x00, 0x2a, 0x00, 0x26, 0x00, 0x26, 0x00, 0x26, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00,
        0x06, 0x2e, 0x44, 0x53, 0x50, 0x48, 0x44, 0x00, 0x00, 0x00, 0x70, 0x00, 0x4e, 0x75, 0x70, 0x00,
        0x4e, 0x75, 0x48, 0xe7, 0x7f, 0xf8, 0x26, 0x29, 0x00, 0x2e, 0xe0, 0x8b, 0xe2, 0x8b, 0xd6, 0xba,
        0x01, 0x62, 0x28, 0x29, 0x00, 0x24, 0x24, 0x69, 0x00, 0x20, 0x41, 0xfa, 0x01, 0x5c, 0x30, 0x11,
        0xc0, 0x7c, 0x00, 0x01, 0x12, 0xbc, 0x00, 0x08, 0x4a, 0x40, 0x67, 0x04, 0x12, 0xbc, 0x00, 0x0a,
        0x22, 0x03, 0x48, 0x41, 0x02, 0x01, 0x00, 0x1f, 0x11, 0x41, 0x00, 0x01, 0x31, 0x43, 0x00, 0x02,
        0x22, 0x04, 0x06, 0x81, 0x00, 0x00, 0x01, 0xff, 0xe0, 0x89, 0xe2, 0x89, 0x11, 0x41, 0x00, 0x04,
        0x42, 0x28, 0x00, 0x05, 0x42, 0x67, 0x3f, 0x3c, 0x00, 0x01, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00,
        0x00, 0x82, 0x42, 0x67, 0x3f, 0x3a, 0x01, 0x0a, 0x3f, 0x3c, 0x00, 0x02, 0xa8, 0x15, 0x4a, 0x5f,
        0x66, 0x00, 0x00, 0x70, 0x42, 0x67, 0x48, 0x7a, 0x01, 0x00, 0x3f, 0x3c, 0x00, 0x06, 0x3f, 0x3c,
        0x00, 0x03, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00, 0x00, 0x5a, 0x41, 0xfa, 0x00, 0xf4, 0x30, 0xfc,
        0x00, 0x01, 0x20, 0xca, 0x20, 0xc4, 0x30, 0xbc, 0x00, 0x07, 0x42, 0x67, 0x48, 0x7a, 0x00, 0xe2,
        0x30, 0x11, 0xc0, 0x7c, 0x00, 0x01, 0x66, 0x06, 0x3f, 0x3c, 0x00, 0x05, 0x60, 0x04, 0x3f, 0x3c,
        0x00, 0x06, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00, 0x00, 0x2a, 0x42, 0x67, 0x48, 0x7a, 0x00, 0xce,
        0x48, 0x7a, 0x00, 0xcc, 0x2f, 0x3c, 0x00, 0x00, 0x00, 0x3c, 0x3f, 0x3c, 0x00, 0x04, 0xa8, 0x15,
        0x4a, 0x5f, 0x66, 0x00, 0x00, 0x0e, 0x23, 0x44, 0x00, 0x28, 0x42, 0x69, 0x00, 0x10, 0x70, 0x00,
        0x60, 0x08, 0x33, 0x7c, 0xff, 0xee, 0x00, 0x10, 0x70, 0xff, 0x4c, 0xdf, 0x1f, 0xfe, 0x4e, 0x75,
    };
    std::memcpy(image_.data() + 512, kMacScsiDriver, sizeof(kMacScsiDriver));
    image_[512 + 0x1c5] = 2;  // partition LBA
    image_[512 + 0x1e0] = 0x80;
    image_[512 + 0x1eb] = 8;  // dQDrive
    image_[512 + 0x1ec] = 0xff;
    image_[512 + 0x1ed] = 0xf8;
    const uint16_t sz = uint16_t(std::min<uint32_t>(hfs_blocks, 0xffff));
    image_[512 + 0x1f0] = uint8_t(sz >> 8);
    image_[512 + 0x1f1] = uint8_t(sz);
    std::memcpy(image_.data() + 1024, hfs.data(), hfs.size());
    blocks_ = uint32_t(image_.size() / 512);
}

int Ncr5380Hdd::cdb_length(uint8_t opcode) {
    switch (opcode >> 5) {
        case 0:
            return 6;
        case 1:
        case 2:
            return 10;
        case 5:
            return 12;
        default:
            return 6;
    }
}

void Ncr5380Hdd::bus_reset() {
    bsy_ = sel_ = req_ = ack_ = atn_ = false;
    drq_ = eop_ = aip_ = dma_ = false;
    phase_ = kFree;
    cdb_len_ = cdb_pos_ = 0;
    xfer_.clear();
    xfer_pos_ = 0;
    rst_ = true;
}

void Ncr5380Hdd::set_phase(uint8_t phase) {
    phase_ = phase;
    req_ = false;
    drq_ = false;
    eop_ = false;
    dma_ = false;
}

void Ncr5380Hdd::update_match() {}

void Ncr5380Hdd::offer_byte() {
    if (xfer_pos_ < xfer_.size()) idr_ = xfer_[xfer_pos_];
    req_ = true;
    if ((phase_ == kDataIn || phase_ == kDataOut) && (dma_ || (mode_ & kModeDma))) drq_ = true;
}

void Ncr5380Hdd::start_command() {
    set_phase(kCommand);
    cdb_pos_ = 0;
    cdb_len_ = 6;
    req_ = true;
}

void Ncr5380Hdd::finish_command() {
    set_phase(kStatus);
    idr_ = status_;
    offer_byte();
}

void Ncr5380Hdd::execute() {
    last_cmd_ = cdb_[0];
    xfer_.clear();
    xfer_pos_ = 0;
    status_ = 0;
    message_ = 0;

    auto fail = [this]() {
        status_ = 0x02;
        finish_command();
    };

    const uint8_t op = cdb_[0];
    if (op == 0x00) {  // TEST UNIT READY
        if (!loaded_) return fail();
        finish_command();
        return;
    }
    if (op == 0x03) {  // REQUEST SENSE
        const int n = cdb_[4] ? cdb_[4] : 18;
        xfer_.assign(size_t(n), 0);
        xfer_[0] = 0x70;
        set_phase(kDataIn);
        offer_byte();
        return;
    }
    if (op == 0x12) {  // INQUIRY
        const int n = cdb_[4] ? cdb_[4] : 36;
        xfer_.assign(36, 0);
        xfer_[0] = 0x00;
        xfer_[1] = 0x00;
        xfer_[2] = 0x02;
        xfer_[3] = 0x01;
        xfer_[4] = 31;
        std::memcpy(xfer_.data() + 8, "DSP     MAC HD          1.0 ", 28);
        if (n < int(xfer_.size())) xfer_.resize(size_t(n));
        set_phase(kDataIn);
        offer_byte();
        return;
    }
    if (op == 0x1a) {  // MODE SENSE(6)
        const int n = cdb_[4] ? cdb_[4] : 4;
        xfer_.assign(4, 0);
        xfer_[0] = 3;
        if (n < 4) xfer_.resize(size_t(n));
        set_phase(kDataIn);
        offer_byte();
        return;
    }
    if (op == 0x25) {  // READ CAPACITY
        if (!loaded_) return fail();
        const uint32_t last = blocks_ ? blocks_ - 1 : 0;
        xfer_.assign(8, 0);
        xfer_[0] = uint8_t(last >> 24);
        xfer_[1] = uint8_t(last >> 16);
        xfer_[2] = uint8_t(last >> 8);
        xfer_[3] = uint8_t(last);
        xfer_[7] = 0x00;
        xfer_[6] = 0x02;  // 512
        set_phase(kDataIn);
        offer_byte();
        return;
    }

    uint32_t lba = 0;
    uint32_t count = 0;
    bool writing = false;
    if (op == 0x08 || op == 0x0a) {
        lba = (uint32_t(cdb_[1] & 0x1f) << 16) | (uint32_t(cdb_[2]) << 8) | cdb_[3];
        count = cdb_[4] ? cdb_[4] : 256;
        writing = op == 0x0a;
    } else if (op == 0x28 || op == 0x2a) {
        lba = (uint32_t(cdb_[2]) << 24) | (uint32_t(cdb_[3]) << 16) | (uint32_t(cdb_[4]) << 8) |
              cdb_[5];
        count = (uint32_t(cdb_[7]) << 8) | cdb_[8];
        writing = op == 0x2a;
    } else {
        finish_command();
        return;
    }
    if (!loaded_ || lba >= blocks_) return fail();
    if (lba + count > blocks_) count = blocks_ - lba;
    const size_t bytes = size_t(count) * 512;
    if (writing) {
        xfer_.assign(bytes, 0);
        set_phase(kDataOut);
        req_ = true;
        if (mode_ & kModeDma) drq_ = true;
        return;
    }
    xfer_.resize(bytes);
    std::memcpy(xfer_.data(), image_.data() + size_t(lba) * 512, bytes);
    xfer_done_ += uint32_t(bytes);
    set_phase(kDataIn);
    offer_byte();
}

void Ncr5380Hdd::take_byte() {
    if (phase_ == kCommand) {
        if (cdb_pos_ < 12) cdb_[cdb_pos_] = odr_;
        if (cdb_pos_ == 0) cdb_len_ = cdb_length(odr_);
        cdb_pos_++;
        req_ = false;
        if (cdb_pos_ >= cdb_len_) execute();
        else
            req_ = true;
        return;
    }
    if (phase_ == kDataOut) {
        if (xfer_pos_ < xfer_.size()) xfer_[xfer_pos_++] = odr_;
        req_ = false;
        if (xfer_pos_ >= xfer_.size()) {
            if (loaded_ && (last_cmd_ == 0x0a || last_cmd_ == 0x2a)) {
                uint32_t lba = 0;
                if (last_cmd_ == 0x0a)
                    lba = (uint32_t(cdb_[1] & 0x1f) << 16) | (uint32_t(cdb_[2]) << 8) | cdb_[3];
                else
                    lba = (uint32_t(cdb_[2]) << 24) | (uint32_t(cdb_[3]) << 16) |
                          (uint32_t(cdb_[4]) << 8) | cdb_[5];
                const size_t off = size_t(lba) * 512;
                const size_t n = std::min(xfer_.size(), image_.size() - off);
                if (off < image_.size()) std::memcpy(image_.data() + off, xfer_.data(), n);
            }
            finish_command();
        } else {
            req_ = true;
            if (mode_ & kModeDma) drq_ = true;
        }
        return;
    }
    if (phase_ == kDataIn) {
        xfer_pos_++;
        req_ = false;
        drq_ = false;
        if (xfer_pos_ >= xfer_.size()) {
            eop_ = true;
            finish_command();
        } else {
            offer_byte();
        }
        return;
    }
    if (phase_ == kStatus) {
        req_ = false;
        set_phase(kMsgIn);
        idr_ = message_;
        offer_byte();
        return;
    }
    if (phase_ == kMsgIn) {
        req_ = false;
        bsy_ = false;
        set_phase(kFree);
        return;
    }
}

void Ncr5380Hdd::on_ack(bool ack) {
    if (ack == ack_) return;
    ack_ = ack;
    if (ack && req_) take_byte();
}

uint8_t Ncr5380Hdd::csr() const {
    uint8_t v = 0;
    if (rst_) v |= 0x80;
    if (bsy_ || (icr_ & kIcBsy)) v |= 0x40;
    if (req_) v |= 0x20;
    if (phase_ & 4) v |= 0x10;
    if (phase_ & 2) v |= 0x08;
    if (phase_ & 1) v |= 0x04;
    if (sel_) v |= 0x02;
    return v;
}

uint8_t Ncr5380Hdd::bsr() const {
    uint8_t v = 0;
    if (eop_) v |= 0x80;
    if (drq_) v |= 0x40;
    if (irq_) v |= 0x10;
    if ((tcr_ & 7) == (phase_ & 7)) v |= 0x08;
    if (atn_) v |= 0x02;
    if (ack_) v |= 0x01;
    return v;
}

uint8_t Ncr5380Hdd::read_reg(int reg, bool dack) {
    if (dack && phase_ == kDataIn && (mode_ & kModeDma)) {
        const uint8_t v = idr_;
        take_byte();
        return v;
    }
    switch (reg) {
        case 0:
            if ((icr_ & kIcDbus) || aip_) return odr_;
            return (req_ || dack) ? idr_ : 0;
        case 1: {
            uint8_t v = uint8_t(icr_ & 0x9f);
            if (aip_) v |= 0x40;
            return v;
        }
        case 2:
            return mode_;
        case 3:
            return tcr_;
        case 4:
            return csr();
        case 5:
            return bsr();
        case 6:
            return idr_;
        case 7:
            irq_ = false;
            rst_ = false;
            return 0;
        default:
            return 0xff;
    }
}

void Ncr5380Hdd::write_reg(int reg, bool dack, uint8_t data) {
    if (dack && phase_ == kDataOut && (mode_ & kModeDma)) {
        odr_ = data;
        take_byte();
        return;
    }
    switch (reg) {
        case 0:
            odr_ = data;
            break;
        case 1: {
            icr_ = data;
            atn_ = (data & kIcAtn) != 0;
            sel_ = (data & kIcSel) != 0;
            if (data & kIcRst) {
                bus_reset();
                break;
            }
            rst_ = false;
            on_ack((data & kIcAck) != 0);
            // Only this disk (SCSI ID 0) answers selection. Initiator BSY is
            // visible on CSR but must not be treated as target BSY.
            if (sel_ && loaded_ && (odr_ & 0x01)) bsy_ = true;
            if (!sel_ && bsy_ && phase_ == kFree) start_command();
            break;
        }
        case 2:
            mode_ = data;
            aip_ = (data & kModeArb) != 0;
            if (!(data & kModeDma)) {
                dma_ = false;
                drq_ = false;
            }
            break;
        case 3:
            tcr_ = data;
            break;
        case 4:
            ser_ = data;
            break;
        case 5:  // start DMA send
            dma_ = true;
            if (phase_ == kDataOut) drq_ = true;
            break;
        case 6:
            break;
        case 7:  // start DMA initiator receive
            dma_ = true;
            if (phase_ == kDataIn) {
                drq_ = true;
                offer_byte();
            }
            break;
        default:
            break;
    }
}

uint8_t Ncr5380Hdd::read(uint32_t address) {
    accesses_++;
    const int reg = int((address >> 4) & 7);
    const bool dack = (address & 0x200) != 0;
    return read_reg(reg, dack);
}

void Ncr5380Hdd::write(uint32_t address, uint8_t data) {
    accesses_++;
    const int reg = int((address >> 4) & 7);
    const bool dack = (address & 0x200) != 0;
    write_reg(reg, dack, data);
}

}  // namespace dsp
