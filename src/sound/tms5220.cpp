#include "sound/tms5220.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

const int kEnergyBits = 4;
const int kPitchBits = 6;
const int kKBits[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};

const uint16_t kEnergyTable[16] = {0,1,2,3,4,6,8,11,16,23,33,47,63,85,114,0};
const uint16_t kPitchTable[64] = {0,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,44,46,48,50,52,53,56,58,60,62,65,68,70,72,76,78,80,84,86,91,94,98,101,105,109,114,118,122,127,132,137,142,148,153,159};
const int16_t kK1[32]={-501,-498,-497,-495,-493,-491,-488,-482,-478,-474,-469,-464,-459,-452,-445,-437,-412,-380,-339,-288,-227,-158,-81,-1,80,157,226,287,337,379,411,436};
const int16_t kK2[32]={-328,-303,-274,-244,-211,-175,-138,-99,-59,-18,24,64,105,143,180,215,248,278,306,331,354,374,392,408,422,435,445,455,463,470,476,506};
const int16_t kK3[16]={-441,-387,-333,-279,-225,-171,-117,-63,-9,45,98,152,206,260,314,368};
const int16_t kK4[16]={-328,-273,-217,-161,-106,-50,5,61,116,172,228,283,339,394,450,506};
const int16_t kK5[16]={-328,-282,-235,-189,-142,-96,-50,-3,43,90,136,182,229,275,322,368};
const int16_t kK6[16]={-256,-212,-168,-123,-79,-35,10,54,98,143,187,232,276,320,365,409};
const int16_t kK7[16]={-308,-260,-212,-164,-117,-69,-21,27,75,122,170,218,266,314,361,409};
const int16_t kK8[8]={-256,-161,-66,29,124,219,314,409};
const int16_t kK9[8]={-256,-176,-96,-15,65,146,226,307};
const int16_t kK10[8]={-205,-132,-59,14,87,160,234,307};
const int16_t* const kKTables[10]={kK1,kK2,kK3,kK4,kK5,kK6,kK7,kK8,kK9,kK10};
const int8_t kChirp[52]={0x00,0x03,0x0f,0x28,0x4c,0x6c,0x71,0x50,0x25,0x26,0x4c,0x44,0x1a,0x32,0x3b,0x13,0x37,0x1a,0x25,0x1f,0x1d,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
const int kInterpShift[8]={0,3,3,3,2,2,1,1};
int k_value(int w,int i){const int max=(1<<kKBits[w])-1;return kKTables[w][std::clamp(i,0,max)];}
inline int32_t matrix_multiply(int32_t a,int32_t b){while(a>511)a-=1024;while(a<-512)a+=1024;while(b>16383)b-=32768;while(b<-16384)b+=32768;return(a*b)>>9;}
}

Tms5220::Tms5220(uint32_t clock):clock_(clock?clock:640000){reset();}
void Tms5220::raise_irq(bool on){irq_asserted_=on;if(irq_cb_)irq_cb_(on);}
void Tms5220::chip_reset(){fifo_.fill(0);fifo_head_=fifo_tail_=fifo_count_=0;bit_buffer_=0;bits_left_=0;speak_external_=talk_status_=false;old_energy_idx_=new_energy_idx_=old_pitch_idx_=new_pitch_idx_=0;old_k_idx_.fill(0);new_k_idx_.fill(0);current_energy_=previous_energy_=current_pitch_=0;current_k_.fill(0);ip_=pc_=subcycle_=pitch_count_=0;inhibit_=false;old_unvoiced_=old_silence_=true;zpar_=uv_zpar_=true;frame_just_parsed_=false;rng_=1;u_.fill(0);x_.fill(0);out_sample_=0;cycle_acc_=0;update_cycle_acc_=0;data_pending_=false;ready_delay_=0;buffer_low_=buffer_empty_=true;raise_irq(true);}
void Tms5220::reset(){wsq_=rsq_=rs_read_=true;volume_=1.0f;chip_reset();}
void Tms5220::update_fifo_flags(bool edge_irq){const bool low=fifo_count_<=8;const bool empty=fifo_count_==0;if(edge_irq&&((low&&!buffer_low_)||(empty&&!buffer_empty_)))raise_irq(true);buffer_low_=low;buffer_empty_=empty;if(speak_external_&&talk_status_&&empty&&bits_left_==0){talk_status_=false;speak_external_=false;}}
void Tms5220::fifo_push(uint8_t v){if(fifo_count_>=16)return;fifo_[fifo_tail_]=v;fifo_tail_=(fifo_tail_+1)&15;++fifo_count_;update_fifo_flags();}
uint8_t Tms5220::fifo_pop(){if(!fifo_count_)return 0;uint8_t v=fifo_[fifo_head_];fifo_[fifo_head_]=0;fifo_head_=(fifo_head_+1)&15;--fifo_count_;return v;}
uint8_t Tms5220::status()const{return uint8_t((talk_status_?0x80:0)|(buffer_low_?0x40:0)|(buffer_empty_?0x20:0));}
void Tms5220::process_command(uint8_t cmd){switch(cmd&0x70){case 0x00:case 0x20:break;case 0x10:case 0x30:case 0x40:case 0x50:break;case 0x60:speak_external_=true;talk_status_=false;fifo_.fill(0);fifo_head_=fifo_tail_=fifo_count_=0;bit_buffer_=0;bits_left_=0;ip_=pc_=subcycle_=pitch_count_=0;frame_just_parsed_=false;zpar_=uv_zpar_=true;old_unvoiced_=old_silence_=true;update_fifo_flags(false);raise_irq(false);break;case 0x70:chip_reset();break;}}
void Tms5220::apply_rs_ws(bool nw,bool nr){const bool ow=wsq_,orq=rsq_;if(ow==nw&&orq==nr)return;wsq_=nw;rsq_=nr;if(!nw&&!nr){chip_reset();return;}if(ow&&!nw&&nr){if(data_pending_){data_pending_=false;write_data(data_latch_);}ready_delay_=16;}if(orq&&!nr&&nw){rs_read_=true;ready_delay_=16;raise_irq(false);if(buffer_empty_||buffer_low_)raise_irq(true);}}
void Tms5220::set_wsq(bool l){apply_rs_ws(l,rsq_);}void Tms5220::set_rsq(bool l){apply_rs_ws(wsq_,l);}void Tms5220::strobe_ws_rs(uint8_t v){apply_rs_ws((v&1)!=0,(v&2)!=0);}
bool Tms5220::readyq()const{if(!wsq_&&!rsq_)return true;return ready_delay_>0||fifo_count_>=16;}
void Tms5220::write_data(uint8_t v){data_pending_=false;if(speak_external_){if(fifo_count_>=16)return;fifo_[fifo_tail_]=v;fifo_tail_=(fifo_tail_+1)&15;++fifo_count_;buffer_low_=fifo_count_<=8;buffer_empty_=fifo_count_==0;if(!talk_status_&&fifo_count_>=9){talk_status_=true;ip_=pc_=subcycle_=pitch_count_=0;bit_buffer_=0;bits_left_=0;zpar_=uv_zpar_=true;old_unvoiced_=old_silence_=true;frame_just_parsed_=false;raise_irq(false);}update_fifo_flags();return;}process_command(v);}
uint32_t Tms5220::extract_bits(int n){uint32_t val=0;while(n--){if(bits_left_==0){if(fifo_count_==0){talk_status_=false;speak_external_=false;update_fifo_flags();return val<<(n+1);}bit_buffer_=fifo_pop();bits_left_=8;update_fifo_flags(true);}val=(val<<1)|(bit_buffer_&1u);bit_buffer_>>=1;--bits_left_;}update_fifo_flags(true);return val;}
bool Tms5220::parse_frame(){old_energy_idx_=new_energy_idx_;old_pitch_idx_=new_pitch_idx_;old_k_idx_=new_k_idx_;new_energy_idx_=int(extract_bits(kEnergyBits));if(!talk_status_)return false;if(new_energy_idx_==15){new_energy_idx_=0;new_pitch_idx_=0;new_k_idx_.fill(0);return false;}if(new_energy_idx_==0){new_pitch_idx_=0;new_k_idx_.fill(0);return true;}const int rep=int(extract_bits(1));new_pitch_idx_=int(extract_bits(kPitchBits));if(rep){new_k_idx_=old_k_idx_;return true;}for(int i=0;i<4;++i)new_k_idx_[i]=int(extract_bits(kKBits[i]));if(new_pitch_idx_==0){for(int i=4;i<kNumK;++i)new_k_idx_[i]=0;}else for(int i=4;i<kNumK;++i)new_k_idx_[i]=int(extract_bits(kKBits[i]));return true;}
int16_t Tms5220::lattice(int16_t excitation){u_[10]=matrix_multiply(previous_energy_,int32_t(excitation)<<6);for(int i=9;i>=0;--i)u_[i]=u_[i+1]-matrix_multiply(current_k_[i],x_[i]);for(int i=9;i>=1;--i)x_[i]=x_[i-1]+matrix_multiply(current_k_[i],u_[i]);x_[0]=u_[0];previous_energy_=current_energy_;int32_t out=u_[0];while(out>16383)out-=32768;while(out<-16384)out+=32768;out=(out<<1)|((out>>9)&1);return int16_t(std::clamp(out,int32_t(-32768),int32_t(32767)));}
void Tms5220::generate_sample(){if(!talk_status_){out_sample_=(out_sample_*15)/16;return;}if(ip_==0&&pc_==12&&subcycle_==1){ip_=0;const bool valid=parse_frame();frame_just_parsed_=true;if(!valid){current_energy_=0;current_pitch_=0;for(int&k:current_k_)k=0;talk_status_=false;speak_external_=false;return;}const bool now_u=new_pitch_idx_==0,now_s=new_energy_idx_==0;inhibit_=(old_unvoiced_!=now_u)||(old_silence_&&!now_s);zpar_=now_s;uv_zpar_=now_u;}if(subcycle_==2&&pc_<=11){const int shift=kInterpShift[ip_&7];const bool allow=!(inhibit_&&ip_!=0);auto step=[&](int c,int t){if(!allow)return c;if(shift==0)return t;return c+((t-c)>>shift);};switch(pc_){case 0:current_energy_=zpar_?0:step(current_energy_,kEnergyTable[new_energy_idx_]);break;case 1:current_pitch_=zpar_?0:step(current_pitch_,kPitchTable[new_pitch_idx_]);break;default:{const int k=pc_-2;const bool kill=zpar_||(k>=4&&uv_zpar_);current_k_[k]=kill?0:step(current_k_[k],k_value(k,new_k_idx_[k]));break;}}}int16_t excitation;if(old_unvoiced_){const uint16_t bit=uint16_t(((rng_>>12)^(rng_>>3)^(rng_>>2)^rng_)&1);rng_=uint16_t((rng_<<1)|bit);excitation=(rng_&1)?64:-64;}else{excitation=kChirp[std::min(pitch_count_,51)];}out_sample_=lattice(excitation);if(current_pitch_>0){if(++pitch_count_>=current_pitch_)pitch_count_=0;}else pitch_count_=0;++subcycle_;if(pc_==12&&subcycle_==2){subcycle_=0;pc_=0;if(frame_just_parsed_){frame_just_parsed_=false;ip_=0;}else{ip_=(ip_+1)&7;if(ip_==0){old_silence_=new_energy_idx_==0;old_unvoiced_=new_pitch_idx_==0;}}}else if(subcycle_==3){subcycle_=0;++pc_;}}
void Tms5220::tick(int cycles){if(cycles<=0)return;if(ready_delay_>0)ready_delay_=std::max(0,ready_delay_-cycles);cycle_acc_+=cycles;const uint32_t cps=std::max<uint32_t>(1,clock_/kInternalRate);while(cycle_acc_>=cps){cycle_acc_-=cps;generate_sample();}}
int16_t Tms5220::last_sample()const{return int16_t(std::clamp(int32_t(float(out_sample_)*volume_),int32_t(-32768),int32_t(32767)));}
int16_t Tms5220::update(){update_cycle_acc_+=clock_;const uint32_t cycles=update_cycle_acc_/kSampleRate;update_cycle_acc_%=kSampleRate;tick(int(cycles));return last_sample();}

} // namespace dsp
