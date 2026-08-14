#include "machine/i2cmem.h"

#include <algorithm>

namespace dsp {

I2CMem::I2CMem(Type type) : type_(type) {
    if (type_ == Type::C08) {
        addr_bytes_ = 1;
        memory_.assign(1024, 0xff);
    } else {
        addr_bytes_ = 2;
        memory_.assign(32768, 0xff);
    }
    reset();
}

void I2CMem::reset() {
    scl_ = false;
    bus_sda_ = true;
    started_ = false;
    phase_ = Phase::DeviceAddr;
    bitpos_ = 0;
    shift_ = 0;
    write_mode_ = true;
    block_select_ = 0;
    addr_byte_count_ = 0;
    addr_high_ = 0;
    address_ = 0;
    driving_ = false;
    driving_ack_ = false;
    pending_first_read_ = false;
    awaiting_master_ack_ = false;
    data_byte_ = 0;
    data_bitpos_ = 0;
}

void I2CMem::load_data(const std::vector<uint8_t>& data) {
    size_t count = std::min(data.size(), memory_.size());
    std::copy(data.begin(), data.begin() + long(count), memory_.begin());
}

void I2CMem::write_data(std::vector<uint8_t>& out) const {
    out.assign(memory_.begin(), memory_.end());
}

void I2CMem::prepare_read_data() {
    address_ = address_ % memory_.size();
    data_byte_ = memory_[address_];
    address_ = uint32_t((address_ + 1) % memory_.size());
    data_bitpos_ = 0;
    driving_ = true;
    driving_ack_ = false;
}

void I2CMem::handle_received_byte(uint8_t byte) {
    switch (phase_) {
        case Phase::DeviceAddr:
            write_mode_ = (byte & 1) == 0;
            block_select_ = (byte >> 1) & 0x07;
            if (write_mode_) {
                phase_ = Phase::WordAddr;
                addr_byte_count_ = 0;
                pending_first_read_ = false;
            } else {
                phase_ = Phase::Data;
                pending_first_read_ = true;
            }
            break;
        case Phase::WordAddr:
            addr_byte_count_++;
            if (addr_bytes_ == 1) {
                address_ = uint32_t((block_select_ << 8) | byte);
                phase_ = Phase::Data;
            } else if (addr_byte_count_ == 1) {
                addr_high_ = byte;
            } else {
                address_ = uint32_t((addr_high_ << 8) | byte);
                phase_ = Phase::Data;
            }
            break;
        case Phase::Data:
            if (!memory_.empty()) {
                memory_[address_ % memory_.size()] = byte;
                address_ = uint32_t((address_ + 1) % memory_.size());
            }
            break;
    }
}

void I2CMem::write_sda(bool level) {
    if (scl_) {
        if (bus_sda_ && !level) {  // falling edge while SCL high: START
            started_ = true;
            phase_ = Phase::DeviceAddr;
            bitpos_ = 0;
            shift_ = 0;
            driving_ = false;
            driving_ack_ = false;
            addr_byte_count_ = 0;
        } else if (!bus_sda_ && level) {  // rising edge while SCL high: STOP
            started_ = false;
            driving_ = false;
            driving_ack_ = false;
            awaiting_master_ack_ = false;
        }
    }
    bus_sda_ = level;
}

void I2CMem::write_scl(bool level) {
    bool rising = !scl_ && level;
    bool falling = scl_ && !level;
    scl_ = level;
    if (!started_) return;

    if (rising) {
        if (awaiting_master_ack_) {
            bool nack = bus_sda_;
            awaiting_master_ack_ = false;
            if (!nack) prepare_read_data();
        } else if (!driving_) {
            shift_ = uint8_t((shift_ << 1) | (bus_sda_ ? 1 : 0));
            bitpos_++;
            if (bitpos_ == 8) {
                bitpos_ = 0;
                uint8_t byte = shift_;
                shift_ = 0;
                handle_received_byte(byte);
                driving_ = true;
                driving_ack_ = true;
            }
        }
        // While driving_ (data-out phase) or driving_ack_, the bit is sampled
        // by the master but does not change until the next falling edge.
    } else if (falling) {
        if (driving_ack_) {
            driving_ack_ = false;
            if (pending_first_read_) {
                pending_first_read_ = false;
                prepare_read_data();
            } else {
                driving_ = false;
            }
        } else if (driving_) {
            data_bitpos_++;
            if (data_bitpos_ == 8) {
                driving_ = false;
                awaiting_master_ack_ = true;
            }
        }
    }
}

bool I2CMem::read_sda() const {
    if (driving_ack_) return false;  // always ACK
    if (driving_) return ((data_byte_ >> (7 - data_bitpos_)) & 1) != 0;
    return true;  // bus released, pulled up
}

}  // namespace dsp
