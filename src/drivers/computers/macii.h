#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "core/machine.h"
#include "cpu/m68000.h"
#include "machine/iwm.h"
#include "machine/mac_rtc.h"
#include "machine/ncr5380_hdd.h"
#include "machine/via6522.h"
namespace dsp {
class AscStub {
public:
    void reset(){regs_.fill(0); regs_[0x801]=0x01; regs_[0x804]=0x01;}
    uint8_t read(uint32_t o){ o&=0x1fff; if(o==0x800)return 0; if(o==0x801)return 1;
        if(o==0x804){uint8_t v=regs_[o]?regs_[o]:1;regs_[o]=0;return v;} return regs_[o]; }
    void write(uint32_t o,uint8_t v){ o&=0x1fff; regs_[o]=v; if(o<=0x804)regs_[0x804]|=1; }
private: std::array<uint8_t,0x2000> regs_{};
};
// Minimal but real Z8530 SCC register model, ported from the macplus driver
// (see its scc_read/scc_write for the full derivation and references). Mac
// II family machines map this at VIA1_BASE+0x4000..0x6000; unlike the Plus,
// read and write share one address range, split naturally by which of our
// own read_byte/write_byte gets called, so only the low bits need decoding.
class SccStub {
public:
    void reset(){
        ptr_[0]=ptr_[1]=0; wr1_[0]=wr1_[1]=0; wr2_=0; wr9_=0;
        wr15_[0]=wr15_[1]=0xf8; ext_pending_[0]=ext_pending_[1]=false; dcd_[0]=dcd_[1]=false;
        irq_=false;
    }
    // ch: 0=A, 1=B (matches macplus's "PA_EXT/DCDA=X, PB_EXT/DCDB=Y" convention,
    // though on desktop Mac IIs both channels are ordinary RS-422 ports).
    uint8_t read(uint32_t local){
        const int which=int((local>>1)&3); const int ch=(which&1)?0:1;
        if(which&2) return 0;  // data register: no serial device attached
        uint8_t rr=ptr_[ch]; ptr_[ch]=0;
        if(rr==0){uint8_t v=0x24; if(dcd_[ch]) v=uint8_t(v|0x08); return v;}
        if(rr==2){
            if(ch==1){
                uint8_t src=0;
                if(ext_pending_[0]) src=0x5; else if(ext_pending_[1]) src=0x1;
                return uint8_t((wr2_&0xf1)|uint8_t(src<<1));
            }
            return wr2_;
        }
        if(rr==15) return wr15_[ch];
        return 0;
    }
    void write(uint32_t local,uint8_t value){
        const int which=int((local>>1)&3); const int ch=(which&1)?0:1;
        if(which&2) return;  // data register: nothing listening
        if(ptr_[ch]==0){
            const uint8_t cmd=uint8_t((value>>3)&7);
            const uint8_t reg=uint8_t(value&7);
            ptr_[ch]=(cmd==1)?uint8_t(reg+8):reg;  // Point High -> WR8-15
            if(cmd==2||cmd==7){ ext_pending_[ch]=false; irq_=ext_pending_[0]||ext_pending_[1]; }
            return;
        }
        const uint8_t reg=ptr_[ch];
        if(reg==1) wr1_[ch]=value;
        else if(reg==2) wr2_=value;
        else if(reg==9) wr9_=value;
        else if(reg==15) wr15_[ch]=value;
        ptr_[ch]=0;
    }
    bool irq() const { return irq_; }
private:
    uint8_t ptr_[2]{0,0}, wr1_[2]{0,0}, wr2_=0, wr9_=0, wr15_[2]{0xf8,0xf8};
    bool ext_pending_[2]{false,false}, dcd_[2]{false,false}, irq_=false;
};

class MacII : public Machine {
public:
    static constexpr uint16_t kOpReset=0x7103, kOpPatchBootGlobs=0x7107, kOpFixMemSize=0x7109;
    static constexpr uint16_t kOpInstallDrivers=0x710A, kOpScsiDispatch=0x7128;
    static constexpr uint32_t kCpuClock=15667200;
    static constexpr int kVTotal=525,kWidth=640,kHeight=480,kVBlankLines=45,kCyclesPerLine=400;
    static constexpr uint32_t kRamSize=0x800000,kRomSizeMax=0x100000,kVramSize=640u*480u;
    static constexpr uint32_t kRomBase=0x40800000u,kRomBaseAlt=0x40000000u;
    static constexpr uint32_t kScratchBase=0x50F80000u,kScratchSize=0x10000u;
    static constexpr uint32_t kAscPhys=0x50F14000u;
    static constexpr int kSampleRate=22254;
    static constexpr double kFps=double(kCpuClock)/double(kVTotal*kCyclesPerLine);
    MacII();
    bool init(const std::string& rom_path,std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path,std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs&) override;
    void set_dip_switch(int,uint8_t) override {}
    const uint32_t* framebuffer() const override {return framebuffer_.data();}
    int screen_width() const override {return kWidth;}
    int screen_height() const override {return kHeight;}
    double frames_per_second() const override {return kFps;}
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override {return kSampleRate;}
    const char* title() const override {return "Macintosh Q650 (Basilisk)";}
    bool uses_keyboard() const override {return true;}
    bool uses_pointer() const override {return true;}
private:
    enum class MapKind{Ram,Rom,Via1,Via2,Scsi,ScsiDrq,Asc,Iwm,Scc,IoStub,Scratch,NubusFb,NubusDecl,Unmapped};
    static uint32_t mac_norm(uint32_t a){return a>=0xFF000000u?(a&0x00FFFFFFu):a;}
    MapKind classify(uint32_t a) const;
    int rom_offset(uint32_t a) const;
    uint32_t scsi_addr(uint32_t a,bool force_dack=false) const;
    uint32_t ram_phys(uint32_t a) const;
    uint8_t read_byte(uint32_t a); void write_byte(uint32_t a,uint8_t v);
    uint16_t read_word(uint32_t a); void write_word(uint32_t a,uint16_t v);
    uint8_t ram_at(uint32_t p) const {return ram_[p&(kRamSize-1)];}
    void ram_at(uint32_t p,uint8_t v){ram_[p&(kRamSize-1)]=v;}
    void write_long_be(uint32_t a,uint32_t v);
    uint32_t read_long_be(uint32_t a);
    uint32_t rom_be32(uint32_t off) const;
    void on_cpu_cycles(int c);
    bool on_emul_op(uint16_t opcode);
    void update_irqs(); void sync_scsi_irq();
    void apply_glue_map(); void apply_basilisk_patches();
    void emul_op_reset(); void seed_exception_vectors();
    void emul_install_drivers();
    void render(); void init_clut(); void build_nubus_decl();
    void on_exc(uint32_t vec,uint32_t pc);
    bool on_aline(uint16_t op,uint32_t pc);
    uint32_t heap_alloc(uint32_t size);
    uint8_t via1_pa_r(); void via1_pa_w(uint8_t v);
    uint8_t via1_pb_r(); void via1_pb_w(uint8_t v);
    uint8_t via2_pa_r(); void via2_pa_w(uint8_t v);
    uint8_t via2_pb_r(); void via2_pb_w(uint8_t v);
    static uint8_t vreg(uint32_t a){return uint8_t((a>>9)&0x0f);}
    M68000 cpu_; Via6522 via1_,via2_;
    Iwm iwm_; MacRtc rtc_; Ncr5380Hdd scsi_; AscStub asc_; SccStub scc_;
    std::vector<uint8_t> ram_,rom_,vram_,nubus_decl_,scratch_;
    uint32_t rom_size_=0x100000,universal_info_=0,asc_base_=0,heap_next_=0x10000;
    std::array<uint32_t,256> clut_{};
    std::array<uint32_t,kWidth*kHeight> framebuffer_{};
    std::vector<int16_t> audio_;
    bool overlay_=true,via1_irq_=false,via2_irq_=false,scsi_irq_level_=false;
    bool rtc_data_=true,reset_done_=false;
    uint8_t glue_=0,nubus_irq_=0x3f;
    uint32_t bank_b_base_=0x00100000;
    int via_acc_=0,frame_count_=0,diag_n_=0,stm_stuck_=0,slot_next_=0,ppat_stuck_=0;
    uint32_t last_pc_=0,scsi_acc_=0,aline_n_=0,scratch_acc_=0;
};
}
