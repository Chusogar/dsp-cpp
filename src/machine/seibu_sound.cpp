#include "machine/seibu_sound.h"

#include <algorithm>

namespace dsp {

namespace {

inline bool bit(uint32_t v, int n) { return (v >> n) & 1; }

uint8_t bitswap8(uint8_t v, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0) {
    return uint8_t(bit(v, b7) << 7 | bit(v, b6) << 6 | bit(v, b5) << 5 | bit(v, b4) << 4 |
                   bit(v, b3) << 3 | bit(v, b2) << 2 | bit(v, b1) << 1 | bit(v, b0));
}

uint8_t decrypt_data(uint32_t a, uint8_t src) {
    if (bit(a, 9) && bit(a, 8)) src ^= 0x80;
    if (bit(a, 11) && bit(a, 4) && bit(a, 1)) src ^= 0x40;
    if (bit(a, 11) && !bit(a, 8) && bit(a, 1)) src ^= 0x04;
    if (bit(a, 13) && !bit(a, 6) && bit(a, 4)) src ^= 0x02;
    if (!bit(a, 11) && bit(a, 9) && bit(a, 2)) src ^= 0x01;
    if (bit(a, 13) && bit(a, 4)) src = bitswap8(src, 7, 6, 5, 4, 3, 2, 0, 1);
    if (bit(a, 8) && bit(a, 4)) src = bitswap8(src, 7, 6, 5, 4, 2, 3, 1, 0);
    return src;
}

uint8_t decrypt_opcode(uint32_t a, uint8_t src) {
    if (bit(a, 9) && bit(a, 8)) src ^= 0x80;
    if (bit(a, 11) && bit(a, 4) && bit(a, 1)) src ^= 0x40;
    if (!bit(a, 13) && bit(a, 12)) src ^= 0x20;
    if (!bit(a, 6) && bit(a, 1)) src ^= 0x10;
    if (!bit(a, 12) && bit(a, 2)) src ^= 0x08;
    if (bit(a, 11) && !bit(a, 8) && bit(a, 1)) src ^= 0x04;
    if (bit(a, 13) && !bit(a, 6) && bit(a, 4)) src ^= 0x02;
    if (!bit(a, 11) && bit(a, 9) && bit(a, 2)) src ^= 0x01;
    if (bit(a, 13) && bit(a, 4)) src = bitswap8(src, 7, 6, 5, 4, 3, 2, 0, 1);
    if (bit(a, 8) && bit(a, 4)) src = bitswap8(src, 7, 6, 5, 4, 2, 3, 1, 0);
    if (bit(a, 12) && bit(a, 9)) src = bitswap8(src, 7, 6, 4, 5, 3, 2, 1, 0);
    if (bit(a, 11) && !bit(a, 6)) src = bitswap8(src, 6, 7, 5, 4, 3, 2, 1, 0);
    return src;
}

}  // namespace

SeibuSound::SeibuSound(uint32_t clock, uint32_t oki_clock, Variant variant)
    : clock_(clock), variant_(variant), z80_(clock), ym_(clock), oki_(oki_clock, true) {
    if (variant_ == Variant::Adpcm) {
        ym2151_ = std::make_unique<YM2151>(clock);
        ym2151_->set_irq_handler([this](bool on) { update_irq_lines(on ? kRst10Assert : kRst10Clear); });
        // 12 MHz / 32 with the /48 prescaler: 7812.5 Hz, 4-bit samples.
        for (auto& chip : adpcm_) chip = std::make_unique<MSM5205>(12000000 / 32, 48, 4);
    }
    z80_.set_memory_handlers([this](uint16_t a) { return read(a); },
                             [this](uint16_t a, uint8_t v) { write(a, v); });
    z80_.set_opcode_read([this](uint16_t a) { return a < 0x2000 ? opcode_[a] : read(a); });
    z80_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });
    // IM 0: the interrupt controller puts RST 18h (main CPU command) or RST 10h
    // (YM3812) on the bus, RST 18h first; acknowledging marks it in service.
    z80_.set_irq_ack_callback([this] {
        uint8_t vector = 0x00;
        if (rst18_irq_ && !rst18_service_) {
            update_irq_lines(kRst18Acknowledge);
            vector = 0xdf;
        } else if (rst10_irq_ && !rst10_service_) {
            update_irq_lines(kRst10Acknowledge);
            vector = 0xd7;
        }
        const bool pending = (rst10_irq_ && !rst10_service_) || (rst18_irq_ && !rst18_service_);
        z80_.set_irq(pending ? IrqLine::Assert : IrqLine::Clear, vector);
    });
    ym_.set_irq_handler([this](bool on) { update_irq_lines(on ? kRst10Assert : kRst10Clear); });
}

void SeibuSound::load_program(const std::vector<uint8_t>& rom, bool encrypted) {
    std::vector<uint8_t> r(rom);
    r.resize(0x10000, 0xff);
    for (uint32_t a = 0; a < 0x2000; ++a) {
        data_[a] = encrypted ? decrypt_data(a, r[a]) : r[a];
        opcode_[a] = encrypted ? decrypt_opcode(a, r[a]) : r[a];
    }
    std::copy_n(r.begin() + 0x8000, 0x8000, banks_[0].begin());
    std::copy_n(r.begin(), 0x8000, banks_[1].begin());
}

void SeibuSound::load_adpcm(const std::vector<uint8_t>& channel0, const std::vector<uint8_t>& channel1) {
    if (variant_ != Variant::Adpcm) return;
    const std::vector<uint8_t>* roms[2] = {&channel0, &channel1};
    for (int i = 0; i < 2; ++i) {
        std::vector<uint8_t> rom(roms[i]->size());
        for (size_t f = 0; f < rom.size(); ++f) rom[f] = bitswap8((*roms[i])[f], 7, 5, 3, 1, 6, 4, 2, 0);
        adpcm_[i]->set_rom(std::move(rom));
    }
}

void SeibuSound::adpcm_write(int channel, uint16_t address, uint8_t value) {
    MSM5205& chip = *adpcm_[channel];
    switch (address & 0xff) {
        case 0x05: chip.set_start(uint32_t(value) << 8); break;
        case 0x06: chip.set_end(uint32_t(value) << 8); break;
        case 0x1a:
            if (value == 0) chip.set_reset(true);
            else if (value == 1) chip.set_reset(false);
            break;
        default: break;
    }
}

void SeibuSound::reset() {
    rst10_irq_ = rst18_irq_ = rst10_service_ = rst18_service_ = false;
    main2sub_pending_ = sub2main_pending_ = false;
    sound_latch_.fill(0);
    sub2main_.fill(0);
    ram_.fill(0);
    bank_ = 0;
    rst18_count_ = 0;
    ym_accum_ = oki_accum_ = adpcm_accum_ = 0;
    oki_level_ = 0;
    audio_.clear();
    z80_.reset();
    z80_.set_irq(IrqLine::Clear);
    ym_.reset();
    oki_.reset();
    if (variant_ == Variant::Adpcm) {
        ym2151_->reset();
        for (auto& chip : adpcm_) chip->reset();
    }
}

uint8_t SeibuSound::read(uint16_t a) {
    if (a < 0x2000) return data_[a];
    if (a < 0x2800) return ram_[a & 0x7ff];
    if (a >= 0x8000) return banks_[bank_][a & 0x7fff];
    switch (a) {
        case 0x4008:
        case 0x4009: return variant_ == Variant::Adpcm ? ym2151_->status() : ym_.status();
        case 0x4010: return sound_latch_[0];
        case 0x4011: return sound_latch_[1];
        case 0x4012: return sub2main_pending_ ? 1 : 0;
        case 0x4013: return coins_;
        case 0x6000: return variant_ == Variant::Oki ? oki_.read() : 0xff;
        default: return 0xff;
    }
}

void SeibuSound::write(uint16_t a, uint8_t v) {
    if (a >= 0x2000 && a < 0x2800) {
        ram_[a & 0x7ff] = v;
        return;
    }
    if (variant_ == Variant::Adpcm) {
        switch (a) {
            case 0x4005:
            case 0x4006:
            case 0x401a: adpcm_write(0, a, v); return;
            case 0x6005:
            case 0x6006:
            case 0x601a: adpcm_write(1, a, v); return;
            case 0x4007: return;  // no banking
            case 0x4008: ym2151_->select_register(v); return;
            case 0x4009: ym2151_->write(v); return;
            case 0x6000: return;
            default: break;
        }
    }
    switch (a) {
        case 0x4000:  // reply posted
            main2sub_pending_ = false;
            sub2main_pending_ = true;
            break;
        case 0x4001: update_irq_lines(kRst18Eoi); break;
        case 0x4002: update_irq_lines(kRst10Eoi); break;
        case 0x4003: update_irq_lines(kRst18Eoi); break;
        case 0x4007: bank_ = v & 1; break;
        case 0x4008: ym_.control(v); break;
        case 0x4009: ym_.write(v); break;
        case 0x4018: sub2main_[0] = v; break;
        case 0x4019: sub2main_[1] = v; break;
        case 0x6000: oki_.write(v); break;
        default: break;  // 401b coin counters
    }
}

uint8_t SeibuSound::main_read(int offset) const {
    switch (offset) {
        case 2: return sub2main_[0];
        case 3: return sub2main_[1];
        case 5: return main2sub_pending_ ? 1 : 0;
        default: return 0xff;
    }
}

void SeibuSound::main_write(int offset, uint8_t value) {
    switch (offset) {
        case 0: sound_latch_[0] = value; break;
        case 1: sound_latch_[1] = value; break;
        case 4:
            ++rst18_count_;
            update_irq_lines(kRst18Assert);
            break;
        case 2:
        case 6:
            sub2main_pending_ = false;
            main2sub_pending_ = true;
            break;
        default: break;
    }
}

void SeibuSound::update_irq_lines(IrqEvent event) {
    switch (event) {
        case kVectorInit: rst10_irq_ = rst18_irq_ = rst10_service_ = rst18_service_ = false; break;
        case kRst10Assert: rst10_irq_ = true; break;
        case kRst10Clear: rst10_irq_ = false; break;
        case kRst10Acknowledge: rst10_service_ = true; break;
        case kRst10Eoi: rst10_service_ = false; break;
        case kRst18Assert: rst18_irq_ = true; break;
        case kRst18Acknowledge:
            rst18_irq_ = false;
            rst18_service_ = true;
            break;
        case kRst18Eoi: rst18_service_ = false; break;
    }
    const bool pending = (rst10_irq_ && !rst10_service_) || (rst18_irq_ && !rst18_service_);
    z80_.set_irq(pending ? IrqLine::Assert : IrqLine::Clear,
                 rst18_irq_ && !rst18_service_ ? 0xdf : 0xd7);
}

int SeibuSound::run(int cycles) { return z80_.run(cycles); }

void SeibuSound::on_cycles(int cycles) {
    if (variant_ == Variant::Adpcm) {
        ym2151_->run_timers(cycles);  // same clock as the Z80
        // Both ADPCM channels are clocked at 7812.5 Hz and held in between.
        adpcm_accum_ += int64_t(cycles) * adpcm_[0]->sample_frequency();
        while (adpcm_accum_ >= int64_t(clock_)) {
            adpcm_accum_ -= int64_t(clock_);
            adpcm_[0]->vclk();
            adpcm_[1]->vclk();
        }
        ym_accum_ += int64_t(cycles) * kSampleRate;
        while (ym_accum_ >= int64_t(clock_)) {
            ym_accum_ -= int64_t(clock_);
            const int32_t s = (ym2151_->update() + adpcm_[0]->output() + adpcm_[1]->output()) / 2;
            audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
        }
        return;
    }
    // The OKI makes a new sample at clock/132 Hz; it is held between them.
    oki_accum_ += int64_t(cycles) * oki_.sample_frequency();
    while (oki_accum_ >= int64_t(clock_)) {
        oki_accum_ -= int64_t(clock_);
        oki_level_ = oki_.update();
    }
    // The YM3812 timers advance per sample, in step with the Z80.
    ym_accum_ += int64_t(cycles) * kSampleRate;
    while (ym_accum_ >= int64_t(clock_)) {
        ym_accum_ -= int64_t(clock_);
        const int32_t s = ym_.update() + oki_level_ * 2 / 5;
        audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
    }
}

void SeibuSound::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
