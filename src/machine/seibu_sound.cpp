#include "machine/seibu_sound.h"

#include <algorithm>
#include <cstring>

#include "cpu/irq_line.h"

namespace dsp {
namespace {

constexpr uint8_t bit(uint16_t v, int n) { return uint8_t((v >> n) & 1); }

uint8_t bitswap8(uint8_t src, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0) {
    auto pick = [&](int b) { return uint8_t(((src >> b) & 1)); };
    return uint8_t((pick(b7) << 7) | (pick(b6) << 6) | (pick(b5) << 5) | (pick(b4) << 4) |
                   (pick(b3) << 3) | (pick(b2) << 2) | (pick(b1) << 1) | pick(b0));
}

}  // namespace

SeibuSound::SeibuSound(uint32_t clock)
    : clock_(clock),
      z80_(clock),
      ym_(clock),
      msm0_(kMsmClock, 48, 4),
      msm1_(kMsmClock, 48, 4) {
    z80_.set_memory_handlers([this](uint16_t a) { return read(a); },
                             [this](uint16_t a, uint8_t v) { write(a, v); });
    z80_.set_opcode_read([this](uint16_t a) { return read_opcode(a); });
    z80_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });
    z80_.set_irq_ack_callback([this] {
        // Matches seibu_m0_vector: acknowledge and expose the IM0 RST opcode
        // that take_irq() reads from irq_vector_ after this callback returns.
        uint8_t vec = 0;
        if (rst18_irq_ && !rst18_service_) {
            rst18_irq_ = false;
            rst18_service_ = true;
            vec = 0xdf;
        } else if (rst10_irq_ && !rst10_service_) {
            rst10_service_ = true;
            vec = 0xd7;
        }
        current_irq_vector_ = vec;
        const bool still =
            (rst10_irq_ && !rst10_service_) || (rst18_irq_ && !rst18_service_);
        z80_.set_irq(still ? IrqLine::Assert : IrqLine::Clear, vec);
    });
    ym_.set_irq_handler([this](bool state) { on_ym_irq(state); });
}

bool SeibuSound::init_roms(const uint8_t* boot, size_t boot_size, const uint8_t* bank,
                           size_t bank_size, bool encrypted, std::string* error) {
    if (!boot || boot_size < 0x2000) {
        if (error) *error = "Seibu sound boot ROM too small";
        return false;
    }
    mem_.fill(0);
    std::memcpy(decrypt_.data(), boot, 0x2000);
    if (encrypted) {
        decrypt_sound();
    } else {
        std::memcpy(mem_.data(), boot, 0x2000);
        std::memcpy(decrypt_.data(), boot, 0x2000);
    }
    if (bank && bank_size > 0) {
        const size_t n = std::min(bank_size, size_t(0x8000));
        std::memcpy(mem_.data() + 0x8000, bank, n);
    }
    return true;
}

void SeibuSound::load_adpcm_roms(const uint8_t* data, size_t bank_size) {
    if (!data || bank_size == 0) return;
    std::vector<uint8_t> a(bank_size), b(bank_size);
    for (size_t i = 0; i < bank_size; ++i) {
        a[i] = bitswap8(data[i], 7, 5, 3, 1, 6, 4, 2, 0);
        b[i] = bitswap8(data[bank_size + i], 7, 5, 3, 1, 6, 4, 2, 0);
    }
    msm0_.set_rom(std::move(a));
    msm1_.set_rom(std::move(b));
}

void SeibuSound::reset() {
    rst10_irq_ = rst18_irq_ = false;
    rst10_service_ = rst18_service_ = false;
    main2sub_pending_ = sub2main_pending_ = false;
    sound_latch_.fill(0);
    sub2main_.fill(0);
    input_ = 0xfc;
    current_irq_vector_ = 0;
    msm_accum_ = audio_accum_ = 0;
    audio_.clear();
    z80_.reset();
    ym_.reset();
    msm0_.reset();
    msm1_.reset();
    msm0_.set_reset(true);
    msm1_.set_reset(true);
    z80_.set_irq(IrqLine::Clear);
}

int SeibuSound::run(int cycles) {
    if (cycles <= 0) return 0;
    return z80_.run(cycles);
}

uint8_t SeibuSound::main_read(int offset) const {
    switch (offset & 7) {
        case 2: return sub2main_[0];
        case 3: return sub2main_[1];
        case 5: return sub2main_pending_ ? 1 : 0;
        default: return 0xff;
    }
}

void SeibuSound::main_write(int offset, uint8_t value) {
    switch (offset & 7) {
        case 0: sound_latch_[0] = value; break;
        case 1: sound_latch_[1] = value; break;
        case 4: update_irq_lines(Rst18Assert); break;
        case 2:
        case 6:
            sub2main_pending_ = false;
            main2sub_pending_ = true;
            break;
        default: break;
    }
}

void SeibuSound::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

uint8_t SeibuSound::read(uint16_t address) {
    if (address <= 0x1fff) return mem_[address];
    if (address >= 0x2000 && address <= 0x27ff) return mem_[address];
    if (address >= 0x8000) return mem_[address];
    switch (address) {
        case 0x4008: return ym_.status();
        case 0x4010: return sound_latch_[0];
        case 0x4011: return sound_latch_[1];
        case 0x4012: return main2sub_pending_ ? 1 : 0;
        case 0x4013: return input_;
        default: return 0xff;
    }
}

uint8_t SeibuSound::read_opcode(uint16_t address) {
    if (address <= 0x1fff) return decrypt_[address];
    return read(address);
}

void SeibuSound::write(uint16_t address, uint8_t value) {
    if (address >= 0x2000 && address <= 0x27ff) {
        mem_[address] = value;
        return;
    }
    switch (address) {
        case 0x4001: update_irq_lines(Rst18Eoi); break;
        case 0x4002: update_irq_lines(Rst10Eoi); break;
        case 0x4003: update_irq_lines(Rst18Eoi); break;
        case 0x4005: msm0_.set_start(uint32_t(value) << 8); break;
        case 0x4006: msm0_.set_end(uint32_t(value) << 8); break;
        case 0x4008: ym_.select_register(value); break;
        case 0x4009: ym_.write(value); break;
        case 0x4018: sub2main_[0] = value; break;
        case 0x4019: sub2main_[1] = value; break;
        case 0x401a:
            if (value == 0) msm0_.set_reset(true);
            else if (value == 1) msm0_.set_reset(false);
            break;
        case 0x6005: msm1_.set_start(uint32_t(value) << 8); break;
        case 0x6006: msm1_.set_end(uint32_t(value) << 8); break;
        case 0x601a:
            if (value == 0) msm1_.set_reset(true);
            else if (value == 1) msm1_.set_reset(false);
            break;
        default: break;
    }
}

void SeibuSound::update_irq_lines(IrqOp op) {
    switch (op) {
        case VectorInit:
            rst10_irq_ = rst18_irq_ = false;
            rst10_service_ = rst18_service_ = false;
            break;
        case Rst10Assert: rst10_irq_ = true; break;
        case Rst10Clear: rst10_irq_ = false; break;
        case Rst10Ack: rst10_service_ = true; break;
        case Rst10Eoi: rst10_service_ = false; break;
        case Rst18Assert: rst18_irq_ = true; break;
        case Rst18Ack:
            rst18_irq_ = false;
            rst18_service_ = true;
            break;
        case Rst18Eoi: rst18_service_ = false; break;
    }
    refresh_irq_line();
}

void SeibuSound::refresh_irq_line() {
    const bool pending =
        (rst10_irq_ && !rst10_service_) || (rst18_irq_ && !rst18_service_);
    if (pending) {
        // Prefer RST18 when both are pending (matches seibu_m0_vector).
        const uint8_t vec = (rst18_irq_ && !rst18_service_) ? 0xdf : 0xd7;
        current_irq_vector_ = vec;
        z80_.set_irq(IrqLine::Assert, vec);
    } else {
        z80_.set_irq(IrqLine::Clear, current_irq_vector_);
    }
}

void SeibuSound::on_ym_irq(bool state) {
    update_irq_lines(state ? Rst10Assert : Rst10Clear);
}

void SeibuSound::on_cycles(int cycles) {
    ym_.run_timers(cycles);

    msm_accum_ += int64_t(cycles) * int64_t(msm0_.sample_frequency());
    while (msm_accum_ >= int64_t(clock_)) {
        msm_accum_ -= int64_t(clock_);
        msm0_.vclk();
        msm1_.vclk();
    }

    audio_accum_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accum_ >= int64_t(clock_)) {
        audio_accum_ -= int64_t(clock_);
        // YM amp ~1.0, each MSM ~0.4 (seibu_sound.pas / msm5205 create).
        const int32_t sample =
            ym_.update() + (msm0_.output() + msm1_.output()) * 2 / 5;
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

uint8_t SeibuSound::decrypt_data(uint16_t a, uint8_t src) {
    if (bit(a, 9) && bit(a, 8)) src = uint8_t(src ^ 0x80);
    if (bit(a, 11) && bit(a, 4) && bit(a, 1)) src = uint8_t(src ^ 0x40);
    if (bit(a, 11) && !bit(a, 8) && bit(a, 1)) src = uint8_t(src ^ 4);
    if (bit(a, 13) && !bit(a, 6) && bit(a, 4)) src = uint8_t(src ^ 2);
    if (!bit(a, 11) && bit(a, 9) && bit(a, 2)) src = uint8_t(src ^ 1);
    if (bit(a, 13) && bit(a, 4)) src = bitswap8(src, 7, 6, 5, 4, 3, 2, 0, 1);
    if (bit(a, 8) && bit(a, 4)) src = bitswap8(src, 7, 6, 5, 4, 2, 3, 1, 0);
    return src;
}

uint8_t SeibuSound::decrypt_opcode(uint16_t a, uint8_t src) {
    if (bit(a, 9) && bit(a, 8)) src = uint8_t(src ^ 0x80);
    if (bit(a, 11) && bit(a, 4) && bit(a, 1)) src = uint8_t(src ^ 0x40);
    if (!bit(a, 13) && bit(a, 12)) src = uint8_t(src ^ 0x20);
    if (!bit(a, 6) && bit(a, 1)) src = uint8_t(src ^ 0x10);
    if (!bit(a, 12) && bit(a, 2)) src = uint8_t(src ^ 8);
    if (bit(a, 11) && !bit(a, 8) && bit(a, 1)) src = uint8_t(src ^ 4);
    if (bit(a, 13) && !bit(a, 6) && bit(a, 4)) src = uint8_t(src ^ 2);
    if (!bit(a, 11) && bit(a, 9) && bit(a, 2)) src = uint8_t(src ^ 1);
    if (bit(a, 13) && bit(a, 4)) src = bitswap8(src, 7, 6, 5, 4, 3, 2, 0, 1);
    if (bit(a, 8) && bit(a, 4)) src = bitswap8(src, 7, 6, 5, 4, 2, 3, 1, 0);
    if (bit(a, 12) && bit(a, 9)) src = bitswap8(src, 7, 6, 4, 5, 3, 2, 1, 0);
    if (bit(a, 11) && !bit(a, 6)) src = bitswap8(src, 6, 7, 5, 4, 3, 2, 1, 0);
    return src;
}

void SeibuSound::decrypt_sound() {
    std::array<uint8_t, 0x2000> data_in = decrypt_;
    for (uint16_t f = 0; f < 0x2000; ++f) {
        mem_[f] = decrypt_data(f, data_in[f]);
        decrypt_[f] = decrypt_opcode(f, data_in[f]);
    }
}

}  // namespace dsp
