#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// NCR 5380 plus one SCSI direct-access disk, mapped the Macintosh Plus way
// at $580000 (register = A6-A4, DACK = A9). IRQ is left unconnected.
class Ncr5380Hdd {
public:
    void reset();
    bool load_file(const std::string& path, std::string* error);
    bool loaded() const { return loaded_; }
    uint32_t blocks() const { return blocks_; }
    uint8_t last_cmd() const { return last_cmd_; }
    uint32_t xfer_bytes() const { return xfer_done_; }
    uint32_t access_count() const { return accesses_; }
    uint8_t last_icr() const { return icr_; }
    uint8_t last_mode() const { return mode_; }
    bool selected() const { return bsy_; }

    uint8_t read(uint32_t address);
    void write(uint32_t address, uint8_t data);

private:
    enum Phase : uint8_t {
        kFree = 0,
        kDataOut = 0,
        kDataIn = 1,
        kCommand = 2,
        kStatus = 3,
        kMsgOut = 6,
        kMsgIn = 7,
    };

    static int cdb_length(uint8_t opcode);
    void wrap_raw_hfs();
    void bus_reset();
    void set_phase(uint8_t phase);
    void update_match();
    void on_ack(bool ack);
    void take_byte();
    void offer_byte();
    void start_command();
    void finish_command();
    void execute();
    uint8_t read_reg(int reg, bool dack);
    void write_reg(int reg, bool dack, uint8_t data);
    uint8_t csr() const;
    uint8_t bsr() const;

    std::vector<uint8_t> image_;
    uint32_t blocks_ = 0;
    bool loaded_ = false;

    uint8_t odr_ = 0;
    uint8_t icr_ = 0;
    uint8_t mode_ = 0;
    uint8_t tcr_ = 0;
    uint8_t ser_ = 0;
    uint8_t idr_ = 0;

    bool rst_ = false;
    bool bsy_ = false;
    bool sel_ = false;
    bool req_ = false;
    bool ack_ = false;
    bool atn_ = false;
    bool drq_ = false;
    bool eop_ = false;
    bool irq_ = false;
    bool aip_ = false;
    bool dma_ = false;
    uint8_t phase_ = kFree;

    uint8_t cdb_[12]{};
    int cdb_len_ = 0;
    int cdb_pos_ = 0;
    uint8_t status_ = 0;
    uint8_t message_ = 0;
    uint8_t last_cmd_ = 0;

    std::vector<uint8_t> xfer_;
    size_t xfer_pos_ = 0;
    uint32_t xfer_done_ = 0;
    uint32_t accesses_ = 0;
};

}  // namespace dsp
