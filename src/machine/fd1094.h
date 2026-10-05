#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Hitachi FD1094 encrypted 68000, ported from MAME fd1094.cpp.
class Fd1094 {
public:
    static constexpr int kKeySize = 0x2000;
    static constexpr int kStateReset = 0x0100;
    static constexpr int kStateIrq = 0x0200;
    static constexpr int kStateRte = 0x0300;

    using StateChangeHandler = std::function<void(uint8_t state)>;

    void set_key(const uint8_t* key, size_t size);
    void set_state_change_handler(StateChangeHandler handler) {
        state_change_ = std::move(handler);
    }

    void reset();
    void change_state(int newstate);
    void on_cmpild(uint8_t reg, uint32_t data);
    void on_irq();
    void on_rte();

    uint8_t state() const { return irq_mode_ ? key_[0] : state_; }
    bool irq_mode() const { return irq_mode_; }
    bool ready() const { return key_ready_; }

    // Decrypt one program-space word. `word_address` is physical_address / 2.
    static uint16_t decrypt_one(uint32_t word_address, uint16_t val, const uint8_t* main_key,
                                uint8_t state, bool vector_fetch);

    // Decrypt an entire ROM image for the given state into `opcodes`.
    void decrypt(const uint16_t* src, uint16_t* opcodes, uint32_t bytes, uint8_t state) const;

    // Lazily-cached decrypted opcode view for the current state.
    const uint16_t* decrypted_opcodes(const uint16_t* src, uint32_t bytes);

private:
    static void ensure_masked_lookup();

    std::array<uint8_t, kKeySize> key_{};
    bool key_ready_ = false;
    uint8_t state_ = 0;
    bool irq_mode_ = false;
    StateChangeHandler state_change_;

    std::array<std::vector<uint16_t>, 256> cache_{};
    const uint16_t* cache_src_ = nullptr;
    uint32_t cache_bytes_ = 0;

    static std::array<std::array<uint8_t, 0x1000>, 2> masked_opcodes_lookup_;
    static bool masked_ready_;
};

}  // namespace dsp
