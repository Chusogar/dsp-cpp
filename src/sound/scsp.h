#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dsp {

// Yamaha YMF292-F "SCSP" (Saturn Custom Sound Processor), as used on the
// Sega Model 2/3 sound boards: 32 PCM slots with envelope generators, pitch
// and amplitude LFOs, FM via the ring buffer, the 128-step effect DSP,
// three timers, interrupt arbitration for the 68000 and the MIDI input FIFO.
//
// Slot, envelope, LFO and DSP code is a port of MAME's scsp.cpp /
// scspdsp.cpp (BSD-3-Clause, ElSemi, R. Belmont); timers advance one tick per
// output sample and the 68000 interrupt level is recomputed every sample.
class Scsp {
public:
    static constexpr int kSampleRate = 44100;

    Scsp();

    void reset();
    // Sound RAM shared with the 68000 (big-endian bytes, size = mask + 1).
    void set_ram(uint8_t* ram, uint32_t mask) {
        ram_ = ram;
        ram_mask_ = mask;
    }

    // Register file at 0x000-0xFFF (byte offsets, 16-bit wide).
    uint16_t read16(uint32_t offset);
    void write16(uint32_t offset, uint16_t data, uint16_t mem_mask = 0xffff);
    uint8_t read8(uint32_t offset) {
        const uint16_t w = read16(offset & ~1u);
        return (offset & 1) ? uint8_t(w) : uint8_t(w >> 8);
    }
    void write8(uint32_t offset, uint8_t data) {
        if (offset & 1) write16(offset & ~1u, data, 0x00ff);
        else write16(offset & ~1u, uint16_t(data << 8), 0xff00);
    }

    void midi_in(uint8_t data);

    // Generates one 44.1 kHz stereo sample and advances the timers.
    void sample(int32_t& left, int32_t& right);
    // 68000 interrupt level requested (0 = none).
    int irq_level();

    // Debug.
    bool slot_active(int n) const { return slots_[size_t(n) & 31].active != 0; }

private:
    enum EgState { kAttack, kDecay1, kDecay2, kRelease };
    struct Eg {
        int volume = 0;
        EgState state = kRelease;
        int step = 0;
        int ar = 0, d1r = 0, d2r = 0, rr = 0, dl = 0;
        uint8_t eghold = 0, lplink = 0;
    };
    struct Lfo {
        uint16_t phase = 0;
        uint32_t phase_step = 0;
        const int* table = nullptr;
        const int* scale = nullptr;
    };
    struct Slot {
        uint16_t data[0x10] = {};
        uint8_t backwards = 0;
        uint8_t active = 0;
        uint32_t cur_addr = 0, nxt_addr = 0, step = 0;
        Eg eg;
        Lfo plfo, alfo;
        int slot = 0;
        int16_t prev = 0;
    };
    struct Dsp {
        uint32_t rbp = 0, rbl = 8 * 1024;
        int16_t coef[64] = {};
        uint16_t madrs[32] = {};
        uint16_t mpro[128 * 4] = {};
        int32_t temp[128] = {};
        int32_t mems[32] = {};
        uint32_t dec = 0;
        int32_t mixs[16] = {};
        int16_t exts[2] = {};
        int16_t efreg[16] = {};
        bool stopped = true;
        int last_step = 0;
    };

    // Register helpers.
    uint16_t& reg(int index) { return regs_[size_t(index)]; }

    int get_ar(int base, int r) const;
    int get_dr(int base, int r) const;
    void compute_eg(Slot& s);
    int eg_update(Slot& s);
    uint32_t step_of(const Slot& s) const;
    void compute_lfo(Slot& s);
    void lfo_compute_step(Lfo& lfo, uint32_t lfof, uint32_t lfows, uint32_t lfos, bool alfo);
    int32_t plfo_step(Lfo& lfo);
    int32_t alfo_step(Lfo& lfo);
    void start_slot(Slot& s);
    void stop_slot(Slot& s, bool keyoff);
    void update_slot_reg(int slot, int r);
    void update_reg(int r);
    void update_reg_read(int r);
    void w16(uint32_t addr, uint16_t value);
    uint16_t r16(uint32_t addr);
    int32_t update_slot(Slot& s);
    void exec_dma();
    void timers_tick();
    uint8_t decode_sci(int irq);

    uint8_t read_byte(uint32_t a) const { return ram_ ? ram_[a & ram_mask_] : 0; }
    uint16_t read_word(uint32_t a) const {
        return ram_ ? uint16_t(ram_[a & ram_mask_] << 8 | ram_[(a + 1) & ram_mask_]) : 0;
    }
    void write_word(uint32_t a, uint16_t v) {
        if (!ram_) return;
        ram_[a & ram_mask_] = uint8_t(v >> 8);
        ram_[(a + 1) & ram_mask_] = uint8_t(v);
    }

    void dsp_start();
    void dsp_step();

    uint8_t* ram_ = nullptr;
    uint32_t ram_mask_ = 0;

    std::array<uint16_t, 0x30 / 2> regs_{};
    std::array<Slot, 32> slots_{};
    int16_t ringbuf_[128] = {};
    uint8_t bufptr_ = 0;
    int16_t* rbufdst_ = nullptr;

    uint8_t irq_tima_ = 0, irq_timbc_ = 0, irq_midi_ = 0, irq_cpu_ = 0, irq_dma_ = 0;
    uint8_t latched_mslc_ = 0;
    uint16_t latched_mslc_data_ = 0;

    uint8_t midi_stack_[256] = {};
    uint8_t midi_w_ = 0, midi_r_ = 0;

    int tim_cnt_[3] = {0xffff, 0xffff, 0xffff};

    struct {
        uint32_t dmea = 0;
        uint16_t drga = 0, dtlg = 0;
        uint8_t dgate = 0, ddir = 0;
    } dma_;

    Dsp dsp_;

    // Tables.
    int32_t eg_table_[0x400];
    int lpan_[0x10000];
    int rpan_[0x10000];
    int ar_table_[64], dr_table_[64];
    int plfo_tri_[256], plfo_sqr_[256], plfo_saw_[256], plfo_noi_[256];
    int alfo_tri_[256], alfo_sqr_[256], alfo_saw_[256], alfo_noi_[256];
    int pscales_[8][256];
    int ascales_[8][256];
    uint32_t noise_ = 0x12345678;
};

}  // namespace dsp
