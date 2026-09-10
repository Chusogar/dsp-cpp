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
    enum class MapKind{Ram,Rom,Via1,Via2,Scsi,ScsiDrq,Asc,Iwm,IoStub,Scratch,NubusFb,NubusDecl,Unmapped};
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
    Iwm iwm_; MacRtc rtc_; Ncr5380Hdd scsi_; AscStub asc_;
    std::vector<uint8_t> ram_,rom_,vram_,nubus_decl_,scratch_;
    uint32_t rom_size_=0x100000,universal_info_=0,asc_base_=0,heap_next_=0x10000;
    std::array<uint32_t,256> clut_{};
    std::array<uint32_t,kWidth*kHeight> framebuffer_{};
    std::vector<int16_t> audio_;
    bool overlay_=true,via1_irq_=false,via2_irq_=false,scsi_irq_level_=false;
    bool rtc_data_=true,reset_done_=false;
    uint8_t glue_=0,nubus_irq_=0x3f;
    uint32_t bank_b_base_=0x00100000;
    int via_acc_=0,frame_count_=0,diag_n_=0,stm_stuck_=0,slot_next_=0;
    uint32_t last_pc_=0,scsi_acc_=0,aline_n_=0,scratch_acc_=0;
};
}
