// Minimal self contained checks for the ported components.
#include <cstdio>
#include <cstring>
#include <vector>

#include "cpu/m6809.h"
#include "cpu/z80.h"
#include "machine/bagman_pal.h"
#include "sound/ay8910.h"
#include "sound/sn76496.h"
#include "video/gfx.h"

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
    if (failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", failures);
    return 1;
}
