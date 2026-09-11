#include "macii.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
namespace dsp {
namespace {
void put_be16(std::vector<uint8_t>& d,size_t o,uint16_t v){d[o]=uint8_t(v>>8);d[o+1]=uint8_t(v);}
void put_be24(std::vector<uint8_t>& d,size_t o,uint32_t v){d[o]=uint8_t((v>>16)&0xff);d[o+1]=uint8_t((v>>8)&0xff);d[o+2]=uint8_t(v);}
void put_be32v(std::vector<uint8_t>& d,size_t o,uint32_t v){d[o]=uint8_t(v>>24);d[o+1]=uint8_t(v>>16);d[o+2]=uint8_t(v>>8);d[o+3]=uint8_t(v);}
void sdir(std::vector<uint8_t>& d,size_t& p,uint8_t type,int32_t abs_t){d[p]=type;put_be24(d,p+1,uint32_t((abs_t-int32_t(p))&0xffffff));p+=4;}
uint32_t apple_crc24(const uint8_t* data,size_t len){uint32_t crc=0;for(size_t i=0;i<len;++i){crc^=uint32_t(data[i])<<16;for(int b=0;b<8;++b){crc<<=1;if(crc&0x1000000u)crc^=0x80001Bu;}}return crc&0xffffffu;}
void patch_bytes(std::vector<uint8_t>& r,uint32_t off,std::initializer_list<uint8_t> b){uint32_t i=0;for(uint8_t v:b){if(off+i<r.size())r[off+i]=v;++i;}}
void patch_nops(std::vector<uint8_t>& r,uint32_t off,int n){for(int i=0;i<n;i++){if(off+uint32_t(i)*2+1<r.size()){r[off+uint32_t(i)*2]=0x4E;r[off+uint32_t(i)*2+1]=0x71;}}}
uint32_t find_rom_data(const std::vector<uint8_t>& r,uint32_t s,uint32_t e,const uint8_t* p,size_t n){
    e=std::min(e,uint32_t(r.size()));for(uint32_t i=s;i+n<=e;i++)if(std::memcmp(&r[i],p,n)==0)return i;return 0;}
}
MacII::MacII():cpu_(kCpuClock,M68000::Type::M68020),via1_(kCpuClock/20),via2_(kCpuClock/20){
    ram_.assign(kRamSize,0);rom_.assign(kRomSizeMax,0xff);vram_.assign(kVramSize,0);
    nubus_decl_.assign(0x10000,0);scratch_.assign(kScratchSize,0);
    init_clut();build_nubus_decl();cpu_.set_address_mask(0xfffffffeu);
    cpu_.set_memory_handlers([this](uint32_t a){return read_word(a);},[this](uint32_t a,uint16_t v){write_word(a,v);});
    cpu_.set_byte_handlers([this](uint32_t a){return read_byte(a);},[this](uint32_t a,uint8_t v){write_byte(a,v);});
    cpu_.set_cycle_handler([this](int c){on_cpu_cycles(c);});
    cpu_.set_exception_handler([this](uint32_t v,uint32_t p){on_exc(v,p);});
    cpu_.set_aline_handler([this](uint16_t op,uint32_t p){return on_aline(op,p);});
    cpu_.set_emul_op_handler([this](uint16_t op){return on_emul_op(op);});
    via1_.set_port_a([this](){return via1_pa_r();},[this](uint8_t v){via1_pa_w(v);});
    via1_.set_port_b([this](){return via1_pb_r();},[this](uint8_t v){via1_pb_w(v);});
    via1_.set_irq_callback([this](IrqLine l){via1_irq_=l!=IrqLine::Clear;update_irqs();});
    via2_.set_port_a([this](){return via2_pa_r();},[this](uint8_t v){via2_pa_w(v);});
    via2_.set_port_b([this](){return via2_pb_r();},[this](uint8_t v){via2_pb_w(v);});
    via2_.set_irq_callback([this](IrqLine l){via2_irq_=l!=IrqLine::Clear;update_irqs();});
}
uint32_t MacII::rom_be32(uint32_t off) const{if(off+3>=rom_size_)return 0;return (uint32_t(rom_[off])<<24)|(uint32_t(rom_[off+1])<<16)|(uint32_t(rom_[off+2])<<8)|rom_[off+3];}
uint32_t MacII::read_long_be(uint32_t a){return (uint32_t(read_byte(a))<<24)|(uint32_t(read_byte(a+1))<<16)|(uint32_t(read_byte(a+2))<<8)|read_byte(a+3);}
uint32_t MacII::heap_alloc(uint32_t size){
    size=(size+3)&~3u;
    if(heap_next_+size>=0x700000u) return 0;
    uint32_t p=heap_next_; heap_next_+=size;
    for(uint32_t i=0;i<size;i++) ram_at(p+i,0);
    return p;
}
bool MacII::on_aline(uint16_t op,uint32_t pc){
    ++aline_n_;
    const uint16_t num=uint16_t(op&0x3FF);
    if(aline_n_<=24)
        std::fprintf(stderr,"macii: A-line $%04x (num=$%03x) D0=$%08x A0=$%08x @$%08x\n",
            op,num,cpu_.d[0].l,cpu_.a[0].l,pc);
    // We fully handle a small set; everything else returns false so the
    // CPU takes vector 10 (our stub advances PC and RTE, or later the real dispatcher).
    switch(num){
    case 0x085: case 0x080: // StripAddress
        cpu_.d[0].l = cpu_.d[0].l & 0x00FFFFFF;
        cpu_.pc_.l = pc+2; return true;
    case 0x02E: { // BlockMove
        uint32_t src=cpu_.a[0].l,dst=cpu_.a[1].l,n=cpu_.d[0].l;
        for(uint32_t i=0;i<n;i++) write_byte(dst+i,read_byte(src+i));
        cpu_.d[0].l=0; cpu_.pc_.l=pc+2; return true;
    }
    case 0x01C: case 0x11C: case 0x01E: case 0x11E: { // NewPtr family
        uint32_t p=heap_alloc(cpu_.d[0].l?cpu_.d[0].l:1);
        cpu_.a[0].l=p; cpu_.d[0].l=p?0:0xFFFFFFCFu;
        cpu_.cc.z=(cpu_.d[0].l==0); cpu_.cc.n=false; cpu_.cc.c=false; cpu_.cc.v=false;
        cpu_.pc_.l=pc+2; return true;
    }
    case 0x01F: // DisposePtr
        cpu_.d[0].l=0; cpu_.pc_.l=pc+2; return true;
    case 0x051: case 0x200: case 0x122: { // NewHandle family
        uint32_t master=heap_alloc(4), data=heap_alloc(cpu_.d[0].l?cpu_.d[0].l:1);
        if(master&&data){write_long_be(master,data);cpu_.a[0].l=master;cpu_.d[0].l=0;}
        else{cpu_.a[0].l=0;cpu_.d[0].l=0xFFFFFFCFu;}
        cpu_.pc_.l=pc+2; return true;
    }
    case 0x023: case 0x029: case 0x02A: case 0x022: // HLock/HUnlock/DisposeHandle
        cpu_.d[0].l=0; cpu_.pc_.l=pc+2; return true;
    case 0x06E: { // Slot Manager — advertise Toby video in NuBus slot 9 (Basilisk-style)
        const int16_t sel = int16_t(cpu_.d[0].l);
        const uint32_t sp = cpu_.a[0].l;
        const uint8_t id0 = read_byte(sp+0x33);
        const uint8_t slot0 = read_byte(sp+0x32);
        if(aline_n_<=48)
            std::fprintf(stderr,"macii: SlotManager sel=%d SpBlock=$%08x slot=%u id=$%02x next=%d\n",
                sel,sp,slot0,id0,slot_next_);
        int32_t err = 0;
        auto sp_slot = [&](uint8_t v){ write_byte(sp+0x32, v); };
        auto sp_id   = [&](uint8_t v){ write_byte(sp+0x33, v); };
        auto sp_res  = [&](uint32_t v){ write_long_be(sp+0, v); };
        auto sp_cat  = [&](uint8_t v){ write_byte(sp+0x34, v); };
        auto sp_type = [&](uint8_t v){ write_byte(sp+0x35, v); };
        auto sp_tb   = [&](uint8_t v){ write_byte(sp+0x36, v); };
        auto sp_hw   = [&](uint8_t v){ write_byte(sp+0x37, v); };
        switch(sel){
        case 0:  // sReadByte
        case 1:  // sReadWord
        case 2:  // sReadLong
        case 3:  // sGetCString
        case 4:  // sGetBlock
        case 5:  // sFindStruct
        case 6:  // sReadInfo
        case 8:  // sReadBoardInfo
        case 16: // sInitPRAMRecs
        case 17: // sPrimaryInit
        case 18: // sCardChanged
        case 20: // sFindSInfoRecPtr
            err = 0; break;
        case 21: { // sReadDrvrName
            // Boot loop at $102E uses sel21 success as "keep scanning".
            // After we have yielded our one sResource (slot_next_!=0), fail so
            // the BEQ at $1032 falls through to the exit path ($104C).
            if(slot_next_ != 0)
                err = -347; // smNoBoard — done enumerating
            else
                err = 0;
            break;
        }
        case 22: { // sNextsRsrc
            // Basilisk / ROM boot walk: yield our Toby sResource once, then smNoMoresRsrcs.
            // Key: only one video sRsrc; after that always -344 so the outer BNE exits.
            if(slot_next_ == 0 && (id0 == 0 || id0 == 0xFF)){
                sp_slot(9);
                sp_id(0x80);
                sp_cat(1);   // catDisplay
                sp_type(1);  // typeVideo
                sp_tb(0);
                sp_hw(1);    // DrvrHW
                sp_res(0xF9000000u); // slot space base (declaration + fb)
                write_long_be(sp+0x08, 0xF9000000u); // spPointer
                write_long_be(sp+0x10, 0xF90FF000u); // sResource ptr near decl
                // Publish ScrnBase early (same as success path $C0C)
                write_long_be(0x0824, 0xF9000000u);
                write_long_be(0x0C24, 0xF9000000u); // MainDevice-ish
                slot_next_ = 1;
                err = 0;
            } else {
                err = -344; // smNoMoresRsrcs
            }
            break;
        }
        case 23: // sNextTypesRsrc
            err = -344; break;
        case 24: // sRsrcInfo
            sp_slot(9); sp_id(0x80); sp_cat(1); sp_type(1); sp_tb(0); sp_hw(1);
            sp_res(0xF9000000u);
            err = 0; break;
        case 25: // sDisposePtr
        case 26: // sCkCardStat
            err = 0; break;
        case 27: // sFindSResource / sExec
            sp_slot(9); sp_id(0x80); sp_res(0xF9000000u);
            write_long_be(sp+0x08, 0xF9000000u);
            // Publish ScrnBase so QD has somewhere to draw
            if(read_long_be(0x0824) == 0)
                write_long_be(0x0824, 0xF9000000u);
            err = 0; break;
        case 19: // sSecondaryInit
        case 9:  // sReadFHeader
            err = 0; break;
        default:
            err = -347; // smNoBoard
            break;
        }
        cpu_.d[0].l = uint32_t(err);
        cpu_.cc.z = (err==0);
        cpu_.cc.n = (err<0);
        cpu_.cc.c = false; cpu_.cc.v = false;
        cpu_.pc_.l=pc+2; return true;
    }
    case 0x000: case 0x001: case 0x002: case 0x003:
    case 0x004: case 0x005: case 0x006: // Open..KillIO
        cpu_.d[0].l=0; cpu_.pc_.l=pc+2; return true;
    case 0x01B: // MaxMem
        cpu_.d[0].l=kRamSize; cpu_.a[0].l=kRamSize; cpu_.pc_.l=pc+2; return true;
    case 0x22F: { // InitGraf — Basilisk/ROM boot pushes -1; REPLACE TOS with ptr-to-thePort
        // Caller: MOVE.L #-1,-(SP) / _InitGraf / MOVEA.L (SP)+,A0 / MOVE.L (A0),A1
        // A0 = &thePort, A1 = GrafPort*. Extra push was sending A1 to NULL.
        uint32_t qd = heap_alloc(0x100);
        uint32_t port = heap_alloc(0x100);
        uint32_t pix_master = heap_alloc(4);
        uint32_t pix = heap_alloc(0x40);
        uint32_t rgn_master = heap_alloc(4);
        uint32_t rgn = heap_alloc(0x20);
        if(!qd||!port||!pix_master||!pix||!rgn_master||!rgn){
            cpu_.d[0].l=0xFFFFFFCFu; cpu_.pc_.l=pc+2; return true;
        }
        write_long_be(pix_master, pix);
        write_long_be(rgn_master, rgn);
        // minimal region (rgnSize=10, empty rect)
        write_byte(rgn+0,0); write_byte(rgn+1,10);
        // PixMap: baseAddr = NuBus slot-9 fb, rowBytes = 0x8000|640, bounds 640x480, pm1
        write_long_be(pix+0, 0xF9000000u);
        write_byte(pix+4, 0x82); write_byte(pix+5, 0x80); // rowBytes
        write_byte(pix+6,0); write_byte(pix+7,0);
        write_byte(pix+8,0); write_byte(pix+9,0);
        write_byte(pix+10,0x01); write_byte(pix+11,0xE0); // bottom 480
        write_byte(pix+12,0x02); write_byte(pix+13,0x80); // right 640
        write_byte(pix+14,0); write_byte(pix+15,0); // pmVersion
        write_byte(pix+16,0); write_byte(pix+17,0); // packType
        write_long_be(pix+22, 0x00480000u); // hRes 72dpi
        write_long_be(pix+26, 0x00480000u);
        write_byte(pix+30,0); write_byte(pix+31,0); // pixelType
        write_byte(pix+32,0); write_byte(pix+33,8);  // pixelSize
        write_byte(pix+34,0); write_byte(pix+35,1);  // cmpCount
        write_byte(pix+36,0); write_byte(pix+37,8);  // cmpSize
        // CGrafPort
        write_long_be(port+2, pix_master);   // portPixMap
        write_byte(port+6, 0xC0); write_byte(port+7, 0x00); // portVersion
        write_long_be(port+8, rgn_master);   // grafVars
        write_byte(port+16,0); write_byte(port+17,0);
        write_byte(port+18,0); write_byte(port+19,0);
        write_byte(port+20,0x01); write_byte(port+21,0xE0);
        write_byte(port+22,0x02); write_byte(port+23,0x80);
        write_long_be(port+24, rgn_master);  // visRgn
        write_long_be(port+28, rgn_master);  // clipRgn
        write_long_be(port+0x16, pix_master); // in case ROM reads $16(A1) as a handle
        uint32_t thePortField = qd + 0xFC;
        write_long_be(thePortField, port);
        // Boot stub pushes CLR.L / CLR.W / MOVE.L #-1 then _InitGraf then MOVEA.L (SP)+.
        // Compact the 6 dummy bytes so TOS is thePortField and RTS still sees the BSR return.
        const uint32_t tos = read_long_be(cpu_.a[7].l);
        if(tos == 0xFFFFFFFFu) cpu_.a[7].l += 6;
        write_long_be(cpu_.a[7].l, thePortField);
        cpu_.d[0].l = 0;
        cpu_.cc.z = true; cpu_.cc.n = false; cpu_.cc.c = false; cpu_.cc.v = false;
        cpu_.pc_.l = pc+2;
        std::fprintf(stderr,"macii: InitGraf thePortField=$%08x port=$%08x SP=$%08x\n",
            thePortField, port, cpu_.a[7].l);
        return true;
    }
    case 0x31E: { // NewPtrSysClear (flagged)
        uint32_t p=heap_alloc(cpu_.d[0].l?cpu_.d[0].l:1);
        cpu_.a[0].l=p; cpu_.d[0].l=p?0:0xFFFFFFCFu;
        cpu_.cc.z=(cpu_.d[0].l==0); cpu_.cc.n=false; cpu_.cc.c=false; cpu_.cc.v=false;
        cpu_.pc_.l=pc+2; return true;
    }
    case 0x1A0: case 0x19A: case 0x1AF: case 0x1A4: { // GetResource
        // Boot ROM $1176: CLR.W -(SP)/_GetResource/MOVE.L (SP)+,D0/BEQ $1182/JSR/RTS
        // Take the nil path: drop CLR.W, set Z, jump to RTS at $1182.
        const uint32_t off = pc & 0xfffffu;
        if(off >= 0x1170u && off <= 0x1180u){
            const uint32_t sp = cpu_.a[7].l;
            // Stack bytes: 00 00 | SERD | 4080116C | ... | 40846F4E ...
            // Skip local $116C; take first ROM return with offset >= $2000
            uint32_t ret = 0; int offb = -1;
            for(int i=0;i<48;i+=2){
                uint32_t v = read_long_be(sp+i);
                if((v & 0xFFF00000u)==0x40800000u || (v & 0xFFF00000u)==0x40000000u){
                    if((v & 0xfffffu) >= 0x2000u){ ret=v; offb=i; break; }
                }
            }
            if(ret){
                cpu_.a[7].l = sp + offb + 4;
                cpu_.d[0].l = 0; cpu_.a[0].l = 0;
                cpu_.cc.z = true; cpu_.cc.n = false;
                cpu_.pc_.l = ret;
                std::fprintf(stderr,"macii: GetResource nil -> $%08x SP=$%08x\n", ret, cpu_.a[7].l);
            } else {
                // fall back: drop CLR.W only, RTS local
                cpu_.a[7].l = sp + 2;
                cpu_.d[0].l = 0; cpu_.a[0].l = 0;
                cpu_.cc.z = true;
                cpu_.pc_.l = (pc & 0xfff00000u) | 0x1182;
                std::fprintf(stderr,"macii: GetResource fallback RTS\n");
            }
            return true;
        }
        cpu_.a[0].l = 0; cpu_.d[0].l = 0;
        cpu_.cc.z = true; cpu_.cc.n = false; cpu_.cc.c = false; cpu_.cc.v = false;
        cpu_.pc_.l = pc+2; return true;
    }
    case 0x1A3: case 0x1A8: // ReleaseResource / DetachResource
        cpu_.d[0].l=0; cpu_.pc_.l=pc+2; return true;
    case 0x195: case 0x9FF: case 0x247: case 0x647:
    case 0x12D: case 0x12E: // SetCursor / InitCursor
    case 0x12C: // HideCursor
    case 0x18F: // InitFonts
    case 0x173: // InitWindows
    case 0x16C: // InitMenus
    case 0x19D: // TEInit
    case 0x175: // InitDialogs
    case 0x1A9: // MoreMasters
    case 0x1E4: case 0x1E5: // SysBeep / SysError
        cpu_.d[0].l=0; cpu_.pc_.l=pc+2; return true;
    case 0x1C9: // GetFNum
        cpu_.d[0].l=0; cpu_.pc_.l=pc+2; return true;
    case 0x03B: // HNoPurge etc
        cpu_.d[0].l=0; cpu_.pc_.l=pc+2; return true;
    case 0x20C: {
        // Trap $AA0C: a Mac II-only Color QuickDraw extended trap (the
        // $AA00-$ABFF range only exists on machines with Color QuickDraw)
        // that looks up a default pixel pattern/cursor ('ppat'/'crsr')
        // from a ROM-internal fallback list when the Resource Manager
        // doesn't have a real one loaded yet. That list is meant to be
        // lazily built the first time it's needed; the ROM call path that
        // builds it isn't one we reach, so the list is left at its
        // power-on -1 sentinel, and the search loop — which has no bound
        // on how large a "count" it will trust from that uninitialized
        // chain — never reliably reaches its own "not found" case, and
        // gets retried from scratch indefinitely by its caller. Short
        // -circuit straight to that same "not found" signal the real
        // search uses on exhaustion (D0 = -1, then return) — this is
        // exactly what a genuinely empty/absent list would also produce,
        // so callers already have to handle it gracefully.
        cpu_.d[0].l = 0xFFFFFFFFu; cpu_.pc_.l = pc+2; return true;
    }
    default:
        if(aline_n_<=30)
            std::fprintf(stderr,"macii: A-line pass-through num=$%03x\n",num);
        return false; // let vector 10 / ROM dispatcher run
    }
}
bool MacII::on_emul_op(uint16_t opcode){
    switch(opcode){
    case kOpReset: emul_op_reset(); return true;
    case kOpPatchBootGlobs:{
        const uint32_t a4=cpu_.a[4].l;
        write_long_be(a4-20,kRamSize);write_byte(a4-26,0);
        write_byte(a4-25,uint8_t(read_byte(a4-25)|1));cpu_.a[6].l=kRamSize;
        std::fprintf(stderr,"macii: EMUL_OP_PATCH_BOOT_GLOBS\n"); return true;}
    case kOpFixMemSize:{
        const uint32_t phys=read_long_be(0x1ef8),log=read_long_be(0x1ef4);
        const uint32_t diff=(phys>log)?(phys-log):0;
        write_long_be(0x1ef8,kRamSize);write_long_be(0x1ef4,kRamSize-diff);
        std::fprintf(stderr,"macii: EMUL_OP_FIX_MEMSIZE\n"); return true;}
    case kOpInstallDrivers: emul_install_drivers(); return true;
    case kOpScsiDispatch: cpu_.d[0].l=0; return true;
    default: return true;
    }
}
void MacII::emul_install_drivers(){
    static int once=0;
    if(once++){ return; }
    std::fprintf(stderr,"macii: EMUL_OP_INSTALL_DRIVERS\n");
    // Basilisk-style fake ASC registers
    uint32_t asc = heap_alloc(0x1000);
    if(!asc) asc = kAscPhys;
    for(uint32_t i=0;i<0x1000;i++) write_byte(asc+i,0);
    write_byte(asc+0x800, 0x0f);
    write_long_be(0x0cc0, asc);
    asc_base_ = asc;
    // Minimal Unit Table
    if(read_long_be(0x011c)==0) write_long_be(0x011c, heap_alloc(0x200));
    // ExpandSysZone / heap limits the ROM expects after driver install
    if(read_long_be(0x0108)==0) write_long_be(0x0108, kRamSize);
    if(read_long_be(0x010c)==0) write_long_be(0x010c, kRamSize);
    // CurrentA5 / ExpandMem-ish so toolbox traps that touch them don't die
    if(read_long_be(0x0904)==0) write_long_be(0x0904, 0x00010800);
    if(read_long_be(0x0ccc)==0) write_long_be(0x0ccc, heap_alloc(0x400)); // ExpandMem
    std::fprintf(stderr,"macii: ASCBase=$%08x\n",asc);
}
void MacII::apply_basilisk_patches(){
    int n=0; auto note=[&](const char* s){std::fprintf(stderr,"macii: + %s\n",s);++n;};
    universal_info_=0;
    static const uint8_t uni_pat[]={0xdc,0x00,0x05,0x05,0x3f,0xff,0x01,0x00};
    for(uint32_t i=0x3400;i+8<rom_size_&&i<0x3c00;i++)
        if(std::memcmp(&rom_[i],uni_pat,8)==0){universal_info_=i-0x10;break;}
    std::fprintf(stderr,"macii: UniversalInfo=$%04x\n",universal_info_);
    if(universal_info_){
        uint32_t nb=rom_be32(universal_info_+12),p=universal_info_+nb;
        if(p+16<rom_size_){rom_[p]=0x03;for(int i=1;i<16;i++)rom_[p+i]=0x08;}
        if(universal_info_+18<rom_size_) rom_[universal_info_+18]=5;
        if(universal_info_+22<rom_size_) rom_[universal_info_+22]=4;
        note("UniversalInfo");
        int32_t map_off=int32_t(rom_be32(universal_info_));
        uint32_t dec=universal_info_+uint32_t(map_off);
        for(uint32_t tp=0x94a;tp+4<=rom_size_;tp+=4){
            int16_t ofs=int16_t((uint16_t(rom_[tp])<<8)|rom_[tp+1]);
            int16_t lmg=int16_t((uint16_t(rom_[tp+2])<<8)|rom_[tp+3]);
            if(uint16_t(ofs)==0xffff) break;
            if(lmg==0x0cc0) continue;
            uint32_t slot=dec+uint32_t(ofs)*4u; if(slot+3>=rom_size_) continue;
            // Same exception as the idx-list pass below: don't stomp a slot
            // that already points at hardware we actually emulate.
            {const uint32_t v=rom_be32(slot);
             if((v&0xFFFF0000u)==0x50F10000u) continue;   // SCSI/SCSIDack
             if(v>=0x50F04000u&&v<0x50F06000u) continue;}  // SCC
            rom_[slot+0]=uint8_t(kScratchBase>>24);rom_[slot+1]=uint8_t(kScratchBase>>16);
            rom_[slot+2]=uint8_t(kScratchBase>>8);rom_[slot+3]=uint8_t(kScratchBase);
        }
        for(int idx:{2,3,4,5,8,9,10,11,13,14,15}){
            uint32_t slot=dec+uint32_t(idx)*4u; if(slot+3>=rom_size_) continue;
            // Keep the ROM's own address if it already points at SCSI (real
            // hardware we emulate) or the SCC (likewise); every other
            // peripheral base still gets redirected to the safe scratch
            // stub, matching what this driver was originally tuned against.
            {const uint32_t v=rom_be32(slot);
             if((v&0xFFFF0000u)==0x50F10000u) continue;
             if(v>=0x50F04000u&&v<0x50F06000u) continue;}
            rom_[slot+0]=uint8_t(kScratchBase>>24);rom_[slot+1]=uint8_t(kScratchBase>>16);
            rom_[slot+2]=uint8_t(kScratchBase>>8);rom_[slot+3]=uint8_t(kScratchBase);
        }
        for(uint32_t tp=0x94a;tp+4<=rom_size_;tp+=4){
            int16_t ofs=int16_t((uint16_t(rom_[tp])<<8)|rom_[tp+1]);
            int16_t lmg=int16_t((uint16_t(rom_[tp+2])<<8)|rom_[tp+3]);
            if(uint16_t(ofs)==0xffff) break;
            if(lmg==0x0cc0){uint32_t slot=dec+uint32_t(ofs)*4u;if(slot+3<rom_size_){
                rom_[slot+0]=uint8_t(kAscPhys>>24);rom_[slot+1]=uint8_t(kAscPhys>>16);
                rom_[slot+2]=uint8_t(kAscPhys>>8);rom_[slot+3]=uint8_t(kAscPhys);}}
        }
        note("PatchHWBases+ASC");
    }
    patch_bytes(rom_,0x90,{0x71,0x03,0x4E,0xF9,0x40,0x80,0x00,0xBA}); note("EMUL_OP_RESET+JMP BA");
    // [disabled: corrupts the bsr.w $73e displacement at 0xC0-0xC3 into a
    //  bogus target once the entry patch above is correctly aligned at
    //  0x90; let the real VIA-probe branch run against our emulated VIA]
    // patch_nops(rom_,0xC2,2); note("GetHardwareInfo");
    // patch_nops(rom_,0xC6,15); note("VIA init");
    if(rom_size_>0x7C4){patch_bytes(rom_,0x7C0,{0x7E,0x04,0x4E,0x75}); note("CPU type");}
    {static const uint8_t cg[]={0x42,0x9a,0x36,0x0a,0x66,0xfa};
     uint32_t b=find_rom_data(rom_,0xa00,0xb00,cg,sizeof(cg)); if(b){patch_nops(rom_,b+2,2);note("clear_globs");}}
    {static const uint8_t scc[]={0x08,0x38,0x00,0x01,0x0d,0xd1,0x67,0x04};
     uint32_t b=find_rom_data(rom_,0xa00,0xa80,scc,sizeof(scc)); if(b){rom_[b]=0x4E;rom_[b+1]=0x75;note("init_scc");}}
    if(rom_size_>0x9C2){rom_[0x9C0]=0x4E;rom_[0x9C1]=0x75;note("init_iwm");}
    if(rom_size_>0x9A2){rom_[0x9A0]=0x4E;rom_[0x9A1]=0x75;note("init_scsi");}
    {static const uint8_t asc[]={0x26,0x68,0x00,0x30,0x12,0x00,0xeb,0x01};
     uint32_t b=find_rom_data(rom_,0x4000,0x5000,asc,sizeof(asc)); if(b){rom_[b]=0x4E;rom_[b+1]=0xD6;note("init_asc");}}
    patch_nops(rom_,0x190,2);
    if(rom_size_>0x9F4E){rom_[0x9F4C]=0x4E;rom_[0x9F4D]=0x75;}
    if(rom_size_>0x820){patch_bytes(rom_,0x800,{
        0x31,0xFC,0x27,0x10,0x0D,0x00,0x31,0xFC,0x27,0x10,0x0D,0x02,
        0x31,0xFC,0x27,0x10,0x0B,0x24,0x31,0xFC,0x27,0x10,0x0C,0xEA,0x4E,0x75}); note("SetupTimeK");}
    {static const uint8_t mid[]={0x45,0xf9,0x5f,0xff,0xff,0xfc,0x20,0x12};
     uint32_t b=find_rom_data(rom_,0x4000,0x5000,mid,sizeof(mid)); if(b){patch_bytes(rom_,b+6,{0x70,0x00,0xB0,0x40,0x4E,0xD6});note("model_id2");}}
    {static const uint8_t m1[]={0x0c,0x47,0x00,0x04,0x62,0x00,0xfd};
     uint32_t b=find_rom_data(rom_,0x80000,0x90000,m1,sizeof(m1));
     if(b){patch_nops(rom_,b,4);if(b+13<rom_size_){rom_[b+10]=0x70;rom_[b+11]=0x00;rom_[b+12]=0x4E;rom_[b+13]=0x71;}note("InitMMU");}
     static const uint8_t m2[]={0x08,0x06,0x00,0x0d,0x67}; b=find_rom_data(rom_,0x80000,0x90000,m2,sizeof(m2)); if(b){rom_[b+4]=0x60;}
     static const uint8_t m3[]={0x0c,0x2e,0x00,0x01,0xff,0xe6,0x66,0x0c,0x4c,0xed,0x03,0x87,0xff,0xe8};
     b=find_rom_data(rom_,0x80000,0x90000,m3,sizeof(m3)); if(b){patch_nops(rom_,b+6,1);}}
    if(rom_size_>0x1144){rom_[0x1142]=0x71;rom_[0x1143]=0x0A;note("EMUL_OP_INSTALL_DRIVERS");}
    if(rom_size_>0x1150){patch_nops(rom_,0x1144,4);}
    if(rom_size_>0x4B0){patch_bytes(rom_,0x490,{
        0x20,0x38,0x01,0x0C,0xD0,0xB8,0x02,0xA6,0xE2,0x88,0x08,0x80,0x00,0x00,
        0x04,0x40,0x04,0x00,0x20,0x40,0x71,0x09,0x4E,0x75}); note("FIX_MEMSIZE");}
    if(rom_size_>0x5B80){patch_bytes(rom_,0x5B78,{0x4E,0x71,0x4E,0x71,0x24,0x01,0x60,0x5E});}
    if(rom_size_>0x9BCC){patch_bytes(rom_,0x9BC4,{0x70,0x02,0x4E,0x71,0x4E,0x71,0x4E,0x71,0x4E,0x71});}
    if(rom_size_>0x112){patch_bytes(rom_,0x10E,{0x71,0x07,0x4E,0x71});}
    if(rom_size_>0x70EA){patch_bytes(rom_,0x70E8,{0x4E,0xD6});}
    if(rom_size_>0xB9ABE){patch_bytes(rom_,0xB9ABA,{0x4E,0x71,0x4E,0x71}); note("STM B9ABA");}
    if(rom_size_>0xB9FB2){patch_bytes(rom_,0xB9FB0,{0x4E,0x71}); note("STM B9FB0");}
    if(rom_size_>0xBA0BA){patch_bytes(rom_,0xBA0B8,{0x60,0x1C}); note("STM BA0B8");}
    // Fingerprint / serial diag: force success path at $B98E0 (MOVEQ #0,D5 already there)
    // NOP the long BRA back loops that keep us in the diag interpreter
    if(rom_size_>0xB9F9C){
        patch_bytes(rom_,0xB9F9A,{0x4E,0x71,0x4E,0x71}); note("STM B9F9A");
    }
    // NOTE: do NOT force JMP from STM diagnostic to InstallDrivers (not Basilisk-style)
    std::fprintf(stderr,"macii: Basilisk patches: %d\n",n);
}
void MacII::seed_exception_vectors(){
    // Hard faults -> self-loop. A-line/F-line get a tiny stub that advances PC and RTE
    // so early traps before the real dispatcher is installed do not spin on PC=0.
    ram_at(0x420,0x60); ram_at(0x421,0xFE); // BRA.S *
    auto setvec=[&](int vn,uint32_t addr){write_long_be(uint32_t(vn*4),addr);};
    for(int vn : {2,3,4,5}) // bus/addr/illegal/div0
        setvec(vn, 0x420);
    // A-line stub at $430: ADDQ.L #2,2(SP) ; MOVEQ #0,D0 ; RTE
    // (advance stacked PC past the A-line opcode, clear D0, return)
    const uint8_t aline_stub[] = {
        0x54,0xAF,0x00,0x02, // ADDQ.L #2, 2(SP)
        0x70,0x00,           // MOVEQ #0,D0
        0x4E,0x73            // RTE
    };
    for(size_t i=0;i<sizeof(aline_stub);i++) ram_at(0x430+i, aline_stub[i]);
    setvec(10, 0x430);
    // F-line: same idea
    for(size_t i=0;i<sizeof(aline_stub);i++) ram_at(0x440+i, aline_stub[i]);
    setvec(11, 0x440);
    // Color QuickDraw's extended traps ($AA00-$ABFF, Mac II only — e.g.
    // GetPixPat/_ppat lookups) fall back to a ROM-internal default
    // pattern/cursor list, reached through a double-indirect pointer chain
    // rooted at the low-memory globals $A50 (and the parallel $A5A/$A58
    // pair) whenever that chain hasn't been populated yet. On real
    // hardware something during Toolbox init lazily builds that list the
    // first time it's needed; in our environment the call path that does
    // that is never reached, so those globals are left at their power-on
    // -1 sentinel. The search code that walks the list has no bounds
    // check against that: it reads a 16-bit "entries remaining" count
    // from wherever the (garbage) chain points, and the upper half of
    // that count register is left over from unrelated earlier work, so it
    // ends up scanning tens of millions of "entries" instead of finding
    // an empty list and returning immediately. Rather than reverse the
    // exact ROM call that's supposed to build this list, hand it a
    // trivially valid EMPTY list ourselves: a real pointer chain whose
    // count is 0 and whose first (only) entry's 4-byte type code is 0,
    // which can never match a real 4-character resource type like 'ppat'
    // or 'crsr'. The search then does its normal one-entry check, finds
    // no match, and returns "not found" immediately, exactly as it would
    // for a legitimately empty list.
    {
        const uint32_t p1 = kRamSize - 4096 + 0x200;  // $A50 -> p1
        const uint32_t p2 = p1 + 0x20;                // [p1] -> p2 ("master pointer")
        write_long_be(p1, p2);
        write_word(uint32_t(p2 + 0x18), 0);         // no extra offset from p2
        write_word(p2, 0);                          // entry count = 0 (one harmless check)
        write_long_be(p2 + 2, 0);                       // entry[0] type = 0 (never matches)
        write_long_be(0x0a50, p1);
        write_long_be(0x0a54, p1);
        write_word(0x0a5a, uint16_t(p2 >> 16));
        write_word(0x0a58, uint16_t(p2));
    }
}
void MacII::emul_op_reset(){
    if(reset_done_) return; reset_done_=true;
    overlay_=false; glue_=0; apply_glue_map();
    std::fill(ram_.end()-4096,ram_.end(),0);
    const uint32_t boot_globs=kRamSize-0x1c;
    write_long_be(boot_globs+0x00,0); write_long_be(boot_globs+0x04,kRamSize);
    write_long_be(boot_globs+0x08,0xffffffff); write_long_be(boot_globs+0x0c,0);
    write_long_be(0x0108,kRamSize); write_long_be(0x010c,kRamSize);
    write_long_be(0x02a6,kRamSize); write_long_be(0x0120,kRomBase); write_long_be(0x02ae,kRomBase);
    write_byte(0x0d00,0x27); write_byte(0x0d01,0x10);
    write_byte(0x0d02,0x27); write_byte(0x0d03,0x10);
    write_long_be(0x0cc0,kAscPhys); asc_base_=kAscPhys;
    seed_exception_vectors();
    if(universal_info_){
        cpu_.d[0].l=rom_be32(universal_info_+0x18);
        cpu_.d[1].l=0x46000000u|(rom_be32(universal_info_+0x1c)&0x09ffffffu);
        cpu_.d[2].l=rom_be32(universal_info_+0x10)&0xefffffff;
        int32_t map_off=int32_t(rom_be32(universal_info_));
        cpu_.a[0].l=kRomBase+universal_info_+uint32_t(map_off);
        cpu_.a[1].l=kRomBase+universal_info_;
    }
    cpu_.a[6].l=boot_globs; cpu_.a[7].l=0x00010000;
    heap_next_=0x10000;
    std::fprintf(stderr,"macii: EMUL_OP_RESET D0=$%08x A1=$%08x\n",cpu_.d[0].l,cpu_.a[1].l);
}
void MacII::build_nubus_decl(){
    auto& d=nubus_decl_;std::fill(d.begin(),d.end(),0);const size_t ns=d.size();
    put_be16(d,0x100,0x0101);const char* bn="DSP Toby Video";d[0x110]=uint8_t(std::strlen(bn));std::memcpy(&d[0x111],bn,std::strlen(bn));
    d[0x130]=3;std::memcpy(&d[0x131],"DSP",3);d[0x140]=4;std::memcpy(&d[0x141],"Toby",4);
    size_t p=0x170;sdir(d,p,0x01,0x130);sdir(d,p,0x02,0x140);d[p]=0xFF;put_be24(d,p+1,0);
    put_be16(d,0x1a0,0);p=0x1b0;sdir(d,p,0x01,0x100);sdir(d,p,0x02,0x110);sdir(d,p,0x04,0x170);sdir(d,p,0x05,0x1a0);d[p]=0xFF;put_be24(d,p+1,0);
    d[0x200]=0x03;d[0x201]=0x01;d[0x202]=0x01;d[0x203]=0x01;
    const char* dnm="Display_Video_Apple_Toby";d[0x210]=uint8_t(std::strlen(dnm));std::memcpy(&d[0x211],dnm,std::strlen(dnm));
    put_be32v(d,0x240,0);put_be32v(d,0x248,0x1000);put_be32v(d,0x250,0);put_be32v(d,0x258,640u*480u);
    put_be16(d,0x260,0);put_be16(d,0x262,640);put_be16(d,0x264,480);put_be16(d,0x266,8);put_be32v(d,0x268,640);
    p=0x2a0;sdir(d,p,0x01,0x200);sdir(d,p,0x02,0x210);sdir(d,p,0x04,0x240);sdir(d,p,0x05,0x248);
    sdir(d,p,0x08,0x250);sdir(d,p,0x09,0x258);sdir(d,p,0x0B,0x260);d[p]=0xFF;put_be24(d,p+1,0);
    p=0x300;sdir(d,p,0x01,0x1b0);sdir(d,p,0x80,0x2a0);d[p]=0xFF;put_be24(d,p+1,0);
    d[ns-1]=0x0F;d[ns-2]=0x01;d[ns-9]=0xA5;d[ns-10]=0x5A;
    put_be24(d,ns-13,uint32_t((int32_t(0x300)-int32_t(ns-13))&0xffffff));
    size_t len=(ns-5)-0x100;put_be24(d,ns-8,uint32_t(len));put_be24(d,ns-5,apple_crc24(d.data()+0x100,len));
}
void MacII::apply_glue_map(){switch(glue_>>6){case 0:bank_b_base_=0x00100000;break;case 1:bank_b_base_=0x00200000;break;case 2:bank_b_base_=0x00800000;break;case 3:bank_b_base_=0x02000000;break;}}
uint32_t MacII::ram_phys(uint32_t a) const{a&=0x3fffffffu;if(a<kRamSize)return a;
    if(bank_b_base_>=kRamSize&&a>=bank_b_base_&&a<bank_b_base_+0x400000u)return 0x400000u+(a-bank_b_base_);return a&(kRamSize-1);}
void MacII::on_exc(uint32_t vec,uint32_t pc){
    static int n=0;
    if(n++<16)
        std::fprintf(stderr,"macii: exc vec=%u PC=$%08x SP=$%08x D0=$%08x A0=$%08x A5=$%08x A6=$%08x A7=$%08x\n",
            vec,pc,cpu_.a[7].l,cpu_.d[0].l,cpu_.a[0].l,cpu_.a[5].l,cpu_.a[6].l,cpu_.a[7].l);
}
void MacII::init_clut(){for(int i=0;i<256;++i){uint8_t g=uint8_t(255-i);clut_[i]=0xff000000u|(uint32_t(g)<<16)|(uint32_t(g)<<8)|g;}clut_[0]=0xffffffffu;clut_[1]=0xff000000u;}
bool MacII::init(const std::string& rom_path,std::string* error){
    std::fprintf(stderr,"macii: init path=%s\n",rom_path.c_str());
    std::vector<uint8_t> blob; std::ifstream f(rom_path,std::ios::binary);
    if(!f){
        // try directory / Q650.ROM or macii.rom
        std::string candidates[] = {rom_path+"/Q650.ROM", rom_path+"/q650.rom", rom_path+"/macii.rom",
            rom_path+"/9779d2c4.rom", rom_path+"/ROM"};
        for(const auto& c: candidates){ f.open(c,std::ios::binary); if(f){std::fprintf(stderr,"macii: opened %s\n",c.c_str());break;} }
    }
    if(f){f.seekg(0,std::ios::end);auto sz=f.tellg();f.seekg(0);
        if(sz>0&&sz<=0x200000){blob.resize(size_t(sz));f.read(reinterpret_cast<char*>(blob.data()),sz);}}
    if(blob.empty()){if(error)*error="cannot open ROM from "+rom_path;return false;}
    rom_size_=std::min(blob.size(),size_t(kRomSizeMax));
    rom_.assign(kRomSizeMax,0xff);std::memcpy(rom_.data(),blob.data(),rom_size_);
    std::fprintf(stderr,"macii: loaded %u ver=$%04x\n",unsigned(rom_size_),(rom_[8]<<8)|rom_[9]);
    apply_basilisk_patches(); warnings_.emplace_back("SlotManager smNoBoard");
    reset(); return true;
}
void MacII::reset(){
    std::fill(ram_.begin(),ram_.end(),0);std::fill(vram_.begin(),vram_.end(),0);std::fill(scratch_.begin(),scratch_.end(),0);
    init_clut();build_nubus_decl();asc_.reset();scc_.reset();overlay_=true;via1_irq_=via2_irq_=false;scsi_irq_level_=false;
    via_acc_=0;glue_=0;bank_b_base_=0x00100000;nubus_irq_=0x3f;frame_count_=0;diag_n_=0;stm_stuck_=0;slot_next_=0;ppat_stuck_=0;
    scsi_acc_=0;aline_n_=0;scratch_acc_=0;reset_done_=false;asc_base_=0;heap_next_=0x10000;
    via1_.reset();via2_.reset();via2_.write_ca1(true);via2_.write_cb1(true);via2_.write_cb2(true);via1_.write_cb1(true);via1_.write_cb2(true);
    iwm_.reset();rtc_.reset();scsi_.reset();cpu_.set_address_mask(0xfffffffeu);cpu_.reset();
    if(cpu_.a[7].l<0x1000u||cpu_.a[7].l>=0x80000000u) cpu_.a[7].l=0x00010000u;
    if((cpu_.pc()&0xfff00000u)!=kRomBase&&(cpu_.pc()&0xfff00000u)!=kRomBaseAlt)
        cpu_.pc_.l=kRomBase+(cpu_.pc()&0xfffffu);
}
bool MacII::load_media(const std::string& path,std::string* error){
    if(scsi_.load_file(path,error)){std::fprintf(stderr,"macii: SCSI %u blocks\n",scsi_.blocks());return true;}
    if(error&&error->empty())*error="cannot load media";return false;}
void MacII::sync_scsi_irq(){const bool level=scsi_.irq();if(level!=scsi_irq_level_){scsi_irq_level_=level;via2_.write_cb2(!level);}}
void MacII::update_irqs(){cpu_.set_irq(2,via2_irq_?IrqLine::Assert:IrqLine::Clear);cpu_.set_irq(1,via1_irq_?IrqLine::Assert:IrqLine::Clear);}
void MacII::on_cpu_cycles(int cycles){
    via_acc_+=cycles;while(via_acc_>=20){via_acc_-=20;via1_.tick(1);via2_.tick(1);}
    {
        // The ROM's A-line/Toolbox trap dispatcher has (at least) two entry
        // points into the same shared body: one that first collapses our
        // 8-byte 68020-style exception frame (format word + PC + SR) down
        // to a 68000-style one via "move.l 2(a7),4(a7)" before touching the
        // stack, and a "raw" one (opening with "subq.l #2,a7") that skips
        // that step and assumes the frame is already 68000-shaped. On real
        // hardware the ROM picks between them based on a CPU-space MOVES
        // probe we can't emulate faithfully (see MOVES in m68000.cpp), and
        // for our frame layout it ends up installing the raw entry as
        // vector 10 — which then leaks the 2-byte format word on every
        // single trap dispatch, draining the stack to nothing after a few
        // thousand calls during Toolbox init. Whenever vector 10 points at
        // a bare "subq.l #2,a7" (opcode $558F), search a little way back
        // for the matching "move.l 2(a7),4(a7) / bra.b" wrapper and
        // redirect there instead — address-independent, so it applies
        // equally to any ROM revision with this dispatcher shape.
        if(classify(read_long_be(0x28))==MapKind::Rom){
            const uint32_t v10=read_long_be(0x28);
            if(read_word(v10)==0x558Fu){
                // v10 opens with "subq.l #2,a7" (2 bytes), then falls into the
                // shared dispatch body. Confirm a candidate wrapper genuinely
                // leads into that SAME shared body (not just a nearby,
                // unrelated occurrence of the move.l idiom) by comparing the
                // next couple of words after each prelude for an exact match.
                const uint16_t body0=read_word(v10+2), body1=read_word(v10+4);
                for(uint32_t back=8;back<=96;back+=2){
                    const uint32_t cand=v10-back;
                    if(read_word(cand)==0x2F6Fu && read_word(cand+2)==0x0002u && read_word(cand+4)==0x0004u
                       && read_word(cand+6)==body0 && read_word(cand+8)==body1){
                        write_long_be(0x28,cand);
                        break;
                    }
                }
            }
        }
        const uint32_t pcnow=cpu_.pc();
        const uint32_t off=pcnow&0xfffffu;
        if(aline_n_>=5){
            static uint32_t last_log_pc=0, post_n=0;
            if(post_n<40 && pcnow!=last_log_pc){
                last_log_pc=pcnow; ++post_n;
                std::fprintf(stderr,"macii: post-IG PC=$%08x SP=$%08x D0=$%08x A0=$%08x A1=$%08x A5=$%08x\n",
                    pcnow,cpu_.a[7].l,cpu_.d[0].l,cpu_.a[0].l,cpu_.a[1].l,cpu_.a[5].l);
            }
        }
        if(pcnow<0x1000u){
            static int low_n=0;
            if(low_n++<12)
                std::fprintf(stderr,"macii: LOW PC=$%08x SP=$%08x (vec10=$%08x)\n",
                    pcnow,cpu_.a[7].l,read_long_be(40));
        }
        if(off==0x2F94||off==0x2F96) cpu_.d[2].l=(cpu_.d[2].l&~0xffu)|0x05;
        if(off==0x2FA4||off==0x2FA6){
            int roff=rom_offset(cpu_.a[1].l);
            if(roff>=0&&uint32_t(roff)+0x28u<=rom_size_){
                uint32_t mask=rom_be32(uint32_t(roff)+0x20), need=rom_be32(uint32_t(roff)+0x24);
                cpu_.d[1].l=(cpu_.d[1].l&~mask)|need;
            }
        }
        if(off>=0xB9800u && off<=0xBA300u){
            // Assist STM diagnostic loops (Basilisk NOPs some of these) but do NOT
            // force a jump to InstallDrivers — let natural ROM path reach 0x1142.
            ++stm_stuck_;
            cpu_.d[2].l=0;
            cpu_.d[7].l|=0x008B0000u;
            if(stm_stuck_==1 && diag_n_<4){
                std::fprintf(stderr,"macii: in STM region PC=$%08x (natural)\n",cpu_.pc());
                ++diag_n_;
            }
            if(stm_stuck_>50000){
                // last-resort escape only after a very long spin
                cpu_.pc_.l=kRomBase+0x1142;
                std::fprintf(stderr,"macii: STM timeout -> InstallDrivers\n");
                stm_stuck_=0;
            }
        } else stm_stuck_=0;
        // Color QuickDraw's default-pattern/cursor list search (see the
        // comment in seed_exception_vectors) can still end up scanning a
        // very long or malformed chain if it's reached through some path
        // other than the one seeded there, or through stale register state
        // left over from earlier work. If the search's own compare loop
        // (cmp.l (a2),d3 / adda.w d0,a2 / dbeq d5,...) is still spinning
        // after far more iterations than any real pattern/cursor list
        // would ever contain, force the DBcc counter to run out so it
        // falls through to its normal "not found" path — the same kind of
        // bounded assist as the STM region above, just scoped to this one
        // search loop instead of resetting CPU registers wholesale.
        if(off==0x12E66u){
            if(++ppat_stuck_>4000) cpu_.d[5].l&=0xffff0000u;
        } else if(off!=0x12E68u && off!=0x12E6Au) ppat_stuck_=0;
        if(off>=0xB9F9Au && off<=0xB9FB6u){
            cpu_.d[0].l = 0;
            cpu_.pc_.l=kRomBase+0xB9FB2;
        }
        // fingerprint success bits when near $B98E0
        if(off>=0xB98E0u && off<=0xB9900u){
            cpu_.d[0].l = 0x2A;
            cpu_.d[7].l |= 0x004B0000u;
        }
        // Serial diagnostic char-out at $BA19x: force fallthrough after a few hits
        // (Basilisk never enters this path because init_scc is RTS'd and SCC is scratch)
        if(off>=0xBA180u && off<=0xBA1C0u){
            static int ser_n = 0;
            ++ser_n;
            cpu_.d[7].l |= 0x00800000u; // pretend success bit
            if(ser_n > 8){
                cpu_.pc_.l = kRomBase + 0xBA1C8; // past the tight out loops
                if(ser_n == 9)
                    std::fprintf(stderr,"macii: skip serial diag out -> $BA1C8\n");
            }
        }
        if(off>=0x70C0u && off<=0x7160u){
            if((cpu_.a[5].l&0xFFFF0000u)==0x50F00000u) cpu_.a[5].l=kAscPhys+(cpu_.a[5].l&0x1FFFu);
            if((cpu_.a[6].l&0xFF000000u)==0x50000000u) cpu_.a[6].l=kRomBase+0x7158;
        }
        if((cpu_.pc()&0xFF000000u)==0x50000000u){
            const uint32_t a6=cpu_.a[6].l;
            if((a6&0xFFF00000u)==kRomBase||(a6&0xFFF00000u)==kRomBaseAlt) cpu_.pc_.l=a6;
            else cpu_.pc_.l=kRomBase+0xBA;
        }
    }
    sync_scsi_irq();
}
void MacII::write_long_be(uint32_t a,uint32_t v){write_byte(a,uint8_t(v>>24));write_byte(a+1,uint8_t(v>>16));write_byte(a+2,uint8_t(v>>8));write_byte(a+3,uint8_t(v));}
int MacII::rom_offset(uint32_t a) const{
    if(a>=kRomBase&&a<kRomBase+rom_size_)return int(a-kRomBase);
    if(a>=kRomBaseAlt&&a<kRomBaseAlt+rom_size_)return int(a-kRomBaseAlt);
    if(a>=kRomBase&&a<kRomBase+0x200000u)return int((a-kRomBase)%rom_size_);
    return -1;
}
MacII::MapKind MacII::classify(uint32_t address) const{
    address=mac_norm(address);
    if(address<0x40000000u)return overlay_?MapKind::Rom:MapKind::Ram;
    if(rom_offset(address)>=0)return MapKind::Rom;
    if((address&0xff000000u)==0x40000000u)return MapKind::Unmapped;
    if(address>=kScratchBase&&address<kScratchBase+kScratchSize) return MapKind::Scratch;
    if((address&0xF0000000u)==0xF0000000u){uint8_t slot=uint8_t((address>>24)&0x0f);
        if(slot>=9&&slot<=0x0e){const uint32_t off=address&0x00FFFFFFu;
            if(off>=0x00F00000u)return MapKind::NubusDecl;if(off<0x00080000u)return MapKind::NubusFb;return MapKind::IoStub;}
        return MapKind::Unmapped;}
    if((address&0xff000000u)==0x50000000u){
        const uint32_t off=(address&~0x00f00000u)&0x000fffffu;
        if(off<0x2000)return MapKind::Via1; if(off<0x4000)return MapKind::Via2;
        if(off>=0x4000&&off<0x6000) return MapKind::Scc;
        if(off>=0x6000&&off<0x8000)return MapKind::ScsiDrq;
        if(off>=0x10000&&off<0x12000)return MapKind::Scsi;
        if(off>=0x12000&&off<0x14000)return MapKind::ScsiDrq;
        if(off>=0x14000&&off<0x16000)return MapKind::Asc;
        if(off>=0x16000&&off<0x18000)return MapKind::Iwm;
        if(off>=0x40000&&off<0x42000)return MapKind::Via1;
        return MapKind::IoStub;}
    if(address>=0x580000&&address<0x600000)return MapKind::Scsi;
    return MapKind::Unmapped;
}
uint32_t MacII::scsi_addr(uint32_t a,bool force_dack) const{a=mac_norm(a);
    if(a>=0x580000&&a<0x600000)return a|(force_dack?0x200u:0);
    uint32_t off=(a&~0x00f00000u)&0x000fffffu;
    bool dack=force_dack||(off>=0x6000&&off<0x8000)||(off>=0x12000&&off<0x14000)||((off&0x200)!=0);
    return 0x580000u|(uint32_t((off>>4)&7)<<4)|(dack?0x200u:0);}
uint8_t MacII::read_byte(uint32_t address){
    address=mac_norm(address);MapKind k=classify(address);
    switch(k){
        case MapKind::Ram:return ram_at(ram_phys(address));
        case MapKind::Rom:{if(address<0x40000000u)return rom_[address%rom_size_];int off=rom_offset(address);return off>=0?rom_[size_t(off)%rom_size_]:0xff;}
        case MapKind::Via1:return via1_.read(vreg(address));case MapKind::Via2:return via2_.read(vreg(address));
        case MapKind::Scratch:{++scratch_acc_;if((address&1)==0)return 0x05;
            uint32_t off=(address>=kScratchBase)?(address-kScratchBase):(address&0xffffu);
            return scratch_[off&(kScratchSize-1)];}
        case MapKind::IoStub:return 0;
        case MapKind::Scsi:case MapKind::ScsiDrq:{++scsi_acc_;if(scsi_acc_<=6)std::fprintf(stderr,"macii: SCSI R $%08x\n",address);
            uint8_t v=scsi_.read(scsi_addr(address,k==MapKind::ScsiDrq));sync_scsi_irq();return v;}
        case MapKind::Asc:return asc_.read((address&~0x00f00000u)&0x1fffu);case MapKind::Iwm:return iwm_.read(vreg(address));
        case MapKind::Scc:return scc_.read(((address&~0x00f00000u)&0x000fffffu)-0x4000u);
        case MapKind::NubusFb:{uint32_t off=address&0xfffffu;return off<kVramSize?vram_[off]:0;}
        case MapKind::NubusDecl:{uint32_t off=address&0xffffu;return off<nubus_decl_.size()?nubus_decl_[off]:0xff;}
        default:return 0xff;}}
void MacII::write_byte(uint32_t address,uint8_t value){
    address=mac_norm(address);MapKind k=classify(address);
    switch(k){
        case MapKind::Ram:ram_at(ram_phys(address),value);return;
        case MapKind::Via1:via1_.write(vreg(address),value);return;case MapKind::Via2:via2_.write(vreg(address),value);return;
        case MapKind::Scratch:{++scratch_acc_;uint32_t off=(address>=kScratchBase)?(address-kScratchBase):(address&0xffffu);
            scratch_[off&(kScratchSize-1)]=value;return;}
        case MapKind::Scsi:case MapKind::ScsiDrq:{++scsi_acc_;if(scsi_acc_<=6)std::fprintf(stderr,"macii: SCSI W $%08x=$%02x\n",address,value);
            scsi_.write(scsi_addr(address,k==MapKind::ScsiDrq),value);sync_scsi_irq();return;}
        case MapKind::Asc:asc_.write((address&~0x00f00000u)&0x1fffu,value);return;case MapKind::Iwm:iwm_.write(vreg(address),value);return;
        case MapKind::Scc:scc_.write(((address&~0x00f00000u)&0x000fffffu)-0x4000u,value);return;
        case MapKind::NubusFb:{uint32_t off=address&0xfffffu;if(off<kVramSize)vram_[off]=value;return;}
        default:return;}}
uint16_t MacII::read_word(uint32_t address){
    address=mac_norm(address);MapKind k=classify(address);if(k==MapKind::Unmapped)return 0xffff;
    if(k==MapKind::Rom){if(address<0x40000000u){uint32_t o=address%rom_size_;return uint16_t((uint16_t(rom_[o])<<8)|rom_[(o+1)%rom_size_]);}
        int off=rom_offset(address);if(off<0)return 0xffff;return uint16_t((uint16_t(rom_[size_t(off)%rom_size_])<<8)|rom_[size_t(off+1)%rom_size_]);}
    if(k==MapKind::Ram)return uint16_t((uint16_t(ram_at(ram_phys(address)))<<8)|ram_at(ram_phys(address+1)));
    uint8_t v=read_byte(address);return uint16_t((uint16_t(v)<<8)|v);}
void MacII::write_word(uint32_t address,uint16_t value){
    address=mac_norm(address);MapKind k=classify(address);if(k==MapKind::Unmapped||k==MapKind::Rom)return;
    if(k==MapKind::Ram){ram_at(ram_phys(address),uint8_t(value>>8));ram_at(ram_phys(address+1),uint8_t(value));return;}
    write_byte(address,uint8_t(value>>8));}
uint8_t MacII::via1_pa_r(){return 0x81;}
void MacII::via1_pa_w(uint8_t value){const bool want=(value&0x10)!=0;if(want!=overlay_){overlay_=want;if(!overlay_){via2_pa_w(0x3f);apply_glue_map();
    if(cpu_.a[7].l>=kRamSize||cpu_.a[7].l<0x400u)cpu_.a[7].l=0x00010000u;}}}
uint8_t MacII::via1_pb_r(){uint8_t v=0x08;if(rtc_data_)v|=0x01;return v;}
void MacII::via1_pb_w(uint8_t value){rtc_data_=(value&1)!=0;}
uint8_t MacII::via2_pa_r(){return uint8_t((glue_&0xc0)|(nubus_irq_&0x3f));}
void MacII::via2_pa_w(uint8_t value){uint8_t ng=value&0xc0;if(ng!=glue_){glue_=ng;if(!overlay_)apply_glue_map();}else glue_=ng;}
uint8_t MacII::via2_pb_r(){return 0xcf;}
void MacII::via2_pb_w(uint8_t value){via1_.write_ca1((value&0x80)!=0);}
void MacII::render(){uint32_t* out=framebuffer_.data();const uint8_t* src=vram_.data();bool any=false;
    for(uint32_t i=0;i<kVramSize;i+=64)if(src[i]){any=true;break;}
    if(!any){for(int i=0;i<kWidth*kHeight;++i){uint8_t g=uint8_t((i/kWidth)<40?200:40);out[i]=0xff000000u|(uint32_t(g)<<16)|(uint32_t(g)<<8)|g;}return;}
    for(int i=0;i<kWidth*kHeight;++i)out[i]=clut_[src[i]];}
void MacII::run_frame(){
    for(int line=0;line<kVTotal;line++){via1_.write_ca1(line<kVBlankLines);via2_.write_ca1(true);cpu_.run(kCyclesPerLine);audio_.push_back(0);}
    last_pc_=cpu_.pc();++frame_count_;
    if(frame_count_==1||frame_count_==5||frame_count_==10||frame_count_==30||frame_count_==60||
       frame_count_==120||frame_count_==240||frame_count_==480||frame_count_==1000)
        std::fprintf(stderr,"macii: fr=%d PC=$%08x D0=$%08x scsi=%u aline=%u\n",
            frame_count_,last_pc_,cpu_.d[0].l,scsi_acc_,aline_n_);
    render();}
void MacII::set_inputs(const MachineInputs&){}
void MacII::drain_audio(std::vector<int16_t>& out){out.insert(out.end(),audio_.begin(),audio_.end());audio_.clear();}
}
