#include "machine/c1541.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace dsp {
namespace {

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }

    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n <= 0) {
        return false;
    }

    f.seekg(0, std::ios::beg);

    out->resize(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(out->data()), n);

    return bool(f);
}

}  // namespace

C1541::C1541()
    : cpu_(kClock),
      via1_(kClock),
      via2_(kClock) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) -> uint8_t { return read_mem(a); },
        [this](uint16_t a, uint8_t v) { write_mem(a, v); });

    /*
        VIA1 at $1800 -- IEC serial bus.
        PB0 = DATA IN     (1 = line high)
        PB1 = DATA OUT    (1 = pull line low, inverting driver)
        PB2 = CLK IN
        PB3 = CLK OUT     (1 = pull line low)
        PB4 = ATNA        (ATN acknowledge)
        PB5/PB6 = device number jumpers (both open = #8)
        PB7 = ATN IN
        CA1 = ATN IN, the DOS acknowledges ATN from its interrupt handler.

        The drive pulls DATA low in hardware whenever ATNA disagrees with the
        ATN line, which is how every drive answers an ATN sequence before its
        CPU has even looked at the bus.
    */
    via1_.set_port_b(
        [this]() -> uint8_t {
            uint8_t v = 0x00;

            if (!bus_data()) v = uint8_t(v | 0x01);
            if (!bus_clk()) v = uint8_t(v | 0x04);
            if (!bus_atn()) v = uint8_t(v | 0x80);

            // PB5/PB6 select the device number: the DOS reads them at $EB3A
            // and adds them to 8, so both low is device #8.

            return v;
        },
        [](uint8_t) {});

    via1_.set_irq_callback([this](IrqLine state) {
        via1_irq_ = (state != IrqLine::Clear);
        cpu_.set_irq((via1_irq_ || via2_irq_) ? IrqLine::Assert : IrqLine::Clear);
    });

    /*
        VIA2 at $1C00 -- disk controller.
        PA  = GCR byte from/to the read-write head.
        PB0/PB1 = stepper phases.
        PB2 = motor.
        PB3 = drive LED.
        PB4 = write protect sense (0 = protected).
        PB5/PB6 = bit rate (density) for the current speed zone.
        PB7 = SYNC (0 while a sync mark is under the head).
        CA1 = BYTE READY, CA2 = SOE (byte ready also drives the 6502 SO pin).
    */
    via2_.set_ca2_handler([this](bool level) { soe_ = level; });

    via2_.set_port_b(
        [this]() -> uint8_t {
            // Only PB4 and PB7 are inputs; the rest are driven by the VIA and
            // read back through Via6522::input_pb(). Reporting them high would
            // make the DOS believe the stepper sits in a phase it never set.
            uint8_t v = 0x00;

            if (!write_protect_) {
                v = uint8_t(v | 0x10);
            }

            if (!sync_) {
                v = uint8_t(v | 0x80);
            }

            return v;
        },
        [this](uint8_t value) { on_via2_pb(value); });

    via2_.set_irq_callback([this](IrqLine state) {
        via2_irq_ = (state != IrqLine::Clear);
        cpu_.set_irq((via1_irq_ || via2_irq_) ? IrqLine::Assert : IrqLine::Clear);
    });
}

void C1541::reset() {
    cpu_.reset();
    via1_.reset();
    via2_.reset();

    ram_.fill(0x00);

    motor_on_ = false;
    led_on_ = false;

    half_track_ = 18 * 2;
    stepper_prev_ = 0;
    cycle_debt_ = 0;

    bit_pos_ = 0;
    bit_timer_ = 0;
    cycles_per_bit_ = 13;

    shift_reg_ = 0;
    shift_count_ = 0;
    ones_ = 0;

    sync_ = false;
    byte_ready_ = false;
    last_byte_ = 0;

    soe_ = false;

    drv_data_ = true;
    drv_clk_ = true;

    host_atn_ = true;
    host_clk_ = true;
    host_data_ = true;

    via1_irq_ = false;
    via2_irq_ = false;

    weak_rng_ = 0xACE1u;
    zero_run_ = 0;

    rebuild_track_gcr();
}

bool C1541::load_rom(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;

    if (!read_file(path, &data)) {
        if (error) {
            *error = "cannot load 1541 ROM: " + path;
        }
        return false;
    }

    return load_rom(data.data(), data.size());
}

bool C1541::load_rom(const uint8_t* data, size_t size) {
    rom_.fill(0xFF);

    if (!data || size == 0) {
        rom_loaded_ = false;
        return false;
    }

    if (size >= rom_.size()) {
        std::memcpy(rom_.data(), data, rom_.size());
    } else {
        std::memcpy(rom_.data(), data, size);
        // 8 KiB ROM: mirror into the upper half.
        if (size == 0x2000) {
            std::memcpy(rom_.data() + 0x2000, data, 0x2000);
        }
    }

    rom_loaded_ = true;
    return true;
}

bool C1541::load_d64(const std::string& path, std::string* error) {
    if (!disk_.load_file(path, error)) {
        return false;
    }

    use_g64_ = false;
    write_protect_ = false;

    rebuild_track_gcr();

    return true;
}

bool C1541::load_d64(const uint8_t* data, size_t size, std::string* error) {
    if (!disk_.load_memory(data, size, error)) {
        return false;
    }

    use_g64_ = false;
    write_protect_ = false;

    rebuild_track_gcr();

    return true;
}

bool C1541::load_g64(const std::string& path, std::string* error) {
    if (!g64_.load_file(path, error)) {
        return false;
    }

    use_g64_ = true;
    write_protect_ = true;

    rebuild_track_gcr();

    return true;
}

bool C1541::load_g64(const uint8_t* data, size_t size, std::string* error) {
    if (!g64_.load_memory(data, size, error)) {
        return false;
    }

    use_g64_ = true;
    write_protect_ = true;

    rebuild_track_gcr();

    return true;
}

void C1541::set_host_atn(bool high) {
    const bool old = host_atn_;
    host_atn_ = high;

    if (old != host_atn_) {
        // The ATN acknowledge hardware reacts to the line, not to the DOS,
        // and CA1 raises the interrupt the ATN handler waits for.
        via1_.write_ca1(!host_atn_);
        update_iec();
    }
}

void C1541::set_host_clk(bool high) {
    host_clk_ = high;
    update_iec();
}

void C1541::set_host_data(bool high) {
    host_data_ = high;
    update_iec();
}

bool C1541::bus_atn() const {
    return host_atn_;
}

bool C1541::bus_clk() const {
    return host_clk_ && drv_clk_;
}

bool C1541::bus_data() const {
    return host_data_ && drv_data_;
}

uint8_t C1541::read_mem(uint16_t addr) {
    if (addr < 0x0800) {
        return ram_[addr & 0x07FF];
    }

    if (addr >= 0x1800 && addr <= 0x180F) {
        return via1_.read(uint8_t(addr & 0x0F));
    }

    if (addr >= 0x1C00 && addr <= 0x1C0F) {
        return via2_.read(uint8_t(addr & 0x0F));
    }

    if (addr >= 0xC000) {
        return rom_[addr & 0x3FFF];
    }

    return 0xFF;
}

void C1541::write_mem(uint16_t addr, uint8_t value) {
    if (addr < 0x0800) {
        ram_[addr & 0x07FF] = value;
        return;
    }

    if (addr >= 0x1800 && addr <= 0x180F) {
        via1_.write(uint8_t(addr & 0x0F), value);
        update_iec_outputs();
        return;
    }

    if (addr >= 0x1C00 && addr <= 0x1C0F) {
        via2_.write(uint8_t(addr & 0x0F), value);
        return;
    }
}

void C1541::update_iec() {
    // Drive outputs depend on the ATN line as well as on VIA1 port B.
    update_iec_outputs();
}

void C1541::update_via1_inputs() {
    // VIA1 PB comes from the port read callback; PA is updated on GCR byte ready.
}

void C1541::update_via2_inputs() {
    // VIA2 PB comes from the port read callback.
}

void C1541::update_iec_outputs() {
    // Only pins programmed as outputs drive the open-collector bus.
    const uint8_t pb = uint8_t(via1_.out_b() & via1_.ddr_b());

    // A set output bit pulls the line low (inverting 7406 drivers).
    const bool data_pull = (pb & 0x02) != 0;
    const bool clk_pull = (pb & 0x08) != 0;

    // ATN acknowledge: DATA is pulled low while ATNA disagrees with ATN.
    const bool atn_low = !host_atn_;
    const bool atna_pull = ((pb & 0x10) != 0) != atn_low;

    drv_data_ = !(data_pull || atna_pull);
    drv_clk_ = !clk_pull;
}

void C1541::on_via2_pb(uint8_t value) {
    motor_on_ = (value & 0x04) != 0;
    led_on_ = (value & 0x08) != 0;

    // PB0/PB1 stepper phases. The DOS keeps the head position in its own RAM,
    // so the first value it writes only defines the phase the mechanics are
    // already in: acting on it would offset every later seek by a half track.
    const int phase = value & 0x03;

    if (phase == stepper_prev_) return;

    const int diff = (phase - stepper_prev_) & 0x03;

    if (diff == 1) {
        step_head(+1);
    } else if (diff == 3) {
        step_head(-1);
    }

    stepper_prev_ = phase;
}

void C1541::step_head(int delta) {
    const int old_half_track = half_track_;

    half_track_ += delta;

    // Track 1 is where the head stop sits: the DOS bumps into it at power up
    // and then counts tracks from there, so the limit has to keep the parity
    // of the half-track position or every later seek lands between tracks.
    if (half_track_ < 2) {
        half_track_ = 2;
    }

    if (half_track_ > 84) {
        half_track_ = 84;
    }

    if (half_track_ == old_half_track) {
        return;
    }

    // G64 may differ per half-track; D64 only has full tracks.
    if (use_g64_) {
        rebuild_track_gcr();
        bit_pos_ = 0;
    } else {
        const int old_full = old_half_track / 2;
        const int new_full = half_track_ / 2;

        if (old_full != new_full ||
            ((old_half_track & 1) != (half_track_ & 1))) {
            rebuild_track_gcr();
            bit_pos_ = 0;
        }
    }
}

void C1541::rebuild_track_gcr() {
    track_bits_.clear();

    zero_run_ = 0;
    bit_timer_ = 0;
    sync_ = false;
    byte_ready_ = false;
    shift_reg_ = 0;
    shift_count_ = 0;

    if (use_g64_ && g64_.open()) {
        int ht = half_track_;

        if (ht < 1) {
            ht = 1;
        }

        if (ht > 84) {
            ht = 84;
        }

        const auto& raw = g64_.track_data(ht);

        if (!raw.empty()) {
            track_bits_ = G64Image::bytes_to_bits(raw);

            const int zone = g64_.speed_zone(ht);

            // Zone 3 densest, zone 0 slowest.
            static const int kCyclesPerBit[4] = {16, 15, 14, 13};
            cycles_per_bit_ = kCyclesPerBit[zone & 3];
        }

        bit_pos_ = track_bits_.empty() ? 0 : bit_pos_ % int(track_bits_.size());
        return;
    }

    if (!disk_.open()) {
        bit_pos_ = 0;
        return;
    }

    const int track = half_track_ / 2;

    if (track < 1 || track > 35) {
        bit_pos_ = 0;
        return;
    }

    if (track <= 17) {
        cycles_per_bit_ = 13;
    } else if (track <= 24) {
        cycles_per_bit_ = 14;
    } else if (track <= 30) {
        cycles_per_bit_ = 15;
    } else {
        cycles_per_bit_ = 16;
    }

    const int spt = D64Image::sectors_per_track(track);

    std::vector<std::array<uint8_t, 256>> sectors(static_cast<size_t>(spt));
    std::vector<const uint8_t*> ptrs(static_cast<size_t>(spt));

    for (int s = 0; s < spt; ++s) {
        const uint8_t trk = static_cast<uint8_t>(track);
        const uint8_t sec = static_cast<uint8_t>(s);

        const uint8_t err = disk_.sector_error(trk, sec);

        if (!disk_.read_sector(trk, sec, sectors[static_cast<size_t>(s)].data())) {
            sectors[static_cast<size_t>(s)].fill(0x00);
        }

        // D64 extended error map:
        // 0/1 normal; 2/3 no header/sync; 4 bad data; 5/9 checksum.
        if (err != 0 && err != 1) {
            if (err == 2 || err == 3) {
                sectors[static_cast<size_t>(s)].fill(0xFF);
            } else if (err == 4) {
                sectors[static_cast<size_t>(s)].fill(0x00);
            } else if (err == 5 || err == 9) {
                sectors[static_cast<size_t>(s)][0] ^= 0xFF;
            }
        }

        ptrs[static_cast<size_t>(s)] = sectors[static_cast<size_t>(s)].data();
    }

    track_bits_ = Gcr::build_track(track, spt, ptrs.data(), disk_.disk_id1(),
                                   disk_.disk_id2());

    // D64 has no real half-tracks; sparse flux so +0.5 seeks still see signal.
    if ((half_track_ & 1) != 0) {
        std::vector<bool> sparse;
        Gcr::append_sync(sparse, 20);
        for (int i = 0; i < 2000; ++i) {
            sparse.push_back(((i * 17) & 1) != 0);
        }
        track_bits_ = std::move(sparse);
        cycles_per_bit_ = 14;
    }

    if (track_bits_.empty()) {
        Gcr::append_sync(track_bits_, 80);
        Gcr::append_gap55(track_bits_, 100);
    }

    bit_pos_ = track_bits_.empty() ? 0 : bit_pos_ % int(track_bits_.size());
}

void C1541::tick_disk(int cycles) {
    if (!motor_on_ || track_bits_.empty()) {
        sync_ = false;
        byte_ready_ = false;
        ones_ = 0;
        return;
    }

    // cycles_per_bit_ counts quarter cycles: the slowest zone runs one bit
    // every four cycles and the fastest one every 3.25, so a whole-cycle
    // counter would make the drive read four times too slowly.
    bit_timer_ += cycles * 4;

    while (bit_timer_ >= cycles_per_bit_) {
        bit_timer_ -= cycles_per_bit_;

        bool bit = track_bits_[static_cast<size_t>(bit_pos_)];

        ++bit_pos_;
        if (bit_pos_ >= int(track_bits_.size())) {
            bit_pos_ = 0;
        }

        // Weak bits: long zero runs become unstable flux.
        if (weak_bits_) {
            if (!bit) {
                ++zero_run_;
                if (zero_run_ >= 6) {
                    weak_rng_ = weak_rng_ * 1664525u + 1013904223u;
                    bit = (weak_rng_ & 1u) != 0;
                }
            } else {
                zero_run_ = 0;
            }
        }

        // SYNC = 10 consecutive ones.
        if (bit) {
            ++ones_;
            if (ones_ >= 10) {
                sync_ = true;
            }
        } else {
            ones_ = 0;
            sync_ = false;
        }

        shift_reg_ = uint8_t((shift_reg_ << 1) | (bit ? 1 : 0));

        if (sync_) {
            shift_count_ = 0;
            byte_ready_ = false;
            continue;
        }

        ++shift_count_;

        if (shift_count_ >= 8) {
            shift_count_ = 0;
            last_byte_ = shift_reg_;
            byte_ready_ = true;

            // PA gets the GCR byte; CA1 gets a pulse.
            via2_.write_pa(last_byte_);
            via2_.write_ca1(false);
            via2_.write_ca1(true);

            // SOE enables 6502 overflow flag.
            if (soe_) {
                cpu_.p.v = true;
            }
        }
    }

    update_via1_inputs();
}

void C1541::run(int cycles) {
    if (!rom_loaded_ || cycles <= 0) {
        return;
    }

    update_iec();

    // One instruction at a time, ticking the VIAs and the head with the cycles
    // it really took: the serial bus protocol is pure software handshaking on
    // both sides, so a drive that gains or loses cycles never completes a
    // transfer. Overshoot is carried over instead of being dropped.
    cycle_debt_ += cycles;

    while (cycle_debt_ > 0) {
        const int ran = cpu_.run(1);
        const int used = (ran > 0) ? ran : 1;

        tick_disk(used);

        via1_.tick(used);
        via2_.tick(used);

        cycle_debt_ -= used;
    }
}

}  // namespace dsp
