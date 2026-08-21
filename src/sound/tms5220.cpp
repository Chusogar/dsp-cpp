#include "sound/tms5220.h"

#include <algorithm>
#include <cstdint>

namespace dsp {
namespace {

constexpr int kEnergyBits = 4;
constexpr int kPitchBits = 6;
constexpr int kKBits[10] = {5,5,4,4,4,4,4,3,3,3};
constexpr int kInterpShift[8] = {0,3,3,3,2,2,1,1};

constexpr uint16_t kEnergy[16] = {0,1,2,3,4,6,8,11,16,23,33,47,63,85,114,0};
constexpr uint16_t kPitch[64] = {0,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,44,46,48,50,52,53,56,58,60,62,65,68,70,72,76,78,80,84,86,91,94,98,101,105,109,114,118,122,127,132,137,142,148,153,159};

constexpr int16_t kK[10][32] = {
 {-501,-498,-497,-495,-493,-491,-488,-482,-478,-474,-469,-464,-459,-452,-445,-437,-412,-380,-339,-288,-227,-158,-81,-1,80,157,226,287,337,379,411,436},
 {-328,-303,-274,-244,-211,-175,-138,-99,-59,-18,24,64,105,143,180,215,248,278,306,331,354,374,392,408,422,435,445,455,463,470,476,506},
 {-441,-387,-333,-279,-225,-171,-117,-63,-9,45,98,152,206,260,314,368},
 {-328,-273,-217,-161,-106,-50,5,61,116,172,228,283,339,394,450,506},
 {-328,-282,-235,-189,-142,-96,-50,-3,43,90,136,182,229,275,322,368},
 {-256,-212,-168,-123,-79,-35,10,54,98,143,187,232,276,320,365,409},
 {-308,-260,-212,-164,-117,-69,-21,27,75,122,170,218,266,314,361,409},
 {-256,-161,-66,29,124,219,314,409},
 {-256,-176,-96,-15,65,146,226,307},
 {-205,-132,-59,14,87,160,234,307}
};

// TI later TMS5220 chirp ROM. Values are signed 8-bit DAC excitation.
constexpr int8_t kChirp[52] = {0,3,15,40,76,108,113,80,37,38,76,68,26,50,59,19,55,26,37,31,29,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

inline int coeff(int n, int index) {
    const int max = (1 << kKBits[n]) - 1;
    index = std::clamp(index, 0, max);
    return kK[n][index];
}

inline int32_t mul(int32_t a, int32_t b) {
    while (a > 511) a -= 1024;
    while (a < -512) a += 1024;
    while (b > 16383) b -= 32768;
    while (b < -16384) b += 32768;
    return (a * b) >> 9;
}

// The TMS5220 analog path is not a raw 16-bit DAC. It clips the 14-bit
// lattice result to a signed 10-bit value and then expands it to audio.
inline int16_t analog_clip(int32_t v) {
    v = std::clamp(v, -8192, 8191);
    int32_t a;
    const int32_t top = v >> 11;
    if (top == 0 || top == -1) {
        a = v >> 4;
    } else if (v < 0) {
        a = -128;
    } else {
        a = 127;
    }
    return int16_t(a << 8);
}

} // namespace

Tms5220::Tms5220(uint32_t clock) : clock_(clock ? clock : 640000) { reset(); }

void Tms5220::raise_irq(bool on) { irq_asserted_ = on; if (irq_cb_) irq_cb_(on); }

void Tms5220::chip_reset() {
    fifo_.fill(0); fifo_head_=fifo_tail_=fifo_count_=0;
    bit_buffer_=0; bits_left_=0;
    speak_external_=talk_status_=false;
    old_energy_idx_=new_energy_idx_=old_pitch_idx_=new_pitch_idx_=0;
    old_k_idx_.fill(0); new_k_idx_.fill(0);
    current_energy_=previous_energy_=current_pitch_=0; current_k_.fill(0);
    ip_=0; pc_=0; subcycle_=1; pitch_count_=0;
    inhibit_=false; old_unvoiced_=true; old_silence_=true;
    zpar_=true; uv_zpar_=true; frame_just_parsed_=false;
    rng_=1; u_.fill(0); x_.fill(0); out_sample_=0;
    cycle_acc_=0; update_cycle_acc_=0;
    data_pending_=false; ready_delay_=0;
    buffer_low_=true; buffer_empty_=true;
    raise_irq(false);
}

void Tms5220::reset() { wsq_=rsq_=rs_read_=true; volume_=1.0f; chip_reset(); }

void Tms5220::update_fifo_flags(bool edge_irq) {
    const bool low = fifo_count_ <= 8;
    const bool empty = fifo_count_ == 0;
    if (edge_irq && ((low && !buffer_low_) || (empty && !buffer_empty_))) raise_irq(true);
    buffer_low_=low; buffer_empty_=empty;
    if (speak_external_ && talk_status_ && empty && bits_left_==0) {
        talk_status_=false; speak_external_=false; raise_irq(true);
    }
}

void Tms5220::fifo_push(uint8_t v) {
    if (fifo_count_ >= 16) return;
    fifo_[fifo_tail_]=v; fifo_tail_=(fifo_tail_+1)&15; ++fifo_count_;
    update_fifo_flags();
}

uint8_t Tms5220::fifo_pop() {
    if (!fifo_count_) return 0;
    uint8_t v=fifo_[fifo_head_]; fifo_[fifo_head_]=0;
    fifo_head_=(fifo_head_+1)&15; --fifo_count_; return v;
}

uint8_t Tms5220::status() const {
    return uint8_t((talk_status_?0x80:0) | (buffer_low_?0x40:0) | (buffer_empty_?0x20:0));
}

void Tms5220::process_command(uint8_t cmd) {
    switch (cmd & 0x70) {
    case 0x60: // SPEAK EXTERNAL
        speak_external_=true; talk_status_=false;
        fifo_.fill(0); fifo_head_=fifo_tail_=fifo_count_=0;
        bit_buffer_=0; bits_left_=0; ip_=0; pc_=0; subcycle_=1;
        pitch_count_=0; zpar_=uv_zpar_=true; old_unvoiced_=old_silence_=true;
        new_energy_idx_=new_pitch_idx_=0; new_k_idx_.fill(0);
        update_fifo_flags(false); raise_irq(false); break;
    case 0x70: // RESET
        chip_reset(); break;
    default:
        break;
    }
}

void Tms5220::apply_rs_ws(bool nw, bool nr) {
    const bool ow=wsq_, orq=rsq_;
    if (ow==nw && orq==nr) return;
    wsq_=nw; rsq_=nr;
    if (!nw && !nr) { chip_reset(); return; }
    if (ow && !nw && nr) {
        if (data_pending_) { data_pending_=false; write_data(data_latch_); }
        ready_delay_=16;
    }
    if (orq && !nr && nw) { rs_read_=true; ready_delay_=16; raise_irq(false); }
}

void Tms5220::set_wsq(bool level) { apply_rs_ws(level,rsq_); }
void Tms5220::set_rsq(bool level) { apply_rs_ws(wsq_,level); }
void Tms5220::strobe_ws_rs(uint8_t v) { apply_rs_ws((v&1)!=0,(v&2)!=0); }
bool Tms5220::readyq() const { return ready_delay_>0 || fifo_count_>=16; }

void Tms5220::write_data(uint8_t value) {
    data_pending_=false;
    if (!speak_external_) { process_command(value); return; }
    if (fifo_count_>=16) return;
    const bool was_low = fifo_count_ <= 8;
    fifo_[fifo_tail_]=value; fifo_tail_=(fifo_tail_+1)&15; ++fifo_count_;
    buffer_low_=fifo_count_<=8; buffer_empty_=false;

    // SPKEE starts TALK on the low->high BL transition (9th byte).
    if (!talk_status_ && was_low && fifo_count_>=9) {
        talk_status_=true; ip_=0; pc_=0; subcycle_=1; pitch_count_=0;
        bit_buffer_=0; bits_left_=0; zpar_=uv_zpar_=true;
        old_unvoiced_=old_silence_=true;
        current_energy_=previous_energy_=current_pitch_=0; current_k_.fill(0);
        x_.fill(0); u_.fill(0);
        raise_irq(false);
    }
    update_fifo_flags();
}

uint32_t Tms5220::extract_bits(int n) {
    uint32_t v=0;
    while (n--) {
        if (bits_left_==0) {
            if (!fifo_count_) {
                talk_status_=false; speak_external_=false; update_fifo_flags();
                return v << (n+1);
            }
            bit_buffer_=fifo_pop(); bits_left_=8; update_fifo_flags(true);
        }
        v=(v<<1)|(bit_buffer_&1u); bit_buffer_>>=1; --bits_left_;
    }
    return v;
}

bool Tms5220::parse_frame() {
    old_energy_idx_=new_energy_idx_; old_pitch_idx_=new_pitch_idx_; old_k_idx_=new_k_idx_;
    new_energy_idx_=int(extract_bits(4));
    if (!talk_status_) return false;
    if (new_energy_idx_==15) { new_energy_idx_=0; new_pitch_idx_=0; new_k_idx_.fill(0); return false; }
    if (new_energy_idx_==0) { new_pitch_idx_=0; new_k_idx_.fill(0); return true; }
    const int repeat=int(extract_bits(1));
    new_pitch_idx_=int(extract_bits(6));
    if (repeat) { new_k_idx_=old_k_idx_; return true; }
    for (int i=0;i<4;i++) new_k_idx_[i]=int(extract_bits(kKBits[i]));
    if (new_pitch_idx_==0) { for(int i=4;i<10;i++) new_k_idx_[i]=0; }
    else for(int i=4;i<10;i++) new_k_idx_[i]=int(extract_bits(kKBits[i]));
    return true;
}

int16_t Tms5220::lattice(int16_t excitation) {
    // Patent/MAME lattice filter: previous_energy is the gain for excitation.
    u_[10]=mul(previous_energy_,int32_t(excitation)<<6);
    for(int i=9;i>=0;i--) u_[i]=u_[i+1]-mul(current_k_[i],x_[i]);
    for(int i=9;i>=1;i--) x_[i]=x_[i-1]+mul(current_k_[i],u_[i]);
    x_[0]=u_[0]; previous_energy_=current_energy_;
    return analog_clip(u_[0]);
}

void Tms5220::generate_sample() {
    if (!talk_status_) { out_sample_=(out_sample_*15)/16; return; }

    // TMS5220 normal mode has exactly two subcycles (A and B) for PC 0..11;
    // PC 12 has only the A cycle. Therefore one interpolation period is
    // 25 synthesis samples and one complete frame is 8*25 = 200 samples.
    if (ip_==0 && pc_==12 && subcycle_==1) {
        if (!parse_frame()) {
            current_energy_=0; current_pitch_=0; current_k_.fill(0);
            talk_status_=false; speak_external_=false;
            raise_irq(true);
            out_sample_=0;
            return;
        }
        const bool nu = new_pitch_idx_==0;
        const bool ns = new_energy_idx_==0;
        inhibit_=(old_unvoiced_!=nu) || (old_silence_ && !ns);
        zpar_=ns; uv_zpar_=nu;
    }

    // Parameter update happens on B cycle (subcycle 2), except PC12.
    if (subcycle_==2 && pc_<=11) {
        const int sh=kInterpShift[ip_&7];
        const bool allow=!(inhibit_ && ip_!=0);
        auto interp=[&](int cur,int target){ if(!allow)return cur; if(sh==0)return target; return cur+((target-cur)>>sh); };
        if(pc_==0) current_energy_=zpar_?0:interp(current_energy_,kEnergy[new_energy_idx_]);
        else if(pc_==1) current_pitch_=zpar_?0:interp(current_pitch_,kPitch[new_pitch_idx_]);
        else {
            const int k=pc_-2;
            const bool zero=zpar_ || (k>=4 && uv_zpar_);
            current_k_[k]=zero?0:interp(current_k_[k],coeff(k,new_k_idx_[k]));
        }
    }

    int16_t excitation;
    if (old_unvoiced_) {
        // 20-bit-equivalent PRNG update used by the TMS52xx excitation circuit.
        for(int i=0;i<20;i++) {
            const uint16_t bit=uint16_t(((rng_>>12)^(rng_>>3)^(rng_>>2)^rng_)&1);
            rng_=int32_t(((uint32_t(rng_)<<1)|bit)&0xffffu);
        }
        excitation=(rng_&1)?-64:64;
    } else {
        excitation=kChirp[std::min(pitch_count_,51)];
    }

    out_sample_=lattice(excitation);

    if(current_pitch_>0) {
        ++pitch_count_;
        if(pitch_count_>=current_pitch_) pitch_count_=0;
    } else pitch_count_=0;
    if(inhibit_ && ip_==0) pitch_count_=0;

    // Advance exactly as the chip: PC 12 has no B cycle.
    if(pc_==12) {
        ++subcycle_;
        if(subcycle_==2) {
            subcycle_=1; pc_=0; ip_=(ip_+1)&7;
            if(ip_==0) { old_silence_=new_energy_idx_==0; old_unvoiced_=new_pitch_idx_==0; }
        }
    } else {
        ++subcycle_;
        if(subcycle_==3) { subcycle_=1; ++pc_; }
    }
}

void Tms5220::tick(int cycles) {
    if(cycles<=0) return;
    if(ready_delay_>0) ready_delay_=std::max(0,ready_delay_-cycles);
    cycle_acc_+=cycles;
    const uint32_t cps=std::max<uint32_t>(1,clock_/8000);
    while(cycle_acc_>=cps) { cycle_acc_-=cps; generate_sample(); }
}

int16_t Tms5220::last_sample() const {
    const int32_t s=int32_t(float(out_sample_)*volume_);
    return int16_t(std::clamp(s,-32768,32767));
}

int16_t Tms5220::update() {
    // Fractional clock accumulator: 640000/44100 is not an integer. The old
    // implementation rounded this to 14 cycles/sample, drifting the speech
    // clock and pitch by several percent.
    update_cycle_acc_ += clock_;
    const uint32_t cycles=update_cycle_acc_/kSampleRate;
    update_cycle_acc_%=kSampleRate;
    tick(int(cycles));
    return last_sample();
}

} // namespace dsp
