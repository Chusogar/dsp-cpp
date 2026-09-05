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
    drq_ = eop_ = irq_ = aip_ = dma_ = pending_req_ = arb_ = sel_phase_ = false;
    phase_ = kFree;
    cdb_len_ = cdb_pos_ = 0;
    status_ = message_ = 0;
    last_cmd_ = 0;
    cmd_count_ = 0;
    write_count_ = 0;
    last_write_lba_ = 0;
    last_write_bytes_ = 0;
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

    // 512-byte .DSPHD stub. The Plus ROM JSRs the first word after loading
    // the driver: _DrvrInstall (-33), fill dCtlDriver, set BootMask, _AddDrive
    // (drive 8 in the high word). Prime uses A0 as the IOParam (Plus convention).
    // Completing a request goes through JIODone ($8FC) so the Device Manager
    // dequeues and can Prime the HFS MDB _Read. ioCompletion is cleared so
    // leftover memtest $FF cannot JSR the ROM RAM-fill and reboot. Status
    // csCode 8 fills DrvSts.
    static const uint8_t kMacScsiDriver[512] = {
        0x48, 0xe7, 0xff, 0xfe, 0x41, 0xfa, 0x00, 0x44, 0x30, 0x3c, 0xff, 0xdf, 0xa0, 0x3d, 0x66, 0x00,
        0x00, 0x32, 0x20, 0x78, 0x01, 0x1c, 0x20, 0x68, 0x00, 0x80, 0x08, 0xd0, 0x00, 0x07, 0x22, 0x50,
        0x45, 0xfa, 0x00, 0x28, 0x22, 0xca, 0x32, 0xd2, 0x32, 0xbc, 0x00, 0x02, 0x31, 0xfc, 0xff, 0xff,
        0x0b, 0x0e, 0x41, 0xfa, 0x01, 0xb0, 0x30, 0x3c, 0x00, 0x08, 0x48, 0x40, 0x30, 0x3c, 0xff, 0xdf,
        0xa0, 0x4e, 0x4c, 0xdf, 0x7f, 0xff, 0x70, 0x00, 0x4e, 0x75, 0x4f, 0x20, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x1c, 0x00, 0x4e, 0x00, 0x1c, 0x00, 0x22, 0x00, 0x1c, 0x00, 0x14, 0x06, 0x2e,
        0x44, 0x53, 0x50, 0x48, 0x44, 0x00, 0x70, 0x00, 0x60, 0x00, 0x00, 0x20, 0x0c, 0x68, 0x00, 0x08,
        0x00, 0x1a, 0x66, 0x14, 0x42, 0x68, 0x00, 0x1c, 0x42, 0x28, 0x00, 0x1e, 0x11, 0x7c, 0x00, 0x08,
        0x00, 0x1f, 0x11, 0x7c, 0x00, 0x01, 0x00, 0x20, 0x70, 0x00, 0x31, 0x40, 0x00, 0x10, 0x42, 0xa8,
        0x00, 0x0c, 0x2f, 0x38, 0x08, 0xfc, 0x4e, 0x75, 0x48, 0xe7, 0x7f, 0xf8, 0x26, 0x48, 0x26, 0x2b,
        0x00, 0x2e, 0xe0, 0x8b, 0xe2, 0x8b, 0xd6, 0xba, 0x01, 0x1a, 0x28, 0x2b, 0x00, 0x24, 0x24, 0x6b,
        0x00, 0x20, 0x41, 0xfa, 0x01, 0x14, 0x30, 0x13, 0xc0, 0x7c, 0x00, 0x01, 0x10, 0xbc, 0x00, 0x08,
        0x4a, 0x40, 0x67, 0x04, 0x10, 0xbc, 0x00, 0x0a, 0x22, 0x03, 0x48, 0x41, 0x02, 0x01, 0x00, 0x1f,
        0x11, 0x41, 0x00, 0x01, 0x31, 0x43, 0x00, 0x02, 0x22, 0x04, 0x06, 0x81, 0x00, 0x00, 0x01, 0xff,
        0xe0, 0x89, 0xe2, 0x89, 0x11, 0x41, 0x00, 0x04, 0x42, 0x28, 0x00, 0x05, 0x42, 0x67, 0x3f, 0x3c,
        0x00, 0x01, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00, 0x00, 0x82, 0x42, 0x67, 0x3f, 0x3a, 0x00, 0xc2,
        0x3f, 0x3c, 0x00, 0x02, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00, 0x00, 0x70, 0x42, 0x67, 0x48, 0x7a,
        0x00, 0xb8, 0x3f, 0x3c, 0x00, 0x06, 0x3f, 0x3c, 0x00, 0x03, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00,
        0x00, 0x5a, 0x41, 0xfa, 0x00, 0xac, 0x30, 0xfc, 0x00, 0x01, 0x20, 0xca, 0x20, 0xc4, 0x30, 0xbc,
        0x00, 0x07, 0x42, 0x67, 0x48, 0x7a, 0x00, 0x9a, 0x30, 0x13, 0xc0, 0x7c, 0x00, 0x01, 0x66, 0x06,
        0x3f, 0x3c, 0x00, 0x05, 0x60, 0x04, 0x3f, 0x3c, 0x00, 0x06, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00,
        0x00, 0x2a, 0x42, 0x67, 0x48, 0x7a, 0x00, 0x86, 0x48, 0x7a, 0x00, 0x84, 0x2f, 0x3c, 0x00, 0x00,
        0x00, 0x3c, 0x3f, 0x3c, 0x00, 0x04, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00, 0x00, 0x0e, 0x27, 0x44,
        0x00, 0x28, 0x42, 0x6b, 0x00, 0x10, 0x70, 0x00, 0x60, 0x08, 0x37, 0x7c, 0xff, 0xee, 0x00, 0x10,
        0x70, 0xff, 0x4c, 0xdf, 0x1f, 0xfe, 0x60, 0x00, 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xff, 0xdf, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    std::memcpy(image_.data() + 512, kMacScsiDriver, sizeof(kMacScsiDriver));
    image_[512 + 0x1c2] = 0;
    image_[512 + 0x1c3] = 0;
    image_[512 + 0x1c4] = 0;
    image_[512 + 0x1c5] = 2;  // HFS starts two blocks after the DDM + stub
    const uint16_t sz = uint16_t(std::min<uint32_t>(hfs_blocks, 0xffff));
    image_[512 + 0x1f0] = uint8_t(sz >> 8);
    image_[512 + 0x1f1] = uint8_t(sz);
    std::memcpy(image_.data() + 1024, hfs.data(), hfs.size());
    // System 7.0.1 boot blocks use bbVersion $44, so the Plus ROM JSRs
    // $2(boot). That code _SysError $62 on a 128K ROM and never returns.
    // PCE/macplus never patches this: it boots a disk that already has an
    // Apple HD SC driver and a 5380 complete enough for that driver to
    // finish the JSR. Our raw HFS image only has a .DSPHD stub, so RTS
    // lets the ROM Start Manager MountVol and load System itself.
    uint8_t* boot = image_.data() + 1024;
    if (hfs.size() >= 1024 && boot[0] == 'L' && boot[1] == 'K' && boot[6] == 0x44 &&
        boot[0x8a] == 0x4a && boot[0x8b] == 0x78 && boot[0xd8] == 0xa9 &&
        boot[0xd9] == 0xc9) {
        boot[2] = 0x4e;
        boot[3] = 0x75;
    }
    // ROM _Launch only accepts APPL. System 7's Finder is type FNDR, so the
    // 128K Launch fallback reads a bit of the file and _SysError 26.
    static const uint8_t kFinderFndr[] = {0x06, 'F', 'i', 'n', 'd', 'e', 'r', 0xb4, 0x02, 0x00,
                                          0x00, 0x00, 'F', 'N', 'D', 'R', 'M', 'A', 'C', 'S'};
    for (size_t i = 0; i + sizeof(kFinderFndr) <= hfs.size(); ++i) {
        if (std::equal(std::begin(kFinderFndr), std::end(kFinderFndr), boot + i)) {
            boot[i + 12] = 'A';
            boot[i + 13] = 'P';
            boot[i + 14] = 'P';
            boot[i + 15] = 'L';
        }
    }
    // System 7 'boot' id 2 (Process Manager stub). The $FA path JSRs this
    // after OpenResFile; 128K GetResource hits memFullErr in SysZone, so
    // the Plus driver copies the body under BufPtr at _Launch.
    boot2_.clear();
    static const uint8_t kBoot2[] = {0x20, 0x4b, 0xa0, 0x25, 0x41, 0xfa, 0x00, 0x12};
    for (size_t i = 4; i + sizeof(kBoot2) <= image_.size(); ++i) {
        if (!std::equal(std::begin(kBoot2), std::end(kBoot2), image_.begin() + static_cast<std::ptrdiff_t>(i)))
            continue;
        const uint32_t n = (uint32_t(image_[i - 4]) << 24) | (uint32_t(image_[i - 3]) << 16) |
                           (uint32_t(image_[i - 2]) << 8) | image_[i - 1];
        if (n < 64 || n > 0x8000 || i + n > image_.size()) continue;
        boot2_.assign(image_.begin() + static_cast<std::ptrdiff_t>(i),
                      image_.begin() + static_cast<std::ptrdiff_t>(i + n));
        break;
    }
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
    drq_ = eop_ = aip_ = dma_ = pending_req_ = arb_ = sel_phase_ = false;
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
    pending_req_ = false;
}

void Ncr5380Hdd::update_match() {}

void Ncr5380Hdd::raise_eop() {
    eop_ = true;
    // System 7's SCSI Manager polls BSR INT after a DMA READ, even
    // when MR2 EOP-IE is clear. The pin stays off the 68000 IPL.
    irq_ = true;
}

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
    if (ack_)
        pending_req_ = true;
    else
        offer_byte();
}

void Ncr5380Hdd::extend_data_in() {
    // System 7's SCSI Manager TIB can ask for more bytes than the CDB
    // count (READ(6) is capped at one "track"). Stay in DATA IN and
    // keep reading sequential blocks until the initiator programs
    // STATUS or MESSAGE IN.
    if (!loaded_) return;
    const uint32_t next = last_lba_ + uint32_t(xfer_.size() / 512);
    if (next >= blocks_) return;
    const size_t off = xfer_.size();
    xfer_.resize(off + 512);
    std::memcpy(xfer_.data() + off, image_.data() + size_t(next) * 512, 512);
    xfer_done_ += 512;
}

void Ncr5380Hdd::execute() {
    last_cmd_ = cdb_[0];
    cmd_log_[cmd_count_ & 15] = last_cmd_;
    cmd_count_++;
    last_lba_ = 0;
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
        if (ack_)
            pending_req_ = true;
        else
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
        if (ack_)
            pending_req_ = true;
        else
            offer_byte();
        return;
    }
    if (op == 0x1a) {  // MODE SENSE(6) — pages match PCE/macplus
        const int n = cdb_[4] ? cdb_[4] : 4;
        const uint8_t page = uint8_t(cdb_[2] & 0x3f);
        xfer_.assign(64, 0);
        size_t len = 0;
        if (page == 0x01) {
            xfer_[0] = 0x01;
            xfer_[1] = 10;
            len = 12;
        } else if (page == 0x03) {
            xfer_[0] = 0x03;
            xfer_[1] = 22;
            len = 24;
        } else if (page == 0x04) {
            xfer_[0] = 0x04;
            xfer_[1] = 22;
            xfer_[5] = 1;
            xfer_[20] = 0x0e;
            xfer_[21] = 0x10;  // 3600 rpm
            len = 32;
        } else if (page == 0x30) {
            // Apple HD SC / System 7 SCSI Manager probe.
            xfer_[0] = 0x30;
            xfer_[1] = 33;
            std::memcpy(xfer_.data() + 14, "APPLE COMPUTER, INC", 19);
            len = 34;
        } else {
            xfer_[0] = 3;
            len = 4;
        }
        if (n < int(len)) xfer_.resize(size_t(n));
        else xfer_.resize(len);
        set_phase(kDataIn);
        if (ack_)
            pending_req_ = true;
        else
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
        if (ack_)
            pending_req_ = true;
        else
            offer_byte();
        return;
    }

    uint32_t lba = 0;
    uint32_t count = 0;
    bool writing = false;
    if (op == 0x08 || op == 0x0a) {
        lba = (uint32_t(cdb_[1] & 0x1f) << 16) | (uint32_t(cdb_[2]) << 8) | cdb_[3];
        last_lba_ = lba;
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
        if (ack_)
            pending_req_ = true;
        else {
            req_ = true;
            if (mode_ & kModeDma) drq_ = true;
        }
        return;
    }
    xfer_.resize(bytes);
    std::memcpy(xfer_.data(), image_.data() + size_t(lba) * 512, bytes);
    xfer_done_ += uint32_t(bytes);
    set_phase(kDataIn);
    if (ack_)
        pending_req_ = true;
    else
        offer_byte();
}

void Ncr5380Hdd::take_byte() {
    if (phase_ == kCommand) {
        if (cdb_pos_ < 12) cdb_[cdb_pos_] = odr_;
        if (cdb_pos_ == 0) cdb_len_ = cdb_length(odr_);
        cdb_pos_++;
        req_ = false;
        if (cdb_pos_ >= cdb_len_)
            execute();
        else
            pending_req_ = true;
        return;
    }
    if (phase_ == kDataOut) {
        if (xfer_pos_ < xfer_.size()) xfer_[xfer_pos_++] = odr_;
        req_ = false;
        drq_ = false;
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
                write_count_++;
                last_write_lba_ = lba;
                last_write_bytes_ = uint32_t(n);
                if (off < image_.size()) std::memcpy(image_.data() + off, xfer_.data(), n);
            }
            finish_command();
        } else {
            pending_req_ = true;
        }
        return;
    }
    if (phase_ == kDataIn) {
        xfer_pos_++;
        req_ = false;
        drq_ = false;
        if (xfer_pos_ >= xfer_.size()) extend_data_in();
        if (xfer_pos_ < xfer_.size())
            pending_req_ = true;
        else {
            finish_command();
            raise_eop();
        }
        return;
    }
    if (phase_ == kStatus) {
        req_ = false;
        set_phase(kMsgIn);
        idr_ = message_;
        pending_req_ = true;
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
    if (ack && req_)
        take_byte();
    else if (!ack && pending_req_) {
        pending_req_ = false;
        if (phase_ == kDataIn || phase_ == kStatus || phase_ == kMsgIn)
            offer_byte();
        else {
            req_ = true;
            if (phase_ == kDataOut && (mode_ & kModeDma)) drq_ = true;
        }
    }
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
        if (pending_req_) {
            pending_req_ = false;
            offer_byte();
        }
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
        if (pending_req_) {
            pending_req_ = false;
            req_ = true;
            drq_ = true;
        }
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
            if (sel_ && loaded_ && (odr_ & 0x01)) {
                bsy_ = true;
                irq_ = true;  // PCE: selection raises 5380 INT (BSR only)
            }
            // PCE/macplus: SEL while arbitrating enters the selection phase.
            if (arb_ && sel_) sel_phase_ = true;
            // Wait for the SCSI Manager to set TCR to COMMAND. Starting
            // the phase on SEL-drop clocks a zero CDB if ODR is still 0.
            if (!sel_ && bsy_ && phase_ == kFree && (tcr_ & 7) == kCommand) start_command();
            break;
        }
        case 2: {
            // PCE/macplus: MR2 ARB starts arbitration; dropping ARB after
            // SEL is how System 7's SCSI Manager enters COMMAND.
            const bool want_arb = (data & kModeArb) != 0;
            if (want_arb && !arb_ && phase_ == kFree) {
                arb_ = true;
                aip_ = true;
            }
            if (!want_arb && arb_) {
                arb_ = false;
                aip_ = false;
                if (sel_phase_ && loaded_ && (odr_ & 0x01) && phase_ == kFree) {
                    bsy_ = true;
                    start_command();
                }
                sel_phase_ = false;
            }
            mode_ = data;
            if ((data & 0x08) && eop_) irq_ = true;
            if (!(data & kModeDma)) {
                dma_ = false;
                drq_ = false;
            }
            break;
        }
        case 3:
            tcr_ = data;
            if ((data & 7) == kCommand && bsy_ && phase_ == kFree) start_command();
            // The Plus SCSI Manager often DMA-reads fewer bytes than the
            // CDB requested (256 of a 512-byte DDM). When it programs STATUS
            // or MESSAGE IN, follow that phase even if data remains.
            if ((data & 7) == kStatus && (phase_ == kDataIn || phase_ == kDataOut)) {
                finish_command();
            } else if ((data & 7) == kMsgIn &&
                       (phase_ == kStatus || phase_ == kDataIn || phase_ == kDataOut)) {
                req_ = false;
                set_phase(kMsgIn);
                idr_ = message_;
                offer_byte();
            }
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
    // MAME mac128_state::scsi_r: register = (word_offset >> 3) & 0xf,
    // DMA when word_offset >= 0x100 ($580000 + $200, A9). IRQ stays off
    // the 68000 IPL — Plus pin 23 is unconnected.
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
