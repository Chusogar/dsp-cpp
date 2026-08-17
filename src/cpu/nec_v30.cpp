#include "cpu/nec_v30.h"

#include <array>

namespace dsp {
namespace {

const std::array<uint8_t, 256> kParity = [] {
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; i++) {
        int bits = 0;
        for (int j = i; j != 0; j >>= 1) bits += j & 1;
        table[size_t(i)] = uint8_t((bits & 1) == 0);
    }
    return table;
}();

}  // namespace

NecV30::NecV30(uint32_t clock, Type type) : clock_(clock), type_(type) {
    if (type_ == Type::V20) {
        prefetch_size_ = 4;
        prefetch_cycles_ = 4;
    } else if (type_ == Type::V33) {
        prefetch_size_ = 6;
        prefetch_cycles_ = 1;
    } else {
        prefetch_size_ = 6;
        prefetch_cycles_ = 2;
    }
}

void NecV30::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void NecV30::set_io_handlers(In8Handler in, Out8Handler out) {
    in8_ = std::move(in);
    out8_ = std::move(out);
}

void NecV30::set_io16_handlers(In16Handler in, Out16Handler out) {
    in16_ = std::move(in);
    out16_ = std::move(out);
}

void NecV30::reset() {
    aw_ = cw_ = dw_ = bw_ = 0;
    sp_ = bp_ = ix_ = iy_ = 0;
    ip_ = 0;
    tf_ = iff_ = df_ = false;
    mf_ = true;
    sign_val_ = 0;
    aux_val_ = 0;
    over_val_ = 0;
    zero_val_ = 1;
    carry_val_ = 0;
    parity_val_ = 1;
    irq_vector_ = 0xff;
    irq_pending_ = 0;
    nmi_state_ = IrqLine::Clear;
    prefetch_reset_ = true;
    prefetch_count_ = 0;
    ps_ = 0xffff;
    ds1_ = ds0_ = ss_ = 0;
    ea_ = 0;
    eo_ = 0;
    seg_prefix_ = false;
    no_interrupt_ = false;
    halted_ = false;
}

uint16_t NecV30::read_word(uint32_t address) {
    uint8_t lo = read_byte(address);
    return uint16_t(lo | (uint16_t(read_byte(address + 1)) << 8));
}

void NecV30::write_word(uint32_t address, uint16_t value) {
    write_byte(address, uint8_t(value));
    write_byte(address + 1, uint8_t(value >> 8));
}

uint8_t NecV30::in_byte(uint16_t port) {
    if (in8_) return in8_(port);
    if (in16_) {
        uint16_t word = in16_(port & 0xfffe);
        return (port & 1) ? uint8_t(word >> 8) : uint8_t(word);
    }
    return 0xff;
}

void NecV30::out_byte(uint16_t port, uint8_t value) {
    if (out8_) {
        out8_(port, value);
        return;
    }
    if (out16_) {
        uint16_t word = in16_ ? in16_(port & 0xfffe) : 0;
        if (port & 1) word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8));
        else word = uint16_t((word & 0xff00) | value);
        out16_(port & 0xfffe, word);
    }
}

uint16_t NecV30::in_word(uint16_t port) {
    if (in16_) return in16_(port);
    return uint16_t(in_byte(port) | (uint16_t(in_byte(uint16_t(port + 1))) << 8));
}

void NecV30::out_word(uint16_t port, uint16_t value) {
    if (out16_) {
        out16_(port, value);
        return;
    }
    out_byte(port, uint8_t(value));
    out_byte(uint16_t(port + 1), uint8_t(value >> 8));
}

uint8_t NecV30::fetch() {
    prefetch_count_--;
    uint8_t value = read_byte((uint32_t(ps_) << 4) + ip_);
    ip_++;
    return value;
}

uint16_t NecV30::fetch_word() { return uint16_t(fetch() | (uint16_t(fetch()) << 8)); }

uint32_t NecV30::default_base(int seg) const {
    if (seg_prefix_ && (seg == kDs0 || seg == kSs)) return prefix_base_;
    switch (seg) {
        case kDs1: return uint32_t(ds1_) << 4;
        case kPs: return uint32_t(ps_) << 4;
        case kSs: return uint32_t(ss_) << 4;
        default: return uint32_t(ds0_) << 4;
    }
}

void NecV30::get_ea(uint8_t modrm) {
    uint8_t rm = modrm & 7;
    uint8_t mod = uint8_t(modrm >> 6);
    uint16_t disp = 0;
    if (mod == 1) {
        disp = uint16_t(int16_t(int8_t(fetch())));
    } else if (mod == 2) {
        disp = fetch_word();
    } else if (mod == 0 && rm == 6) {
        eo_ = fetch_word();
        ea_ = default_base(kDs0) + eo_;
        return;
    }

    uint16_t base = 0;
    bool use_ss = false;
    switch (rm) {
        case 0: base = uint16_t(bw_ + ix_); break;
        case 1: base = uint16_t(bw_ + iy_); break;
        case 2:
            base = uint16_t(bp_ + ix_);
            use_ss = true;
            break;
        case 3:
            base = uint16_t(bp_ + iy_);
            use_ss = true;
            break;
        case 4: base = ix_; break;
        case 5: base = iy_; break;
        case 6:
            base = bp_;
            use_ss = true;
            break;
        case 7: base = bw_; break;
    }
    eo_ = uint16_t(base + disp);
    ea_ = default_base(use_ss ? kSs : kDs0) + eo_;
}

uint8_t NecV30::get_breg(int index) const {
    const uint16_t* words[4] = {&aw_, &cw_, &dw_, &bw_};
    const uint16_t& w = *words[index & 3];
    return (index & 4) ? uint8_t(w >> 8) : uint8_t(w);
}

void NecV30::set_breg(int index, uint8_t value) {
    uint16_t* words[4] = {&aw_, &cw_, &dw_, &bw_};
    uint16_t& w = *words[index & 3];
    if (index & 4) w = uint16_t((w & 0x00ff) | (uint16_t(value) << 8));
    else w = uint16_t((w & 0xff00) | value);
}

uint16_t NecV30::get_wreg(int index) const {
    const uint16_t* words[8] = {&aw_, &cw_, &dw_, &bw_, &sp_, &bp_, &ix_, &iy_};
    return *words[index & 7];
}

void NecV30::set_wreg(int index, uint16_t value) {
    uint16_t* words[8] = {&aw_, &cw_, &dw_, &bw_, &sp_, &bp_, &ix_, &iy_};
    *words[index & 7] = value;
}

uint8_t NecV30::get_rm_byte(uint8_t modrm) {
    if (modrm >= 0xc0) return get_breg(modrm & 7);
    get_ea(modrm);
    return read_byte(ea_);
}

uint16_t NecV30::get_rm_word(uint8_t modrm) {
    if (modrm >= 0xc0) return get_wreg(modrm & 7);
    get_ea(modrm);
    return read_word(ea_);
}

void NecV30::put_rm_byte(uint8_t modrm, uint8_t value) {
    if (modrm >= 0xc0) set_breg(modrm & 7, value);
    else {
        get_ea(modrm);
        write_byte(ea_, value);
    }
}

void NecV30::put_rm_word(uint8_t modrm, uint16_t value) {
    if (modrm >= 0xc0) set_wreg(modrm & 7, value);
    else {
        get_ea(modrm);
        write_word(ea_, value);
    }
}

void NecV30::putback_rm_byte(uint8_t modrm, uint8_t value) {
    if (modrm >= 0xc0) set_breg(modrm & 7, value);
    else write_byte(ea_, value);
}

void NecV30::putback_rm_word(uint8_t modrm, uint16_t value) {
    if (modrm >= 0xc0) set_wreg(modrm & 7, value);
    else write_word(ea_, value);
}

void NecV30::put_imm_rm_byte(uint8_t modrm) {
    if (modrm >= 0xc0) set_breg(modrm & 7, fetch());
    else {
        get_ea(modrm);
        write_byte(ea_, fetch());
    }
}

void NecV30::put_imm_rm_word(uint8_t modrm) {
    if (modrm >= 0xc0) set_wreg(modrm & 7, fetch_word());
    else {
        get_ea(modrm);
        write_word(ea_, fetch_word());
    }
}

void NecV30::clks(int v20, int v30, int v33) {
    clk(type_ == Type::V20 ? v20 : (type_ == Type::V33 ? v33 : v30));
}

void NecV30::clkw(int v20o, int v30o, int v33o, int v20e, int v30e, int v33e, uint16_t addr) {
    bool odd = (addr & 1) != 0;
    if (type_ == Type::V20) clk(odd ? v20o : v20e);
    else if (type_ == Type::V33) clk(odd ? v33o : v33e);
    else clk(odd ? v30o : v30e);
}

void NecV30::clkm(int v20, int v30, int v33, int v20m, int v30m, int v33m, uint8_t modrm) {
    if (modrm >= 0xc0) clks(v20, v30, v33);
    else clks(v20m, v30m, v33m);
}

void NecV30::clkr(int v20o, int v30o, int v33o, int v20e, int v30e, int v33e, int vall,
                  uint8_t modrm) {
    if (modrm >= 0xc0) clk(vall);
    else clkw(v20o, v30o, v33o, v20e, v30e, v33e, uint16_t(ea_));
}

void NecV30::do_prefetch(int previous_icount) {
    int diff = icount_ - previous_icount;
    while (prefetch_count_ < 0) {
        prefetch_count_++;
        if (diff > prefetch_cycles_) diff -= prefetch_cycles_;
        else icount_ -= prefetch_cycles_;
    }
    if (prefetch_reset_) {
        prefetch_count_ = 0;
        prefetch_reset_ = false;
        return;
    }
    while (diff >= prefetch_cycles_ && prefetch_count_ < prefetch_size_) {
        diff -= prefetch_cycles_;
        prefetch_count_++;
    }
}

void NecV30::set_szpf_byte(uint32_t x) {
    uint8_t v = uint8_t(x);
    sign_val_ = int8_t(v);
    zero_val_ = v;
    parity_val_ = kParity[v];
}

void NecV30::set_szpf_word(uint32_t x) {
    uint16_t v = uint16_t(x);
    sign_val_ = int16_t(v);
    zero_val_ = v;
    parity_val_ = kParity[v & 0xff];
}

uint16_t NecV30::compress_flags() const {
    uint16_t flags = 0x0002;
    if (cf()) flags |= 0x0001;
    if (pf()) flags |= 0x0004;
    if (af()) flags |= 0x0010;
    if (zf()) flags |= 0x0040;
    if (sf()) flags |= 0x0080;
    if (tf_) flags |= 0x0100;
    if (iff_) flags |= 0x0200;
    if (df_) flags |= 0x0400;
    if (of()) flags |= 0x0800;
    if (mf_) flags |= 0x8000;
    return flags;
}

void NecV30::expand_flags(uint16_t value) {
    carry_val_ = value & 1;
    parity_val_ = (value & 4) ? 1 : 0;
    aux_val_ = value & 0x10;
    zero_val_ = (value & 0x40) ? 0 : 1;
    sign_val_ = (value & 0x80) ? -1 : 0;
    tf_ = (value & 0x100) != 0;
    iff_ = (value & 0x200) != 0;
    df_ = (value & 0x400) != 0;
    over_val_ = value & 0x800;
    mf_ = (value & 0x8000) != 0;
}

uint8_t NecV30::add_b(uint32_t dst, uint32_t src) {
    uint32_t res = dst + src;
    carry_val_ = res & 0x100;
    over_val_ = (res ^ src) & (res ^ dst) & 0x80;
    aux_val_ = (res ^ (src ^ dst)) & 0x10;
    set_szpf_byte(res);
    return uint8_t(res);
}

uint16_t NecV30::add_w(uint32_t dst, uint32_t src) {
    uint32_t res = dst + src;
    carry_val_ = res & 0x10000;
    over_val_ = (res ^ src) & (res ^ dst) & 0x8000;
    aux_val_ = (res ^ (src ^ dst)) & 0x10;
    set_szpf_word(res);
    return uint16_t(res);
}

uint8_t NecV30::sub_b(uint32_t dst, uint32_t src) {
    uint32_t res = dst - src;
    carry_val_ = res & 0x100;
    over_val_ = (dst ^ src) & (dst ^ res) & 0x80;
    aux_val_ = (res ^ (src ^ dst)) & 0x10;
    set_szpf_byte(res);
    return uint8_t(res);
}

uint16_t NecV30::sub_w(uint32_t dst, uint32_t src) {
    uint32_t res = dst - src;
    carry_val_ = res & 0x10000;
    over_val_ = (dst ^ src) & (dst ^ res) & 0x8000;
    aux_val_ = (res ^ (src ^ dst)) & 0x10;
    set_szpf_word(res);
    return uint16_t(res);
}

uint8_t NecV30::and_b(uint32_t dst, uint32_t src) {
    uint8_t res = uint8_t(dst & src);
    carry_val_ = over_val_ = aux_val_ = 0;
    set_szpf_byte(res);
    return res;
}

uint16_t NecV30::and_w(uint32_t dst, uint32_t src) {
    uint16_t res = uint16_t(dst & src);
    carry_val_ = over_val_ = aux_val_ = 0;
    set_szpf_word(res);
    return res;
}

uint8_t NecV30::or_b(uint32_t dst, uint32_t src) {
    uint8_t res = uint8_t(dst | src);
    carry_val_ = over_val_ = aux_val_ = 0;
    set_szpf_byte(res);
    return res;
}

uint16_t NecV30::or_w(uint32_t dst, uint32_t src) {
    uint16_t res = uint16_t(dst | src);
    carry_val_ = over_val_ = aux_val_ = 0;
    set_szpf_word(res);
    return res;
}

uint8_t NecV30::xor_b(uint32_t dst, uint32_t src) {
    uint8_t res = uint8_t(dst ^ src);
    carry_val_ = over_val_ = aux_val_ = 0;
    set_szpf_byte(res);
    return res;
}

uint16_t NecV30::xor_w(uint32_t dst, uint32_t src) {
    uint16_t res = uint16_t(dst ^ src);
    carry_val_ = over_val_ = aux_val_ = 0;
    set_szpf_word(res);
    return res;
}

uint8_t NecV30::alu_b(uint8_t op, uint32_t dst, uint32_t src) {
    switch (op) {
        case 0: return add_b(dst, src);
        case 1: return or_b(dst, src);
        case 2: return add_b(dst, src + (cf() ? 1 : 0));
        case 3: return sub_b(dst, src + (cf() ? 1 : 0));
        case 4: return and_b(dst, src);
        case 5: return sub_b(dst, src);
        case 6: return xor_b(dst, src);
        default: return sub_b(dst, src);  // CMP
    }
}

uint16_t NecV30::alu_w(uint8_t op, uint32_t dst, uint32_t src) {
    switch (op) {
        case 0: return add_w(dst, src);
        case 1: return or_w(dst, src);
        case 2: return add_w(dst, src + (cf() ? 1 : 0));
        case 3: return sub_w(dst, src + (cf() ? 1 : 0));
        case 4: return and_w(dst, src);
        case 5: return sub_w(dst, src);
        case 6: return xor_w(dst, src);
        default: return sub_w(dst, src);
    }
}

uint8_t NecV30::rol_b(uint8_t dst) {
    carry_val_ = dst & 0x80;
    return uint8_t((dst << 1) + (cf() ? 1 : 0));
}

uint16_t NecV30::rol_w(uint16_t dst) {
    carry_val_ = dst & 0x8000;
    return uint16_t((dst << 1) + (cf() ? 1 : 0));
}

uint8_t NecV30::ror_b(uint8_t dst) {
    carry_val_ = dst & 1;
    return uint8_t((dst >> 1) + (cf() ? 0x80 : 0));
}

uint16_t NecV30::ror_w(uint16_t dst) {
    carry_val_ = dst & 1;
    return uint16_t((dst >> 1) + (cf() ? 0x8000 : 0));
}

uint8_t NecV30::rolc_b(uint8_t dst) {
    uint32_t temp = (uint32_t(dst) << 1) + (cf() ? 1 : 0);
    carry_val_ = temp & 0x100;
    return uint8_t(temp);
}

uint16_t NecV30::rolc_w(uint16_t dst) {
    uint32_t temp = (uint32_t(dst) << 1) + (cf() ? 1 : 0);
    carry_val_ = temp & 0x10000;
    return uint16_t(temp);
}

uint8_t NecV30::rorc_b(uint8_t dst) {
    uint32_t temp = dst + (cf() ? 0x100u : 0);
    carry_val_ = temp & 1;
    return uint8_t(temp >> 1);
}

uint16_t NecV30::rorc_w(uint16_t dst) {
    uint32_t temp = dst + (cf() ? 0x10000u : 0);
    carry_val_ = temp & 1;
    return uint16_t(temp >> 1);
}

uint8_t NecV30::shl_b(uint8_t dst, uint8_t c) {
    clk(c);
    uint32_t res = uint32_t(dst) << c;
    carry_val_ = res & 0x100;
    set_szpf_byte(res);
    return uint8_t(res);
}

uint16_t NecV30::shl_w(uint16_t dst, uint8_t c) {
    clk(c);
    uint32_t res = uint32_t(dst) << c;
    carry_val_ = res & 0x10000;
    set_szpf_word(res);
    return uint16_t(res);
}

uint8_t NecV30::shr_b(uint8_t dst, uint8_t c) {
    clk(c);
    dst = uint8_t(dst >> (c - 1));
    carry_val_ = dst & 1;
    dst = uint8_t(dst >> 1);
    set_szpf_byte(dst);
    return dst;
}

uint16_t NecV30::shr_w(uint16_t dst, uint8_t c) {
    clk(c);
    dst = uint16_t(dst >> (c - 1));
    carry_val_ = dst & 1;
    dst = uint16_t(dst >> 1);
    set_szpf_word(dst);
    return dst;
}

uint8_t NecV30::shra_b(uint8_t dst, uint8_t c) {
    clk(c);
    int8_t temp = int8_t(dst);
    temp = int8_t(temp >> (c - 1));
    carry_val_ = temp & 1;
    temp = int8_t(temp >> 1);
    set_szpf_byte(uint8_t(temp));
    return uint8_t(temp);
}

uint16_t NecV30::shra_w(uint16_t dst, uint8_t c) {
    clk(c);
    int16_t temp = int16_t(dst);
    temp = int16_t(temp >> (c - 1));
    carry_val_ = temp & 1;
    temp = int16_t(temp >> 1);
    set_szpf_word(uint16_t(temp));
    return uint16_t(temp);
}

void NecV30::push(uint16_t value) {
    sp_ = uint16_t(sp_ - 2);
    write_word((uint32_t(ss_) << 4) + sp_, value);
}

uint16_t NecV30::pop() {
    uint16_t value = read_word((uint32_t(ss_) << 4) + sp_);
    sp_ = uint16_t(sp_ + 2);
    return value;
}

void NecV30::i_pushf() {
    push(compress_flags());
    clks(12, 8, 3);
}

void NecV30::i_popf() {
    expand_flags(pop());
    clks(12, 8, 5);
}

void NecV30::nec_interrupt(uint16_t vector) {
    i_pushf();
    tf_ = false;
    iff_ = false;
    mf_ = true;
    push(ps_);
    push(ip_);
    ip_ = read_word(uint32_t(vector) * 4);
    ps_ = read_word(uint32_t(vector) * 4 + 2);
    prefetch_reset_ = true;
    halted_ = false;
}

void NecV30::adj4(int param1, int param2) {
    if (af() || ((aw_ & 0xf) > 9)) {
        uint16_t tmp = uint16_t((aw_ & 0xff) + param1);
        set_breg(0, uint8_t(tmp));
        aux_val_ = 1;
        if (tmp & 0x100) carry_val_ = 1;
    }
    if (cf() || ((aw_ & 0xff) > 0x9f)) {
        set_breg(0, uint8_t(get_breg(0) + param2));
        carry_val_ = 1;
    }
    set_szpf_byte(get_breg(0));
}

void NecV30::adjb(int param1, int param2) {
    if (af() || ((aw_ & 0xf) > 9)) {
        set_breg(0, uint8_t(get_breg(0) + param1));
        set_breg(4, uint8_t(get_breg(4) + param2));
        aux_val_ = 1;
        carry_val_ = 1;
    } else {
        aux_val_ = 0;
        carry_val_ = 0;
    }
    set_breg(0, uint8_t(aw_ & 0x0f));
}

void NecV30::i_movsb() {
    put_mem_b(kDs1, iy_, get_mem_b(kDs0, ix_));
    iy_ = uint16_t(iy_ + dir());
    ix_ = uint16_t(ix_ + dir());
    clks(8, 8, 6);
}

void NecV30::i_movsw() {
    put_mem_w(kDs1, iy_, get_mem_w(kDs0, ix_));
    iy_ = uint16_t(iy_ + 2 * dir());
    ix_ = uint16_t(ix_ + 2 * dir());
    clks(16, 16, 10);
}

void NecV30::i_cmpsb() {
    uint32_t src = get_mem_b(kDs1, iy_);
    uint32_t dst = get_mem_b(kDs0, ix_);
    sub_b(dst, src);
    iy_ = uint16_t(iy_ + dir());
    ix_ = uint16_t(ix_ + dir());
    clks(14, 14, 14);
}

void NecV30::i_cmpsw() {
    uint32_t src = get_mem_w(kDs1, iy_);
    uint32_t dst = get_mem_w(kDs0, ix_);
    sub_w(dst, src);
    iy_ = uint16_t(iy_ + 2 * dir());
    ix_ = uint16_t(ix_ + 2 * dir());
    clks(14, 14, 14);
}

void NecV30::i_stosb() {
    put_mem_b(kDs1, iy_, get_breg(0));
    iy_ = uint16_t(iy_ + dir());
    clks(4, 4, 3);
}

void NecV30::i_stosw() {
    put_mem_w(kDs1, iy_, aw_);
    iy_ = uint16_t(iy_ + 2 * dir());
    clkw(8, 8, 5, 8, 4, 3, iy_);
}

void NecV30::i_lodsb() {
    set_breg(0, get_mem_b(kDs0, ix_));
    ix_ = uint16_t(ix_ + dir());
    clks(4, 4, 3);
}

void NecV30::i_lodsw() {
    aw_ = get_mem_w(kDs0, ix_);
    ix_ = uint16_t(ix_ + 2 * dir());
    clkw(8, 8, 5, 8, 4, 3, ix_);
}

void NecV30::i_scasb() {
    sub_b(get_breg(0), get_mem_b(kDs1, iy_));
    iy_ = uint16_t(iy_ + dir());
    clks(4, 4, 3);
}

void NecV30::i_scasw() {
    sub_w(aw_, get_mem_w(kDs1, iy_));
    iy_ = uint16_t(iy_ + 2 * dir());
    clkw(8, 8, 5, 8, 4, 3, iy_);
}

void NecV30::i_insb() {
    put_mem_b(kDs1, iy_, in_byte(dw_));
    iy_ = uint16_t(iy_ + dir());
    clk(8);
}

void NecV30::i_insw() {
    put_mem_w(kDs1, iy_, in_word(dw_));
    iy_ = uint16_t(iy_ + 2 * dir());
    clks(18, 10, 8);
}

void NecV30::i_outsb() {
    out_byte(dw_, get_mem_b(kDs0, ix_));
    ix_ = uint16_t(ix_ + dir());
    clk(8);
}

void NecV30::i_outsw() {
    out_word(dw_, get_mem_w(kDs0, ix_));
    ix_ = uint16_t(ix_ + 2 * dir());
    clks(18, 10, 8);
}

void NecV30::i_jmp(bool flag) { i_jcc(flag); }

bool NecV30::i_jcc(bool flag) {
    int8_t tmp = int8_t(fetch());
    if (flag) {
        ip_ = uint16_t(ip_ + tmp);
        prefetch_reset_ = true;
        clks(3, 10, 10);
        return true;
    }
    return false;
}

void NecV30::add4s() {
    int count = (get_breg(1) + 1) / 2;
    uint16_t di = iy_;
    uint16_t si = ix_;
    zero_val_ = 0;
    carry_val_ = 0;
    for (int i = 0; i < count; i++) {
        clks(18, 19, 19);
        uint8_t tmp = get_mem_b(kDs0, si);
        uint8_t tmp2 = get_mem_b(kDs1, di);
        int v1 = (tmp >> 4) * 10 + (tmp & 0xf);
        int v2 = (tmp2 >> 4) * 10 + (tmp2 & 0xf);
        int result = v1 + v2 + (cf() ? 1 : 0);
        carry_val_ = result > 99 ? 1 : 0;
        result %= 100;
        uint8_t packed = uint8_t(((result / 10) << 4) | (result % 10));
        put_mem_b(kDs1, di, packed);
        if (packed) zero_val_ = 1;
        si++;
        di++;
    }
}

void NecV30::sub4s() {
    int count = (get_breg(1) + 1) / 2;
    uint16_t di = iy_;
    uint16_t si = ix_;
    zero_val_ = 0;
    carry_val_ = 0;
    for (int i = 0; i < count; i++) {
        clks(18, 19, 19);
        uint8_t tmp = get_mem_b(kDs1, di);
        uint8_t tmp2 = get_mem_b(kDs0, si);
        int v1 = (tmp >> 4) * 10 + (tmp & 0xf);
        int v2 = (tmp2 >> 4) * 10 + (tmp2 & 0xf);
        int result;
        if (v1 < v2 + (cf() ? 1 : 0)) {
            v1 += 100;
            result = v1 - (v2 + (cf() ? 1 : 0));
            carry_val_ = 1;
        } else {
            result = v1 - (v2 + (cf() ? 1 : 0));
            carry_val_ = 0;
        }
        uint8_t packed = uint8_t(((result / 10) << 4) | (result % 10));
        put_mem_b(kDs1, di, packed);
        if (packed) zero_val_ = 1;
        si++;
        di++;
    }
}

void NecV30::cmp4s() {
    int count = (get_breg(1) + 1) / 2;
    uint16_t di = iy_;
    uint16_t si = ix_;
    zero_val_ = 0;
    carry_val_ = 0;
    for (int i = 0; i < count; i++) {
        clks(14, 19, 19);
        uint8_t tmp = get_mem_b(kDs1, di);
        uint8_t tmp2 = get_mem_b(kDs0, si);
        int v1 = (tmp >> 4) * 10 + (tmp & 0xf);
        int v2 = (tmp2 >> 4) * 10 + (tmp2 & 0xf);
        int result;
        if (v1 < v2 + (cf() ? 1 : 0)) {
            v1 += 100;
            result = v1 - (v2 + (cf() ? 1 : 0));
            carry_val_ = 1;
        } else {
            result = v1 - (v2 + (cf() ? 1 : 0));
            carry_val_ = 0;
        }
        uint8_t packed = uint8_t(((result / 10) << 4) | (result % 10));
        if (packed) zero_val_ = 1;
        si++;
        di++;
    }
}

void NecV30::i_pre_nec() {
    uint8_t op = fetch();
    uint8_t modrm = 0;
    uint32_t tmp = 0;
    uint32_t tmp2 = 0;
    auto bitop_byte = [&]() {
        modrm = fetch();
        tmp = get_rm_byte(modrm);
    };
    auto bitop_word = [&]() {
        modrm = fetch();
        tmp = get_rm_word(modrm);
    };
    switch (op) {
        case 0x10:
            bitop_byte();
            clks(3, 3, 4);
            tmp2 = get_breg(1) & 7;
            zero_val_ = (tmp & (1u << tmp2)) ? 1 : 0;
            carry_val_ = over_val_ = 0;
            break;
        case 0x11:
            bitop_word();
            clks(3, 3, 4);
            tmp2 = get_breg(1) & 0xf;
            zero_val_ = (tmp & (1u << tmp2)) ? 1 : 0;
            carry_val_ = over_val_ = 0;
            break;
        case 0x12:
            bitop_byte();
            clks(5, 5, 4);
            tmp2 = get_breg(1) & 7;
            tmp &= ~(1u << tmp2);
            putback_rm_byte(modrm, uint8_t(tmp));
            break;
        case 0x13:
            bitop_word();
            clks(5, 5, 4);
            tmp2 = get_breg(1) & 0xf;
            tmp &= ~(1u << tmp2);
            putback_rm_word(modrm, uint16_t(tmp));
            break;
        case 0x14:
            bitop_byte();
            clks(4, 4, 4);
            tmp2 = get_breg(1) & 7;
            tmp |= (1u << tmp2);
            putback_rm_byte(modrm, uint8_t(tmp));
            break;
        case 0x15:
            bitop_word();
            clks(4, 4, 4);
            tmp2 = get_breg(1) & 0xf;
            tmp |= (1u << tmp2);
            putback_rm_word(modrm, uint16_t(tmp));
            break;
        case 0x16:
            bitop_byte();
            clks(4, 4, 4);
            tmp2 = get_breg(1) & 7;
            tmp ^= (1u << tmp2);
            putback_rm_byte(modrm, uint8_t(tmp));
            break;
        case 0x17:
            bitop_word();
            clks(4, 4, 4);
            tmp2 = get_breg(1) & 0xf;
            tmp ^= (1u << tmp2);
            putback_rm_word(modrm, uint16_t(tmp));
            break;
        case 0x18:
            bitop_byte();
            clks(4, 4, 4);
            tmp2 = fetch() & 7;
            zero_val_ = (tmp & (1u << tmp2)) ? 1 : 0;
            carry_val_ = over_val_ = 0;
            break;
        case 0x19:
            bitop_word();
            clks(4, 4, 4);
            tmp2 = fetch() & 0xf;
            zero_val_ = (tmp & (1u << tmp2)) ? 1 : 0;
            carry_val_ = over_val_ = 0;
            break;
        case 0x1a:
            bitop_byte();
            clks(6, 6, 4);
            tmp2 = fetch() & 7;
            tmp &= ~(1u << tmp2);
            putback_rm_byte(modrm, uint8_t(tmp));
            break;
        case 0x1b:
            bitop_word();
            clks(6, 6, 4);
            tmp2 = fetch() & 0xf;
            tmp &= ~(1u << tmp2);
            putback_rm_word(modrm, uint16_t(tmp));
            break;
        case 0x1c:
            bitop_byte();
            clks(5, 5, 4);
            tmp2 = fetch() & 7;
            tmp |= (1u << tmp2);
            putback_rm_byte(modrm, uint8_t(tmp));
            break;
        case 0x1d:
            bitop_word();
            clks(5, 5, 4);
            tmp2 = fetch() & 0xf;
            tmp |= (1u << tmp2);
            putback_rm_word(modrm, uint16_t(tmp));
            break;
        case 0x1e:
            bitop_byte();
            clks(5, 5, 4);
            tmp2 = fetch() & 7;
            tmp ^= (1u << tmp2);
            putback_rm_byte(modrm, uint8_t(tmp));
            break;
        case 0x1f:
            bitop_word();
            clks(5, 5, 4);
            tmp2 = fetch() & 0xf;
            tmp ^= (1u << tmp2);
            putback_rm_word(modrm, uint16_t(tmp));
            break;
        case 0x20:
            add4s();
            clks(7, 7, 2);
            break;
        case 0x22:
            sub4s();
            clks(7, 7, 2);
            break;
        case 0x26:
            cmp4s();
            clks(7, 7, 2);
            break;
        case 0x28: {
            modrm = fetch();
            tmp = get_rm_byte(modrm);
            tmp <<= 4;
            tmp |= get_breg(0) & 0xf;
            set_breg(0, uint8_t((get_breg(0) & 0xf0) | ((tmp >> 8) & 0xf)));
            putback_rm_byte(modrm, uint8_t(tmp));
            clkm(13, 13, 9, 28, 28, 15, modrm);
            break;
        }
        case 0x2a: {
            modrm = fetch();
            tmp = get_rm_byte(modrm);
            tmp2 = uint32_t(get_breg(0) & 0xf) << 4;
            set_breg(0, uint8_t((get_breg(0) & 0xf0) | (tmp & 0xf)));
            tmp = tmp2 | (tmp >> 4);
            putback_rm_byte(modrm, uint8_t(tmp));
            clkm(17, 17, 13, 32, 32, 19, modrm);
            break;
        }
        default:
            clk(2);
            break;
    }
}

void NecV30::repeat_string(uint8_t opcode, bool check_zf, bool zf_must_be) {
    uint16_t c = cw_;
    auto once = [&]() {
        switch (opcode) {
            case 0x6c: i_insb(); break;
            case 0x6d: i_insw(); break;
            case 0x6e: i_outsb(); break;
            case 0x6f: i_outsw(); break;
            case 0xa4: i_movsb(); break;
            case 0xa5: i_movsw(); break;
            case 0xa6: i_cmpsb(); break;
            case 0xa7: i_cmpsw(); break;
            case 0xaa: i_stosb(); break;
            case 0xab: i_stosw(); break;
            case 0xac: i_lodsb(); break;
            case 0xad: i_lodsw(); break;
            case 0xae: i_scasb(); break;
            case 0xaf: i_scasw(); break;
            default: execute_op(opcode); return false;
        }
        return true;
    };
    clk(2);
    if (opcode < 0x6c || (opcode > 0x6f && opcode < 0xa4) || opcode > 0xaf) {
        once();
        seg_prefix_ = false;
        return;
    }
    if (c) {
        do {
            once();
            c--;
        } while (c > 0 && (!check_zf || zf() == zf_must_be));
        cw_ = c;
    }
    seg_prefix_ = false;
}

void NecV30::execute_op(uint8_t op) {
    switch (op) {
        case 0x00: case 0x08: case 0x10: case 0x18:
        case 0x20: case 0x28: case 0x30: case 0x38: {
            uint8_t modrm = fetch();
            uint8_t dst = get_rm_byte(modrm);
            uint8_t src = reg_byte(modrm);
            uint8_t res = alu_b(uint8_t(op >> 3), dst, src);
            if ((op & 0x38) != 0x38) putback_rm_byte(modrm, res);
            clkm(2, 2, 2, 16, 16, 7, modrm);
            break;
        }
        case 0x01: case 0x09: case 0x11: case 0x19:
        case 0x21: case 0x29: case 0x31: case 0x39: {
            uint8_t modrm = fetch();
            uint16_t dst = get_rm_word(modrm);
            uint16_t src = reg_word(modrm);
            uint16_t res = alu_w(uint8_t(op >> 3), dst, src);
            if ((op & 0x38) != 0x38) putback_rm_word(modrm, res);
            clkr(24, 24, 11, 24, 16, 7, 2, modrm);
            break;
        }
        case 0x02: case 0x0a: case 0x12: case 0x1a:
        case 0x22: case 0x2a: case 0x32: case 0x3a: {
            uint8_t modrm = fetch();
            uint8_t dst = reg_byte(modrm);
            uint8_t src = get_rm_byte(modrm);
            uint8_t res = alu_b(uint8_t(op >> 3), dst, src);
            if ((op & 0x38) != 0x38) set_reg_byte(modrm, res);
            clkm(2, 2, 2, 11, 11, 6, modrm);
            break;
        }
        case 0x03: case 0x0b: case 0x13: case 0x1b:
        case 0x23: case 0x2b: case 0x33: case 0x3b: {
            uint8_t modrm = fetch();
            uint16_t dst = reg_word(modrm);
            uint16_t src = get_rm_word(modrm);
            uint16_t res = alu_w(uint8_t(op >> 3), dst, src);
            if ((op & 0x38) != 0x38) set_reg_word(modrm, res);
            clkr(15, 15, 8, 15, 11, 6, 2, modrm);
            break;
        }
        case 0x04: case 0x0c: case 0x14: case 0x1c:
        case 0x24: case 0x2c: case 0x34: case 0x3c: {
            uint8_t src = fetch();
            uint8_t res = alu_b(uint8_t(op >> 3), get_breg(0), src);
            if ((op & 0x38) != 0x38) set_breg(0, res);
            clks(4, 4, 2);
            break;
        }
        case 0x05: case 0x0d: case 0x15: case 0x1d:
        case 0x25: case 0x2d: case 0x35: case 0x3d: {
            uint16_t src = fetch_word();
            uint16_t res = alu_w(uint8_t(op >> 3), aw_, src);
            if ((op & 0x38) != 0x38) aw_ = res;
            clks(4, 4, 2);
            break;
        }
        case 0x06: push(ds1_); clks(12, 8, 3); break;
        case 0x07: ds1_ = pop(); clks(12, 8, 5); break;
        case 0x0e: push(ps_); clks(12, 8, 3); break;
        case 0x0f: i_pre_nec(); break;
        case 0x16: push(ss_); clks(12, 8, 3); break;
        case 0x17: ss_ = pop(); clks(12, 8, 5); no_interrupt_ = true; break;
        case 0x1e: push(ds0_); clks(12, 8, 3); break;
        case 0x1f: ds0_ = pop(); clks(12, 8, 5); break;
        case 0x26:
            seg_prefix_ = true;
            prefix_base_ = uint32_t(ds1_) << 4;
            clk(2);
            execute_op(fetch());
            seg_prefix_ = false;
            break;
        case 0x27: adj4(6, 0x60); clks(3, 3, 2); break;
        case 0x2e:
            seg_prefix_ = true;
            prefix_base_ = uint32_t(ps_) << 4;
            clk(2);
            execute_op(fetch());
            seg_prefix_ = false;
            break;
        case 0x2f: adj4(-6, -0x60); clks(3, 3, 2); break;
        case 0x36:
            seg_prefix_ = true;
            prefix_base_ = uint32_t(ss_) << 4;
            clk(2);
            execute_op(fetch());
            seg_prefix_ = false;
            break;
        case 0x37: adjb(6, (get_breg(0) > 0xf9) ? 2 : 1); clks(7, 7, 4); break;
        case 0x3e:
            seg_prefix_ = true;
            prefix_base_ = uint32_t(ds0_) << 4;
            clk(2);
            execute_op(fetch());
            seg_prefix_ = false;
            break;
        case 0x3f: adjb(-6, (get_breg(0) < 6) ? -2 : -1); clks(7, 7, 4); break;
        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47: {
            uint16_t tmp = get_wreg(op & 7);
            uint16_t tmp1 = uint16_t(tmp + 1);
            over_val_ = (tmp == 0x7fff);
            aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
            set_szpf_word(tmp1);
            set_wreg(op & 7, tmp1);
            clk(2);
            break;
        }
        case 0x48: case 0x49: case 0x4a: case 0x4b:
        case 0x4c: case 0x4d: case 0x4e: case 0x4f: {
            uint16_t tmp = get_wreg(op & 7);
            uint16_t tmp1 = uint16_t(tmp - 1);
            over_val_ = (tmp == 0x8000);
            aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
            set_szpf_word(tmp1);
            set_wreg(op & 7, tmp1);
            clk(2);
            break;
        }
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            push(get_wreg(op & 7));
            clks(12, 8, 3);
            break;
        case 0x58: case 0x59: case 0x5a: case 0x5b:
        case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            set_wreg(op & 7, pop());
            clks(12, 8, 5);
            break;
        case 0x60: {
            uint16_t tmp = sp_;
            push(aw_);
            push(cw_);
            push(dw_);
            push(bw_);
            push(tmp);
            push(bp_);
            push(ix_);
            push(iy_);
            clks(67, 35, 20);
            break;
        }
        case 0x61: {
            iy_ = pop();
            ix_ = pop();
            bp_ = pop();
            pop();
            bw_ = pop();
            dw_ = pop();
            cw_ = pop();
            aw_ = pop();
            clks(75, 43, 22);
            break;
        }
        case 0x62: {
            uint8_t modrm = fetch();
            uint16_t low = get_rm_word(modrm);
            uint16_t high = get_next_rm_word();
            uint16_t tmp = reg_word(modrm);
            if (tmp < low || tmp > high) nec_interrupt(kChkindVector);
            clk(20);
            break;
        }
        case 0x64:
        case 0x65: {
            uint8_t next = fetch();
            if (next == 0x26 || next == 0x2e || next == 0x36 || next == 0x3e) {
                seg_prefix_ = true;
                prefix_base_ = uint32_t(next == 0x26 ? ds1_ : next == 0x2e ? ps_ : next == 0x36 ? ss_ : ds0_)
                               << 4;
                next = fetch();
                clk(2);
            }
            bool want_cf = (op == 0x65);
            uint16_t c = cw_;
            clk(2);
            if (c) {
                do {
                    switch (next) {
                        case 0xa4: i_movsb(); break;
                        case 0xa5: i_movsw(); break;
                        case 0xa6: i_cmpsb(); break;
                        case 0xa7: i_cmpsw(); break;
                        case 0xaa: i_stosb(); break;
                        case 0xab: i_stosw(); break;
                        case 0xac: i_lodsb(); break;
                        case 0xad: i_lodsw(); break;
                        case 0xae: i_scasb(); break;
                        case 0xaf: i_scasw(); break;
                        default: execute_op(next); c = 1; break;
                    }
                    c--;
                } while (c > 0 && (cf() == want_cf));
                cw_ = c;
            }
            seg_prefix_ = false;
            break;
        }
        case 0x68: push(fetch_word()); clkw(12, 12, 5, 12, 8, 5, sp_); break;
        case 0x69: {
            uint8_t modrm = fetch();
            uint16_t src = get_rm_word(modrm);
            uint16_t tmp = fetch_word();
            int32_t dst = int32_t(int16_t(src)) * int32_t(int16_t(tmp));
            carry_val_ = over_val_ = (((dst >> 15) != 0) && ((dst >> 15) != -1)) ? 1 : 0;
            set_reg_word(modrm, uint16_t(dst));
            clk(modrm >= 0xc0 ? 38 : 47);
            break;
        }
        case 0x6a: push(uint16_t(int16_t(int8_t(fetch())))); clkw(11, 11, 5, 11, 7, 3, sp_); break;
        case 0x6b: {
            uint8_t modrm = fetch();
            uint16_t src = get_rm_word(modrm);
            uint16_t src2 = uint16_t(int16_t(int8_t(fetch())));
            int32_t dst = int32_t(int16_t(src)) * int32_t(int16_t(src2));
            carry_val_ = over_val_ = (((dst >> 15) != 0) && ((dst >> 15) != -1)) ? 1 : 0;
            set_reg_word(modrm, uint16_t(dst));
            clk(modrm >= 0xc0 ? 31 : 39);
            break;
        }
        case 0x6c: i_insb(); break;
        case 0x6d: i_insw(); break;
        case 0x6e: i_outsb(); break;
        case 0x6f: i_outsw(); break;
        case 0x70: if (!i_jcc(of())) clks(4, 4, 3); break;
        case 0x71: if (!i_jcc(!of())) clks(4, 4, 3); break;
        case 0x72: if (!i_jcc(cf())) clks(4, 4, 3); break;
        case 0x73: if (!i_jcc(!cf())) clks(4, 4, 3); break;
        case 0x74: if (!i_jcc(zf())) clks(4, 4, 3); break;
        case 0x75: if (!i_jcc(!zf())) clks(4, 4, 3); break;
        case 0x76: if (!i_jcc(cf() || zf())) clks(4, 4, 3); break;
        case 0x77: if (!i_jcc(!(cf() || zf()))) clks(4, 4, 3); break;
        case 0x78: if (!i_jcc(sf())) clks(4, 4, 3); break;
        case 0x79: if (!i_jcc(!sf())) clks(4, 4, 3); break;
        case 0x7a: if (!i_jcc(pf())) clks(4, 4, 3); break;
        case 0x7b: if (!i_jcc(!pf())) clks(4, 4, 3); break;
        case 0x7c: if (!i_jcc((sf() != of()) && !zf())) clks(4, 4, 3); break;
        case 0x7d: if (!i_jcc(zf() || (sf() == of()))) clks(4, 4, 3); break;
        case 0x7e: if (!i_jcc(zf() || (sf() != of()))) clks(4, 4, 3); break;
        case 0x7f: if (!i_jcc((sf() == of()) && !zf())) clks(4, 4, 3); break;
        case 0x80: case 0x82: {
            uint8_t modrm = fetch();
            uint32_t dst = get_rm_byte(modrm);
            uint32_t src = (op == 0x82) ? uint32_t(uint8_t(int8_t(fetch()))) : fetch();
            if (modrm >= 0xc0) clks(4, 4, 2);
            else if ((modrm & 0x38) == 0x38) clks(13, 13, 6);
            else clks(18, 18, 7);
            uint8_t res = alu_b(uint8_t((modrm >> 3) & 7), dst, src);
            if ((modrm & 0x38) != 0x38) putback_rm_byte(modrm, res);
            break;
        }
        case 0x81: case 0x83: {
            uint8_t modrm = fetch();
            uint32_t dst = get_rm_word(modrm);
            uint32_t src = (op == 0x83) ? uint32_t(uint16_t(int16_t(int8_t(fetch())))) : fetch_word();
            if (modrm >= 0xc0) clks(4, 4, 2);
            else if ((modrm & 0x38) == 0x38) clkw(17, 17, 8, 17, 13, 6, uint16_t(ea_));
            else clkw(26, 26, 11, 26, 18, 7, uint16_t(ea_));
            uint16_t res = alu_w(uint8_t((modrm >> 3) & 7), dst, src);
            if ((modrm & 0x38) != 0x38) putback_rm_word(modrm, res);
            break;
        }
        case 0x84: {
            uint8_t modrm = fetch();
            and_b(get_rm_byte(modrm), reg_byte(modrm));
            clkm(2, 2, 2, 10, 10, 6, modrm);
            break;
        }
        case 0x85: {
            uint8_t modrm = fetch();
            and_w(get_rm_word(modrm), reg_word(modrm));
            clkr(14, 14, 8, 14, 10, 6, 2, modrm);
            break;
        }
        case 0x86: {
            uint8_t modrm = fetch();
            uint8_t dst = get_rm_byte(modrm);
            uint8_t src = reg_byte(modrm);
            set_reg_byte(modrm, dst);
            putback_rm_byte(modrm, src);
            clkm(3, 3, 3, 16, 18, 8, modrm);
            break;
        }
        case 0x87: {
            uint8_t modrm = fetch();
            uint16_t dst = get_rm_word(modrm);
            uint16_t src = reg_word(modrm);
            set_reg_word(modrm, dst);
            putback_rm_word(modrm, src);
            clkr(24, 24, 12, 24, 16, 8, 3, modrm);
            break;
        }
        case 0x88: {
            uint8_t modrm = fetch();
            put_rm_byte(modrm, reg_byte(modrm));
            clkm(2, 2, 2, 9, 9, 3, modrm);
            break;
        }
        case 0x89: {
            uint8_t modrm = fetch();
            put_rm_word(modrm, reg_word(modrm));
            clkr(13, 13, 5, 13, 9, 3, 2, modrm);
            break;
        }
        case 0x8a: {
            uint8_t modrm = fetch();
            set_reg_byte(modrm, get_rm_byte(modrm));
            clkm(2, 2, 2, 11, 11, 5, modrm);
            break;
        }
        case 0x8b: {
            uint8_t modrm = fetch();
            set_reg_word(modrm, get_rm_word(modrm));
            clkr(15, 15, 7, 15, 11, 5, 2, modrm);
            break;
        }
        case 0x8c: {
            uint8_t modrm = fetch();
            uint16_t src = 0;
            switch (modrm & 0x38) {
                case 0x00: src = ds1_; break;
                case 0x08: src = ps_; break;
                case 0x10: src = ss_; break;
                default: src = ds0_; break;
            }
            put_rm_word(modrm, src);
            clkr(14, 14, 5, 14, 10, 3, 2, modrm);
            break;
        }
        case 0x8d: {
            uint8_t modrm = fetch();
            get_ea(modrm);
            set_reg_word(modrm, eo_);
            clks(4, 4, 2);
            break;
        }
        case 0x8e: {
            uint8_t modrm = fetch();
            uint16_t src = get_rm_word(modrm);
            clkr(15, 15, 7, 15, 11, 5, 2, modrm);
            switch (modrm & 0x38) {
                case 0x00: ds1_ = src; break;
                case 0x08: ps_ = src; break;
                case 0x10: ss_ = src; break;
                default: ds0_ = src; break;
            }
            no_interrupt_ = true;
            break;
        }
        case 0x8f: {
            uint8_t modrm = fetch();
            uint16_t tmp = pop();
            put_rm_word(modrm, tmp);
            clk(21);
            break;
        }
        case 0x90: clk(3); break;
        case 0x91: case 0x92: case 0x93: case 0x94:
        case 0x95: case 0x96: case 0x97: {
            uint16_t tmp = get_wreg(op & 7);
            set_wreg(op & 7, aw_);
            aw_ = tmp;
            clk(3);
            break;
        }
        case 0x98:
            set_breg(4, (get_breg(0) & 0x80) ? 0xff : 0);
            clk(2);
            break;
        case 0x99:
            dw_ = (get_breg(4) & 0x80) ? 0xffff : 0;
            clk(4);
            break;
        case 0x9a: {
            uint16_t off = fetch_word();
            uint16_t seg = fetch_word();
            push(ps_);
            push(ip_);
            ip_ = off;
            ps_ = seg;
            prefetch_reset_ = true;
            clkw(29, 29, 13, 29, 21, 9, sp_);
            break;
        }
        case 0x9b: clk(5); break;
        case 0x9c: i_pushf(); break;
        case 0x9d: i_popf(); break;
        case 0x9e: {
            uint16_t tmp = uint16_t((compress_flags() & 0xff00) | (get_breg(4) & 0xd5));
            expand_flags(tmp);
            clks(3, 3, 2);
            break;
        }
        case 0x9f:
            set_breg(4, uint8_t(compress_flags()));
            clks(3, 3, 2);
            break;
        case 0xa0: set_breg(0, get_mem_b(kDs0, fetch_word())); clks(10, 10, 5); break;
        case 0xa1: {
            uint16_t addr = fetch_word();
            aw_ = get_mem_w(kDs0, addr);
            clkw(14, 14, 7, 14, 10, 5, addr);
            break;
        }
        case 0xa2: put_mem_b(kDs0, fetch_word(), get_breg(0)); clks(9, 9, 3); break;
        case 0xa3: {
            uint16_t addr = fetch_word();
            put_mem_w(kDs0, addr, aw_);
            clkw(13, 13, 5, 13, 9, 3, addr);
            break;
        }
        case 0xa4: i_movsb(); break;
        case 0xa5: i_movsw(); break;
        case 0xa6: i_cmpsb(); break;
        case 0xa7: i_cmpsw(); break;
        case 0xa8: and_b(get_breg(0), fetch()); clks(4, 4, 2); break;
        case 0xa9: and_w(aw_, fetch_word()); clks(4, 4, 2); break;
        case 0xaa: i_stosb(); break;
        case 0xab: i_stosw(); break;
        case 0xac: i_lodsb(); break;
        case 0xad: i_lodsw(); break;
        case 0xae: i_scasb(); break;
        case 0xaf: i_scasw(); break;
        case 0xb0: case 0xb1: case 0xb2: case 0xb3:
        case 0xb4: case 0xb5: case 0xb6: case 0xb7:
            set_breg(op & 7, fetch());
            clks(4, 4, 2);
            break;
        case 0xb8: case 0xb9: case 0xba: case 0xbb:
        case 0xbc: case 0xbd: case 0xbe: case 0xbf:
            set_wreg(op & 7, fetch_word());
            clks(4, 4, 2);
            break;
        case 0xc0: {
            uint8_t modrm = fetch();
            uint8_t dst = get_rm_byte(modrm);
            uint8_t c = fetch();
            clkm(7, 7, 2, 19, 19, 6, modrm);
            if (c) {
                switch (modrm & 0x38) {
                    case 0x00:
                        do { dst = rol_b(dst); c--; clk(1); } while (c);
                        putback_rm_byte(modrm, dst);
                        break;
                    case 0x08:
                        do { dst = ror_b(dst); c--; clk(1); } while (c);
                        putback_rm_byte(modrm, dst);
                        break;
                    case 0x10:
                        do { dst = rolc_b(dst); c--; clk(1); } while (c);
                        putback_rm_byte(modrm, dst);
                        break;
                    case 0x18:
                        do { dst = rorc_b(dst); c--; clk(1); } while (c);
                        putback_rm_byte(modrm, dst);
                        break;
                    case 0x20: putback_rm_byte(modrm, shl_b(dst, c)); break;
                    case 0x28: putback_rm_byte(modrm, shr_b(dst, c)); break;
                    case 0x38: putback_rm_byte(modrm, shra_b(dst, c)); break;
                    default: break;
                }
            }
            break;
        }
        case 0xc1: {
            uint8_t modrm = fetch();
            uint16_t dst = get_rm_word(modrm);
            uint8_t c = fetch();
            clkm(7, 7, 2, 27, 19, 6, modrm);
            if (c) {
                switch (modrm & 0x38) {
                    case 0x00:
                        do { dst = rol_w(dst); c--; clk(1); } while (c);
                        putback_rm_word(modrm, dst);
                        break;
                    case 0x08:
                        do { dst = ror_w(dst); c--; clk(1); } while (c);
                        putback_rm_word(modrm, dst);
                        break;
                    case 0x10:
                        do { dst = rolc_w(dst); c--; clk(1); } while (c);
                        putback_rm_word(modrm, dst);
                        break;
                    case 0x18:
                        do { dst = rorc_w(dst); c--; clk(1); } while (c);
                        putback_rm_word(modrm, dst);
                        break;
                    case 0x20: putback_rm_word(modrm, shl_w(dst, c)); break;
                    case 0x28: putback_rm_word(modrm, shr_w(dst, c)); break;
                    case 0x38: putback_rm_word(modrm, shra_w(dst, c)); break;
                    default: break;
                }
            }
            break;
        }
        case 0xc2: {
            uint16_t count = fetch_word();
            ip_ = pop();
            sp_ = uint16_t(sp_ + count);
            prefetch_reset_ = true;
            clks(24, 24, 10);
            break;
        }
        case 0xc3:
            ip_ = pop();
            prefetch_reset_ = true;
            clks(19, 19, 10);
            break;
        case 0xc4: {
            uint8_t modrm = fetch();
            uint16_t tmp = get_rm_word(modrm);
            set_reg_word(modrm, tmp);
            ds1_ = get_next_rm_word();
            clkw(26, 26, 14, 26, 18, 10, uint16_t(ea_));
            break;
        }
        case 0xc5: {
            uint8_t modrm = fetch();
            uint16_t tmp = get_rm_word(modrm);
            set_reg_word(modrm, tmp);
            ds0_ = get_next_rm_word();
            clkw(26, 26, 14, 26, 18, 10, uint16_t(ea_));
            break;
        }
        case 0xc6: {
            uint8_t modrm = fetch();
            put_imm_rm_byte(modrm);
            clk(modrm >= 0xc0 ? 4 : 11);
            break;
        }
        case 0xc7: {
            uint8_t modrm = fetch();
            put_imm_rm_word(modrm);
            clk(modrm >= 0xc0 ? 4 : 15);
            break;
        }
        case 0xc8: {
            uint16_t nb = fetch_word();
            uint8_t level = fetch();
            clk(23);
            push(bp_);
            bp_ = sp_;
            sp_ = uint16_t(sp_ - nb);
            for (uint8_t i = 1; i < level; i++) {
                push(get_mem_w(kSs, uint16_t(bp_ - i * 2)));
                clk(16);
            }
            if (level) push(bp_);
            break;
        }
        case 0xc9:
            sp_ = bp_;
            bp_ = pop();
            clk(8);
            break;
        case 0xca: {
            uint16_t count = fetch_word();
            ip_ = pop();
            ps_ = pop();
            sp_ = uint16_t(sp_ + count);
            prefetch_reset_ = true;
            clks(32, 32, 16);
            break;
        }
        case 0xcb:
            ip_ = pop();
            ps_ = pop();
            prefetch_reset_ = true;
            clks(29, 29, 16);
            break;
        case 0xcc: nec_interrupt(3); clks(50, 50, 24); break;
        case 0xcd: nec_interrupt(fetch()); clks(50, 50, 24); break;
        case 0xce:
            if (of()) {
                nec_interrupt(kBrkvVector);
                clks(52, 52, 26);
            } else clk(3);
            break;
        case 0xcf:
            ip_ = pop();
            ps_ = pop();
            i_popf();
            prefetch_reset_ = true;
            clks(39, 39, 19);
            break;
        case 0xd0: {
            uint8_t modrm = fetch();
            uint8_t src = get_rm_byte(modrm);
            uint8_t dst = src;
            clkm(6, 6, 2, 16, 16, 7, modrm);
            switch (modrm & 0x38) {
                case 0x00: dst = rol_b(dst); over_val_ = (src ^ dst) & 0x80; break;
                case 0x08: dst = ror_b(dst); over_val_ = (src ^ dst) & 0x80; break;
                case 0x10: dst = rolc_b(dst); over_val_ = (src ^ dst) & 0x80; break;
                case 0x18: dst = rorc_b(dst); over_val_ = (src ^ dst) & 0x80; break;
                case 0x20: dst = shl_b(dst, 1); over_val_ = (src ^ dst) & 0x80; break;
                case 0x28: dst = shr_b(dst, 1); over_val_ = (src ^ dst) & 0x80; break;
                case 0x38: dst = shra_b(dst, 1); over_val_ = 0; break;
                default: break;
            }
            putback_rm_byte(modrm, dst);
            break;
        }
        case 0xd1: {
            uint8_t modrm = fetch();
            uint16_t src = get_rm_word(modrm);
            uint16_t dst = src;
            clkm(6, 6, 2, 24, 16, 7, modrm);
            switch (modrm & 0x38) {
                case 0x00: dst = rol_w(dst); over_val_ = (src ^ dst) & 0x8000; break;
                case 0x08: dst = ror_w(dst); over_val_ = (src ^ dst) & 0x8000; break;
                case 0x10: dst = rolc_w(dst); over_val_ = (src ^ dst) & 0x8000; break;
                case 0x18: dst = rorc_w(dst); over_val_ = (src ^ dst) & 0x8000; break;
                case 0x20: dst = shl_w(dst, 1); over_val_ = (src ^ dst) & 0x8000; break;
                case 0x28: dst = shr_w(dst, 1); over_val_ = (src ^ dst) & 0x8000; break;
                case 0x38: dst = shra_w(dst, 1); over_val_ = 0; break;
                default: break;
            }
            putback_rm_word(modrm, dst);
            break;
        }
        case 0xd2: {
            uint8_t modrm = fetch();
            uint8_t dst = get_rm_byte(modrm);
            uint8_t c = get_breg(1);
            clkm(7, 7, 2, 19, 19, 6, modrm);
            if (c) {
                switch (modrm & 0x38) {
                    case 0x00:
                        do { dst = rol_b(dst); c--; clk(1); } while (c);
                        putback_rm_byte(modrm, dst);
                        break;
                    case 0x08:
                        do { dst = ror_b(dst); c--; clk(1); } while (c);
                        putback_rm_byte(modrm, dst);
                        break;
                    case 0x10:
                        do { dst = rolc_b(dst); c--; clk(1); } while (c);
                        putback_rm_byte(modrm, dst);
                        break;
                    case 0x18:
                        do { dst = rorc_b(dst); c--; clk(1); } while (c);
                        putback_rm_byte(modrm, dst);
                        break;
                    case 0x20: putback_rm_byte(modrm, shl_b(dst, c)); break;
                    case 0x28: putback_rm_byte(modrm, shr_b(dst, c)); break;
                    case 0x38: putback_rm_byte(modrm, shra_b(dst, c)); break;
                    default: break;
                }
            }
            break;
        }
        case 0xd3: {
            uint8_t modrm = fetch();
            uint16_t dst = get_rm_word(modrm);
            uint8_t c = get_breg(1);
            clkm(7, 7, 2, 27, 19, 6, modrm);
            if (c) {
                switch (modrm & 0x38) {
                    case 0x00:
                        do { dst = rol_w(dst); c--; clk(1); } while (c);
                        putback_rm_word(modrm, dst);
                        break;
                    case 0x08:
                        do { dst = ror_w(dst); c--; clk(1); } while (c);
                        putback_rm_word(modrm, dst);
                        break;
                    case 0x10:
                        do { dst = rolc_w(dst); c--; clk(1); } while (c);
                        putback_rm_word(modrm, dst);
                        break;
                    case 0x18:
                        do { dst = rorc_w(dst); c--; clk(1); } while (c);
                        putback_rm_word(modrm, dst);
                        break;
                    case 0x20: putback_rm_word(modrm, shl_w(dst, c)); break;
                    case 0x28: putback_rm_word(modrm, shr_w(dst, c)); break;
                    case 0x38: putback_rm_word(modrm, shra_w(dst, c)); break;
                    default: break;
                }
            }
            break;
        }
        case 0xd4:
            fetch();
            set_breg(4, uint8_t(get_breg(0) / 10));
            set_breg(0, uint8_t(get_breg(0) % 10));
            set_szpf_word(aw_);
            clks(15, 15, 12);
            break;
        case 0xd5:
            fetch();
            set_breg(0, uint8_t(get_breg(4) * 10 + get_breg(0)));
            set_breg(4, 0);
            set_szpf_byte(get_breg(0));
            clks(7, 7, 8);
            break;
        case 0xd6:
            set_breg(0, cf() ? 0xff : 0x00);
            clk(3);
            break;
        case 0xd7: {
            uint16_t dest = uint16_t(bw_ + get_breg(0));
            set_breg(0, get_mem_b(kDs0, dest));
            clks(9, 9, 5);
            break;
        }
        case 0xd8: case 0xd9: case 0xda: case 0xdb:
        case 0xdc: case 0xdd: case 0xde: case 0xdf: {
            uint8_t modrm = fetch();
            get_rm_byte(modrm);
            clk(2);
            break;
        }
        case 0xe0: {
            int8_t disp = int8_t(fetch());
            cw_--;
            if (!zf() && cw_) {
                ip_ = uint16_t(ip_ + disp);
                clks(14, 14, 6);
            } else clks(5, 5, 3);
            break;
        }
        case 0xe1: {
            int8_t disp = int8_t(fetch());
            cw_--;
            if (zf() && cw_) {
                ip_ = uint16_t(ip_ + disp);
                clks(14, 14, 6);
            } else clks(5, 5, 3);
            break;
        }
        case 0xe2: {
            int8_t disp = int8_t(fetch());
            cw_--;
            if (cw_) {
                ip_ = uint16_t(ip_ + disp);
                clks(13, 13, 6);
            } else clks(5, 5, 3);
            break;
        }
        case 0xe3: {
            int8_t disp = int8_t(fetch());
            if (cw_ == 0) {
                ip_ = uint16_t(ip_ + disp);
                clks(13, 13, 6);
            } else clks(5, 5, 3);
            break;
        }
        case 0xe4: set_breg(0, in_byte(fetch())); clks(9, 9, 5); break;
        case 0xe5: {
            uint8_t port = fetch();
            aw_ = in_word(port);
            clkw(13, 13, 7, 13, 9, 5, port);
            break;
        }
        case 0xe6: out_byte(fetch(), get_breg(0)); clks(8, 8, 3); break;
        case 0xe7: {
            uint8_t port = fetch();
            out_word(port, aw_);
            clkw(12, 12, 5, 12, 8, 3, port);
            break;
        }
        case 0xe8: {
            uint16_t tmp = fetch_word();
            push(ip_);
            ip_ = uint16_t(ip_ + int16_t(tmp));
            prefetch_reset_ = true;
            clk(24);
            break;
        }
        case 0xe9: {
            uint16_t tmp = fetch_word();
            ip_ = uint16_t(ip_ + int16_t(tmp));
            prefetch_reset_ = true;
            clk(15);
            break;
        }
        case 0xea: {
            uint16_t off = fetch_word();
            uint16_t seg = fetch_word();
            ip_ = off;
            ps_ = seg;
            prefetch_reset_ = true;
            clk(27);
            break;
        }
        case 0xeb: {
            int8_t tmp = int8_t(fetch());
            clk(12);
            ip_ = uint16_t(ip_ + tmp);
            break;
        }
        case 0xec: set_breg(0, in_byte(dw_)); clks(8, 8, 5); break;
        case 0xed: aw_ = in_word(dw_); clkw(12, 12, 7, 12, 8, 5, dw_); break;
        case 0xee: out_byte(dw_, get_breg(0)); clks(8, 8, 3); break;
        case 0xef: out_word(dw_, aw_); clkw(12, 12, 5, 12, 8, 3, dw_); break;
        case 0xf0:
            no_interrupt_ = true;
            clk(2);
            execute_op(fetch());
            break;
        case 0xf1: clk(2); break;
        case 0xf2:
        case 0xf3: {
            uint8_t next = fetch();
            if (next == 0x26 || next == 0x2e || next == 0x36 || next == 0x3e) {
                seg_prefix_ = true;
                prefix_base_ = uint32_t(next == 0x26 ? ds1_ : next == 0x2e ? ps_ : next == 0x36 ? ss_ : ds0_)
                               << 4;
                next = fetch();
                clk(2);
            }
            bool check_zf = (next == 0xa6 || next == 0xa7 || next == 0xae || next == 0xaf);
            repeat_string(next, check_zf, op == 0xf3);
            break;
        }
        case 0xf4:
            halted_ = true;
            break;
        case 0xf5:
            carry_val_ = cf() ? 0 : 1;
            clk(2);
            break;
        case 0xf6: {
            uint8_t modrm = fetch();
            uint32_t tmp = get_rm_byte(modrm);
            switch (modrm & 0x38) {
                case 0x00:
                    tmp &= fetch();
                    carry_val_ = over_val_ = 0;
                    set_szpf_byte(tmp);
                    clk(modrm >= 0xc0 ? 4 : 11);
                    break;
                case 0x10:
                    putback_rm_byte(modrm, uint8_t(~tmp));
                    clk(modrm >= 0xc0 ? 2 : 16);
                    break;
                case 0x18:
                    carry_val_ = (tmp != 0);
                    tmp = uint8_t(~tmp) + 1;
                    set_szpf_byte(tmp);
                    putback_rm_byte(modrm, uint8_t(tmp));
                    clk(modrm >= 0xc0 ? 2 : 16);
                    break;
                case 0x20: {
                    uint32_t uresult = uint32_t(get_breg(0)) * tmp;
                    aw_ = uint16_t(uresult);
                    carry_val_ = over_val_ = (get_breg(4) != 0);
                    clk(modrm >= 0xc0 ? 30 : 36);
                    break;
                }
                case 0x28: {
                    int32_t result = int16_t(int8_t(get_breg(0))) * int16_t(int8_t(tmp));
                    aw_ = uint16_t(result);
                    carry_val_ = over_val_ = (get_breg(4) != 0);
                    clk(modrm >= 0xc0 ? 30 : 36);
                    break;
                }
                case 0x30:
                    if (tmp) {
                        uint32_t uresult = aw_;
                        uint32_t uresult2 = uresult % tmp;
                        uresult /= tmp;
                        if (uresult > 0xff) nec_interrupt(kDivideVector);
                        else {
                            set_breg(0, uint8_t(uresult));
                            set_breg(4, uint8_t(uresult2));
                        }
                    } else nec_interrupt(kDivideVector);
                    clk(modrm >= 0xc0 ? 43 : 53);
                    break;
                case 0x38:
                    if (tmp) {
                        int32_t result = int16_t(aw_);
                        int32_t result2 = result % int16_t(int8_t(tmp));
                        result /= int16_t(int8_t(tmp));
                        if (result > 0xff || result < -0x80) nec_interrupt(kDivideVector);
                        else {
                            set_breg(0, uint8_t(result));
                            set_breg(4, uint8_t(result2));
                        }
                    } else nec_interrupt(kDivideVector);
                    clk(modrm >= 0xc0 ? 43 : 53);
                    break;
                default: break;
            }
            break;
        }
        case 0xf7: {
            uint8_t modrm = fetch();
            uint32_t tmp = get_rm_word(modrm);
            switch (modrm & 0x38) {
                case 0x00:
                    tmp &= fetch_word();
                    carry_val_ = over_val_ = 0;
                    set_szpf_word(tmp);
                    clk(modrm >= 0xc0 ? 4 : 11);
                    break;
                case 0x10:
                    putback_rm_word(modrm, uint16_t(~tmp));
                    clk(modrm >= 0xc0 ? 2 : 16);
                    break;
                case 0x18:
                    carry_val_ = (tmp != 0);
                    tmp = uint16_t(~tmp) + 1;
                    set_szpf_word(tmp);
                    putback_rm_word(modrm, uint16_t(tmp));
                    clk(modrm >= 0xc0 ? 2 : 16);
                    break;
                case 0x20: {
                    uint32_t uresult = uint32_t(aw_) * tmp;
                    aw_ = uint16_t(uresult);
                    dw_ = uint16_t(uresult >> 16);
                    carry_val_ = over_val_ = (dw_ != 0);
                    clk(modrm >= 0xc0 ? 30 : 36);
                    break;
                }
                case 0x28: {
                    int32_t result = int32_t(int16_t(aw_)) * int32_t(int16_t(tmp));
                    aw_ = uint16_t(result);
                    dw_ = uint16_t(uint32_t(result) >> 16);
                    carry_val_ = over_val_ = (dw_ != 0);
                    clk(modrm >= 0xc0 ? 30 : 36);
                    break;
                }
                case 0x30:
                    if (tmp) {
                        uint32_t uresult = (uint32_t(dw_) << 16) | aw_;
                        uint32_t uresult2 = uresult % tmp;
                        uresult /= tmp;
                        if (uresult > 0xffff) nec_interrupt(kDivideVector);
                        else {
                            aw_ = uint16_t(uresult);
                            dw_ = uint16_t(uresult2);
                        }
                    } else nec_interrupt(kDivideVector);
                    clk(modrm >= 0xc0 ? 43 : 53);
                    break;
                case 0x38:
                    if (tmp) {
                        int32_t result = int32_t((uint32_t(dw_) << 16) | aw_);
                        int32_t result2 = result % int32_t(int16_t(tmp));
                        result /= int32_t(int16_t(tmp));
                        if (result > 0xffff || result < -0x8000) nec_interrupt(kDivideVector);
                        else {
                            aw_ = uint16_t(result);
                            dw_ = uint16_t(result2);
                        }
                    } else nec_interrupt(kDivideVector);
                    clk(modrm >= 0xc0 ? 43 : 53);
                    break;
                default: break;
            }
            break;
        }
        case 0xf8: carry_val_ = 0; clk(2); break;
        case 0xf9: carry_val_ = 1; clk(2); break;
        case 0xfa: iff_ = false; clk(2); break;
        case 0xfb: iff_ = true; clk(2); break;
        case 0xfc: df_ = false; clk(2); break;
        case 0xfd: df_ = true; clk(2); break;
        case 0xfe: {
            uint8_t modrm = fetch();
            uint32_t tmp = get_rm_byte(modrm);
            if ((modrm & 0x38) == 0x00) {
                uint32_t tmp1 = tmp + 1;
                over_val_ = (tmp == 0x7f);
                aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
                set_szpf_byte(tmp1);
                putback_rm_byte(modrm, uint8_t(tmp1));
            } else {
                uint32_t tmp1 = tmp - 1;
                over_val_ = (tmp == 0x80);
                aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
                set_szpf_byte(tmp1);
                putback_rm_byte(modrm, uint8_t(tmp1));
            }
            clkm(2, 2, 2, 16, 16, 7, modrm);
            break;
        }
        case 0xff: {
            uint8_t modrm = fetch();
            uint32_t tmp = get_rm_word(modrm);
            switch (modrm & 0x38) {
                case 0x00: {
                    uint32_t tmp1 = tmp + 1;
                    over_val_ = (tmp == 0x7fff);
                    aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
                    set_szpf_word(tmp1);
                    putback_rm_word(modrm, uint16_t(tmp1));
                    clkm(2, 2, 2, 24, 16, 7, modrm);
                    break;
                }
                case 0x08: {
                    uint32_t tmp1 = tmp - 1;
                    over_val_ = (tmp == 0x8000);
                    aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
                    set_szpf_word(tmp1);
                    putback_rm_word(modrm, uint16_t(tmp1));
                    clkm(2, 2, 2, 24, 16, 7, modrm);
                    break;
                }
                case 0x10:
                    push(ip_);
                    ip_ = uint16_t(tmp);
                    prefetch_reset_ = true;
                    clk(modrm >= 0xc0 ? 16 : 20);
                    break;
                case 0x18: {
                    uint16_t tmp1 = ps_;
                    ps_ = get_next_rm_word();
                    push(tmp1);
                    push(ip_);
                    ip_ = uint16_t(tmp);
                    prefetch_reset_ = true;
                    clk(modrm >= 0xc0 ? 16 : 26);
                    break;
                }
                case 0x20:
                    ip_ = uint16_t(tmp);
                    prefetch_reset_ = true;
                    clk(13);
                    break;
                case 0x28:
                    ip_ = uint16_t(tmp);
                    ps_ = get_next_rm_word();
                    prefetch_reset_ = true;
                    clk(15);
                    break;
                case 0x30:
                    push(uint16_t(tmp));
                    clk(4);
                    break;
                default: clk(2); break;
            }
            break;
        }
        default:
            clk(10);
            break;
    }
}

void NecV30::set_irq(IrqLine state, uint8_t vector) {
    if (state == IrqLine::Clear) irq_pending_ &= uint8_t(~kIntIrq);
    else {
        irq_vector_ = vector;
        irq_pending_ |= kIntIrq;
        halted_ = false;
    }
}

void NecV30::set_nmi(IrqLine state) {
    if (nmi_state_ == state) return;
    nmi_state_ = state;
    if (state != IrqLine::Clear) {
        irq_pending_ |= kNmiIrq;
        halted_ = false;
    }
}

int NecV30::run(int cycles) {
    executed_ = 0;
    icount_ = 0;
    if (halted_) {
        executed_ = cycles;
        if (cycle_handler_) cycle_handler_(cycles);
        return executed_;
    }
    while (icount_ < cycles) {
        if (irq_pending_ && !no_interrupt_) {
            if (irq_pending_ & kNmiIrq) {
                nec_interrupt(kNmiVector);
                irq_pending_ &= uint8_t(~kNmiIrq);
                nmi_state_ = IrqLine::Clear;
                clk(9);
            } else if (iff_ && irq_pending_) {
                nec_interrupt(irq_vector_);
                irq_pending_ &= uint8_t(~kIntIrq);
                clk(14);
            }
        }
        no_interrupt_ = false;
        if (halted_) {
            int remain = cycles - icount_;
            if (remain > 0) clk(remain);
            break;
        }
        int prev = icount_;
        execute_op(fetch());
        int delta = icount_ - prev;
        if (cycle_handler_ && delta) cycle_handler_(delta);
        do_prefetch(prev);
    }
    executed_ = icount_;
    return executed_;
}

}  // namespace dsp
