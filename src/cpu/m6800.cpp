#include "cpu/m6800.h"
namespace dsp {
void M6800::reset() {
    a=b=0; x=0; sp=0xffff; cc=0xd0;
    pc_=uint16_t((read(0xfffe)<<8)|read(0xffff));
    wait_=false; nmi_prev_=IrqLine::Clear;
}
int M6800::do_irq() {
    if (nmi_state_==IrqLine::Assert && nmi_prev_!=IrqLine::Assert) {
        nmi_prev_=nmi_state_; wait_=false;
        push_w(pc_); push_w(x); push(a); push(b); push(cc); cc|=kI;
        pc_=uint16_t((read(0xfffc)<<8)|read(0xfffd)); return 12;
    }
    nmi_prev_=nmi_state_;
    if (irq_state_==IrqLine::Assert && !(cc&kI)) {
        wait_=false; push_w(pc_); push_w(x); push(a); push(b); push(cc); cc|=kI;
        pc_=uint16_t((read(0xfff8)<<8)|read(0xfff9)); return 12;
    }
    return 0;
}
int M6800::run(int cycles) {
    int ex=0;
    while (ex<cycles) {
        int c=do_irq();
        if (!c) c = wait_ ? 1 : exec_one();
        if (c<=0) c=1;
        ex+=c;
        if (cycle_handler_) cycle_handler_(c);
    }
    return ex;
}
int M6800::exec_one() {
    const uint8_t op=fetch();
    auto setnz=[&](uint8_t v){ set_nz(v); cc&=~kV; };
    auto sub=[&](uint8_t& r,uint8_t v){
        int16_t t=int16_t(r)-v; uint8_t r8=uint8_t(t);
        cc=uint8_t((cc&~(kN|kZ|kV|kC))|(r8&0x80?kN:0)|(r8==0?kZ:0)|(((r^v)&(r^r8)&0x80)?kV:0)|(t<0?kC:0));
        r=r8;
    };
    auto add=[&](uint8_t& r,uint8_t v){
        uint16_t t=uint16_t(r)+v; uint8_t r8=uint8_t(t);
        cc=uint8_t((cc&~(kN|kZ|kV|kC))|(r8&0x80?kN:0)|(r8==0?kZ:0)|(((r^r8)&(v^r8)&0x80)?kV:0)|(t>0xff?kC:0));
        r=r8;
    };
    auto cmp=[&](uint8_t r,uint8_t v){ uint8_t t=r; sub(t,v); };
    switch(op){
    case 0x01: return 2;
    case 0x0c: cc&=~kC; return 2; case 0x0d: cc|=kC; return 2;
    case 0x0e: cc&=~kI; return 2; case 0x0f: cc|=kI; return 2;
    case 0x16: b=a; setnz(b); return 2; case 0x17: a=b; setnz(a); return 2;
    case 0x20: {int8_t r=int8_t(fetch()); pc_=uint16_t(pc_+r); return 4;}
    case 0x24: {int8_t r=int8_t(fetch()); if(!(cc&kC)) pc_=uint16_t(pc_+r); return 4;}
    case 0x25: {int8_t r=int8_t(fetch()); if(cc&kC) pc_=uint16_t(pc_+r); return 4;}
    case 0x26: {int8_t r=int8_t(fetch()); if(!(cc&kZ)) pc_=uint16_t(pc_+r); return 4;}
    case 0x27: {int8_t r=int8_t(fetch()); if(cc&kZ) pc_=uint16_t(pc_+r); return 4;}
    case 0x2a: {int8_t r=int8_t(fetch()); if(!(cc&kN)) pc_=uint16_t(pc_+r); return 4;}
    case 0x2b: {int8_t r=int8_t(fetch()); if(cc&kN) pc_=uint16_t(pc_+r); return 4;}
    case 0x32: a=pop(); setnz(a); return 4; case 0x33: b=pop(); setnz(b); return 4;
    case 0x36: push(a); return 4; case 0x37: push(b); return 4;
    case 0x39: pc_=pop_w(); return 5;
    case 0x3b: cc=pop(); b=pop(); a=pop(); x=pop_w(); pc_=pop_w(); return 10;
    case 0x3e: wait_=true; return 9;
    case 0x4a: a=uint8_t(a-1); set_nz(a); return 2; case 0x4c: a=uint8_t(a+1); set_nz(a); return 2;
    case 0x4d: set_nz(a); cc&=~kV; return 2; case 0x4f: a=0; setnz(a); return 2;
    case 0x5a: b=uint8_t(b-1); set_nz(b); return 2; case 0x5c: b=uint8_t(b+1); set_nz(b); return 2;
    case 0x5f: b=0; setnz(b); return 2;
    case 0x6e: pc_=uint16_t(x+fetch()); return 4;
    case 0x7e: pc_=fetch_word(); return 3;
    case 0x80: sub(a,fetch()); return 2; case 0x81: cmp(a,fetch()); return 2;
    case 0x84: a&=fetch(); setnz(a); return 2; case 0x86: a=fetch(); setnz(a); return 2;
    case 0x8a: a|=fetch(); setnz(a); return 2; case 0x8b: add(a,fetch()); return 2;
    case 0x8d: {int8_t r=int8_t(fetch()); push_w(pc_); pc_=uint16_t(pc_+r); return 8;}
    case 0x8e: sp=fetch_word(); return 3;
    case 0x96: a=read(fetch()); setnz(a); return 3;
    case 0x97: {uint16_t ad=fetch(); write(ad,a); setnz(a); return 4;}
    case 0xa6: a=read(uint16_t(x+fetch())); setnz(a); return 5;
    case 0xa7: {uint16_t ad=uint16_t(x+fetch()); write(ad,a); setnz(a); return 6;}
    case 0xad: {uint16_t ad=uint16_t(x+fetch()); push_w(pc_); pc_=ad; return 8;}
    case 0xb6: a=read(fetch_word()); setnz(a); return 4;
    case 0xb7: {uint16_t ad=fetch_word(); write(ad,a); setnz(a); return 5;}
    case 0xbd: {uint16_t ad=fetch_word(); push_w(pc_); pc_=ad; return 9;}
    case 0xc0: sub(b,fetch()); return 2; case 0xc1: cmp(b,fetch()); return 2;
    case 0xc6: b=fetch(); setnz(b); return 2; case 0xca: b|=fetch(); setnz(b); return 2;
    case 0xcb: add(b,fetch()); return 2; case 0xce: x=fetch_word(); set_nz(uint8_t(x>>8)); if(!x)cc|=kZ; return 3;
    case 0xd6: b=read(fetch()); setnz(b); return 3;
    case 0xd7: {uint16_t ad=fetch(); write(ad,b); setnz(b); return 4;}
    case 0xe6: b=read(uint16_t(x+fetch())); setnz(b); return 5;
    case 0xe7: {uint16_t ad=uint16_t(x+fetch()); write(ad,b); setnz(b); return 6;}
    case 0xf6: b=read(fetch_word()); setnz(b); return 4;
    case 0xf7: {uint16_t ad=fetch_word(); write(ad,b); setnz(b); return 5;}
    case 0xfe: {uint16_t ad=fetch_word(); x=uint16_t((read(ad)<<8)|read(uint16_t(ad+1))); return 5;}
    default: return 2;
    }
}
}
