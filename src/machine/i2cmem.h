#pragma once

#include <cstdint>
#include <vector>

namespace dsp {

// Generic bit-banged I2C serial EEPROM, ported from i2cmem.pas.
// Only the two variants used by ColecoVision cartridges are supported:
// the 24C08 (1 KiB, 1 address byte + 2 block-select bits from the device
// address) and the 24C256 (32 KiB, 2 address bytes).
class I2CMem {
public:
    enum class Type { C08, C256 };

    explicit I2CMem(Type type);

    void reset();

    // CPU facing pins (bagman.pas style booleans, coleco.pas calls these
    // with 0/1 through write_scl(0)/write_scl(1)).
    void write_scl(bool level);
    void write_sda(bool level);
    bool read_sda() const;

    // Loads/dumps the whole memory image, used for .nv save files.
    void load_data(const std::vector<uint8_t>& data);
    void write_data(std::vector<uint8_t>& out) const;

    size_t size() const { return memory_.size(); }

private:
    enum class Phase { DeviceAddr, WordAddr, Data };

    void handle_received_byte(uint8_t byte);
    void prepare_read_data();

    Type type_;
    int addr_bytes_;  // 1 (24C08) or 2 (24C256) word-address bytes
    std::vector<uint8_t> memory_;

    bool scl_ = false;
    bool bus_sda_ = true;

    bool started_ = false;
    Phase phase_ = Phase::DeviceAddr;
    int bitpos_ = 0;
    uint8_t shift_ = 0;

    bool write_mode_ = true;
    int block_select_ = 0;
    int addr_byte_count_ = 0;
    uint8_t addr_high_ = 0;
    uint32_t address_ = 0;

    bool driving_ = false;
    bool driving_ack_ = false;
    bool pending_first_read_ = false;
    bool awaiting_master_ack_ = false;
    uint8_t data_byte_ = 0;
    int data_bitpos_ = 0;
};

}  // namespace dsp
