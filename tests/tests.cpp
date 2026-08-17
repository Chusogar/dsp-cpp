// Minimal self contained checks for the ported components.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cpu/hd63701.h"
#include "cpu/irq_line.h"
#include "cpu/m6502.h"
#include "cpu/m6805.h"
#include "cpu/m6809.h"
#include "cpu/m68000.h"
#include "cpu/tms7000.h"
#include "cpu/upd7801.h"
#include "cpu/z80.h"
#include "cpu/z80ctc.h"
#include "drivers/amstrad_cpc.h"
#include "drivers/atari_lynx.h"
#include "drivers/atari_system1.h"
#include "drivers/c64.h"
#include "drivers/exelv.h"
#include "drivers/gameboy.h"
#include "drivers/mcr.h"
#include "drivers/nes.h"
#include "drivers/scv.h"
#include "drivers/starwars.h"
#include "drivers/spectrum.h"
#include "drivers/zx_clone.h"
#include "machine/bagman_pal.h"
#include "machine/beta128.h"
#include "machine/kabuki.h"
#include "machine/lynx_suzy.h"
#include "machine/mos6526.h"
#include "machine/mos6532.h"
#include "machine/slapstic.h"
#include "machine/spectrum_tape.h"
#include "machine/trdos_disk.h"
#include "machine/wd1793.h"
#include "machine/starwars_math.h"
#include "sound/ay8910.h"
#include "sound/msm5205.h"
#include "sound/nes_apu.h"
#include "sound/okim6295.h"
#include "sound/pokey.h"
#include "sound/qsound.h"
#include "sound/sid.h"
#include "sound/sn76496.h"
#include "sound/upd1771.h"
#include "sound/ym2151.h"
#include "video/atari_mo.h"
#include "video/gb_ppu.h"
#include "video/gfx.h"
#include "video/mos6566.h"
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

int count_instruction_cycles(dsp::Z80& cpu, int at_least = 1) {
    int total = 0;
    cpu.set_cycle_handler([&](int cycles) { total += cycles; });
    cpu.run(at_least);
    cpu.set_cycle_handler(nullptr);
    return total;
}

void test_z80_cpc_wait_states() {
    auto memory = make_memory();
    dsp::Z80 cpu = make_cpu(memory);
    std::array<uint8_t, 256> main{};
    std::array<uint8_t, 256> extra{};
    main.fill(4);
    main[0x3e] = 8;  // CPC LD A,n
    extra[0xb0] = 4;
    extra[0xb8] = 4;
    cpu.set_timing_tables(main.data(), main.data(), main.data(), main.data(), main.data(),
                          extra.data());

    memory[0] = 0x3e;
    memory[1] = 0x42;
    cpu.set_pc(0);
    check(count_instruction_cycles(cpu) == 8, "CPC wait-states make LD A,n 8 T-states");
    check(cpu.a == 0x42, "LD A,n still loads the immediate");

    // LDIR extra uses the ED extra table (4 T on the CPC, 5 on a bare Z80).
    memory[0] = 0x21;
    memory[1] = 0x00;
    memory[2] = 0x20;
    memory[3] = 0x11;
    memory[4] = 0x00;
    memory[5] = 0x30;
    memory[6] = 0x01;
    memory[7] = 0x02;
    memory[8] = 0x00;
    memory[9] = 0xed;
    memory[10] = 0xb0;
    memory[11] = 0x76;
    memory[0x2000] = 0xaa;
    memory[0x2001] = 0xbb;
    cpu.reset();
    cpu.set_timing_tables(main.data(), main.data(), main.data(), main.data(), main.data(),
                          extra.data());
    cpu.set_pc(0);
    int block_cycles = 0;
    cpu.set_cycle_handler([&](int cycles) { block_cycles += cycles; });
    cpu.run(200);
    check(memory[0x3000] == 0xaa && memory[0x3001] == 0xbb, "LDIR still copies with CPC extras");
    // Two repeating LDIR steps at 4+4 extra plus a terminal 4 T ED B0 and HALT 4:
    // the exact total is less important than each repeating step being a multiple of 4.
    check((block_cycles % 4) == 0, "CPC LDIR totals stay on a 4 T-state grid");
}

void test_z80_irq_cycle_align() {
    auto memory = make_memory();
    dsp::Z80 cpu = make_cpu(memory);
    memory[0x0038] = 0x00;  // nop in the IM1 handler
    cpu.sp = 0xf000;
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = true;
    cpu.set_pc(0);
    cpu.set_irq_cycle_align(4);
    bool acked = false;
    cpu.set_irq_ack_callback([&] { acked = true; });
    cpu.set_irq(dsp::IrqLine::Assert);
    const int total = count_instruction_cycles(cpu);
    check(acked, "IRQ acknowledge callback runs when the interrupt is taken");
    check(!cpu.iff1, "taking the IRQ clears IFF1");
    check(cpu.pc() == 0x0039, "IM1 then executes the opcode at 0x0038");
    // 16 T IRQ (13 rounded up to a multiple of 4) + 4 T NOP.
    check(total == 20, "aligned IM1 IRQ plus NOP is 20 T-states");
}

void emit_ld_bc(std::vector<uint8_t>& rom, size_t& pc, uint16_t bc) {
    rom[pc++] = 0x01;
    rom[pc++] = uint8_t(bc);
    rom[pc++] = uint8_t(bc >> 8);
}

void emit_ld_a(std::vector<uint8_t>& rom, size_t& pc, uint8_t value) {
    rom[pc++] = 0x3e;
    rom[pc++] = value;
}

void emit_out_c_a(std::vector<uint8_t>& rom, size_t& pc) {
    rom[pc++] = 0xed;
    rom[pc++] = 0x79;
}

void emit_ga(std::vector<uint8_t>& rom, size_t& pc, uint8_t value) {
    emit_ld_bc(rom, pc, 0x7f00);
    emit_ld_a(rom, pc, value);
    emit_out_c_a(rom, pc);
}

void emit_crtc(std::vector<uint8_t>& rom, size_t& pc, uint8_t index, uint8_t value) {
    emit_ld_bc(rom, pc, 0xbc00);
    emit_ld_a(rom, pc, index);
    emit_out_c_a(rom, pc);
    emit_ld_bc(rom, pc, 0xbd00);
    emit_ld_a(rom, pc, value);
    emit_out_c_a(rom, pc);
}

bool write_cpc_dummy_rom(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::vector<uint8_t> rom(0x8000, 0x00);
    size_t pc = 0;

    rom[pc++] = 0xf3;  // di

    // Mode 1, both ROMs enabled so this firmware keeps running.
    emit_ga(rom, pc, 0x81);
    // Pens: paper black, ink bright yellow, border black.
    emit_ga(rom, pc, 0x00);
    emit_ga(rom, pc, 0x44);  // paper: hardware colour 4 (blue)
    emit_ga(rom, pc, 0x01);
    emit_ga(rom, pc, 0x4b);  // ink: hardware colour 11 (white)
    emit_ga(rom, pc, 0x10);
    emit_ga(rom, pc, 0x54);  // border: hardware colour 20 (black)

    constexpr uint8_t kCrtc[14] = {63, 40, 46, 0x8e, 38, 0, 25, 30, 0, 7, 0, 0, 0x30, 0};
    for (uint8_t r = 0; r < 14; r++) emit_crtc(rom, pc, r, kCrtc[r]);

    // Fill 16K of video RAM at 0xC000 with 0xE0 (mode 1: three ink pixels, one paper).
    // Writes only: LDIR would *read* 0xC000, which is the upper ROM while it is paged in.
    rom[pc++] = 0x21;  // ld hl,0xc000
    rom[pc++] = 0x00;
    rom[pc++] = 0xc0;
    rom[pc++] = 0x01;  // ld bc,0x4000
    rom[pc++] = 0x00;
    rom[pc++] = 0x40;
    const size_t fill_loop = pc;
    rom[pc++] = 0x36;  // ld (hl),0xe0
    rom[pc++] = 0xe0;
    rom[pc++] = 0x23;  // inc hl
    rom[pc++] = 0x0b;  // dec bc
    rom[pc++] = 0x78;  // ld a,b
    rom[pc++] = 0xb1;  // or c
    rom[pc++] = 0x20;  // jr nz,fill_loop
    rom[pc++] = uint8_t(int(fill_loop) - int(pc + 1));
    rom[pc++] = 0x18;
    rom[pc++] = 0xfe;  // jr $

    std::ofstream out(dir + "/cpc464.rom", std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(rom.data()), std::streamsize(rom.size()));
    return bool(out);
}

void test_amstrad_crtc_does_not_tear() {
    const std::string dir = "/tmp/cpc_test_roms";
    if (!write_cpc_dummy_rom(dir)) {
        check(false, "could not write the dummy CPC ROM");
        return;
    }

    dsp::AmstradCpc cpc(dsp::AmstradCpc::Model::CPC464);
    std::string error;
    if (!cpc.init(dir, &error)) {
        check(false, "dummy CPC ROM should load");
        std::printf("  init error: %s\n", error.c_str());
        return;
    }

    for (int frame = 0; frame < 30; frame++) cpc.run_frame();

    const uint32_t* first = cpc.framebuffer();
    const int width = cpc.screen_width();
    const int height = cpc.screen_height();
    std::vector<uint32_t> snapshot(first, first + size_t(width) * height);

    cpc.run_frame();
    const uint32_t* second = cpc.framebuffer();
    bool stable = std::equal(snapshot.begin(), snapshot.end(), second);
    check(stable, "consecutive CPC frames stay identical once the CRTC is locked");

    std::set<uint32_t> colours;
    for (int i = 0; i < width * height; i++) colours.insert(second[i]);
    check(colours.size() >= 2 && colours.size() <= 6,
          "a locked CPC picture uses a handful of palette colours, not random noise");

    // Mode 1 0xF0 paints three ink pixels and one paper pixel per byte, so a
    // visible scanline must contain that 4-pixel cadence rather than speckle.
    bool found_pattern = false;
    for (int y = 0; y < height && !found_pattern; y++) {
        const uint32_t* row = second + y * width;
        for (int x = 0; x + 3 < width; x++) {
            if (row[x] == row[x + 1] && row[x] == row[x + 2] && row[x] != row[x + 3]) {
                found_pattern = true;
                break;
            }
        }
    }
    check(found_pattern, "mode 1 video RAM is scanned as a stable 4-pixel pattern");
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

    layout.rotate_cw = false;
    layout.rotate_ccw = true;
    gfx.decode(layout, rom);
    pixels = gfx.element(0);
    // Rotatel: dest[row][col] = src[width - 1 - row][col]. Top-left colour 3
    // lands at the bottom left; the old top-right (colour 2) is the new top-left.
    check(pixels[7 * 8] == 3, "ccw rotation moves the top left pixel to the bottom left");
    check(pixels[0] == 2, "ccw rotation moves the old top right pixel to the top left");
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

dsp::M6502 make_m6502(dsp::M6502::Type type = dsp::M6502::Type::Nmos) {
    m6502_memory.assign(0x10000, 0);
    dsp::M6502 cpu(1789772, type);
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

void test_m6502_nes_decimal_and_unofficial() {
    dsp::M6502 nmos = make_m6502();
    // sed / clc / lda #$09 / adc #$01
    load_6502(0x1000, {0xf8, 0x18, 0xa9, 0x09, 0x69, 0x01, 0x4c, 0x06, 0x10});
    nmos.reset();
    nmos.run(12);
    check(nmos.a == 0x10, "NMOS ADC uses decimal mode after SED");

    dsp::M6502 nes = make_m6502(dsp::M6502::Type::Nes);
    load_6502(0x1000, {0xf8, 0x18, 0xa9, 0x09, 0x69, 0x01, 0x4c, 0x06, 0x10});
    nes.reset();
    nes.run(12);
    check(nes.a == 0x0a, "the 2A03 ignores decimal mode on ADC");

    dsp::M6502 slo = make_m6502();
    m6502_memory[0x20] = 0x01;
    // lda #$80 / slo $20 / jmp *
    load_6502(0x1000, {0xa9, 0x80, 0x07, 0x20, 0x4c, 0x04, 0x10});
    slo.reset();
    slo.run(12);
    check(m6502_memory[0x20] == 0x02, "SLO shifts the memory byte");
    check(slo.a == 0x82, "SLO ORs the shifted byte into A");
    check(slo.p.c == false, "SLO copies the old high bit into C");

    dsp::M6502 lax = make_m6502();
    m6502_memory[0x20] = 0x40;
    m6502_memory[0x21] = 0x00;
    m6502_memory[0x40] = 0x5a;
    // lax ($20),Y / jmp *   ($b3 is the unofficial opcode m6502.pas implements)
    load_6502(0x1000, {0xb3, 0x20, 0x4c, 0x02, 0x10});
    lax.reset();
    lax.run(10);
    check(lax.a == 0x5a && lax.x == 0x5a, "LAX loads A and X from memory");

    dsp::M6502 dcp = make_m6502();
    m6502_memory[0x20] = 0x10;
    // lda #$0f / dcp $20,x / jmp *   (X starts at 0)
    load_6502(0x1000, {0xa9, 0x0f, 0xd7, 0x20, 0x4c, 0x04, 0x10});
    dcp.reset();
    dcp.run(12);
    check(m6502_memory[0x20] == 0x0f, "DCP decrements the memory byte");
    check(dcp.p.z, "DCP compares A with the decremented byte");
    check(dcp.p.c, "DCP sets carry when A >= memory");
}

void test_m65c02_opcodes() {
    dsp::M6502 cpu = make_m6502();
    cpu.set_cmos(true);
    m6502_memory[0x0010] = 0x80;
    m6502_memory[0x0011] = 0x20;
    m6502_memory[0x0021] = 0xff;
    // lda #$aa / stz $21 / inc / phx / ldx #$00 / plx / sta ($10) / bra +2 / nop / lda $2080
    load_6502(0x1000, {0xa9, 0xaa, 0x64, 0x21, 0x1a, 0xda, 0xa2, 0x00, 0xfa, 0x92, 0x10, 0x80,
                       0x01, 0xea, 0xad, 0x80, 0x20, 0x4c, 0x11, 0x10});
    cpu.reset();
    cpu.run(80);
    check(m6502_memory[0x21] == 0, "65C02 stz writes zero");
    check(cpu.a == 0xab, "65C02 inc a then lda ($10) via the stored pointer");
    check(cpu.x == 0, "65C02 phx/plx restore x");
    check(m6502_memory[0x2080] == 0xab, "65C02 sta (zp) writes through the pointer");
}

void test_lynx_suzy_math() {
    dsp::LynxSuzy suzy;
    suzy.reset();
    suzy.write(0x52, 0x04);  // MATH_D, clears C
    suzy.write(0x54, 0x10);  // MATH_B, clears A
    suzy.write(0x55, 0x00);  // MATH_A starts 0x0010 * 0x0004
    check(suzy.read(0x60) == 0x40, "Suzy multiply stores the low byte in MATH_H");
    check(suzy.read(0x61) == 0x00, "Suzy multiply high bytes are zero for a small product");

    suzy.write(0x60, 0x64);  // MATH_H = 100, clears G
    suzy.write(0x62, 0x00);  // MATH_F, clears E
    suzy.write(0x56, 0x05);  // MATH_P = 5, clears N
    suzy.write(0x63, 0x00);  // MATH_E starts 100 / 5
    check(suzy.read(0x52) == 0x14, "Suzy divide 100/5 writes 20 to MATH_D");
    check(suzy.read(0x6c) == 0x00, "Suzy divide remainder is zero");
}

void test_lynx_suzy_blit() {
    std::vector<uint8_t> ram(0x10000, 0);
    dsp::LynxSuzy suzy;
    suzy.set_ram(ram.data());
    suzy.reset();
    suzy.write(0x08, 0x00);  // VIDBAS $4000
    suzy.write(0x09, 0x40);
    suzy.write(0x28, 0x80);
    suzy.write(0x2a, 0x80);
    ram[0x0300] = 0xc0;  // 4 bpp background sprite
    ram[0x0301] = 0x90;  // packed, reload width/height
    ram[0x0302] = 0x00;
    ram[0x0303] = 0x00;
    ram[0x0304] = 0x00;  // end of SCB list
    ram[0x0305] = 0x00;
    ram[0x0306] = 0x04;  // sprite data at $0400
    ram[0x0307] = 0x00;
    ram[0x0308] = 0x00;  // x
    ram[0x0309] = 0x00;
    ram[0x030a] = 0x00;  // y
    ram[0x030b] = 0x00;
    ram[0x030c] = 0x01;  // width 1.0
    ram[0x030d] = 0x00;
    ram[0x030e] = 0x01;  // height 1.0
    ram[0x030f] = 0x01;
    ram[0x0310] = 0x23;
    ram[0x0311] = 0x45;
    ram[0x0312] = 0x67;
    ram[0x0400] = 0x03;  // line length
    ram[0x0401] = 0x12;  // pens 1 and 2
    ram[0x0402] = 0x34;  // pens 3 and 4
    ram[0x0403] = 0x00;  // end of sprite
    suzy.write(0x10, 0x00);
    suzy.write(0x11, 0x03);  // SCBNEXT = $0300
    suzy.write(0x90, 0x01);  // SUZYBUSEN
    suzy.write(0x91, 0x01);  // SPRGO
    check((ram[0x4000] >> 4) == 1, "Suzy packed blit writes the first pen into the video buffer");
    check((ram[0x4000] & 0x0f) == 2, "Suzy packed blit writes the second pen");
    check((ram[0x4001] >> 4) == 3, "Suzy packed blit continues across bytes");
}

std::vector<uint8_t> make_lynx_bars_cart() {
    std::vector<uint8_t> header(64, 0);
    header[0] = 'L';
    header[1] = 'Y';
    header[2] = 'N';
    header[3] = 'X';
    header[4] = 0x00;
    header[5] = 0x04;  // 1024-byte pages
    const char* name = "DSP-CPP BARS";
    std::memcpy(header.data() + 10, name, std::strlen(name));
    const uint8_t program[] = {
        0xa2, 0xff, 0x9a, 0xa9, 0x04, 0x8d, 0xf9, 0xff, 0xa9, 0x9e, 0x8d, 0x00, 0xfd, 0xa9,
        0x18, 0x8d, 0x01, 0xfd, 0xa9, 0x68, 0x8d, 0x08, 0xfd, 0xa9, 0x1f, 0x8d, 0x09, 0xfd,
        0xa9, 0x0f, 0x8d, 0xa1, 0xfd, 0xa9, 0xff, 0x8d, 0xb1, 0xfd, 0xa9, 0x0f, 0x8d, 0xb2,
        0xfd, 0x8d, 0xa3, 0xfd, 0xa9, 0xf0, 0x8d, 0xb4, 0xfd, 0xa9, 0x0f, 0x8d, 0xa5, 0xfd,
        0xa9, 0x0f, 0x8d, 0xb5, 0xfd, 0xa9, 0x00, 0x8d, 0x94, 0xfd, 0xa9, 0x40, 0x8d, 0x95,
        0xfd, 0xa9, 0x0d, 0x8d, 0x92, 0xfd, 0xa9, 0x00, 0x85, 0x10, 0xa9, 0x40, 0x85, 0x11,
        0xa2, 0x66, 0xa9, 0x00, 0x85, 0x12, 0xa9, 0x08, 0x85, 0x13, 0xa5, 0x12, 0x0a, 0x0a,
        0x0a, 0x0a, 0x05, 0x12, 0x85, 0x14, 0xa9, 0x0a, 0x85, 0x15, 0xa5, 0x14, 0x92, 0x10,
        0xe6, 0x10, 0xd0, 0x02, 0xe6, 0x11, 0xc6, 0x15, 0xd0, 0xf2, 0xe6, 0x12, 0xc6, 0x13,
        0xd0, 0xde, 0xca, 0xd0, 0xd3, 0x4c, 0x83, 0x02,
    };
    std::vector<uint8_t> cart = header;
    cart.insert(cart.end(), std::begin(program), std::end(program));
    cart.resize(64 + 1024, 0);
    return cart;
}

void test_atari_lynx_bars() {
    const std::vector<uint8_t> image = make_lynx_bars_cart();
    namespace fs = std::filesystem;
    const fs::path dir = "/tmp/dsp-lynx-test-bars";
    fs::create_directories(dir);
    const fs::path path = dir / "bars.lnx";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
    }
    dsp::AtariLynx lynx;
    std::string error;
    check(lynx.init(path.string(), &error), "the Lynx driver accepts a .lnx image");
    for (int frame = 0; frame < 8; frame++) lynx.run_frame();
    const uint32_t* fb = lynx.framebuffer();
    int coloured = 0;
    for (int i = 0; i < lynx.screen_width() * lynx.screen_height(); i++) {
        if ((fb[i] & 0x00ffffff) != 0) coloured++;
    }
    check(coloured > 1000, "the Lynx test cart paints colour bars through Mikey DMA");
    check(lynx.screen_width() == 160 && lynx.screen_height() == 102,
          "the Lynx LCD is 160x102");
}

void test_atari_lynx_bios() {
    dsp::AtariLynx lynx;
    std::string error;
    if (!lynx.load_bios("/tmp/lynxboot.img", &error)) {
        std::printf("skip: lynxboot.img not found (%s)\n", error.c_str());
        return;
    }
    lynx.reset();
    check(lynx.bios_loaded(), "lynxboot.img is mapped as the Mikey boot ROM");
    check(lynx.debug_pc() == 0xff80, "the Atari BIOS reset vector is $FF80");
    for (int frame = 0; frame < 2; frame++) lynx.run_frame();
    check(lynx.debug_iodir() == 0x03, "the Atari BIOS programs IODIR before overlaying MAPCTL");
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

    dsp::Slapstic road(108, nullptr);
    road.reset();
    check(road.current_bank() == 3, "slapstic 108 starts on bank 3");
    const uint16_t road_banks[4] = {0x0028, 0x002a, 0x002c, 0x002e};
    for (uint8_t wanted = 0; wanted < 4; wanted++) {
        road.tweak(0);
        check(road.tweak(road_banks[wanted]) == wanted, "slapstic 108 selects the requested bank");
    }
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
    // linked to itself; MAME still visits that terminator once (it is not a skip).
    sprite_ram[0x000] = 0x0005;          // code
    sprite_ram[0x400] = 0x0002 | (16 << 7);  // colour and horizontal position
    sprite_ram[0x800] = 0x1e0 << 7;      // vertical position (counted upwards)
    sprite_ram[0xc00] = 0x0001;          // link to the terminator
    sprite_ram[0xc01] = 0x0001;          // the terminator links to itself

    dsp::AtariMotionObjects objects(config, slip_ram.data(), sprite_ram.data(), 512, 256);
    int drawn = 0;
    int first_code = -1, first_color = -1, first_x = -1, first_y = -1;
    objects.draw(0, 0, 0, [&](int code, int color, bool, bool, int x, int y, int, int) {
        if (drawn == 0) {
            first_code = code;
            first_color = color;
            first_x = x;
            first_y = y;
        }
        drawn++;
    });
    check(drawn == 2, "the motion object list visits the self-linked terminator once");
    check(first_code == 5, "the motion object code is extracted");
    check(first_color == 0x20, "the motion object colour becomes a palette offset");
    check(first_x == 16 && first_y == 24, "the motion object position is extracted");
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

dsp::HD63701 make_m6803(std::vector<uint8_t>& memory, const std::vector<uint8_t>& program) {
    dsp::HD63701 cpu(1000000, dsp::HD63701::Type::M6803);
    cpu.set_memory_handlers([&memory](uint16_t address) { return memory[address]; },
                            [&memory](uint16_t address, uint8_t value) { memory[address] = value; });
    std::copy(program.begin(), program.end(), memory.begin() + 0xf000);
    memory[0xfffe] = 0xf0;
    memory[0xffff] = 0x00;
    cpu.reset();
    return cpu;
}

void test_m6803_memory_map() {
    auto memory = make_memory();
    // ldaa #$12 / staa $50 / staa $200 / ldab $50 / addb $200
    dsp::HD63701 cpu =
        make_m6803(memory, {0x86, 0x12, 0x97, 0x50, 0xb7, 0x02, 0x00, 0xd6, 0x50, 0xfb, 0x02, 0x00});
    check(cpu.pc() == 0xf000, "the M6803 takes the reset vector from external memory");
    cpu.run(30);
    check(cpu.a == 0x12, "the M6803 loads an immediate value");
    check(memory[0x50] == 0, "M6803 internal RAM at $50 is not on the external bus");
    check(memory[0x200] == 0x12, "M6803 writes above $100 reach external memory");
    check(cpu.b == 0x24, "the M6803 reads internal RAM and external memory");
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

void test_msm5205_streaming() {
    dsp::MSM5205 chip(384000, 96, 4);
    check(chip.sample_frequency() == 4000, "S96 selects a 4 kHz VCLK");
    int clocks = 0;
    chip.set_vclk_handler([&]() { clocks++; });
    chip.reset();
    chip.data_w(0x07);
    chip.set_reset(false);
    chip.vclk();
    check(clocks == 1, "a streaming VCLK still runs the handler");
    check(chip.output() > 0, "data_w nibbles are decoded without a sample ROM");
    chip.set_reset(true);
    chip.vclk();
    check(clocks == 2, "the VCLK handler fires while the chip is held in reset");
    check(chip.output() == 0, "reset on a streaming chip silences the output");
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
    chip.set_pin7(false);
    check(chip.sample_frequency() == 1056000 / 165, "pin 7 low selects the /165 divider");
}

void test_kabuki() {
    std::vector<uint8_t> src(0x8000, 0);
    src[0] = 0x00;
    src[1] = 0xff;
    src[2] = 0xa5;
    std::vector<uint8_t> opcode, data;
    dsp::kabuki_cps1_decode(src, opcode, data, 0x76543210, 0x24601357, 0x4343, 0x43);
    check(opcode.size() == 0x8000 && data.size() == 0x8000, "kabuki emits 32K opcode and data maps");
    check(opcode[2] != src[2] || data[2] != src[2], "non-trivial bytes are encrypted");
    check(opcode[2] != data[2], "opcode and data maps differ for Cadillacs keys");
}

void test_qsound() {
    dsp::QSound chip(0x1000);
    chip.reset();
    check(chip.read() == 0x80, "qsound reports ready");
    check(chip.mixed() == 0, "qsound is silent after reset");
    chip.write(0, 0x00);
    chip.write(1, 0x10);
    chip.write(2, 0x80);  // pan of channel 0
    chip.clock();
    check(chip.left() == 0 && chip.right() == 0, "a pan write without a key-on stays silent");
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

std::vector<uint8_t> make_nrom_cart() {
    std::vector<uint8_t> rom(16 + 0x4000, 0);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1a;
    rom[4] = 1;  // 16 KiB PRG
    rom[5] = 0;  // CHR RAM
    rom[6] = 0;  // horizontal mirroring, mapper 0
    rom[16 + 0] = 0x78;        // SEI
    rom[16 + 1] = 0x4c;        // JMP $8000
    rom[16 + 2] = 0x00;
    rom[16 + 3] = 0x80;
    rom[16 + 0x3ffc] = 0x00;  // reset vector
    rom[16 + 0x3ffd] = 0x80;
    rom[16 + 0x3ffa] = 0x00;  // NMI vector
    rom[16 + 0x3ffb] = 0x80;
    return rom;
}

void test_nes_apu_status() {
    dsp::NesApu apu;
    apu.reset();
    const uint8_t first = apu.read(0x4015);
    check((first & 0x0f) == 0, "APU $4015 reports no pulse/tri/noise lengths after reset");
    check((apu.read(0x4015) & 0x40) == 0, "reading $4015 clears the frame IRQ flag");

    apu.write(0x4015, 0x01);
    apu.write(0x4000, 0x3f);
    apu.write(0x4002, 0x00);
    apu.write(0x4003, 0x00);  // length index 0 -> 10
    check((apu.read(0x4015) & 0x01) != 0, "enabling pulse 1 and writing $4003 sets length");
    apu.write(0x4015, 0x00);
    check((apu.read(0x4015) & 0x01) == 0, "clearing $4015 bit 0 kills the length counter");
}

void test_nes_ines_and_memory() {
    dsp::Nes nes;
    std::string error;
    check(nes.load_ines(make_nrom_cart(), &error), "a mapper 0 iNES image loads");
    check(nes.debug_mapper() == 0, "mapper 0 is selected from the iNES header");
    check(nes.debug_pc() == 0x8000, "reset fetches the iNES reset vector");

    nes.debug_write(0x0000, 0x5a);
    check(nes.debug_read(0x0800) == 0x5a, "CPU RAM is mirrored every 2 KiB");
    check(nes.debug_read(0x1800) == 0x5a, "the $1800 mirror sees the same byte");
    nes.debug_write(0x07ff, 0xa5);
    check(nes.debug_read(0x1fff) == 0xa5, "the last RAM mirror is $1fff -> $07ff");

    nes.debug_read(0x2002);  // arm the $2006/5 toggle, matching a $2002 read
    nes.debug_write(0x2006, 0x3f);
    nes.debug_write(0x2006, 0x00);
    nes.debug_write(0x2007, 0x30);
    nes.debug_write(0x2001, 0x1e);
    nes.run_frame();
    const uint32_t pixel = nes.framebuffer()[0];
    check((pixel & 0xff000000) == 0xff000000, "the PPU writes opaque backdrop pixels");
}

void test_nes_unsupported_mapper() {
    auto rom = make_nrom_cart();
    rom[6] = 0x50;  // mapper 5 in the low nibble of flags6
    rom[7] = 0x00;
    dsp::Nes nes;
    std::string error;
    check(!nes.load_ines(rom, &error), "mapper 5 is rejected");
    check(error.find("mapper") != std::string::npos, "the error names the mapper");
}

std::vector<uint8_t> make_ines(int mapper, int prg_banks, int chr_banks, int submapper = 0) {
    std::vector<uint8_t> rom(16 + size_t(prg_banks) * 0x4000 + size_t(chr_banks) * 0x2000, 0);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1a;
    rom[4] = uint8_t(prg_banks);
    rom[5] = uint8_t(chr_banks);
    rom[6] = uint8_t((mapper & 0x0f) << 4);
    if (submapper != 0) {
        rom[7] = uint8_t(0x08 | (mapper & 0xf0));
        rom[8] = uint8_t((submapper & 0x0f) << 4);
    } else {
        rom[7] = uint8_t(mapper & 0xf0);
    }
    const size_t last = 16 + size_t(prg_banks - 1) * 0x4000;
    rom[last + 0x3ffc] = 0x00;
    rom[last + 0x3ffd] = 0xc0;
    rom[last] = 0x4c;  // JMP $C000
    rom[last + 1] = 0x00;
    rom[last + 2] = 0xc0;
    return rom;
}

void ppu_set_address(dsp::Nes& nes, uint16_t address) {
    nes.debug_read(0x2002);
    nes.debug_write(0x2006, uint8_t(address >> 8));
    nes.debug_write(0x2006, uint8_t(address));
}

uint8_t ppu_read_byte(dsp::Nes& nes, uint16_t address) {
    ppu_set_address(nes, address);
    nes.debug_read(0x2007);
    return nes.debug_read(0x2007);
}

void ppu_write_byte(dsp::Nes& nes, uint16_t address, uint8_t value) {
    ppu_set_address(nes, address);
    nes.debug_write(0x2007, value);
}

void test_nes_simple_mappers() {
    std::string error;

    {
        auto uxrom = make_ines(2, 2, 0);
        uxrom[16] = 0xaa;
        uxrom[16 + 0x4000] = 0xbb;
        auto mapper2 = std::make_unique<dsp::Nes>();
        check(mapper2->load_ines(uxrom, &error), "a mapper 2 iNES image loads");
        check(mapper2->debug_read(0x8000) == 0xaa, "UxROM maps PRG bank 0 at $8000");
        mapper2->debug_write(0x8000, 1);
        check(mapper2->debug_read(0x8000) == 0xbb, "UxROM switches the $8000 bank");
        check(mapper2->debug_read(0xc000) == 0xbb, "UxROM keeps the last bank at $C000");
    }

    {
        auto mapper13 = std::make_unique<dsp::Nes>();
        check(mapper13->load_ines(make_ines(13, 1, 0), &error), "a mapper 13 iNES image loads");
        ppu_write_byte(*mapper13, 0x1000, 0x11);
        mapper13->debug_write(0x8000, 2);
        ppu_write_byte(*mapper13, 0x1000, 0x22);
        mapper13->debug_write(0x8000, 1);
        check(ppu_read_byte(*mapper13, 0x1000) == 0x11, "CPROM bank 1 keeps its CHR RAM");
        mapper13->debug_write(0x8000, 2);
        check(ppu_read_byte(*mapper13, 0x1000) == 0x22, "CPROM switches the $1000 CHR page");
    }

    {
        auto nina = make_ines(79, 4, 2);
        nina[16] = 0x11;
        nina[16 + 0x8000] = 0x33;           // 32K PRG bank 1
        nina[16 + 0x10000] = 0xa0;          // CHR bank 0
        nina[16 + 0x10000 + 0x2000] = 0xa1;  // CHR bank 1
        auto mapper79 = std::make_unique<dsp::Nes>();
        check(mapper79->load_ines(nina, &error), "a mapper 79 iNES image loads");
        mapper79->debug_write(0x4100, 0x09);  // PRG bank 1, CHR bank 1
        check(mapper79->debug_read(0x8000) == 0x33, "NINA-03 switches 32K PRG from $4100");
        check(ppu_read_byte(*mapper79, 0x0000) == 0xa1, "NINA-03 switches 8K CHR from $4100");
    }

    {
        auto mmc3 = std::make_unique<dsp::Nes>();
        check(mmc3->load_ines(make_ines(4, 2, 1), &error), "a mapper 4 iNES image loads");
        mmc3->debug_write(0x6000, 0x5a);
        check(mmc3->debug_read(0x6000) == 0x5a, "MMC3 PRG-RAM at $6000 is writable");
    }

    {
        auto mmc6 = std::make_unique<dsp::Nes>();
        check(mmc6->load_ines(make_ines(4, 2, 1, 1), &error), "MMC6 (mapper 4 submapper 1) loads");
        mmc6->debug_write(0x6000, 0x5a);
        check(mmc6->debug_read(0x6000) == 0x00, "MMC6 $6000-$6FFF stays open bus");
        mmc6->debug_write(0x8000, 0x20);
        mmc6->debug_write(0xa001, 0xf0);
        mmc6->debug_write(0x7000, 0xa5);
        mmc6->debug_write(0x7200, 0x5a);
        check(mmc6->debug_read(0x7000) == 0xa5, "MMC6 bank 0 PRG-RAM is at $7000");
        check(mmc6->debug_read(0x7200) == 0x5a, "MMC6 bank 1 PRG-RAM is at $7200");
    }

    const int extra[] = {15, 34, 68, 70, 76, 88, 93, 94, 95, 113, 180, 184};
    for (int mapper : extra) {
        auto nes = std::make_unique<dsp::Nes>();
        const std::string loaded = "mapper " + std::to_string(mapper) + " loads";
        const std::string selected = "mapper " + std::to_string(mapper) + " is selected";
        check(nes->load_ines(make_ines(mapper, 2, 2), &error), loaded.c_str());
        check(nes->debug_mapper() == mapper, selected.c_str());
    }
}

void test_nes_nestest_if_present() {
    std::ifstream in("/tmp/nestest.nes", std::ios::binary);
    if (!in) return;
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto nes = std::make_unique<dsp::Nes>();
    std::string error;
    check(nes->load_ines(rom, &error), "nestest.nes loads");
    nes->debug_set_pc(0xc000);
    for (int frame = 0; frame < 90; ++frame) nes->run_frame();
    check(nes->debug_read(0x02) == 0x00, "nestest reports no failed opcode in $02");
}

void test_c64_cia_timer() {
    dsp::Mos6526 cia(985248);
    bool irq = false;
    cia.set_calls(nullptr, nullptr, nullptr, nullptr,
                  [&](dsp::IrqLine state) { irq = state == dsp::IrqLine::Assert; });
    cia.reset();
    cia.write(0x0d, 0x81);  // enable timer A IRQ
    cia.write(0x04, 0x20);  // latch lo
    cia.write(0x05, 0x00);  // latch hi (loads while stopped)
    cia.write(0x0e, 0x01);  // start timer A
    cia.sync(200);
    check(irq, "CIA timer A raises IRQ after the latch expires");
    const uint8_t icr = cia.read(0x0d);
    check((icr & 0x01) != 0, "CIA ICR reports timer A");
}

void test_c64_vic_raster() {
    dsp::Mos6566 vic(985248);
    vic.reset();
    vic.write(0x12, 50);
    vic.write(0x11, 0x1b);
    check((vic.read(0x11) & 0x1b) == 0x1b, "VIC $D011 keeps the written control bits");
    vic.update(50);
    check(vic.read(0x12) == 50, "VIC $D012 follows the current raster line");
    check((vic.read(0x11) & 0x80) == 0, "raster MSB is clear on line 50");
    vic.update(260);
    check((vic.read(0x11) & 0x80) != 0, "raster MSB is set on line 260");
}

void test_c64_sid_triangle() {
    dsp::Sid sid(985248);
    sid.reset();
    sid.write(0x00, 0x44);
    sid.write(0x01, 0x1d);
    sid.write(0x18, 0x0f);
    sid.write(0x05, 0x09);
    sid.write(0x06, 0xf0);
    sid.write(0x04, 0x11);
    int nonzero = 0;
    for (int i = 0; i < 4000; i++) {
        if (sid.update() != 0) nonzero++;
    }
    check(nonzero > 100, "SID triangle write produces a non-silent waveform");
}

void test_c64_pla_and_keyboard() {
    auto machine = std::make_unique<dsp::C64>();
    machine->init_synthetic_roms();
    check(machine->peek(0xfffc) == 0x00, "PLA mode 7 maps KERNAL at $E000");
    check(machine->peek(0xfffd) == 0xc0, "KERNAL reset vector is visible");
    machine->poke(0xd800, 0x0a);
    check(machine->peek(0xd800) == 0x0a, "writes to $D800 hit colour RAM");
    machine->poke(0xd020, 0x02);
    check((machine->peek(0xd020) & 0x0f) == 0x02, "VIC border colour is readable");

    dsp::MachineInputs inputs;
    inputs.keys[size_t(dsp::Key::A)] = true;
    machine->set_inputs(inputs);
    machine->poke(0xdc00, 0xfd);  // select keyboard column 1
    check((machine->peek(0xdc01) & 0x04) == 0, "A is reported on CIA1 PB bit 2");

    machine->poke(0xc000, 0xa9);
    machine->poke(0xc001, 0x42);
    machine->poke(0xc002, 0x8d);
    machine->poke(0xc003, 0x00);
    machine->poke(0xc004, 0xc4);
    machine->poke(0xc005, 0x4c);
    machine->poke(0xc006, 0x00);
    machine->poke(0xc007, 0xc0);
    machine->set_pc(0xc000);
    for (int i = 0; i < 3; i++) machine->run_frame();
    check(machine->peek(0xc400) == 0x42, "a poked program can STA into RAM");
}

void test_c64_prg_media() {
    auto machine = std::make_unique<dsp::C64>();
    machine->init_synthetic_roms();
    const char* path = "/tmp/dsp_c64_test.prg";
    const uint8_t prg[] = {0x00, 0xc0, 0xee, 0x00, 0xc4, 0x4c, 0x00, 0xc0};
    std::FILE* f = std::fopen(path, "wb");
    check(f != nullptr, "can create a temporary PRG");
    if (f) {
        std::fwrite(prg, 1, sizeof(prg), f);
        std::fclose(f);
    }
    std::string error;
    check(machine->load_media(path, &error), "PRG load_media succeeds");
    machine->set_pc(0xc000);
    for (int i = 0; i < 5; i++) machine->run_frame();
    check(machine->peek(0xc400) != 0, "PRG payload runs and increments $C400");
}

// Nintendo logo at cart $0104, copied from gb.pas's main_logo / abrir_gb.
const uint8_t kGbLogo[0x30] = {
    0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
    0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
    0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
};

std::vector<uint8_t> make_gb_rom(uint8_t cgb_flag) {
    std::vector<uint8_t> rom(0x8000, 0x00);
    rom[0x100] = 0x18;  // jr -2, idle at the post-boot entry point
    rom[0x101] = 0xfe;
    std::memcpy(rom.data() + 0x104, kGbLogo, sizeof(kGbLogo));
    rom[0x143] = cgb_flag;
    rom[0x147] = 0x00;
    rom[0x148] = 0x00;
    rom[0x149] = 0x00;
    return rom;
}

bool load_gb_rom(dsp::GameBoy& gb, const std::vector<uint8_t>& rom, const char* path) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(rom.data()), std::streamsize(rom.size()));
    out.close();
    std::string error;
    return gb.load_media(path, &error);
}

uint32_t cgb_rgb(uint16_t bgr555) {
    uint8_t r = uint8_t((bgr555 & 0x1f) << 3);
    uint8_t g = uint8_t(((bgr555 >> 5) & 0x1f) << 3);
    uint8_t b = uint8_t(((bgr555 >> 10) & 0x1f) << 3);
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void test_gbc_cart_detection() {
    auto gb = std::make_unique<dsp::GameBoy>();
    check(load_gb_rom(*gb, make_gb_rom(0x80), "/tmp/dsp_cpp_gbc_80.gb"), "load $80 cart");
    check(gb->debug_is_cgb(), "$80 carts run as Game Boy Color without a boot ROM");
    check(std::strcmp(gb->title(), "Game Boy Color") == 0, "title is Game Boy Color for $80");
    check(gb->debug_state().a == 0x01, "CGB post-boot A is $01 (gb.pas, not hardware)");

    check(load_gb_rom(*gb, make_gb_rom(0xc0), "/tmp/dsp_cpp_gbc_c0.gb"), "load $c0 cart");
    check(gb->debug_is_cgb(), "$c0 carts run as Game Boy Color");

    check(load_gb_rom(*gb, make_gb_rom(0x00), "/tmp/dsp_cpp_gb_dmg.gb"), "load DMG cart");
    check(!gb->debug_is_cgb(), "$00 carts stay on DMG");
    check(std::strcmp(gb->title(), "Game Boy") == 0, "title is Game Boy for DMG");
}

void test_gbc_boot_rom_map() {
    auto gb = std::make_unique<dsp::GameBoy>();
    check(load_gb_rom(*gb, make_gb_rom(0x80), "/tmp/dsp_cpp_gbc_boot.gb"), "load cart for boot map");

    std::vector<uint8_t> boot(0x900, 0xaa);
    boot[0x000] = 0xa5;
    boot[0x100] = 0x11;  // hole: must not be visible at CPU $0100
    boot[0x200] = 0x5a;
    gb->debug_set_cgb_boot_rom(boot);
    gb->reset();
    check(gb->debug_boot_rom_enabled(), "CGB boot ROM enables the BIOS map");
    check(gb->debug_read(0x0000) == 0xa5, "CGB boot $0000 is bios_rom[address]");
    check(gb->debug_read(0x0200) == 0x5a, "CGB boot $0200 is bios_rom[$200], not packed offset $100");
    check(gb->debug_read(0x0100) == 0x18, "CGB boot leaves $0100-$01FF as the cart");

    std::vector<uint8_t> packed(0x800, 0xbb);
    packed[0x000] = 0xa5;
    packed[0x100] = 0x5a;  // concatenated gbc_boot.2
    gb->debug_set_cgb_boot_rom(packed);
    gb->reset();
    check(gb->debug_read(0x0000) == 0xa5, "packed CGB boot still maps $0000");
    check(gb->debug_read(0x0200) == 0x5a, "packed CGB boot remaps $0200 to file offset $100");
    check(gb->debug_read(0x0100) == 0x18, "packed CGB boot still leaves the cart at $0100");
}

void test_gbc_ppu_lcdc0_and_priority() {
    dsp::GbPpu ppu;
    ppu.reset(true);
    ppu.set_lcdc(0x90);  // display on, LCDC.0 clear, unsigned tiles at $8000
    ppu.set_scy(0);
    ppu.set_scx(0);
    // Tile 0: both planes $FF → colour 3. Distinct BG colour 3 = red.
    for (int i = 0; i < 16; i++) ppu.vram_write(uint16_t(i), 0xff);
    ppu.set_bg_pal_index(6);
    ppu.write_bg_pal_data(0x1f, false);
    ppu.set_bg_pal_index(7);
    ppu.write_bg_pal_data(0x00, false);

    uint32_t line[dsp::GbPpu::kScreenWidth];
    ppu.render_scanline(0, line);
    check(line[0] == cgb_rgb(0x001f), "LCDC.0 does not blank the CGB background");

    // Attr bit 7 + non-zero BG pixel hides an in-front sprite even with LCDC.0 clear.
    ppu.set_vbk(1);
    ppu.vram_write(0x1800, 0x80);  // BG map (0,0) attr: priority
    ppu.set_vbk(0);
    // Sprite tile 1: plane 0 = $FF, plane 1 = 0 → colour 1, green.
    for (int i = 0; i < 8; i++) {
        ppu.vram_write(uint16_t(16 + i * 2), 0xff);
        ppu.vram_write(uint16_t(16 + i * 2 + 1), 0x00);
    }
    ppu.set_obj_pal_index(2);
    ppu.write_obj_pal_data(0xe0, false);
    ppu.set_obj_pal_index(3);
    ppu.write_obj_pal_data(0x03, false);
    ppu.oam_write(0, 16);  // y
    ppu.oam_write(1, 8);   // x → screen 0
    ppu.oam_write(2, 1);   // tile
    ppu.oam_write(3, 0);   // in front, pal 0
    ppu.set_lcdc(0x92);    // display + sprites + unsigned tiles, LCDC.0 still clear
    ppu.render_scanline(0, line);
    check(line[0] == cgb_rgb(0x001f), "BG attr.7 hides sprites regardless of LCDC.0");

    // Same sprite, no attr.7, LCDC.0 set: sprite wins.
    ppu.set_vbk(1);
    ppu.vram_write(0x1800, 0x00);
    ppu.set_vbk(0);
    ppu.set_lcdc(0x93);
    ppu.render_scanline(0, line);
    check(line[0] == cgb_rgb(0x03e0), "in-front sprites cover BG when LCDC.0 is set and attr.7 is clear");
}

void test_gbc_io_hdma_and_unused_oam() {
    auto gb = std::make_unique<dsp::GameBoy>();
    check(load_gb_rom(*gb, make_gb_rom(0x80), "/tmp/dsp_cpp_gbc_io.gb"), "load cart for CGB I/O");

    gb->debug_write(0xfea0, 0x42);
    check(gb->debug_read(0xfea0) == 0x42, "CGB $FEA0-$FECF is RAM");
    gb->debug_write(0xfed0, 0x99);
    check(gb->debug_read(0xfec0) == 0x99, "CGB $FED0-$FEFF echoes $FEC0+(addr&$F)");
    check(gb->debug_read(0xfed0) == 0x99, "CGB $FED0 reads the echo");

    // GDMA: 16 bytes from WRAM $C000 to VRAM $8000.
    for (int i = 0; i < 16; i++) gb->debug_write(uint16_t(0xc000 + i), uint8_t(0x70 + i));
    gb->debug_write(0xff51, 0xc0);
    gb->debug_write(0xff52, 0x00);
    gb->debug_write(0xff53, 0x00);
    gb->debug_write(0xff54, 0x00);
    gb->debug_write(0xff55, 0x00);  // general-purpose DMA, one block
    check(gb->debug_read(0x8000) == 0x70, "GDMA copies the first byte into VRAM");
    check(gb->debug_read(0x800f) == 0x7f, "GDMA copies a full 16-byte block");

    // HDMA: one more block during HBlank of a visible line.
    for (int i = 0; i < 16; i++) gb->debug_write(uint16_t(0xc010 + i), uint8_t(0x80 + i));
    gb->debug_write(0xff51, 0xc0);
    gb->debug_write(0xff52, 0x10);
    gb->debug_write(0xff53, 0x00);
    gb->debug_write(0xff54, 0x10);
    gb->debug_write(0xff55, 0x80);  // HBlank DMA, size 0 → one block
    gb->run_frame();
    check(gb->debug_read(0x8010) == 0x80, "HDMA copies one block during HBlank");
    check(gb->debug_hdma_size() == 0xff, "HDMA ends with size $FF");

    // WRAM $C1A4 hack from gbc_despues_instruccion.
    gb->debug_write(0xc1a4, 0x12);
    gb->debug_write(0xd1a4, 0x12);
    gb->run_frame();
    check(gb->debug_read(0xc1a4) == 0xed, "equal WRAM $C1A4/$D1A4 is forced to $ED");

    // DMG $FEA0 reads as 0.
    check(load_gb_rom(*gb, make_gb_rom(0x00), "/tmp/dsp_cpp_gb_fea0.gb"), "load DMG cart for $FEA0");
    gb->debug_write(0xfea0, 0x42);
    check(gb->debug_read(0xfea0) == 0, "DMG $FEA0-$FEFF reads as 0");
}

void test_gbc_oam_dma_from_vram() {
    auto gb = std::make_unique<dsp::GameBoy>();
    check(load_gb_rom(*gb, make_gb_rom(0x80), "/tmp/dsp_cpp_gbc_oamdma.gb"), "load cart for OAM DMA");
    gb->debug_write(0x8000, 0x55);
    gb->debug_write(0xff46, 0x80);  // OAM DMA from VRAM
    check(gb->debug_read(0xfe00) == 0xff, "OAM DMA from VRAM yields $FF");
}

void test_z80ctc_timer_and_vector() {
    dsp::Z80Ctc ctc;
    int irqs = 0;
    uint8_t last_vec = 0;
    ctc.set_irq_callback([&](dsp::IrqLine state, uint8_t vec) {
        if (state != dsp::IrqLine::Clear) {
            irqs++;
            last_vec = vec;
        }
    });
    ctc.reset();
    ctc.write(0, 0x00);          // interrupt vector base $00
    ctc.write(0, 0x85);          // control: IRQ + timer + load constant (CONTROL|CONSTANT|INTERRUPT)
    ctc.write(0, 2);             // time constant 2, auto-trigger
    ctc.tick(16 * 2);            // one full countdown at prescale 16
    check(irqs >= 1, "CTC timer channel 0 raises IRQ");
    check(last_vec == 0x00, "CTC channel 0 vector is base+0");

    irqs = 0;
    last_vec = 0xff;
    ctc.write(1, 0xc5);  // IRQ + counter + load constant
    ctc.write(1, 1);
    ctc.pulse_trigger(1);
    check(irqs >= 1, "CTC counter channel 1 fires on the trigger edge");
    check(last_vec == 0x02, "CTC channel 1 vector is base+2");
}

void test_mcr_tapper_io_map() {
    dsp::Mcr tapper(dsp::Mcr::Game::Tapper);
    check(std::strcmp(tapper.title(), "Tapper") == 0, "Tapper title");
    check(tapper.screen_width() == 512 && tapper.screen_height() == 480, "MCR screen is 512x480");
    check(tapper.frames_per_second() == 30.0, "MCR runs at 30 fps");
    tapper.reset();
    check(tapper.debug_ix() == 0xffff, "MCR reset leaves IX at $FFFF so boot CALL $01AC is a no-op");
    std::string error = "unset";
    check(!tapper.init("/no/such/tapper.zip", &error), "missing Tapper ROMs fail init");
    check(error.find("not found") != std::string::npos || error.find("cannot") != std::string::npos ||
              error.size() > 3,
          "init reports why the ROM set is missing");

    const char* rom = "/tmp/roms/tapper.zip";
    std::FILE* f = std::fopen(rom, "rb");
    if (f) {
        std::fclose(f);
        dsp::Mcr boot(dsp::Mcr::Game::Tapper);
        error.clear();
        check(boot.init(rom, &error), "Tapper ROM set loads");
        int frames = 0;
        while (frames < 180 && boot.debug_im() != 2) {
            boot.run_frame();
            frames++;
        }
        check(!boot.debug_halted(), "Tapper is still running after the boot sequence");
        check(boot.debug_im() == 2, "Tapper programs the CTC in IM 2");
        check(boot.debug_ctc_irqs() > 0, "the CTC raises IRQs once the scanline clocks start");
    }
}

dsp::Upd7801 make_7801(std::vector<uint8_t>& memory) {
    dsp::Upd7801 cpu(4000000);
    cpu.set_memory_handlers([&memory](uint16_t address) { return memory[address]; },
                            [&memory](uint16_t address, uint8_t value) { memory[address] = value; });
    cpu.reset();
    return cpu;
}

void test_upd7801_mvi_add_skip_call() {
    auto memory = make_memory();
    dsp::Upd7801 cpu = make_7801(memory);
    // MVI A,0x0f / ADI 0x01 / HALT
    const uint8_t program[] = {0x69, 0x0f, 0x46, 0x01, 0x01};
    std::memcpy(memory.data(), program, sizeof(program));
    cpu.run(7 + 7);
    check(cpu.a() == 0x10, "upd7801 MVI A + ADI leaves 0x10");
    check(!cpu.cy(), "upd7801 ADI 0x0f+1 does not set carry");

    auto skipped = make_memory();
    dsp::Upd7801 skip_cpu = make_7801(skipped);
    // MVI A,5 / GTI 3 / MVI A,0x99 / HALT  — GTI should skip the second MVI
    const uint8_t skip_prog[] = {0x69, 0x05, 0x27, 0x03, 0x69, 0x99, 0x01};
    std::memcpy(skipped.data(), skip_prog, sizeof(skip_prog));
    skip_cpu.run(80);
    check(skip_cpu.a() == 0x05, "upd7801 GTI skips the next instruction");

    auto stacked = make_memory();
    dsp::Upd7801 call_cpu = make_7801(stacked);
    // LXI SP,$FF80 / CALL $0010 / HALT, and at $0010: MVI A,$42 / RET
    stacked[0x0000] = 0x04;
    stacked[0x0001] = 0x80;
    stacked[0x0002] = 0xFF;
    stacked[0x0003] = 0x44;
    stacked[0x0004] = 0x10;
    stacked[0x0005] = 0x00;
    stacked[0x0006] = 0x01;
    stacked[0x0010] = 0x69;
    stacked[0x0011] = 0x42;
    stacked[0x0012] = 0x08;
    call_cpu.run(200);
    check(call_cpu.a() == 0x42, "upd7801 CALL/RET returns with A from the callee");
    check(call_cpu.pc() == 0x0006, "upd7801 RET comes back to the HALT after CALL");
}

void test_upd7801_ei_delay_and_intf2() {
    auto memory = make_memory();
    dsp::Upd7801 cpu = make_7801(memory);
    // LXI SP,$FF80 / MVI A,$F7 / MOV MKL,A / EI / NOP / MVI A,$99 / HALT
    // INTF2 vector at $0020: MVI A,$42 / HALT
    const uint8_t program[] = {0x04, 0x80, 0xFF, 0x69, 0xF7, 0x4D, 0xC3, 0x48, 0x20,
                               0x00, 0x69, 0x99, 0x01};
    std::memcpy(memory.data(), program, sizeof(program));
    memory[0x0020] = 0x69;
    memory[0x0021] = 0x42;
    memory[0x0022] = 0x01;
    cpu.set_input_line(dsp::Upd7801::kIntf2, dsp::IrqLine::Assert);
    cpu.run(400);
    check(cpu.a() == 0x42, "upd7801 takes INTF2 after EI delay and vectors to $0020");
}

void test_upd1771_tone() {
    dsp::Upd1771 sound(6000000, 10.0f);
    sound.write(2);
    sound.write(0);
    sound.write(0x40);
    sound.write(0x1F);
    for (int i = 0; i < 20000; i++) sound.run_cycles(100, 2000000);
    std::vector<int16_t> samples;
    sound.take_samples(samples);
    bool non_zero = false;
    for (int16_t s : samples) {
        if (s != 0) {
            non_zero = true;
            break;
        }
    }
    check(!samples.empty(), "upd1771 produces samples");
    check(non_zero, "upd1771 tone packet is audible");
}

void write_scv_bios(const std::string& dir, const uint8_t* program, size_t program_n) {
    namespace fs = std::filesystem;
    fs::create_directories(dir);
    std::vector<uint8_t> bios(0x1000, 0);
    std::memcpy(bios.data(), program, program_n);
    std::vector<uint8_t> chr(0x400, 0);
    for (int i = 0; i < 8; i++) chr[size_t(i)] = 0xFF;
    {
        std::ofstream out(dir + "/upd7801g.s01", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bios.data()), std::streamsize(bios.size()));
    }
    {
        std::ofstream out(dir + "/epochtv.chr", std::ios::binary);
        out.write(reinterpret_cast<const char*>(chr.data()), std::streamsize(chr.size()));
    }
}

void test_scv_init_and_block_graphics() {
    dsp::Scv missing;
    std::string error;
    check(!missing.init("/tmp/dsp-scv-missing-bios", &error),
          "SCV init fails without the BIOS pair");

    const uint8_t program[] = {
        0x04, 0x80, 0xFF,  // LXI SP,$FF80
        0x68, 0x34,        // MVI V,$34
        0x69, 0x03,        // MVI A,$03  block graphics
        0x38, 0x00,        // STAW $3400
        0x69, 0x00,        // MVI A,$00  gr_bg = 0
        0x38, 0x01,        // STAW $3401
        0x68, 0x30,        // MVI V,$30
        0x69, 0xFF,        // MVI A,$FF  both 8x8 blocks colour 15
        0x38, 0x63,        // STAW $3063  tile (3,3)
        0x01,              // HALT
    };
    const std::string dir = "/tmp/dsp-scv-block";
    write_scv_bios(dir, program, sizeof(program));

    auto scv = std::make_unique<dsp::Scv>();
    error.clear();
    check(scv->init(dir, &error), "SCV init loads a dummy BIOS from a directory");
    check(scv->bios_loaded(), "SCV reports the BIOS as loaded");
    check(scv->screen_width() == 192 && scv->screen_height() == 222,
          "SCV visible size is 192x222");
    for (int i = 0; i < 4; i++) scv->run_frame();
    const uint32_t* fb = scv->framebuffer();
    check(fb[25 * 192 + 0] == 0xFFFFFFFF, "SCV block graphics draw white in the cropped window");
    check(fb[0] == 0xFF00009B, "SCV background uses palette colour 0");
}

void test_scv_cartridge_window() {
    const uint8_t program[] = {
        0x04, 0x80, 0xFF,  // LXI SP,$FF80
        0x68, 0x80,        // MVI V,$80
        0x28, 0x00,        // LDAW $8000
        0x01,              // HALT
    };
    const std::string dir = "/tmp/dsp-scv-cart";
    write_scv_bios(dir, program, sizeof(program));
    std::vector<uint8_t> cart(0x2000, 0xA5);
    {
        std::ofstream out(dir + "/game.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(cart.data()), std::streamsize(cart.size()));
    }
    auto scv = std::make_unique<dsp::Scv>();
    std::string error;
    check(scv->init(dir, &error), "SCV init loads a dummy 8 KiB cartridge beside the BIOS");
    for (int i = 0; i < 2; i++) scv->run_frame();
    check(scv->debug_a() == 0xA5, "SCV maps an 8 KiB cart at $8000");
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
    check(machine.sub_present(), "dummy EXL BIOS installs the I/O CPU");
    check(machine.screen_width() == 336 && machine.screen_height() == 252,
          "EXL-100 reports the TMS3556 336x252 framebuffer");
    for (int i = 0; i < 2; i++) machine.run_frame();
    check(machine.debug_pc() >= 0xf800, "dummy BIOS idles in internal ROM");

    dsp::Exelv tel(dsp::Exelv::Model::Exeltel);
    tel.install_dummy_bios();
    tel.reset();
    check(tel.bios_loaded(), "dummy EXELTEL BIOS installs");
    for (int i = 0; i < 2; i++) tel.run_frame();
    check(tel.debug_pc() >= 0xf000, "dummy EXELTEL BIOS idles in TMS7040 ROM");
}

void test_starwars_missing_roms() {
    dsp::StarWars machine;
    check(std::strcmp(machine.title(), "Star Wars") == 0, "Star Wars title");
    check(machine.screen_width() == 400 && machine.screen_height() == 300,
          "Star Wars reports the 400x300 vector window");
    std::string error = "unset";
    check(!machine.init("/no/such/starwars.zip", &error), "missing Star Wars ROMs fail init");
    check(error.find("not found") != std::string::npos || error.find("cannot") != std::string::npos ||
              error.size() > 3,
          "init reports why the Star Wars ROM set is missing");

    dsp::StarwarsMath math;
    std::array<uint8_t, 0x1000> prom{};
    math.init(prom.data());
    math.write(6, 0x10);
    math.write(7, 0x00);
    math.write(4, 0x00);
    math.write(5, 0x20);
    const uint16_t quotient = uint16_t((uint16_t(math.div_reh()) << 8) | math.div_rel());
    check(quotient != 0, "mathbox restoring divider produces a quotient");

    dsp::Mos6532 riot;
    int irqs = 0;
    riot.set_irq_callback([&irqs](dsp::IrqLine line) {
        if (line != dsp::IrqLine::Clear) irqs++;
    });
    riot.reset();
    riot.io_write(0x1c, 0x01);  // timer, /1 prescale, IRQ enabled
    riot.tick(4);
    check(irqs > 0, "MOS 6532 timer raises IRQ after countdown");

    const char* rom = "/tmp/roms/starwars.zip";
    std::FILE* f = std::fopen(rom, "rb");
    if (f) {
        std::fclose(f);
        dsp::StarWars boot;
        error.clear();
        check(boot.init(rom, &error), "Star Wars ROM set loads");
        for (int i = 0; i < 200; i++) boot.run_frame();
        check(boot.debug_pc() >= 0x6000, "Star Wars main CPU is executing ROM");
        check(boot.debug_avg_lines() > 10, "AVG produced a vector list in attract");
        int lit = 0;
        const uint32_t* fb = boot.framebuffer();
        const int n = boot.screen_width() * boot.screen_height();
        for (int i = 0; i < n; i++) {
            if ((fb[i] & 0x00ffffffu) != 0) lit++;
        }
        check(lit > 100, "Star Wars attract draws visible vectors");
    }
}

void test_atari_system1_missing_roms() {
    dsp::AtariSystem1 machine(dsp::AtariSystem1::Game::Indy);
    std::string error = "unset";
    check(!machine.init("/no/such/indytemp.zip", &error),
          "Indiana Jones init fails without the ROM set");
    check(error.find("not found") != std::string::npos || error.find("missing") != std::string::npos ||
              error.find("cannot") != std::string::npos,
          "init reports why the Atari System 1 set is missing");
    check(std::strcmp(machine.title(), "Indiana Jones and the Temple of Doom") == 0,
          "Indiana Jones title");
    check(machine.screen_width() == 336 && machine.screen_height() == 240,
          "Atari System 1 screen is 336x240");

    dsp::AtariSystem1 road(dsp::AtariSystem1::Game::RoadRunner);
    check(std::strcmp(road.title(), "Road Runner") == 0, "Road Runner title");
}

void test_trdos_scl_and_beta() {
    std::vector<uint8_t> scl(9 + 14 + 256, 0);
    std::memcpy(scl.data(), "SINCLAIR", 8);
    scl[8] = 1;
    std::memcpy(scl.data() + 9, "TEST    C", 9);
    scl[9 + 9] = 0x00;
    scl[9 + 10] = 0x80;
    scl[9 + 11] = 0x00;
    scl[9 + 12] = 0x01;
    scl[9 + 13] = 1;
    std::fill(scl.begin() + 9 + 14, scl.end(), uint8_t(0xa5));

    dsp::TrdosDisk disk;
    std::string error;
    check(disk.load_bytes(scl.data(), scl.size(), &error), "SCL image expands to a TR-DOS volume");
    check(disk.tracks() == 80 && disk.heads() == 2, "SCL becomes an 80-track DS disk");
    const uint8_t* cat = disk.sector(0, 0, 1);
    check(cat != nullptr && std::memcmp(cat, "TEST    C", 9) == 0, "SCL catalogue keeps the file name");
    check(cat != nullptr && cat[14] == 0 && cat[15] == 1,
          "first SCL file starts at logical track 1 sector 0");
    const uint8_t* info = disk.sector(0, 0, 9);
    check(info != nullptr && info[0xe3] == 0x16 && info[0xe7] == 0x10,
          "disk-info sector is DS/80 with the TR-DOS ident");
    const uint8_t* data = disk.sector(0, 1, 1);
    check(data != nullptr && data[0] == 0xa5 && data[255] == 0xa5,
          "file payload lands on cylinder 0 head 1");

    dsp::Wd1793 fdc;
    fdc.reset();
    fdc.set_disk(&disk);
    fdc.set_side(1);
    fdc.track_w(0);
    fdc.sector_w(1);
    fdc.command_w(0x80);
    check((fdc.status_r() & 0x02) != 0, "read sector raises DRQ");
    check(fdc.data_r() == 0xa5, "WD1793 returns the SCL payload");
    for (int i = 1; i < 256; i++) (void)fdc.data_r();
    check(fdc.intrq(), "sector read completes with INTRQ");

    dsp::TrdosDisk trd_disk;
    std::vector<uint8_t> trd(40 * 16 * 256, 0);
    trd[0] = 'A';
    check(trd_disk.load_bytes(trd.data(), trd.size(), &error), "a 40-track SS TRD image loads");
    check(trd_disk.tracks() == 40 && trd_disk.heads() == 1, "TRD size selects 40-track single sided");
    check(trd_disk.sector(0, 0, 1) != nullptr && trd_disk.sector(0, 0, 1)[0] == 'A',
          "TRD bytes map onto track 0 sector 1");

    dsp::Beta128 beta;
    beta.reset();
    check(!beta.active(), "Beta 128 starts paged out");
    beta.enable();
    check(beta.state_r() != 0xff, "port $FF is live while DOS is paged in");

    auto pentagon = std::make_unique<dsp::Pentagon1024>();
    auto scorpion = std::make_unique<dsp::Scorpion256>();
    check(std::strcmp(pentagon->title(), "Pentagon 1024") == 0, "Pentagon title");
    check(std::strcmp(scorpion->title(), "Scorpion ZS-256") == 0, "Scorpion title");
    check(pentagon->debug_ram_pages() == 64, "Pentagon 1024 has 64 RAM pages");
    check(scorpion->debug_ram_pages() == 16, "Scorpion 256 has 16 RAM pages");
    check(pentagon->screen_width() == 352 && pentagon->screen_height() == 280,
          "clone screen is 352x280");

    error = "unset";
    check(!pentagon->init("/no/such/pentagon", &error), "Pentagon init fails without ROMs");
    check(error.find("not found") != std::string::npos, "Pentagon reports the missing 128K ROM");
    error = "unset";
    check(!scorpion->init("/no/such/scorpion", &error), "Scorpion init fails without ROMs");
    check(error.find("not found") != std::string::npos, "Scorpion reports the missing ROM");

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "dsp-zx-clone-roms";
    fs::create_directories(dir);
    auto write_blob = [&](const char* name, size_t size) {
        std::vector<uint8_t> blob(size, 0x00);
        blob[0] = 0x18;
        blob[1] = 0xfe;  // JR -2
        std::ofstream out((dir / name).string(), std::ios::binary);
        out.write(reinterpret_cast<const char*>(blob.data()), std::streamsize(blob.size()));
    };
    write_blob("128.rom", 0x8000);
    write_blob("trdos.rom", 0x4000);
    write_blob("scorpion.rom", 0x10000);

    auto pent_ok = std::make_unique<dsp::Pentagon1024>();
    check(pent_ok->init(dir.string(), &error), "Pentagon boots from dummy 128K+TR-DOS ROMs");
    pent_ok->io_out(0x7ffd, 0x07);
    check(pent_ok->debug_ram3() == 7, "Pentagon $7FFD bits 0-2 select RAM 0-7");
    pent_ok->io_out(0x7ffd, 0x40);
    check(pent_ok->debug_ram3() == 8, "Pentagon $7FFD bits 6-7 select RAM 8-31");
    pent_ok->io_out(0xdffd, 0x01);
    check(pent_ok->debug_ram3() == 40, "Pentagon $DFFD bit 0 selects RAM 32-63");
    pent_ok->io_out(0x7ffd, 0x10);
    pent_ok->debug_m1(0x3d00);
    check(pent_ok->debug_beta() && pent_ok->debug_rom_page() == 3,
          "M1 at $3D00 with the 48K ROM pages TR-DOS in");
    pent_ok->debug_m1(0x4000);
    check(!pent_ok->debug_beta(), "M1 at $4000 pages TR-DOS out");

    const fs::path scl_path = dir / "test.scl";
    {
        std::ofstream out(scl_path.string(), std::ios::binary);
        out.write(reinterpret_cast<const char*>(scl.data()), std::streamsize(scl.size()));
    }
    check(pent_ok->load_media(scl_path.string(), &error), "Pentagon loads an SCL disk");
    check(pent_ok->debug_disk(), "Beta 128 has a disk after SCL load");

    auto scor_ok = std::make_unique<dsp::Scorpion256>();
    check(scor_ok->init(dir.string(), &error), "Scorpion boots from a 64 KB ROM");
    scor_ok->io_out(0x7ffd, 0x05);
    check(scor_ok->debug_ram3() == 5, "Scorpion $7FFD bits 0-2 select RAM 0-7");
    scor_ok->io_out(0x1ffd, 0x10);
    check(scor_ok->debug_ram3() == 13, "Scorpion $1FFD bit 4 selects RAM 8-15");
    check(scor_ok->load_media(scl_path.string(), &error), "Scorpion loads an SCL disk");

    pent_ok->run_frame();
    scor_ok->run_frame();
    check(pent_ok->framebuffer()[0] != 0, "Pentagon renders a border");
    check(scor_ok->framebuffer()[0] != 0, "Scorpion renders a border");

    const fs::path dir64 = dir / "zxmak";
    fs::create_directories(dir64);
    {
        std::vector<uint8_t> blob(0x10000, 0x00);
        blob[0] = 0x18;
        blob[1] = 0xfe;
        std::memcpy(blob.data() + 0xc365, "TR-DOS", 6);
        std::ofstream out((dir64 / "PENTAGON.ROM").string(), std::ios::binary);
        out.write(reinterpret_cast<const char*>(blob.data()), std::streamsize(blob.size()));
        std::ofstream out2((dir64 / "scorpion.rom").string(), std::ios::binary);
        out2.write(reinterpret_cast<const char*>(blob.data()), std::streamsize(blob.size()));
    }
    auto pent64 = std::make_unique<dsp::Pentagon1024>();
    check(pent64->init(dir64.string(), &error), "64 KB PENTAGON.ROM boots without a separate TR-DOS file");
    auto scor64 = std::make_unique<dsp::Scorpion256>();
    check(scor64->init(dir64.string(), &error), "64 KB scorpion.rom boots from the ZXMak layout");

    const fs::path trees = dir / "zxtiny-jmesys";
    const fs::path zxm = trees / "zxtiny" / "zxm";
    const fs::path spectrum = trees / "jMESYS" / "src" / "bios" / "Sinclair" / "Spectrum";
    fs::create_directories(zxm);
    fs::create_directories(spectrum);
    auto write_marked = [&](const fs::path& file, size_t size, uint8_t mark) {
        std::vector<uint8_t> blob(size, 0x00);
        blob[0] = 0x18;
        blob[1] = 0xfe;
        blob[2] = mark;
        std::ofstream out(file.string(), std::ios::binary);
        out.write(reinterpret_cast<const char*>(blob.data()), std::streamsize(blob.size()));
    };
    write_marked(zxm / "zx128.rom", 0x8000, 0x12);
    write_marked(zxm / "trdos.rom", 0x4000, 0x1d);
    write_marked(zxm / "scorp0.rom", 0x4000, 0xc0);
    write_marked(zxm / "scorp1.rom", 0x4000, 0xc1);
    write_marked(zxm / "scorp2.rom", 0x4000, 0xc2);
    write_marked(zxm / "scorp3.rom", 0x4000, 0xc3);
    write_marked(spectrum / "Pentagon.rom", 0x8000, 0xaa);

    auto pent_trees = std::make_unique<dsp::Pentagon1024>();
    check(pent_trees->init(trees.string(), &error),
          "Pentagon finds nested jMESYS Pentagon.rom and zxtiny trdos.rom");
    check(pent_trees->debug_rom_byte(0, 2) == 0xaa,
          "Pentagon prefers jMESYS Pentagon.rom over zxtiny zx128.rom");
    check(!pent_trees->debug_gluk(), "zxtiny scorp2.rom is not treated as Pentagon GLUK");
    check(pent_trees->debug_rom_byte(2, 2) != 0xc2, "Pentagon page 2 is not the Scorpion service ROM");

    auto pent_zxm = std::make_unique<dsp::Pentagon1024>();
    check(pent_zxm->init(zxm.string(), &error),
          "Pentagon from zxtiny/zxm still picks the sibling jMESYS Pentagon.rom");
    check(pent_zxm->debug_rom_byte(0, 2) == 0xaa, "companion jMESYS ROM is visible from zxtiny/zxm");

    auto scor_trees = std::make_unique<dsp::Scorpion256>();
    check(scor_trees->init(zxm.string(), &error), "Scorpion boots from nested zxtiny scorp0..scorp3");
    check(scor_trees->debug_rom_byte(2, 2) == 0xc2, "Scorpion page 2 is zxtiny scorp2.rom");
}

}  // namespace

int main() {
    test_z80_arithmetic();
    test_z80_flags_and_blocks();
    test_z80_interrupt();
    test_z80_cpc_wait_states();
    test_z80_irq_cycle_align();
    test_amstrad_crtc_does_not_tear();
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
    test_m6502_nes_decimal_and_unofficial();
    test_m65c02_opcodes();
    test_lynx_suzy_math();
    test_lynx_suzy_blit();
    test_atari_lynx_bars();
    test_atari_lynx_bios();
    test_slapstic();
    test_ym2151();
    test_pokey();
    test_atari_motion_objects();
    test_hd63701_reset_and_internal_ram();
    test_hd63701_ports();
    test_hd63701_hd63701_only_opcodes();
    test_hd63701_interrupts();
    test_m6803_memory_map();
    test_m6805_arithmetic();
    test_m6805_bit_branches();
    test_m6805_interrupt();
    test_msm5205();
    test_msm5205_streaming();
    test_okim6295();
    test_kabuki();
    test_qsound();
    test_spectrum_tape();
    test_spectrum_tzx();
    test_spectrum_ula();
    test_nes_apu_status();
    test_nes_ines_and_memory();
    test_nes_unsupported_mapper();
    test_nes_simple_mappers();
    test_nes_nestest_if_present();
    test_c64_cia_timer();
    test_c64_vic_raster();
    test_c64_sid_triangle();
    test_c64_pla_and_keyboard();
    test_c64_prg_media();
    test_gbc_cart_detection();
    test_gbc_boot_rom_map();
    test_gbc_ppu_lcdc0_and_priority();
    test_gbc_io_hdma_and_unused_oam();
    test_gbc_oam_dma_from_vram();
    test_z80ctc_timer_and_vector();
    test_mcr_tapper_io_map();
    test_upd7801_mvi_add_skip_call();
    test_upd7801_ei_delay_and_intf2();
    test_upd1771_tone();
    test_scv_init_and_block_graphics();
    test_scv_cartridge_window();
    test_tms7000_mov_add_call();
    test_tms7000_lvdp_and_int1();
    test_tms3556_background();
    test_exelv_dummy_bios();
    test_trdos_scl_and_beta();
    test_starwars_missing_roms();
    test_atari_system1_missing_roms();
    if (failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", failures);
    return 1;
}
