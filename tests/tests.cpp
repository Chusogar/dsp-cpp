// Minimal self contained checks for the ported components.
#include <cstdio>
#include <cstring>
#include <vector>

#include "cpu/hd63701.h"
#include "cpu/m6502.h"
#include "cpu/m6805.h"
#include "cpu/m6809.h"
#include "cpu/m68000.h"
#include "cpu/tms7000.h"
#include "cpu/z80.h"
#include "drivers/exelv.h"
#include "drivers/spectrum.h"
#include "machine/bagman_pal.h"
#include "machine/slapstic.h"
#include "machine/spectrum_tape.h"
#include "sound/ay8910.h"
#include "sound/msm5205.h"
#include "sound/okim6295.h"
#include "sound/pokey.h"
#include "sound/sn76496.h"
#include "sound/ym2151.h"
#include "video/atari_mo.h"
#include "video/gfx.h"
#include "video/tms3556.h"

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

std::vector<uint8_t> make_memory() { return std::vector<uint8_t>(0x10000, 0); }

dsp::Z80 make_cpu(std::vector<uint8_t>& memory) {
    dsp::Z80 cpu(3072000);
    cpu.set_memory_handlers([&memory](uint16_t address) { return memory[address]; },
                            [&memory](uint16_t address, uint8_t value) { memory[address] = value; });
    cpu.reset();
    return cpu;
}

void test_z80_arithmetic() {
    auto memory = make_memory();
    dsp::Z80 cpu = make_cpu(memory);
    // ld a,0x0f / add a,0x01 / daa
    const uint8_t program[] = {0x3e, 0x0f, 0xc6, 0x01, 0x27};
    std::memcpy(memory.data(), program, sizeof(program));
    cpu.run(7 + 7 + 4);
    check(cpu.a == 0x16, "daa converts 0x10 to bcd 0x16");
    check((cpu.f & dsp::Z80::NF) == 0, "daa keeps N clear after an add");
}

void test_z80_flags_and_blocks() {
    auto memory = make_memory();
    dsp::Z80 cpu = make_cpu(memory);
    // ld hl,0x2000 / ld de,0x3000 / ld bc,0x0004 / ldir / halt
    const uint8_t program[] = {0x21, 0x00, 0x20, 0x11, 0x00, 0x30, 0x01, 0x04,
                               0x00, 0xed, 0xb0, 0x76};
    std::memcpy(memory.data(), program, sizeof(program));
    for (int i = 0; i < 4; i++) memory[0x2000 + i] = uint8_t(0xa0 + i);
    cpu.run(200);
    check(memory[0x3000] == 0xa0 && memory[0x3003] == 0xa3, "ldir copies the block");
    check(cpu.halted, "halt stops execution");
}

void test_z80_interrupt() {
    auto memory = make_memory();
    dsp::Z80 cpu = make_cpu(memory);
    // im 1 / ei / nop ... and a rst 38 handler that sets a=0x42
    const uint8_t program[] = {0xed, 0x56, 0xfb, 0x00, 0x00, 0x00};
    std::memcpy(memory.data(), program, sizeof(program));
    memory[0x0038] = 0x3e;  // ld a,0x42
    memory[0x0039] = 0x42;
    memory[0x003a] = 0xed;  // reti
    memory[0x003b] = 0x4d;
    cpu.sp = 0xf000;
    cpu.run(12);  // im 1 + ei
    cpu.set_irq(dsp::IrqLine::Hold);
    cpu.run(40);
    check(cpu.a == 0x42, "mode 1 interrupt vectors through 0x0038");
}

void test_bagman_pal() {
    dsp::BagmanPal pal;
    pal.reset();
    uint8_t value = pal.read();
    check(value <= 0x3f, "the PAL only drives six data bits");
    // Toggling an input must be reflected in the outputs.
    pal.write(0, 0);
    pal.write(1, 0);
    uint8_t other = pal.read();
    check(other <= 0x3f, "the PAL stays within six data bits after writes");
    check(other != value || value == 0, "changing the PAL inputs changes its output");
}

void test_gfx_decode() {
    // Two 8x8 planes: plane 0 has the top row set, plane 1 the left column.
    std::vector<uint8_t> rom(16, 0);
    rom[0] = 0xff;  // first row of plane 0
    for (int y = 0; y < 8; y++) rom[8 + y] = 0x80;  // left column of plane 1

    dsp::GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 1;
    layout.planes = 2;
    layout.char_increment = 64;
    layout.plane_offsets = {0, 64};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0, 8, 16, 24, 32, 40, 48, 56};

    dsp::GfxSet gfx;
    gfx.decode(layout, rom);
    const uint8_t* pixels = gfx.element(0);
    check(pixels[0] == 3, "overlapping planes produce colour 3");
    check(pixels[1] == 2, "plane 0 alone produces colour 2");
    check(pixels[8] == 1, "plane 1 alone produces colour 1");
    check(pixels[9] == 0, "empty pixels stay transparent");

    layout.rotate_cw = true;
    gfx.decode(layout, rom);
    pixels = gfx.element(0);
    check(pixels[7] == 3, "rotation moves the top left pixel to the top right");
}

void test_palette_weights() {
    auto weights = dsp::compute_resistor_weights(0, 255, -1.0,
                                                 {{{1000, 470, 220}, 470, 0},
                                                  {{1000, 470, 220}, 470, 0},
                                                  {{470, 220}, 470, 0}});
    check(weights.size() == 3, "three resistor networks are returned");
    int white = dsp::combine_weights(weights[0], {1, 1, 1});
    check(white == 255, "all bits set gives full intensity");
    check(dsp::combine_weights(weights[0], {0, 0, 0}) == 0, "no bits set gives black");
}

void test_ay8910() {
    dsp::AY8910 psg(1536000);
    psg.reset();
    psg.control(7);
    psg.write(0x3e);  // enable channel A tone only
    psg.control(0);
    psg.write(0x40);  // period
    psg.control(8);
    psg.write(0x0f);  // maximum volume
    bool non_zero = false;
    for (int i = 0; i < 4410; i++) {
        if (psg.update() != 0) non_zero = true;
    }
    check(non_zero, "the PSG generates a tone");
}

dsp::M6809 make_m6809(std::vector<uint8_t>& memory) {
    dsp::M6809 cpu(1536000);
    cpu.set_memory_handlers(
        [&memory](uint16_t address) { return memory[address]; },
        [&memory](uint16_t address, uint8_t value) { memory[address] = value; });
    return cpu;
}

void test_m6809_reset_and_loads() {
    auto memory = make_memory();
    memory[0xfffe] = 0x10;  // reset vector -> 0x1000
    memory[0xffff] = 0x00;
    // lda #$12 / ldb #$34 / std $2000 / ldx #$1234 / leax 1,x
    const uint8_t program[] = {0x86, 0x12, 0xc6, 0x34, 0xfd, 0x20, 0x00,
                               0x8e, 0x12, 0x34, 0x30, 0x01};
    std::memcpy(memory.data() + 0x1000, program, sizeof(program));

    dsp::M6809 cpu = make_m6809(memory);
    cpu.reset();
    check(cpu.pc() == 0x1000, "m6809 starts at the reset vector");
    cpu.run(20);
    check(cpu.a == 0x12 && cpu.b == 0x34, "m6809 immediate loads");
    check(memory[0x2000] == 0x12 && memory[0x2001] == 0x34, "m6809 stores D big endian");
    check(cpu.x == 0x1235, "m6809 leax with a 5 bit indexed offset");
}

void test_m6809_branches_and_stack() {
    auto memory = make_memory();
    memory[0xfffe] = 0x10;
    // lds #$3000 / lda #$05 / pshs a / clra / puls a / cmpa #$05 / beq +2 / lda #$ff
    const uint8_t program[] = {0x10, 0xce, 0x30, 0x00, 0x86, 0x05, 0x34, 0x02, 0x4f,
                               0x35, 0x02, 0x81, 0x05, 0x27, 0x02, 0x86, 0xff};
    std::memcpy(memory.data() + 0x1000, program, sizeof(program));

    dsp::M6809 cpu = make_m6809(memory);
    cpu.reset();
    cpu.run(40);
    check(cpu.s == 0x3000, "m6809 pshs/puls balance the stack");
    check(cpu.a == 0x05, "m6809 beq skips the branch target");
}

void test_m6809_interrupts() {
    auto memory = make_memory();
    memory[0xfffe] = 0x10;
    memory[0xfff8] = 0x20;  // irq vector -> 0x2000
    memory[0xfff6] = 0x30;  // firq vector -> 0x3000
    // andcc #$af clears I and F, then a tight loop
    const uint8_t program[] = {0x10, 0xce, 0x40, 0x00, 0x1c, 0xaf, 0x12, 0x12, 0x12, 0x12};
    std::memcpy(memory.data() + 0x1000, program, sizeof(program));
    memory[0x2000] = 0x86;  // lda #$77
    memory[0x2001] = 0x77;
    memory[0x3000] = 0x86;  // lda #$55
    memory[0x3001] = 0x55;

    dsp::M6809 cpu = make_m6809(memory);
    cpu.reset();
    cpu.run(12);
    check(!cpu.cc.i && !cpu.cc.f, "m6809 andcc clears the interrupt masks");

    cpu.set_irq(dsp::IrqLine::Hold);
    cpu.run(4);
    check(cpu.a == 0x77, "m6809 takes the irq vector");
    check(cpu.cc.i && cpu.cc.e, "m6809 irq masks itself and stacks the full state");
    check(memory[0x3ffe] == 0x10 || memory[0x3fff] == 0x10, "m6809 irq stacks the return address");

    dsp::M6809 fast = make_m6809(memory);
    fast.reset();
    fast.run(12);
    fast.set_firq(dsp::IrqLine::Hold);
    fast.run(4);
    check(fast.a == 0x55, "m6809 takes the firq vector");
    check(!fast.cc.e, "m6809 firq stacks only pc and cc");
}

void test_sn76496() {
    dsp::SN76496 psg(14318180 / 8);
    bool silent = true;
    for (int i = 0; i < 128; i++) {
        if (psg.update() != 0) silent = false;
    }
    check(silent, "the SN76496 is silent after reset");

    psg.write(0x80 | 0x20);  // channel 0 frequency, low nibble
    psg.write(0x01);         // channel 0 frequency, high bits
    psg.write(0x90);         // channel 0 volume, maximum
    bool non_zero = false;
    bool changed = false;
    int32_t first = psg.update();
    for (int i = 0; i < 4410; i++) {
        int32_t sample = psg.update();
        if (sample != 0) non_zero = true;
        if (sample != first) changed = true;
    }
    check(non_zero, "the SN76496 generates a tone");
    check(changed, "the SN76496 tone oscillates");

    psg.write(0xe4);  // noise control: white noise, N/512
    psg.write(0xf0);  // noise volume, maximum
    bool noise = false;
    for (int i = 0; i < 4410; i++) {
        if (psg.update() != 0) noise = true;
    }
    check(noise, "the SN76496 generates noise");
}

std::vector<uint8_t> m68k_memory;

dsp::M68000 make_m68k() {
    m68k_memory.assign(0x10000, 0);
    dsp::M68000 cpu(7159090, dsp::M68000::Type::M68010);
    cpu.set_memory_handlers(
        [](uint32_t address) {
            return uint16_t((m68k_memory[address & 0xfffe] << 8) | m68k_memory[(address & 0xfffe) + 1]);
        },
        [](uint32_t address, uint16_t value) {
            m68k_memory[address & 0xfffe] = uint8_t(value >> 8);
            m68k_memory[(address & 0xfffe) + 1] = uint8_t(value);
        });
    return cpu;
}

void put_word(uint32_t address, uint16_t value) {
    m68k_memory[address] = uint8_t(value >> 8);
    m68k_memory[address + 1] = uint8_t(value);
}

void put_long(uint32_t address, uint32_t value) {
    put_word(address, uint16_t(value >> 16));
    put_word(address + 2, uint16_t(value));
}

void test_m68000_reset_and_moves() {
    dsp::M68000 cpu = make_m68k();
    put_long(0x0000, 0x00001000);  // initial stack pointer
    put_long(0x0004, 0x00000400);  // initial program counter
    put_word(0x0400, 0x7aff);      // moveq #-1,d5
    put_word(0x0402, 0x7c2a);      // moveq #$2a,d6
    put_word(0x0404, 0x0686);      // addi.l #$100,d6
    put_long(0x0406, 0x00000100);
    put_word(0x040a, 0x60fe);      // bra.s *
    cpu.reset();
    check(cpu.a[7].l == 0x1000, "the 68000 loads the stack pointer from vector 0");
    check(cpu.pc() == 0x400, "the 68000 loads the program counter from vector 1");
    cpu.run(200);
    check(cpu.d[5].l == 0xffffffff, "moveq sign extends into the whole register");
    check(cpu.d[6].l == 0x12a, "addi.l adds a long immediate");
}

void test_m68000_branches_and_subroutines() {
    dsp::M68000 cpu = make_m68k();
    put_long(0x0000, 0x00001000);
    put_long(0x0004, 0x00000400);
    put_word(0x0400, 0x7003);      // moveq #3,d0
    put_word(0x0402, 0x6106);      // bsr.s $40a
    put_word(0x0404, 0x60fe);      // bra.s *  (endless loop)
    put_word(0x040a, 0x5280);      // addq.l #1,d0
    put_word(0x040c, 0x4e75);      // rts
    cpu.reset();
    cpu.run(200);
    check(cpu.d[0].l == 4, "bsr/rts execute the subroutine once");
    check(cpu.a[7].l == 0x1000, "rts restores the stack pointer");
}

void test_m68000_interrupt() {
    dsp::M68000 cpu = make_m68k();
    put_long(0x0000, 0x00001000);
    put_long(0x0004, 0x00000400);
    put_long(0x0078, 0x00000500);  // autovector for level 6
    put_word(0x0400, 0x027c);      // andi #$f8ff,sr (interrupt mask 0)
    put_word(0x0402, 0xf8ff);
    put_word(0x0404, 0x60fe);      // bra.s *
    put_word(0x0500, 0x7201);      // moveq #1,d1
    put_word(0x0502, 0x60fe);      // bra.s *
    cpu.reset();
    cpu.run(40);
    cpu.set_irq(6, dsp::IrqLine::Assert);
    cpu.run(200);
    check(cpu.d[1].l == 1, "the 68000 takes an autovectored level 6 interrupt");
}

std::vector<uint8_t> m6502_memory;

dsp::M6502 make_m6502() {
    m6502_memory.assign(0x10000, 0);
    dsp::M6502 cpu(1789772);
    cpu.set_memory_handlers([](uint16_t address) { return m6502_memory[address]; },
                            [](uint16_t address, uint8_t value) { m6502_memory[address] = value; });
    return cpu;
}

void load_6502(uint16_t address, const std::vector<uint8_t>& code) {
    for (size_t i = 0; i < code.size(); i++) m6502_memory[address + i] = code[i];
    m6502_memory[0xfffc] = uint8_t(address);
    m6502_memory[0xfffd] = uint8_t(address >> 8);
}

void test_m6502_arithmetic() {
    dsp::M6502 cpu = make_m6502();
    // lda #$40 / adc #$40 / sbc #$01
    load_6502(0x1000, {0xa9, 0x40, 0x18, 0x69, 0x40, 0x38, 0xe9, 0x01});
    cpu.reset();
    check(cpu.pc() == 0x1000, "the 6502 loads the reset vector");
    cpu.run(6);
    check(cpu.a == 0x80, "adc adds the accumulator");
    check(cpu.p.v, "adc reports the signed overflow");
    cpu.run(4);
    check(cpu.a == 0x7f, "sbc subtracts with borrow");
}

void test_m6502_stack_and_interrupts() {
    dsp::M6502 cpu = make_m6502();
    // jsr $1010 / jmp * ; at $1010: lda #$7f / rts
    load_6502(0x1000, {0x20, 0x10, 0x10, 0x4c, 0x03, 0x10});
    m6502_memory[0x1010] = 0xa9;
    m6502_memory[0x1011] = 0x7f;
    m6502_memory[0x1012] = 0x60;
    // nmi handler at $1100: inc $20 / rti
    m6502_memory[0xfffa] = 0x00;
    m6502_memory[0xfffb] = 0x11;
    m6502_memory[0x1100] = 0xe6;
    m6502_memory[0x1101] = 0x20;
    m6502_memory[0x1102] = 0x40;
    cpu.reset();
    cpu.run(20);
    check(cpu.a == 0x7f, "jsr/rts run the subroutine");
    cpu.set_nmi(dsp::IrqLine::Assert);
    cpu.run(40);
    check(m6502_memory[0x20] == 1, "the 6502 takes the NMI once");
}

void test_m6502_pushed_flags() {
    dsp::M6502 cpu = make_m6502();
    // sei / php / jmp *  -- the pushed byte must have both unused bits set
    load_6502(0x1000, {0x78, 0x08, 0x4c, 0x02, 0x10});
    // irq handler at $1100: pla / sta $30 / jmp *
    m6502_memory[0xfffe] = 0x00;
    m6502_memory[0xffff] = 0x11;
    m6502_memory[0x1100] = 0x68;
    m6502_memory[0x1101] = 0x85;
    m6502_memory[0x1102] = 0x30;
    m6502_memory[0x1103] = 0x4c;
    m6502_memory[0x1104] = 0x03;
    m6502_memory[0x1105] = 0x11;
    cpu.reset();
    cpu.run(20);
    check((m6502_memory[0x1fd] & 0x30) == 0x30, "php pushes the break and unused bits");
    // cli / jmp * so the pending IRQ is taken
    load_6502(0x2000, {0x58, 0x4c, 0x01, 0x20});
    cpu.reset();
    cpu.set_irq(dsp::IrqLine::Assert);
    cpu.run(60);
    check((m6502_memory[0x30] & 0x30) == 0x20,
          "an interrupt pushes the flags with the break bit clear");
}

void test_slapstic() {
    dsp::Slapstic slapstic(104, nullptr);
    slapstic.reset();
    check(slapstic.current_bank() == 3, "the slapstic starts on its default bank");
    // The standard sequence is an access to offset 0 followed by the bank value.
    const uint16_t banks[4] = {0x0020, 0x0028, 0x0030, 0x0038};
    for (uint8_t wanted = 0; wanted < 4; wanted++) {
        slapstic.tweak(0);
        check(slapstic.tweak(banks[wanted]) == wanted, "the slapstic selects the requested bank");
    }
    // Without the unlock access the bank must not change.
    slapstic.tweak(0x1234);
    check(slapstic.tweak(banks[1]) == 3, "the slapstic ignores bank values while disabled");
}

void test_ym2151() {
    dsp::YM2151 ym(14318180 / 4);
    ym.reset();
    check((ym.status() & 0x03) == 0, "the YM2151 clears its timer flags on reset");
    ym.write_reg(0x14, 0x00);
    ym.write_reg(0x10, 0x10);  // timer A period
    ym.write_reg(0x14, 0x05);  // start timer A, enable its flag
    ym.run_timers(14318180 / 4);
    check((ym.status() & 0x01) != 0, "the YM2151 raises the timer A flag");
    ym.write_reg(0x14, 0x15);  // reset the flag
    check((ym.status() & 0x01) == 0, "the YM2151 clears the timer A flag on request");

    ym.write_reg(0x20, 0xc7);  // both outputs, algorithm 7
    ym.write_reg(0x28, 0x4a);  // key code
    ym.write_reg(0x60, 0x00);  // maximum level for every operator
    ym.write_reg(0x68, 0x00);
    ym.write_reg(0x70, 0x00);
    ym.write_reg(0x78, 0x00);
    for (int reg = 0x80; reg < 0xa0; reg += 8) ym.write_reg(uint8_t(reg), 0x1f);
    ym.write_reg(0x08, 0x78);  // key on
    bool audible = false;
    for (int i = 0; i < 4410; i++) {
        if (ym.update() != 0) audible = true;
    }
    check(audible, "the YM2151 produces sound after a key on");
}

void test_pokey() {
    dsp::Pokey pokey(14318180 / 8);
    pokey.reset();
    bool silent = true;
    for (int i = 0; i < 128; i++) {
        pokey.run(32);
        if (pokey.update() != 0) silent = false;
    }
    check(silent, "the POKEY is silent after reset");

    pokey.write(0x08, 0x00);  // audctl
    pokey.write(0x00, 0x40);  // channel 0 frequency
    pokey.write(0x01, 0xaf);  // channel 0: pure tone, maximum volume
    bool audible = false;
    int32_t first = pokey.update();
    bool changed = false;
    for (int i = 0; i < 8192; i++) {
        pokey.run(16);
        int32_t sample = pokey.update();
        if (sample != 0) audible = true;
        if (sample != first) changed = true;
    }
    check(audible, "the POKEY generates a tone");
    check(changed, "the POKEY tone oscillates");
}

void test_atari_motion_objects() {
    std::vector<uint16_t> sprite_ram(0x1000, 0);
    std::vector<uint16_t> slip_ram(0x40, 0);
    dsp::AtariMotionObjects::Config config;
    config.tile_width = 8;
    config.tile_height = 8;
    config.linked = true;
    config.split = true;
    config.link_entry = {0, 0, 0, 0x03ff};
    config.code_entry = {{0x7fff, 0, 0, 0}, {0, 0, 0, 0}};
    config.color_entry = {{0, 0x000f, 0, 0}, {0, 0, 0, 0}};
    config.xpos_entry = {0, 0xff80, 0, 0};
    config.ypos_entry = {0, 0, 0xff80, 0};
    config.width_entry = {0, 0, 0x0038, 0};
    config.height_entry = {0, 0, 0x0007, 0};
    config.hflip_entry = {0, 0, 0x0040, 0};
    // An 8x8 object with code 5, colour 2 at (16, 24). The list ends with an entry
    // linked to itself, which the hardware uses as the terminator and never draws.
    sprite_ram[0x000] = 0x0005;          // code
    sprite_ram[0x400] = 0x0002 | (16 << 7);  // colour and horizontal position
    sprite_ram[0x800] = 0x1e0 << 7;      // vertical position (counted upwards)
    sprite_ram[0xc00] = 0x0001;          // link to the terminator
    sprite_ram[0xc01] = 0x0001;          // the terminator links to itself

    dsp::AtariMotionObjects objects(config, slip_ram.data(), sprite_ram.data(), 512, 256);
    int drawn = 0;
    int last_code = -1, last_color = -1, last_x = -1, last_y = -1;
    objects.draw(0, 0, 0, [&](int code, int color, bool, bool, int x, int y) {
        drawn++;
        last_code = code;
        last_color = color;
        last_x = x;
        last_y = y;
    });
    check(drawn == 1, "the motion object list stops on the self link");
    check(last_code == 5, "the motion object code is extracted");
    check(last_color == 0x20, "the motion object colour becomes a palette offset");
    check(last_x == 16 && last_y == 24, "the motion object position is extracted");
}

// Builds a HD63701 whose internal ROM holds `program` at $c000 and whose reset
// vector points there. External memory is a plain 64 KB buffer.
dsp::HD63701 make_hd63701(std::vector<uint8_t>& memory, const std::vector<uint8_t>& program) {
    dsp::HD63701 cpu(1000000);
    cpu.set_memory_handlers([&memory](uint16_t address) { return memory[address]; },
                            [&memory](uint16_t address, uint8_t value) { memory[address] = value; });
    std::array<uint8_t, 0x4000>& rom = cpu.internal_rom();
    rom.fill(0);
    for (size_t index = 0; index < program.size(); index++) rom[index] = program[index];
    rom[0x3ffe] = 0xc0;  // reset vector
    rom[0x3fff] = 0x00;
    cpu.reset();
    return cpu;
}

void test_hd63701_reset_and_internal_ram() {
    auto memory = make_memory();
    // ldaa #$12 / staa $50 / ldab $50 / addb #$01
    dsp::HD63701 cpu = make_hd63701(memory, {0x86, 0x12, 0x97, 0x50, 0xd6, 0x50, 0xcb, 0x01});
    check(cpu.pc() == 0xc000, "the HD63701 takes the reset vector from the internal ROM");
    cpu.run(20);
    check(cpu.a == 0x12, "the HD63701 loads an immediate value");
    check(cpu.b == 0x13, "the HD63701 stores to and reads back its internal RAM");
    check(memory[0x50] == 0, "internal RAM is not visible on the external bus");
}

// The MCU has 0x800 bytes of address space, with its I/O ports at the bottom.
dsp::M6805 make_m6805(std::vector<uint8_t>& memory, const std::vector<uint8_t>& program) {
    std::copy(program.begin(), program.end(), memory.begin() + 0x100);
    memory[0x7fe] = 0x01;  // reset vector, $0100
    memory[0x7ff] = 0x00;
    memory[0x7fa] = 0x02;  // interrupt vector, $0200
    memory[0x7fb] = 0x00;
    dsp::M6805 cpu(3000000, dsp::M6805::Type::M68705);
    cpu.set_memory_handlers([&memory](uint16_t address) { return memory[address & 0x7ff]; },
                            [&memory](uint16_t address, uint8_t value) {
                                memory[address & 0x7ff] = value;
                            });
    cpu.reset();
    return cpu;
}

void test_m6805_arithmetic() {
    auto memory = make_memory();
    // lda #$10 / add #$05 / sta $50 / ldx #$03 / incx / stx $51
    dsp::M6805 cpu = make_m6805(
        memory, {0xa6, 0x10, 0xab, 0x05, 0xb7, 0x50, 0xae, 0x03, 0x5c, 0xbf, 0x51});
    check(cpu.pc() == 0x0100, "the 6805 takes the reset vector from $fffe");
    cpu.run(30);
    check(memory[0x50] == 0x15, "the 6805 adds and stores through direct addressing");
    check(memory[0x51] == 0x04, "incx increments the index register");
}

void test_m6805_bit_branches() {
    auto memory = make_memory();
    memory[0x40] = 0x02;
    // brset 1,$40,+1 / clra / lda #$77 / bclr 1,$40
    dsp::M6805 cpu = make_m6805(memory, {0x02, 0x40, 0x01, 0x4f, 0xa6, 0x77, 0x13, 0x40});
    cpu.run(30);
    check(cpu.a == 0x77, "brset skips the clra when the bit is set");
    check(memory[0x40] == 0x00, "bclr clears the bit of a direct address");
}

void test_m6805_interrupt() {
    auto memory = make_memory();
    // cli / nop / nop ...  with rti at the interrupt vector
    dsp::M6805 cpu = make_m6805(memory, {0x9a, 0x9d, 0x9d, 0x9d});
    memory[0x200] = 0x9d;  // nop
    memory[0x201] = 0x80;  // rti
    cpu.a = 0x33;
    cpu.run(4);  // cli and the first nop
    cpu.set_irq(dsp::IrqLine::Assert);
    cpu.run(1);  // the interrupt plus the nop of its handler
    check(cpu.pc() == 0x0201, "an unmasked interrupt jumps through the vector at $fffa");
    check(cpu.cc.i, "the interrupt masks further interrupts");
    cpu.run(1);
    check(cpu.pc() == 0x0102 && cpu.a == 0x33, "rti restores the context of the interrupt");
}

void test_hd63701_ports() {
    auto memory = make_memory();
    // ldaa #$ff / staa $00 (DDR1 = all outputs) / ldaa #$5a / staa $02 (port 1)
    // ldab $03 (port 2, an input because DDR2 stays at zero)
    dsp::HD63701 cpu = make_hd63701(
        memory, {0x86, 0xff, 0x97, 0x00, 0x86, 0x5a, 0x97, 0x02, 0xd6, 0x03});
    uint8_t written = 0;
    int writes = 0;
    cpu.set_port_write(0, [&](uint8_t value) {
        written = value;
        writes++;
    });
    cpu.set_port_read(1, []() { return uint8_t(0xa5); });
    cpu.run(30);
    check(writes > 0 && written == 0x5a, "a port write reaches the output handler");
    check(cpu.b == 0xa5, "a port read comes from the input handler");
}

void test_hd63701_hd63701_only_opcodes() {
    auto memory = make_memory();
    // ldaa #$12 / ldab #$34 / ldx #$abcd / xgdx / oim #$0f,$60 / aim #$f1,$60 /
    // tim #$01,$60
    dsp::HD63701 cpu = make_hd63701(memory, {0x86, 0x12, 0xc6, 0x34, 0xce, 0xab, 0xcd, 0x18,
                                             0x72, 0x0f, 0x60, 0x71, 0xf1, 0x60, 0x7b, 0x01, 0x60});
    cpu.run(40);
    check(cpu.a == 0xab && cpu.b == 0xcd, "xgdx moves X into D");
    // The internal RAM byte at $60 starts at zero: oim sets the low nibble and
    // aim keeps only the bits also present in $f1.
    check(!cpu.cc.z, "tim reports the tested bit as set");
}

void test_hd63701_interrupts() {
    auto memory = make_memory();
    // cli / nop ... with the IRQ vector ($fff8) pointing at $c020.
    dsp::HD63701 cpu(1000000);
    cpu.set_memory_handlers([&memory](uint16_t address) { return memory[address]; },
                            [&memory](uint16_t address, uint8_t value) { memory[address] = value; });
    std::array<uint8_t, 0x4000>& rom = cpu.internal_rom();
    rom.fill(0x01);  // nop
    rom[0x0000] = 0x0e;  // cli
    rom[0x0020] = 0x20;  // bra *, so the handler stays put
    rom[0x0021] = 0xfe;
    rom[0x0040] = 0x20;
    rom[0x0041] = 0xfe;
    rom[0x3ff8] = 0xc0;
    rom[0x3ff9] = 0x20;
    rom[0x3ffc] = 0xc0;  // NMI vector
    rom[0x3ffd] = 0x40;
    rom[0x3ffe] = 0xc0;
    rom[0x3fff] = 0x00;
    cpu.reset();
    cpu.sp = 0x01ff;
    cpu.run(4);
    check(!cpu.cc.i, "cli clears the interrupt mask");
    cpu.set_irq(dsp::IrqLine::Assert);
    cpu.run(20);
    check(cpu.pc() == 0xc020 || cpu.pc() == 0xc022,
          "an IRQ jumps through the vector at $fff8");

    cpu.reset();
    cpu.sp = 0x01ff;
    cpu.set_nmi(dsp::IrqLine::Assert);
    cpu.run(20);
    check(cpu.pc() == 0xc040 || cpu.pc() == 0xc042,
          "an NMI jumps through the vector at $fffc even with I set");
}

void test_msm5205() {
    dsp::MSM5205 chip(384000, 48, 4);
    check(chip.sample_frequency() == 8000, "the MSM5205 prescaler sets the sample rate");
    std::vector<uint8_t> rom(0x100, 0);
    // A ramp of maximum positive steps followed by silence.
    for (int index = 0; index < 8; index++) rom[index] = 0x77;
    chip.set_rom(rom);
    chip.reset();
    check(chip.idle(), "the MSM5205 starts idle");
    chip.set_start(0);
    chip.set_end(0x40);
    chip.set_reset(false);
    for (int index = 0; index < 8; index++) chip.vclk();
    check(!chip.idle(), "the MSM5205 plays while inside the sample");
    check(chip.output() > 0, "positive ADPCM nibbles push the output up");
    chip.set_reset(true);
    check(chip.output() == 0, "resetting the MSM5205 silences it");
}

void test_okim6295() {
    dsp::OKIM6295 chip(1056000, true);
    check(chip.sample_frequency() == 8000, "pin 7 high selects the /132 divider");
    std::vector<uint8_t> rom(0x1000, 0);
    // Sample 1: table entry at $08 holds the start and end addresses.
    rom[0x08] = 0x00;
    rom[0x09] = 0x02;
    rom[0x0a] = 0x00;  // start $000200
    rom[0x0b] = 0x00;
    rom[0x0c] = 0x02;
    rom[0x0d] = 0x20;  // end $000220
    for (int index = 0x200; index <= 0x220; index++) rom[index] = 0x77;
    chip.set_rom(rom);
    chip.reset();
    check(chip.update() == 0, "an idle OKI6295 is silent");
    chip.write(0x81);  // select sample 1
    chip.write(0x10);  // start it on voice 1 at full volume
    check((chip.read() & 0x01) != 0, "the status byte reports the busy voice");
    int32_t sample = 0;
    for (int index = 0; index < 16; index++) sample = chip.update();
    check(sample != 0, "the OKI6295 decodes the selected sample");
    chip.write(0x08);  // silence voice 1
    check((chip.read() & 0x01) == 0, "the silence command stops the voice");
}

// A .tap image with a single block, the format the ROM loader expects.
std::vector<uint8_t> make_tap(const std::vector<uint8_t>& block) {
    std::vector<uint8_t> tape;
    tape.push_back(uint8_t(block.size() & 0xff));
    tape.push_back(uint8_t(block.size() >> 8));
    tape.insert(tape.end(), block.begin(), block.end());
    return tape;
}

void test_spectrum_tape() {
    dsp::SpectrumTape tape;
    std::string error;
    // Flag $00 marks a header, so the long pilot tone is used.
    check(tape.load_from_memory(make_tap({0x00, 0xff, 0xff}), &error), "a .tap image loads");
    check(tape.loaded() && !tape.playing(), "the tape starts stopped");
    tape.advance(1000000);
    check(!tape.ear(), "a stopped tape does not move");

    tape.set_playing(true);
    int edges = 0;
    bool level = tape.ear();
    for (int index = 0; index < dsp::SpectrumTape::kHeaderPilotPulses; index++) {
        tape.advance(dsp::SpectrumTape::kPilotPulse);
        if (tape.ear() != level) {
            level = tape.ear();
            edges++;
        }
    }
    check(edges == dsp::SpectrumTape::kHeaderPilotPulses,
          "the header pilot tone is 8063 pulses long");

    // Sync pair, then the first bit of the $00 flag byte as a short pulse.
    tape.advance(dsp::SpectrumTape::kSync1Pulse);
    tape.advance(dsp::SpectrumTape::kSync2Pulse);
    level = tape.ear();
    tape.advance(dsp::SpectrumTape::kBit0Pulse);
    check(tape.ear() != level, "a zero bit is two 855 T pulses");

    dsp::SpectrumTape empty;
    check(!empty.load_from_memory({0x01}, &error), "a truncated image is rejected");

    // A block closes with a short pulse: without that edge the ROM loader never
    // sees the end of the last bit and waits forever.
    dsp::SpectrumTape single;
    check(single.load_from_memory(make_tap({0xff, 0x00}), &error), "a data block loads");
    single.set_playing(true);
    single.advance(dsp::SpectrumTape::kDataPilotPulses * dsp::SpectrumTape::kPilotPulse +
                   dsp::SpectrumTape::kSync1Pulse + dsp::SpectrumTape::kSync2Pulse +
                   16 * dsp::SpectrumTape::kBit1Pulse + 16 * dsp::SpectrumTape::kBit0Pulse);
    level = single.ear();
    single.advance(dsp::SpectrumTape::kTailPulse);
    check(single.ear() != level && !single.ear(), "a block ends with a closing edge");
    single.advance(dsp::SpectrumTape::kPauseCycles);
    check(!single.ear() && single.finished(), "the tape stops after the last block");
}

// Measures the pulses a tape emits until `cycles` T states have gone by.
std::vector<int> tape_pulses(dsp::SpectrumTape& tape, int cycles) {
    std::vector<int> pulses;
    bool level = tape.ear();
    int length = 0;
    for (int cycle = 0; cycle < cycles; cycle++) {
        tape.advance(1);
        length++;
        if (tape.ear() != level) {
            level = tape.ear();
            pulses.push_back(length);
            length = 0;
        }
    }
    return pulses;
}

void test_spectrum_tzx() {
    auto tzx = [](const std::vector<uint8_t>& blocks) {
        std::vector<uint8_t> tape = {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1a, 0x01, 0x14};
        tape.insert(tape.end(), blocks.begin(), blocks.end());
        return tape;
    };
    std::string error;

    // $30 text and $32 archive info are skipped, $12 is a pure tone of four
    // 100 T pulses and $13 a sequence of two pulses.
    dsp::SpectrumTape tape;
    check(tape.load_from_memory(tzx({0x30, 0x02, 'h', 'i',                        //
                                     0x32, 0x03, 0x00, 0x00, 0x00, 0x00,          //
                                     0x12, 0x64, 0x00, 0x04, 0x00,                //
                                     0x13, 0x02, 0xc8, 0x00, 0x2c, 0x01}),        //
                                &error),
          "a .tzx image with tone and pulse blocks loads");
    check(tape.block_count() == 2, "the text and archive info blocks are skipped");
    tape.set_playing(true);
    const std::vector<int> pulses = tape_pulses(tape, 4 * 100 + 200 + 300);
    check(pulses == std::vector<int>({100, 100, 100, 100, 200, 300}),
          "the pure tone and the pulse sequence keep their lengths");

    // $14 pure data: $c0 and $00 with only two bits used in the last byte.
    dsp::SpectrumTape pure;
    check(pure.load_from_memory(tzx({0x14, 0x0a, 0x00, 0x14, 0x00, 0x02, 0x00, 0x00,  //
                                     0x02, 0x00, 0x00, 0xc0, 0x00}),
                                &error),
          "a pure data block loads");
    pure.set_playing(true);
    const std::vector<int> bits = tape_pulses(pure, 8 * 2 * 10 + 2 * 2 * 20);
    check(bits.size() == 20, "only the used bits of the last byte are played");
    check(bits[0] == 20 && bits[3] == 20 && bits[4] == 10 && bits[19] == 10,
          "the one bits of $c0 are 20 T and the zero bits 10 T");

    // $15 direct recording: the level follows the sample bits, no toggling.
    dsp::SpectrumTape direct;
    check(direct.load_from_memory(tzx({0x15, 0x0a, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0xf0}),
                                  &error),
          "a direct recording block loads");
    direct.set_playing(true);
    check(direct.ear(), "the first sample of $f0 is high");
    direct.advance(40);
    check(!direct.ear(), "the fifth sample of $f0 is low");

    // $24/$25 repeat the pure tone inside the loop three times.
    dsp::SpectrumTape loop;
    check(loop.load_from_memory(tzx({0x24, 0x03, 0x00,              //
                                     0x12, 0x64, 0x00, 0x02, 0x00,  //
                                     0x25}),
                                &error),
          "a loop block loads");
    loop.set_playing(true);
    check(tape_pulses(loop, 6 * 100).size() == 6, "the loop plays its body three times");
}

void test_spectrum_ula() {
    dsp::Spectrum48k spectrum;
    dsp::MachineInputs inputs;
    inputs.keys[size_t(dsp::Key::A)] = true;
    inputs.keys[size_t(dsp::Key::Enter)] = true;
    spectrum.set_inputs(inputs);
    // A is bit 0 of the half row selected by A9, enter bit 0 of the one on A14.
    check((spectrum.io_in(0xfdfe) & 0x01) == 0, "the ULA reports the pressed key");
    check((spectrum.io_in(0xfbfe) & 0x01) != 0, "other half rows stay high");
    check((spectrum.io_in(0xbffe) & 0x01) == 0, "enter is read on A14");

    dsp::MachineInputs released;
    spectrum.set_inputs(released);
    check((spectrum.io_in(0xfdfe) & 0x1f) == 0x1f, "releasing the key frees the matrix");

    spectrum.io_out(0xfe, 0x10);
    check((spectrum.io_in(0xfffe) & 0x40) != 0, "the speaker bit comes back on EAR");
    spectrum.io_out(0xfe, 0x00);
    check((spectrum.io_in(0xfffe) & 0x40) == 0, "and clears with it");

    dsp::MachineInputs joystick;
    joystick.player1.right = true;
    joystick.player1.button1 = true;
    spectrum.set_inputs(joystick);
    check(spectrum.io_in(0x001f) == 0x11, "the Kempston joystick answers on A5 low");
}

void test_tms7000_mov_add_call() {
    dsp::Tms7000 cpu(4915200, dsp::Tms7000::Chip::Tms7020);
    std::vector<uint8_t> rom(0x800, 0x00);
    // Reset vector at $FFFE -> $F800
    rom[0x7fe] = 0xf8;
    rom[0x7ff] = 0x00;
    // MOV %$12,A / ADD %$34,A / CALL $F810 / IDLE
    // $F800:
    rom[0x000] = 0x22;
    rom[0x001] = 0x12;  // MOV %12, A
    rom[0x002] = 0x28;
    rom[0x003] = 0x34;  // ADD %34, A
    rom[0x004] = 0x8e;
    rom[0x005] = 0xf8;
    rom[0x006] = 0x10;  // CALL $F810
    rom[0x007] = 0x01;  // IDLE
    // $F810: MOV %$42, B / RETS
    rom[0x010] = 0x52;
    rom[0x011] = 0x42;
    rom[0x012] = 0x0a;
    cpu.set_internal_rom(rom.data(), rom.size());
    cpu.reset();
    cpu.run(200);
    check(cpu.a() == 0x46, "tms7000 MOV+ADD leaves 0x46 in A");
    check(cpu.b() == 0x42, "tms7000 CALL/RETS runs the callee");
    check(cpu.idle(), "tms7000 IDLE stops at the idle opcode");
}

void test_tms7000_lvdp_and_int1() {
    dsp::Tms7000 cpu(4915200, dsp::Tms7000::Chip::Tms7020);
    cpu.set_exl_lvdp(true);
    uint8_t vram_port = 0x5a;
    cpu.set_memory_handlers([&](uint16_t a) -> uint8_t {
        if (a == 0x0124) return vram_port;
        if (a == 0x0128) return 0xff;
        return 0xff;
    }, [](uint16_t, uint8_t) {});
    std::vector<uint8_t> rom(0x800, 0x00);
    rom[0x7fe] = 0xf8;
    rom[0x7ff] = 0x00;
    rom[0x000] = 0xd7;
    rom[0x001] = 0x28;  // LVDP (discards the immediate)
    rom[0x002] = 0x01;  // IDLE
    cpu.set_internal_rom(rom.data(), rom.size());
    cpu.reset();
    cpu.run(40);
    check(cpu.a() == 0x5a, "EXL LVDP reads the TMS3556 VRAM port into A");

    dsp::Tms7000 irq_cpu(4915200, dsp::Tms7000::Chip::Tms7020);
    std::vector<uint8_t> irq_rom(0x800, 0x00);
    irq_rom[0x7fe] = 0xf8;
    irq_rom[0x7ff] = 0x00;
    irq_rom[0x7fc] = 0xf8;  // INT1 vector $FFFC
    irq_rom[0x7fd] = 0x20;
    irq_rom[0x000] = 0x05;  // EINT
    irq_rom[0x001] = 0xa2;
    irq_rom[0x002] = 0x01;
    irq_rom[0x003] = 0x00;  // MOVP %$01, P0  (enable INT1)
    irq_rom[0x004] = 0x01;  // IDLE
    irq_rom[0x020] = 0x22;
    irq_rom[0x021] = 0xaa;  // MOV %$AA, A
    irq_rom[0x022] = 0x01;
    irq_cpu.set_internal_rom(irq_rom.data(), irq_rom.size());
    irq_cpu.reset();
    irq_cpu.run(40);
    irq_cpu.set_input_line(dsp::Tms7000::kInt1, dsp::IrqLine::Hold);
    irq_cpu.run(40);
    check(irq_cpu.a() == 0xaa, "tms7000 INT1 vectors through $FFFC");
}

void test_tms3556_background() {
    dsp::Tms3556 vdp;
    vdp.reset();
    vdp.reg_w(0x07);  // select CM4
    vdp.reg_w(0xe0);  // background colour 7 (white)
    for (int i = 0; i < dsp::Tms3556::kScanlines * 2; i++) vdp.interrupt();
    const uint32_t* fb = vdp.framebuffer();
    const uint32_t white = dsp::Tms3556::rgb3(7);
    check(fb[10] == white, "tms3556 off-mode fills the border with CM4 background");
}

void test_exelv_dummy_bios() {
    dsp::Exelv missing(dsp::Exelv::Model::Exl100);
    std::string error;
    check(!missing.init("/tmp/dsp-exl-missing-bios", &error),
          "EXL-100 init fails without the TMS7020 BIOS");

    dsp::Exelv machine(dsp::Exelv::Model::Exl100);
    machine.install_dummy_bios();
    machine.reset();
    check(machine.bios_loaded(), "dummy EXL BIOS installs");
    check(machine.screen_width() == 336 && machine.screen_height() == 252,
          "EXL-100 reports the TMS3556 336x252 framebuffer");
    for (int i = 0; i < 2; i++) machine.run_frame();
    check(machine.debug_pc() >= 0xf800, "dummy BIOS idles in internal ROM");
}

}  // namespace

int main() {
    test_z80_arithmetic();
    test_z80_flags_and_blocks();
    test_z80_interrupt();
    test_bagman_pal();
    test_gfx_decode();
    test_palette_weights();
    test_ay8910();
    test_m6809_reset_and_loads();
    test_m6809_branches_and_stack();
    test_m6809_interrupts();
    test_sn76496();
    test_m68000_reset_and_moves();
    test_m68000_branches_and_subroutines();
    test_m68000_interrupt();
    test_m6502_arithmetic();
    test_m6502_stack_and_interrupts();
    test_m6502_pushed_flags();
    test_slapstic();
    test_ym2151();
    test_pokey();
    test_atari_motion_objects();
    test_hd63701_reset_and_internal_ram();
    test_hd63701_ports();
    test_hd63701_hd63701_only_opcodes();
    test_hd63701_interrupts();
    test_m6805_arithmetic();
    test_m6805_bit_branches();
    test_m6805_interrupt();
    test_msm5205();
    test_okim6295();
    test_spectrum_tape();
    test_spectrum_tzx();
    test_spectrum_ula();
    test_tms7000_mov_add_call();
    test_tms7000_lvdp_and_int1();
    test_tms3556_background();
    test_exelv_dummy_bios();
    if (failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", failures);
    return 1;
}
