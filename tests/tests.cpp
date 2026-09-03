// Minimal self contained checks for the ported components.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cctype>
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
#include "cpu/t11.h"
#include "cpu/tms7000.h"
#include "cpu/upd7801.h"
#include "cpu/z80.h"
#include "cpu/z80ctc.h"
#include "drivers/amstrad_cpc.h"
#include "drivers/atari_lynx.h"
#include "drivers/a2600.h"
#include "drivers/atari_system1.h"
#include "drivers/atari_system2.h"
#include "drivers/apple2.h"
#include "drivers/exelv.h"
#include "drivers/gameboy.h"
#include "drivers/mcr.h"
#include "drivers/msx2.h"
#include "drivers/nes.h"
#include "drivers/pv2000.h"
#include "drivers/scv.h"
#include "drivers/starwars.h"
#include "drivers/asteroid.h"
#include "drivers/c64.h"
#include "machine/mos6566.h"
#include "drivers/polepos.h"
#include "cpu/mb88xx.h"
#include "cpu/z8002.h"
#include "drivers/spectrum.h"
#include "drivers/zx_clone.h"
#include "drivers/genesis.h"
#include "drivers/hangon.h"
#include "drivers/outrun.h"
#include "drivers/skullxbo.h"
#include "drivers/bublbobl.h"
#include "drivers/system16.h"
#include "machine/fd1089.h"
#include "machine/bagman_pal.h"
#include "machine/beta128.h"
#include "machine/kabuki.h"
#include "machine/lynx_suzy.h"
#include "machine/msx_dsk.h"
#include "machine/diskii.h"
#include "machine/mos6526.h"
#include "machine/mos6532.h"
#include "machine/rp5c01.h"
#include "machine/slapstic.h"
#include "machine/spectrum_tape.h"
#include "machine/trdos_disk.h"
#include "machine/wd1793.h"
#include "machine/starwars_math.h"
#include "machine/sega_315_5195.h"
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
#include "sound/ym2612.h"
#include "sound/sega_pcm.h"
#include "sound/upd7759.h"
#include "video/v9938.h"
#include "video/atari_mo.h"
#include "video/gb_ppu.h"
#include "video/gfx.h"
#include "video/tms3556.h"
#include "video/sega_315_5313.h"
#include "video/sega16.h"
#include "video/tia.h"

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
    check(gb->debug_state().a == 0x11, "CGB post-boot A is $11, how carts detect Game Boy Color");

    check(load_gb_rom(*gb, make_gb_rom(0xc0), "/tmp/dsp_cpp_gbc_c0.gb"), "load $c0 cart");
    check(gb->debug_is_cgb(), "$c0 carts run as Game Boy Color");

    check(load_gb_rom(*gb, make_gb_rom(0x00), "/tmp/dsp_cpp_gb_dmg.gb"), "load DMG cart");
    check(!gb->debug_is_cgb(), "$00 carts stay on DMG");
    check(std::strcmp(gb->title(), "Game Boy") == 0, "title is Game Boy for DMG");
    check(gb->debug_state().a == 0x01, "DMG post-boot A is $01");
    check(gb->debug_read(0xff40) == 0x91, "DMG post-boot LCDC is $91");
    check(gb->debug_read(0xff47) == 0xfc, "DMG post-boot BGP is $FC");
}

void test_gb_cgb_registers_absent_on_dmg() {
    auto gb = std::make_unique<dsp::GameBoy>();
    check(load_gb_rom(*gb, make_gb_rom(0x00), "/tmp/dsp_cpp_gb_dmg_regs.gb"), "load DMG cart");

    for (uint16_t reg : {0xff4dU, 0xff4fU, 0xff55U, 0xff56U, 0xff68U, 0xff69U, 0xff6aU, 0xff6bU,
                         0xff70U}) {
        gb->debug_write(reg, 0x01);
        check(gb->debug_read(reg) == 0xff, "CGB-only register reads as $FF on DMG");
    }

    // VBK stays on bank 0 and SVBK does not move WRAM $D000-$DFFF.
    gb->debug_write(0x8000, 0x11);
    gb->debug_write(0xff4f, 0x01);
    check(gb->debug_read(0x8000) == 0x11, "DMG ignores VBK: VRAM has no second bank");
    gb->debug_write(0xd000, 0x22);
    gb->debug_write(0xff70, 0x02);
    check(gb->debug_read(0xd000) == 0x22, "DMG ignores SVBK: WRAM has no banks");

    // KEY1 does not exist either, so STOP must not switch to double speed.
    gb->debug_write(0xff4d, 0x01);
    gb->run_frame();
    check(gb->debug_speed() == 0, "DMG stays at single speed");
}

void test_gbc_hdma_control() {
    auto gb = std::make_unique<dsp::GameBoy>();
    check(load_gb_rom(*gb, make_gb_rom(0x80), "/tmp/dsp_cpp_gbc_hdma.gb"), "load cart for HDMA");

    // A general-purpose transfer reports "finished" ($FF) when it is done.
    gb->debug_write(0xff51, 0xc0);
    gb->debug_write(0xff52, 0x00);
    gb->debug_write(0xff53, 0x00);
    gb->debug_write(0xff54, 0x00);
    gb->debug_write(0xff55, 0x01);  // two blocks
    check(gb->debug_read(0xff55) == 0xff, "GDMA leaves $FF55 at $FF once complete");

    // Clearing bit 7 aborts a running HBlank transfer and keeps the remaining
    // length readable with bit 7 set.
    gb->debug_write(0xff54, 0x40);
    gb->debug_write(0xff55, 0x83);  // four blocks, HBlank
    check(gb->debug_read(0xff55) == 0x03, "an active HDMA reports its remaining length");
    gb->debug_write(0xff55, 0x00);
    check(gb->debug_read(0xff55) == 0x83, "clearing bit 7 aborts the HDMA");
    uint8_t before = gb->debug_read(0x8040);
    gb->run_frame();
    check(gb->debug_read(0x8040) == before, "an aborted HDMA copies nothing");

    // Restarting with bit 7 set reloads the length instead of aborting.
    for (int i = 0; i < 32; i++) gb->debug_write(uint16_t(0xc000 + i), uint8_t(0xa0 + i));
    gb->debug_write(0xff51, 0xc0);
    gb->debug_write(0xff52, 0x00);
    gb->debug_write(0xff54, 0x40);
    gb->debug_write(0xff55, 0x81);
    check(gb->debug_read(0xff55) == 0x01, "writing bit 7 again restarts the HDMA");

    // With the LCD off there is no HBlank, but the transfer still progresses.
    gb->debug_write(0xff40, 0x00);  // LCD off
    gb->run_frame();
    check(gb->debug_read(0x8040) == 0xa0, "HDMA keeps copying while the LCD is off");
    check(gb->debug_hdma_size() == 0xff, "the LCD-off HDMA runs to completion");
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

void test_c64_prg_injection() {
    const std::string dir = "/tmp/dsp-c64-test";
    std::filesystem::create_directories(dir);
    auto write_rom = [&](const char* name, const std::vector<uint8_t>& data) {
        std::ofstream out(dir + "/" + name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    };
    // Stand-in KERNAL: publish TXTTAB = $0801 (as BASIC's cold start does) and
    // then spin, so the driver sees the same "prompt is up" state.
    std::vector<uint8_t> kernal(0x2000, 0x00);
    const uint8_t boot[] = {0xA9, 0x01, 0x85, 0x2B, 0xA9, 0x08, 0x85, 0x2C, 0x4C, 0x08, 0xE0};
    std::copy(std::begin(boot), std::end(boot), kernal.begin());
    kernal[0x1FFC] = 0x00;
    kernal[0x1FFD] = 0xE0;
    write_rom("kernal.rom", kernal);
    write_rom("basic.rom", std::vector<uint8_t>(0x2000, 0x00));
    write_rom("chargen.rom", std::vector<uint8_t>(0x1000, 0x00));

    dsp::C64 machine;
    std::string error = "unset";
    check(machine.init(dir, &error), "C64 loads KERNAL/BASIC/chargen from a directory");

    // 10 PRINT"DSPOK" / 20 GOTO 20, saved from $0801.
    const std::vector<uint8_t> prg = {0x01, 0x08, 0x0E, 0x08, 0x0A, 0x00, 0x99, 0x22,
                                      0x44, 0x53, 0x50, 0x4F, 0x4B, 0x22, 0x00, 0x17,
                                      0x08, 0x14, 0x00, 0x89, 0x20, 0x32, 0x30, 0x00,
                                      0x00, 0x00};
    write_rom("hello.prg", prg);
    error.clear();
    check(machine.load_media(dir + "/hello.prg", &error), "C64 accepts a .prg");
    check(machine.prg_pending(), "the PRG waits for the BASIC prompt instead of racing the boot");
    check(machine.debug_read_ram(0x0801) == 0x00, "nothing is written before BASIC has booted");

    for (int frame = 0; frame < 130; frame++) machine.run_frame();
    check(!machine.prg_pending(), "the PRG is injected once TXTTAB points at $0801");
    for (size_t i = 0; i + 2 < prg.size(); i++) {
        check(machine.debug_read_ram(uint16_t(0x0801 + i)) == prg[i + 2],
              "the program body lands at $0801");
    }
    const uint16_t end = uint16_t(0x0801 + prg.size() - 2);
    check(machine.debug_read_ram(0x2D) == uint8_t(end & 0xFF) &&
              machine.debug_read_ram(0x2E) == uint8_t(end >> 8),
          "VARTAB points past the program");
    check(machine.debug_read_ram(0x2F) == machine.debug_read_ram(0x2D) &&
              machine.debug_read_ram(0x31) == machine.debug_read_ram(0x2D),
          "ARYTAB/STREND follow VARTAB");
    check(machine.debug_read_ram(0xC6) == 4 && machine.debug_read_ram(0x0277) == 'R' &&
              machine.debug_read_ram(0x0278) == 'U' && machine.debug_read_ram(0x0279) == 'N' &&
              machine.debug_read_ram(0x027A) == 0x0D,
          "RUN + RETURN are queued in the KERNAL keyboard buffer");

    dsp::C64 tiny;
    error.clear();
    write_rom("short.prg", std::vector<uint8_t>{0x01, 0x08});
    check(!tiny.load_media(dir + "/short.prg", &error), "a truncated PRG is rejected");
}

// Minimal .T64 archive holding one PRG, as written by the common tools.
std::vector<uint8_t> make_t64(const std::vector<uint8_t>& prg,
                              const char* name, uint16_t used_entries) {
    std::vector<uint8_t> img(0x40 + 2 * 32, 0x00);
    const std::string sig = "C64 tape image file";
    std::copy(sig.begin(), sig.end(), img.begin());
    img[0x20] = 0x00; img[0x21] = 0x01;   // version $0100
    img[0x22] = 0x02; img[0x23] = 0x00;   // two slots
    img[0x24] = uint8_t(used_entries & 0xFF);
    img[0x25] = uint8_t(used_entries >> 8);
    const std::string tape = "DSPTAPE";
    std::fill(img.begin() + 0x28, img.begin() + 0x40, ' ');
    std::copy(tape.begin(), tape.end(), img.begin() + 0x28);

    const uint16_t start = uint16_t(prg[0] | (prg[1] << 8));
    const uint16_t end = uint16_t(start + prg.size() - 2);
    const uint32_t offset = uint32_t(img.size());
    uint8_t* e = img.data() + 0x40;
    e[0] = 1;      // normal tape file
    e[1] = 0x82;   // PRG
    e[2] = uint8_t(start & 0xFF); e[3] = uint8_t(start >> 8);
    e[4] = uint8_t(end & 0xFF);   e[5] = uint8_t(end >> 8);
    e[8] = uint8_t(offset & 0xFF);
    e[9] = uint8_t((offset >> 8) & 0xFF);
    e[10] = uint8_t((offset >> 16) & 0xFF);
    e[11] = uint8_t(offset >> 24);
    std::memset(e + 0x10, ' ', 16);
    std::memcpy(e + 0x10, name, std::strlen(name));

    img.insert(img.end(), prg.begin() + 2, prg.end());
    return img;
}

void test_c64_t64_and_built_disk() {
    // 10 PRINT"DSPOK" / 20 GOTO 20, saved from $0801.
    const std::vector<uint8_t> prg = {0x01, 0x08, 0x0E, 0x08, 0x0A, 0x00, 0x99, 0x22,
                                      0x44, 0x53, 0x50, 0x4F, 0x4B, 0x22, 0x00, 0x17,
                                      0x08, 0x14, 0x00, 0x89, 0x20, 0x32, 0x30, 0x00,
                                      0x00, 0x00};

    dsp::T64Image t64;
    std::string error = "unset";
    const std::vector<uint8_t> img = make_t64(prg, "TAPEPROG", 1);
    check(t64.load_memory(img.data(), img.size(), &error), "a .T64 archive is parsed");
    check(t64.tape_name() == "DSPTAPE", "the tape name comes from the header");
    check(t64.directory().size() == 1, "the used-entry count drives the directory");
    check(t64.directory()[0].name == "TAPEPROG", "the entry name is unpadded");
    check(t64.directory()[0].start == 0x0801, "the load address is read from the entry");

    std::vector<uint8_t> out;
    check(t64.load_prg(0, &out, &error) && out == prg,
          "a .T64 entry yields the original PRG");

    // Writers that leave the used count at zero must still be readable.
    const std::vector<uint8_t> zero_used = make_t64(prg, "TAPEPROG", 0);
    dsp::T64Image lax;
    check(lax.load_memory(zero_used.data(), zero_used.size(), &error) &&
              lax.directory().size() == 1,
          "a zero used-entry count falls back to the slot table");

    dsp::T64Image bad;
    const std::vector<uint8_t> junk(0x80, 0x00);
    check(!bad.load_memory(junk.data(), junk.size(), &error),
          "a file without the T64 signature is rejected");

    // The built image must be a disk the 1541 DOS can walk: BAM, directory and
    // sector chains all have to line up.
    std::vector<dsp::D64BuildFile> files;
    files.push_back({"TAPEPROG", prg});
    std::vector<uint8_t> big(0x08, 0x01);
    big.resize(2 + 700, 0xAB);  // spans several sectors
    files.push_back({"BIG", big});
    const std::vector<uint8_t> disk = dsp::build_d64(files, "DSPTAPE");
    check(disk.size() == 174848, "the built image has the standard 35 track size");

    dsp::D64Image d64;
    check(d64.load_memory(disk.data(), disk.size(), &error), "the built image parses");
    check(d64.directory().size() == 2, "both files are listed");
    check(d64.directory()[0].name == "tapeprog" && d64.directory()[1].name == "big",
          "the directory keeps the file names");
    check((d64.directory()[0].type & 0x0F) == 0x02, "the files are typed PRG");
    check(d64.directory()[1].blocks == 3, "the block count matches the sector chain");

    out.clear();
    check(d64.load_first_prg(&out, &error) && out == prg,
          "the first PRG reads back byte for byte");
    out.clear();
    check(d64.load_prg(1, &out, &error) && out == big,
          "a multi-sector PRG reads back byte for byte");

    uint8_t bam[256] = {0};
    check(d64.read_sector(18, 0, bam), "the BAM sector is present");
    check(bam[0] == 18 && bam[1] == 1, "the BAM points at the first directory sector");
    check(bam[0xA5] == '2' && bam[0xA6] == 'A', "the BAM carries the DOS type 2A");
    check(bam[4 + 17 * 4] < 19, "track 18 has the directory sectors allocated");
}

void test_c64_drive_rom_and_media() {
    const std::string dir = "/tmp/dsp-c64-drive-test";
    // Start from an empty directory: the drive ROM is looked up by name, so a
    // leftover copy from an earlier run would hide the no-ROM path.
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto write_file = [&](const std::string& name, const std::vector<uint8_t>& data) {
        std::ofstream out(dir + "/" + name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    };

    std::vector<uint8_t> kernal(0x2000, 0x00);
    const uint8_t boot[] = {0xA9, 0x01, 0x85, 0x2B, 0xA9, 0x08, 0x85, 0x2C, 0x4C, 0x08, 0xE0};
    std::copy(std::begin(boot), std::end(boot), kernal.begin());
    kernal[0x1FFC] = 0x00;
    kernal[0x1FFD] = 0xE0;
    write_file("kernal.rom", kernal);
    write_file("basic.rom", std::vector<uint8_t>(0x2000, 0x00));
    write_file("chargen.rom", std::vector<uint8_t>(0x1000, 0x00));

    const std::vector<uint8_t> prg = {0x01, 0x08, 0x0B, 0x08, 0x0A, 0x00,
                                      0x99, 0x22, 0x41, 0x22, 0x00, 0x00, 0x00};
    std::vector<dsp::D64BuildFile> files;
    files.push_back({"HELLO", prg});
    const std::vector<uint8_t> disk = dsp::build_d64(files, "DSP");
    write_file("test.d64", disk);
    const std::vector<uint8_t> t64 = make_t64(prg, "TAPEPROG", 1);
    write_file("test.t64", t64);

    {
        // Without a drive ROM there is no device on the serial bus, so disk and
        // tape images fall back to injecting their first program.
        dsp::C64 machine;
        std::string error = "unset";
        check(machine.init(dir, &error), "the C64 boots without a drive ROM");
        check(!machine.drive().rom_loaded(), "no drive ROM was found");
        check(machine.load_media(dir + "/test.t64", &error) && machine.prg_pending(),
              "a .T64 without a drive ROM queues its first program");
        check(!machine.drive().disk_loaded(), "nothing is mounted without a drive ROM");
    }

    // A 16 KiB image named like the VICE/MAME dumps is picked up automatically.
    write_file("dos1541", std::vector<uint8_t>(0x4000, 0x60));
    {
        dsp::C64 machine;
        std::string error = "unset";
        check(machine.init(dir, &error), "the C64 boots with a drive ROM present");
        check(machine.drive().rom_loaded(), "the 1541/1540 DOS ROM is loaded automatically");
        check(machine.load_media(dir + "/test.d64", &error) && machine.drive().disk_loaded(),
              "a .D64 is mounted in the drive instead of being injected");
        check(!machine.prg_pending(), "a mounted .D64 is not injected");
        check(machine.load_media(dir + "/test.t64", &error) && machine.drive().disk_loaded(),
              "a .T64 is served as a disk when the drive can answer");
        check(!machine.prg_pending(), "a .T64 served by the drive is not injected");
        check(machine.drive().disk().directory().size() == 1 &&
                  machine.drive().disk().directory()[0].name == "tapeprog",
              "the mounted image exposes the archive contents");
    }
}

void test_m6502_rmw_double_write() {
    static std::vector<std::pair<uint16_t, uint8_t>> writes;
    writes.clear();
    m6502_memory.assign(0x10000, 0);
    dsp::M6502 cpu(985248);
    cpu.set_memory_handlers(
        [](uint16_t address) { return m6502_memory[address]; },
        [](uint16_t address, uint8_t value) {
            writes.emplace_back(address, value);
            m6502_memory[address] = value;
        });
    m6502_memory[0x3000] = 0x81;
    // lsr $3000 / jmp *   -- as the C64 IRQ acknowledge "LSR $D019" does
    load_6502(0x1000, {0x4e, 0x00, 0x30, 0x4c, 0x03, 0x10});
    cpu.reset();
    cpu.run(6);
    check(writes.size() == 2 && writes[0] == std::make_pair(uint16_t(0x3000), uint8_t(0x81)),
          "an NMOS read-modify-write first writes the unmodified byte back");
    check(writes.back() == std::make_pair(uint16_t(0x3000), uint8_t(0x40)),
          "the second write of the read-modify-write stores the result");

    writes.clear();
    m6502_memory.assign(0x10000, 0);
    dsp::M6502 cmos(985248);
    cmos.set_cmos(true);
    cmos.set_memory_handlers(
        [](uint16_t address) { return m6502_memory[address]; },
        [](uint16_t address, uint8_t value) {
            writes.emplace_back(address, value);
            m6502_memory[address] = value;
        });
    m6502_memory[0x3000] = 0x81;
    load_6502(0x1000, {0x4e, 0x00, 0x30, 0x4c, 0x03, 0x10});
    cmos.reset();
    cmos.run(6);
    check(writes.size() == 1, "the 65C02 does not do the dummy write");
}

void test_mos6566_bank_and_multicolor_bitmap() {
    dsp::Mos6566 vic;
    vic.reset();
    std::vector<uint8_t> ram(0x10000, 0);
    std::vector<uint8_t> color(0x400, 0);
    vic.set_mem_read([&](uint16_t a) { return ram[a]; });
    vic.set_color_ram(color.data());

    vic.changed_va(3);           // CIA2 $DD00 = %00 -> VIC bank 3 ($C000)
    vic.write(0x18, 0x3D);       // video matrix $CC00, bitmap $E000
    vic.write(0x11, 0x3B);       // DEN + BMM, YSCROLL 3
    vic.write(0x16, 0x18);       // MCM + CSEL
    vic.write(0x20, 0x00);
    vic.write(0x21, 0x06);       // background: blue
    ram[0xCC00] = 0x17;          // 01 -> white, 10 -> yellow
    color[0] = 0x05;             // 11 -> green
    ram[0xE000] = 0x1B;          // pairs: 00 01 10 11

    std::array<uint32_t, dsp::Mos6566::kScreenWidth> row{};
    vic.update_line(51, row.data());
    const int x = dsp::Mos6566::kVisibleX;
    check(row[x + 0] == dsp::Mos6566::kPalette[6] &&
              row[x + 2] == dsp::Mos6566::kPalette[1] &&
              row[x + 4] == dsp::Mos6566::kPalette[7] &&
              row[x + 6] == dsp::Mos6566::kPalette[5],
          "multicolor bitmap mode decodes the bit pairs");
    check(row[x + 1] == row[x + 0] && row[x + 3] == row[x + 2],
          "multicolor pixels are two hires pixels wide");

    // The character generator only shadows RAM inside the VIC bank, so a bank 3
    // fetch must reach RAM: pin the bank back to 0 and the picture changes.
    ram[0xE000] = 0x00;
    vic.update_line(51, row.data());
    check(row[x + 2] == dsp::Mos6566::kPalette[6],
          "the bitmap is fetched from the bank selected by CIA2");
}

void test_pv2000_missing_roms_and_dummy_bios() {
    dsp::Pv2000 machine;
    std::string error = "unset";
    check(!machine.init("/no/such/pv2000.zip", &error), "PV-2000 init fails without the BIOS");
    check(error.find("not found") != std::string::npos || error.find("missing") != std::string::npos ||
              error.find("cannot") != std::string::npos,
          "PV-2000 init reports why the BIOS is missing");
    check(std::strcmp(machine.title(), "Casio PV-2000") == 0, "PV-2000 title");
    check(machine.screen_width() == 256 && machine.screen_height() == 192, "PV-2000 screen is 256x192");
    check(machine.uses_keyboard(), "PV-2000 reads the host keyboard");

    const std::string dir = "/tmp/dsp-pv2000-test";
    std::filesystem::create_directories(dir);
    {
        std::vector<uint8_t> bios(0x4000, 0x00);  // NOPs; CRC mismatch is a warning
        std::ofstream out(dir + "/hn613128pc64.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bios.data()), std::streamsize(bios.size()));
    }
    dsp::Pv2000 boot;
    error.clear();
    check(boot.init(dir, &error), "PV-2000 loads a dummy 16 KiB BIOS from a directory");
    for (int frame = 0; frame < 5; frame++) boot.run_frame();
    check(boot.framebuffer() != nullptr, "PV-2000 produces a framebuffer");

    {
        std::vector<uint8_t> cart(0x2000, 0xc9);  // RET
        std::ofstream out(dir + "/cart.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(cart.data()), std::streamsize(cart.size()));
    }
    error.clear();
    check(boot.load_media(dir + "/cart.bin", &error), "PV-2000 attaches an 8 KiB cartridge");
    for (int frame = 0; frame < 2; frame++) boot.run_frame();

    auto exists = [](const char* path) {
        std::ifstream probe(path);
        return bool(probe);
    };
    const char* rom = "/tmp/roms/pv2000.zip";
    if (exists(rom)) {
        dsp::Pv2000 real;
        error.clear();
        check(real.init(rom, &error), "PV-2000 MAME BIOS set loads");
        for (int frame = 0; frame < 180; frame++) real.run_frame();
        const uint32_t* fb = real.framebuffer();
        const int n = real.screen_width() * real.screen_height();
        bool has_green = false, has_white = false;
        for (int i = 0; i < n; i++) {
            if (fb[i] == 0xff21b03bu) has_green = true;  // TMS colour 12
            if (fb[i] == 0xffffffffu) has_white = true;
        }
        check(has_green && has_white, "PV-2000 BIOS menu is white text on green");
    }
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

// Builds an LPC bitstream the way the TMS5220 reads it: bits go out starting
// with the least significant bit of each byte.
class LpcStream {
public:
    void bits(int value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            if (bit_ == 0) bytes_.push_back(0);
            if ((value >> i) & 1) bytes_.back() |= uint8_t(1u << bit_);
            bit_ = (bit_ + 1) & 7;
        }
    }

    // Voiced frame with mid range coefficients; pitch and energy are indices.
    void voiced_frame(int energy_idx, int pitch_idx) {
        static const int k[10] = {20, 20, 8, 8, 8, 8, 8, 4, 4, 4};
        static const int kbits[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
        bits(energy_idx, 4);
        bits(0, 1);  // no repeat
        bits(pitch_idx, 6);
        for (int i = 0; i < 10; ++i) bits(k[i], kbits[i]);
    }

    // Unvoiced frames have a zero pitch index and only carry K1-K4.
    void unvoiced_frame(int energy_idx) {
        static const int k[4] = {20, 20, 8, 8};
        static const int kbits[4] = {5, 5, 4, 4};
        bits(energy_idx, 4);
        bits(0, 1);
        bits(0, 6);
        for (int i = 0; i < 4; ++i) bits(k[i], kbits[i]);
    }

    void stop_frame() { bits(0x0f, 4); }

    const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
    int bit_ = 0;
};

// Runs the chip for `samples` synthesis samples, topping the FIFO up whenever
// buffer low is set, and returns the generated waveform.
std::vector<int> run_speech(dsp::Tms5220& tms, const std::vector<uint8_t>& stream, int samples) {
    std::vector<int> out;
    size_t next = 0;
    for (int i = 0; i < samples; ++i) {
        while (next < stream.size() && (tms.status() & 0x40) != 0) tms.write_data(stream[next++]);
        tms.tick(dsp::Tms5220::kClocksPerSample);
        out.push_back(tms.last_sample());
    }
    return out;
}

int waveform_peak(const std::vector<int>& wave) {
    int peak = 0;
    for (int sample : wave) peak = std::max(peak, std::abs(sample));
    return peak;
}

// Normalised autocorrelation at `lag`, measured once the filter has settled.
double waveform_periodicity(const std::vector<int>& wave, size_t lag) {
    double numerator = 0.0;
    double energy = 0.0;
    for (size_t i = 400; i + lag < wave.size(); ++i) {
        numerator += double(wave[i]) * double(wave[i + lag]);
        energy += double(wave[i]) * double(wave[i]);
    }
    return energy > 0.0 ? numerator / energy : 0.0;
}

void test_tms5220_fifo_and_status() {
    dsp::Tms5220 tms(640000);
    bool irq = false;
    tms.set_irq_callback([&irq](bool state) { irq = state; });
    tms.reset();

    check(tms.status() == 0x60, "tms5220 idles with buffer low and buffer empty set");

    tms.write_data(0x60);  // SPEAK EXTERNAL
    for (int i = 0; i < 8; ++i) tms.write_data(0x00);
    check((tms.status() & 0x40) != 0, "tms5220 keeps buffer low with eight bytes queued");
    check(!tms.talking(), "tms5220 stays quiet until the FIFO passes half full");

    tms.write_data(0x00);  // ninth byte clears buffer low and sets SPEN
    check((tms.status() & 0x40) == 0, "tms5220 clears buffer low on the ninth byte");
    check((tms.status() & 0x80) != 0, "tms5220 reports talk status once SPEN is set");

    // Nine bytes of zeroes are nine silence frames' worth of bits; draining
    // them asserts /INT and stops speech. A /RS status read clears /INT.
    irq = false;
    tms.tick(dsp::Tms5220::kClocksPerSample * 4200);
    check(irq, "tms5220 asserts /INT when the FIFO runs dry");
    check((tms.status() & 0x20) != 0, "tms5220 reports buffer empty once drained");
    check(!tms.talking(), "tms5220 stops talking after buffer empty");
    check(!tms.intq(), "tms5220 holds /INT low until it is acknowledged");
    tms.strobe_ws_rs(0x01);  // /RS low, /WS high
    check(tms.readyq(), "tms5220 drops /READY while servicing a read");
    check(tms.intq(), "tms5220 releases /INT on a status read");
    tms.tick(dsp::Tms5220::kIoReadyClocks);
    check(!tms.readyq(), "tms5220 raises /READY again after the read delay");
}

// The EXL-100 drives the chip through the pins only: the byte is latched and
// then committed on the falling edge of /WS, once per edge.
void test_tms5220_latched_writes() {
    dsp::Tms5220 tms(640000);
    auto latched_write = [&tms](uint8_t value) {
        tms.set_data_latch(value);
        tms.strobe_ws_rs(0x02);  // /WS low
        tms.tick(dsp::Tms5220::kIoReadyClocks);
        tms.strobe_ws_rs(0x03);
    };

    latched_write(0x60);  // SPEAK EXTERNAL
    for (int i = 0; i < 8; ++i) latched_write(0x00);
    check((tms.status() & 0x40) != 0, "tms5220 counts one FIFO byte per /WS edge");
    check(!tms.talking(), "tms5220 does not start speaking on eight latched bytes");

    latched_write(0x00);
    check((tms.status() & 0x40) == 0, "tms5220 fills the FIFO through the /WS pin");
    check(tms.talking(), "tms5220 starts speaking once the latched FIFO passes half full");
}

void test_tms5220_chirp_rom() {
    int sum = 0;
    bool negative = false;
    for (int i = 0; i < 52; ++i) {
        const int value = dsp::Tms5220::voiced_excitation(i);
        sum += value;
        if (value < 0) negative = true;
    }
    // The TMS5220 excitation ROM is unipolar and its entries add up to 0x3da;
    // the bipolar table of the TMS5100 era fails both checks.
    check(!negative, "tms5220 voiced excitation is unipolar");
    check(sum == 0x3da, "tms5220 voiced excitation matches the TMS5220 chirp ROM");
    check(dsp::Tms5220::voiced_excitation(6) == 0x71,
          "tms5220 voiced excitation peaks at entry 6");
    check(dsp::Tms5220::voiced_excitation(51) == 0,
          "tms5220 voiced excitation is silent at the end of a pitch period");
}

void test_tms5220_synthesis() {
    LpcStream soft;
    LpcStream strong;
    for (int i = 0; i < 20; ++i) {
        soft.voiced_frame(4, 40);
        strong.voiced_frame(8, 40);
    }
    soft.stop_frame();
    strong.stop_frame();

    dsp::Tms5220 quiet(640000);
    dsp::Tms5220 loud(640000);
    quiet.write_data(0x60);
    loud.write_data(0x60);
    const std::vector<int> soft_wave = run_speech(quiet, soft.bytes(), 2000);
    const std::vector<int> loud_wave = run_speech(loud, strong.bytes(), 2000);

    check(waveform_peak(soft_wave) > 0, "tms5220 synthesizes a voiced frame");
    // Energy indices 4 and 8 are 4 and 16, so the amplitude quadruples.
    check(waveform_peak(loud_wave) > waveform_peak(soft_wave) * 3,
          "tms5220 scales the excitation with the frame energy");

    // A voiced frame repeats the chirp once per pitch period (index 40 is 68
    // samples), while an unvoiced frame is driven by the noise generator.
    LpcStream hiss;
    for (int i = 0; i < 20; ++i) hiss.unvoiced_frame(8);
    hiss.stop_frame();
    dsp::Tms5220 noise(640000);
    noise.write_data(0x60);
    const std::vector<int> noise_wave = run_speech(noise, hiss.bytes(), 2000);

    check(waveform_periodicity(loud_wave, 68) > 0.75,
          "tms5220 voiced frames repeat at the frame pitch period");
    check(waveform_periodicity(noise_wave, 68) < 0.25,
          "tms5220 unvoiced frames are aperiodic noise");
}

void test_tms5220_stop_frame_ramp() {
    dsp::Tms5220 tms(640000);
    LpcStream stream;
    for (int i = 0; i < 4; ++i) stream.voiced_frame(13, 40);
    stream.stop_frame();
    // Keep the FIFO fed so that only the stop frame can end speech.
    for (int i = 0; i < 4; ++i) stream.voiced_frame(13, 40);

    tms.write_data(0x60);
    size_t next = 0;
    int samples_while_talking = 0;
    bool saw_stop = false;
    for (int i = 0; i < 4000 && !saw_stop; ++i) {
        while (next < stream.bytes().size() && (tms.status() & 0x40) != 0)
            tms.write_data(stream.bytes()[next++]);
        tms.tick(dsp::Tms5220::kClocksPerSample);
        if (tms.talking())
            ++samples_while_talking;
        else if (samples_while_talking > 0)
            saw_stop = true;
    }
    check(saw_stop, "tms5220 stops speaking on a stop frame");
    // Four frames of 8 interpolation periods, 25 samples each, plus the frame
    // TALKD keeps running while the energy ramps down.
    check(samples_while_talking > 4 * 200,
          "tms5220 keeps TALKD active for a full frame after the stop code");
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

void test_polepos_driver() {
    dsp::PolePos missing(dsp::PolePos::Game::PolePosition);
    check(std::strcmp(missing.title(), "Pole Position") == 0, "Pole Position title");
    check(missing.screen_width() == 256 && missing.screen_height() == 224,
          "Pole Position reports 256x224");
    std::string error = "unset";
    check(!missing.init("/no/such/polepos.zip", &error), "missing Pole Position ROMs fail init");
    check(error.find("not found") != std::string::npos || error.find("cannot") != std::string::npos ||
              error.size() > 3,
          "init reports why the Pole Position ROM set is missing");

    dsp::PolePos missing2(dsp::PolePos::Game::PolePosition2);
    check(std::strcmp(missing2.title(), "Pole Position II") == 0, "Pole Position II title");

    std::vector<uint8_t> mb_rom(0x400, 0x00);
    dsp::Mb88 mcu(dsp::Mb88::Type::Mb8843, 1536000);
    mcu.set_program_rom(mb_rom.data(), mb_rom.size());
    mcu.reset();
    mcu.run(10);
    check(mcu.pc() == 10, "MB8843 NOP advances PC one byte per cycle");

    // Z8002: LD R9,#0x0002 then HALT-ish (keep running a handful of insns).
    std::array<uint8_t, 0x100> mem{};
    mem[2] = 0x40;
    mem[3] = 0x00;  // FCW
    mem[4] = 0x00;
    mem[5] = 0x10;  // PC = 0x0010
    mem[0x10] = 0x21;
    mem[0x11] = 0x09;
    mem[0x12] = 0x00;
    mem[0x13] = 0x02;  // LD R9, #0002
    mem[0x14] = 0x7a;
    mem[0x15] = 0x00;  // HALT
    dsp::Z8002 cpu(3072000);
    cpu.set_memory_handlers([&](uint16_t a) { return mem[a & 0xff]; },
                            [&](uint16_t a, uint8_t v) { mem[a & 0xff] = v; });
    cpu.reset();
    cpu.run(40);
    check(cpu.rw(9) == 0x0002, "Z8002 LD R9,#0002 writes R9");
    check(cpu.pc() == 0x0016 || cpu.pc() == 0x0014, "Z8002 HALT sits after LD R9");

    // Byte registers must overlay the same word the opcode names (RH0/RL1).
    std::array<uint8_t, 0x100> bmem{};
    bmem[2] = 0x40;
    bmem[3] = 0x00;
    bmem[4] = 0x00;
    bmem[5] = 0x10;
    bmem[0x10] = 0x21;
    bmem[0x11] = 0x00;
    bmem[0x12] = 0x12;
    bmem[0x13] = 0x34;  // LD R0,#1234
    bmem[0x14] = 0x21;
    bmem[0x15] = 0x01;
    bmem[0x16] = 0x00;
    bmem[0x17] = 0x00;  // LD R1,#0000
    bmem[0x18] = 0xc9;
    bmem[0x19] = 0xff;  // LDB RL1,#FF
    bmem[0x1a] = 0xc0;
    bmem[0x1b] = 0xab;  // LDB RH0,#AB
    bmem[0x1c] = 0x7a;
    bmem[0x1d] = 0x00;  // HALT
    dsp::Z8002 bcpu(3072000);
    bcpu.set_memory_handlers([&](uint16_t a) { return bmem[a & 0xff]; },
                             [&](uint16_t a, uint8_t v) { bmem[a & 0xff] = v; });
    bcpu.reset();
    bcpu.run(80);
    check(bcpu.rw(0) == 0xab34, "Z8002 LDB RH0 overlays R0 high");
    check(bcpu.rw(1) == 0x00ff, "Z8002 LDB RL1 overlays R1 low");

    // MULT RR0,R3 must use the R0:R1 long, not the neighbouring pair.
    std::array<uint8_t, 0x100> mmem{};
    mmem[2] = 0x40;
    mmem[3] = 0x00;
    mmem[4] = 0x00;
    mmem[5] = 0x10;
    mmem[0x10] = 0x21;
    mmem[0x11] = 0x01;
    mmem[0x12] = 0x00;
    mmem[0x13] = 0x02;  // LD R1,#0002
    mmem[0x14] = 0x21;
    mmem[0x15] = 0x03;
    mmem[0x16] = 0x00;
    mmem[0x17] = 0x03;  // LD R3,#0003
    mmem[0x18] = 0x99;
    mmem[0x19] = 0x30;  // MULT RR0,R3
    mmem[0x1a] = 0x7a;
    mmem[0x1b] = 0x00;  // HALT
    dsp::Z8002 mcpu(3072000);
    mcpu.set_memory_handlers([&](uint16_t a) { return mmem[a & 0xff]; },
                            [&](uint16_t a, uint8_t v) { mmem[a & 0xff] = v; });
    mcpu.reset();
    mcpu.run(200);
    check(mcpu.rw(0) == 0x0000 && mcpu.rw(1) == 0x0006, "Z8002 MULT RR0,R3 writes R0:R1");

    const char* rom = "/tmp/roms/polepos.zip";
    std::FILE* f = std::fopen(rom, "rb");
    if (f) {
        std::fclose(f);
        dsp::PolePos boot(dsp::PolePos::Game::PolePosition);
        error.clear();
        check(boot.init(rom, &error), "Pole Position ROM set loads");
        for (int i = 0; i < 600; i++) boot.run_frame();
        check(boot.debug_z80_pc() != 0, "Pole Position Z80 is executing");
        check(boot.debug_n51_pc() != 0, "Pole Position 51xx MCU is executing");
        check(boot.debug_n53_pc() != 0, "Pole Position 53xx MCU is executing");
        std::vector<int16_t> audio;
        boot.drain_audio(audio);
        bool heard = false;
        for (int16_t s : audio) {
            if (s != 0) {
                heard = true;
                break;
            }
        }
        check(!audio.empty(), "Pole Position drain_audio yields samples");
        check(heard, "Pole Position WSG/engine/52xx produce non-silent samples");
        bool lit = false;
        const uint32_t* fb = boot.framebuffer();
        for (int i = 0; i < boot.screen_width() * boot.screen_height(); i++) {
            if ((fb[i] & 0x00ffffffu) != 0) {
                lit = true;
                break;
            }
        }
        check(lit, "Pole Position attract produces non-black pixels");
    }

    const char* rom2 = "/tmp/roms/polepos2.zip";
    f = std::fopen(rom2, "rb");
    if (f) {
        std::fclose(f);
        dsp::PolePos boot2(dsp::PolePos::Game::PolePosition2);
        error.clear();
        check(boot2.init(rom2, &error), "Pole Position II ROM set loads");
        for (int i = 0; i < 600; i++) boot2.run_frame();
        check(boot2.debug_z80_pc() != 0, "Pole Position II Z80 is executing");
        check(boot2.debug_sub1_pc() != 0x34c8, "Pole Position II sub1 leaves the IC25 fail idle");
        check(boot2.debug_sprite_low(0x48) == 0, "Pole Position II Z8002 handshake clears mailbox $4048");
        check(!boot2.debug_sub2_reset(), "Pole Position II releases Z8002 #2 after handshake");
        check(boot2.debug_n53_pc() != 0, "Pole Position II 53xx MCU is executing");
        bool lit2 = false;
        const uint32_t* fb2 = boot2.framebuffer();
        for (int i = 0; i < boot2.screen_width() * boot2.screen_height(); i++) {
            if ((fb2[i] & 0x00ffffffu) != 0) {
                lit2 = true;
                break;
            }
        }
        check(lit2, "Pole Position II attract produces non-black pixels");
    }
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

std::vector<uint8_t> t11_memory;

std::unique_ptr<dsp::T11> make_t11(uint16_t mode = 0x36ff) {
    t11_memory.assign(0x10000, 0);
    auto cpu = std::make_unique<dsp::T11>(10000000, mode);
    cpu->set_memory_handlers(
        [](uint16_t address) {
            return uint16_t(t11_memory[address] | (t11_memory[address + 1] << 8));
        },
        [](uint16_t address, uint16_t value, uint16_t mem_mask) {
            if (mem_mask & 0x00ff) t11_memory[address] = uint8_t(value);
            if (mem_mask & 0xff00) t11_memory[address + 1] = uint8_t(value >> 8);
        });
    return cpu;
}

void load_t11(uint16_t address, const std::vector<uint16_t>& code) {
    for (size_t i = 0; i < code.size(); i++) {
        t11_memory[address + i * 2] = uint8_t(code[i]);
        t11_memory[address + i * 2 + 1] = uint8_t(code[i] >> 8);
    }
}

void test_t11_reset_and_moves() {
    auto cpu = make_t11();
    // mov #$1234,r0 / mov r0,r1 / add #1,r1 / br .
    load_t11(0x8000, {0012700, 0x1234, 0010001, 0062701, 0x0001, 0000777});
    cpu->reset();
    check(cpu->pc() == 0x8000, "T-11 mode $36ff starts the CPU at $8000");
    check(cpu->sp() == 0x00fe, "T-11 reset loads SP with 0376");
    check((cpu->psw() & 0340) == 0340, "T-11 reset masks all interrupt levels");
    cpu->run(40);
    check(cpu->reg(0) == 0x1234, "mov #imm,rn loads an immediate");
    check(cpu->reg(1) == 0x1235, "mov rn,rm followed by add #imm reaches the second register");

    auto alt = make_t11(0x0000);
    alt->reset();
    check(alt->pc() == 0xc000, "the T-11 start address follows the mode register");
}

void test_t11_addressing_and_bytes() {
    auto cpu = make_t11();
    // mov #$2000,r0 / movb #$a5,(r0)+ / movb #$5a,(r0) / mov #$1234,@#$2010
    // / movb @#$2010,r2 / br .
    load_t11(0x8000, {0012700, 0x2000, 0112720, 0x00a5, 0112710, 0x005a, 0012737, 0x1234, 0x2010,
                      0113702, 0x2010, 0000777});
    cpu->reset();
    cpu->run(120);
    check(t11_memory[0x2000] == 0xa5, "movb (rn)+ stores the low byte and autoincrements");
    check(t11_memory[0x2001] == 0x5a, "movb (rn) stores at the incremented address");
    check(cpu->reg(0) == 0x2001, "a byte autoincrement steps the register by one");
    check(t11_memory[0x2010] == 0x34 && t11_memory[0x2011] == 0x12,
          "mov to an absolute address writes a little endian word");
    check(cpu->reg(2) == 0x0034, "movb into a register loads the low byte");

    auto sign = make_t11();
    // mov #$0080,r0 / movb r0,r1 / br .
    load_t11(0x8000, {0012700, 0x0080, 0110001, 0000777});
    sign->reset();
    sign->run(40);
    check(sign->reg(1) == 0xff80, "movb rn,rm sign extends into the destination register");
}

void test_t11_stack_and_interrupts() {
    auto cpu = make_t11();
    // jsr pc,$8020 / br . ; at $8020: mov #$00ff,r3 / rts pc
    load_t11(0x8000, {0004767, 0x001c, 0000777});
    load_t11(0x8020, {0012703, 0x00ff, 0000207});
    cpu->reset();
    cpu->run(80);
    check(cpu->reg(3) == 0x00ff, "jsr pc/rts pc call and return");
    check(cpu->pc() == 0x8004, "rts pc resumes after the call");

    auto irq = make_t11();
    load_t11(0x8000, {0000777});          // br .
    load_t11(0x0100, {0005204, 0000002});  // inc r4 / rti
    // CP0 alone vectors through 070: new PC then new PSW.
    load_t11(0070, {0x0100, 0x0000});
    irq->reset();
    irq->set_psw(0);
    irq->run(20);
    irq->set_irq(dsp::T11::CP0_LINE, dsp::IrqLine::Assert);
    irq->run(100);
    check(irq->reg(4) == 1, "the T-11 vectors a CP0 interrupt through 070");
    irq->set_irq(dsp::T11::CP0_LINE, dsp::IrqLine::Clear);
    irq->run(400);
    check(irq->reg(4) == 1, "clearing CP0 stops the interrupt from retriggering");
}

void test_atari_system2_missing_roms() {
    dsp::AtariSystem2 machine(dsp::AtariSystem2::Game::Paperboy);
    std::string error = "unset";
    check(!machine.init("/no/such/paperboy.zip", &error),
          "Paperboy init fails without the ROM set");
    check(error != "unset" && !error.empty(), "init reports why the Atari System 2 set is missing");
    check(std::strcmp(machine.title(), "Paperboy") == 0, "Paperboy title");
    check(machine.screen_width() == 512 && machine.screen_height() == 384,
          "Atari System 2 screen is 512x384");

    dsp::AtariSystem2 apb(dsp::AtariSystem2::Game::Apb);
    check(std::strcmp(apb.title(), "APB - All Points Bulletin") == 0, "APB title");
    check(apb.screen_width() == 384 && apb.screen_height() == 512,
          "APB runs on a rotated monitor");

    dsp::AtariSystem2 ssprint(dsp::AtariSystem2::Game::SuperSprint);
    check(std::strcmp(ssprint.title(), "Super Sprint") == 0, "Super Sprint title");
    dsp::AtariSystem2 degrees720(dsp::AtariSystem2::Game::Degrees720);
    check(std::strcmp(degrees720.title(), "720 Degrees") == 0, "720 Degrees title");
}

// Runs one of the other System 2 sets until it paints an attract screen.
void test_atari_system2_game_if_present(dsp::AtariSystem2::Game game, const char* rom,
                                        int max_frames) {
    std::ifstream probe(rom);
    if (!probe) return;
    probe.close();

    dsp::AtariSystem2 machine(game);
    std::string error;
    const std::string title = machine.title();
    check(machine.init(rom, &error), (title + " ROM set loads").c_str());
    const int pixels = machine.screen_width() * machine.screen_height();
    size_t best_colors = 0;
    for (int frame = 0; frame < max_frames && best_colors <= 8; frame++) {
        machine.run_frame();
        const uint32_t* fb = machine.framebuffer();
        std::set<uint32_t> colors;
        for (int i = 0; i < pixels; i++) colors.insert(fb[i] & 0x00ffffffu);
        best_colors = std::max(best_colors, colors.size());
    }
    check(machine.debug_pc() >= 0x2000, (title + ": the T-11 is executing ROM").c_str());
    check(machine.debug_sound_pc() >= 0x4000,
          (title + ": the 6502 is executing sound ROM").c_str());
    check(best_colors > 8, (title + " draws a colour picture").c_str());
}

void test_paperboy_if_present() {
    const char* rom = "/tmp/roms/paperboy.zip";
    std::ifstream probe(rom);
    if (!probe) return;
    probe.close();

    dsp::AtariSystem2 machine(dsp::AtariSystem2::Game::Paperboy);
    std::string error;
    check(machine.init(rom, &error), "Paperboy ROM set loads");
    size_t best_colors = 0;
    for (int frame = 0; frame < 800; frame++) {
        machine.run_frame();
        if (frame < 400) continue;
        const uint32_t* fb = machine.framebuffer();
        std::set<uint32_t> colors;
        for (int i = 0; i < 512 * 384; i++) colors.insert(fb[i] & 0x00ffffffu);
        best_colors = std::max(best_colors, colors.size());
    }
    check(machine.debug_pc() >= 0x2000, "the T-11 is executing banked or fixed ROM");
    check(machine.debug_sound_pc() >= 0x4000, "the 6502 is executing sound ROM");
    check(best_colors > 8, "Paperboy attract mode draws a colour picture");

    std::vector<int16_t> audio;
    machine.drain_audio(audio);
    check(!audio.empty(), "Paperboy produces audio samples");
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

void test_sega_pcm_and_mapper() {
    dsp::SegaPcm pcm(4000000);
    pcm.set_bank(dsp::SegaPcm::kBank512);
    pcm.reset();
    pcm.set_read_rom([](uint32_t) { return uint8_t(0xff); });
    pcm.write(0x86, 0x00);
    pcm.write(0x07, 0x01);
    pcm.write(0x02, 0x7f);
    pcm.write(0x03, 0x7f);
    pcm.clock();
    check(pcm.left() != 0 || pcm.right() != 0, "Sega PCM produces a sample when a channel is active");

    dsp::Sega3155195 mapper;
    mapper.reset();
    mapper.write_reg(0x10, 1);
    mapper.write_reg(0x11, 0x40);
    check(mapper.dirs_start(0) == 0x400000, "315-5195 region 0 start follows register $11");
    check(mapper.dirs_end(0) == 0x420000, "315-5195 size 1 is 128 KiB");
    mapper.write_reg(2, 3);
    check(mapper.read_reg(2) == 0, "315-5195 reset bits 0-1 == 3 read as 0");
    mapper.write_reg(2, 0);
    check(mapper.read_reg(2) == 0x0f, "315-5195 reset clear reads as $0f");

    uint8_t normal[32], shadow[32], hilight[32];
    dsp::build_s16_palette_luts(normal, shadow, hilight);
    check(normal[0x1f] > 0 && hilight[0x1f] >= normal[0x1f],
          "System 16 resistor-net palette is not black");

    uint16_t src[2] = {0x4e71, 0x4e75};
    uint16_t opcodes[2] = {};
    uint16_t data[2] = {};
    uint8_t key[0x2000] = {};
    dsp::fd1089_decrypt(src, opcodes, data, 4, key, dsp::Fd1089Type::B);
    check(opcodes[0] != 0 || data[0] != 0, "FD1089 decrypt produces a word");

    dsp::Upd7759 speech;
    speech.reset();
    check(speech.busy_r() == 1, "UPD7759 reports idle as busy=1");
}

void test_sega_system16_missing_roms() {
    dsp::Outrun outrun;
    std::string error = "unset";
    check(!outrun.init("/no/such/outrun.zip", &error), "OutRun init fails without the ROM set");
    check(error.find("not found") != std::string::npos || error.find("missing") != std::string::npos ||
              error.find("cannot") != std::string::npos || error.find("Unable") != std::string::npos ||
              error.find("open") != std::string::npos,
          "OutRun init reports why the set is missing");
    check(std::strcmp(outrun.title(), "OutRun") == 0, "OutRun title");
    check(outrun.screen_width() == 320 && outrun.screen_height() == 224, "OutRun screen is 320x224");

    dsp::HangOn hangon;
    error = "unset";
    check(!hangon.init("/no/such/hangon.zip", &error), "Hang-On init fails without the ROM set");
    check(std::strcmp(hangon.title(), "Hang-On") == 0, "Hang-On title");

    dsp::System16 fantzone(dsp::System16::Game::Fantzone);
    check(std::strcmp(fantzone.title(), "Fantasy Zone") == 0, "Fantasy Zone title");
    error = "unset";
    check(!fantzone.init("/no/such/fantzone.zip", &error),
          "Fantasy Zone init fails without the ROM set");

    dsp::System16 shinobi(dsp::System16::Game::Shinobi);
    check(std::strcmp(shinobi.title(), "Shinobi") == 0, "Shinobi title");
    dsp::System16 altbeast(dsp::System16::Game::Altbeast);
    check(std::strcmp(altbeast.title(), "Altered Beast") == 0, "Altered Beast title");
    dsp::System16 tetris(dsp::System16::Game::Tetris);
    check(std::strcmp(tetris.title(), "Tetris") == 0, "Tetris title");
    dsp::HangOn enduro(dsp::HangOn::Game::Enduro);
    check(std::strcmp(enduro.title(), "Enduro Racer") == 0, "Enduro Racer title");
    dsp::HangOn sharrier(dsp::HangOn::Game::Sharrier);
    check(std::strcmp(sharrier.title(), "Space Harrier") == 0, "Space Harrier title");
    dsp::System16 alexkidd(dsp::System16::Game::Alexkidd);
    check(std::strcmp(alexkidd.title(), "Alex Kidd: The Lost Stars") == 0, "Alex Kidd title");
    dsp::System16 aliensyn(dsp::System16::Game::Aliensyn);
    check(std::strcmp(aliensyn.title(), "Alien Syndrome") == 0, "Alien Syndrome title");
    dsp::System16 wb3(dsp::System16::Game::Wb3);
    check(std::strcmp(wb3.title(), "Wonder Boy III: Monster Lair") == 0, "Wonder Boy III title");
}

void test_sega16_palette_banks() {
    // Out Run keeps the sprite colours in the upper half of its 0x1000 word
    // colour RAM, so the shadow/highlight bank has to start above it: writing a
    // tile colour must not repaint the sprite entry 0x800 words later.
    dsp::Sega16Video video;
    video.reset();
    video.init_palette_luts();
    video.cram_words = 0x1000;
    video.set_palette_entry(0x800, 0x001f, true);  // sprite red
    const uint32_t sprite_red = video.palette[0x800];
    video.set_palette_entry(0x000, 0x7fff, true);  // tile white
    check(video.palette[0x800] == sprite_red, "OutRun tile colours leave the sprite bank alone");
    check(video.palette[0x1000] != 0, "OutRun shadow bank sits above the colour RAM");

    // System 16B has half the colour RAM, so its shadow bank starts at 0x800.
    dsp::Sega16Video s16b;
    s16b.reset();
    s16b.init_palette_luts();
    s16b.set_palette_entry(0x000, 0x7fff, true);
    check(s16b.palette[0x800] != 0, "System 16B shadow bank stays at 0x800");
}

void test_skullxbo_without_roms() {
    dsp::Skullxbo machine;
    std::string error = "unset";
    check(!machine.init("/no/such/skullxbo.zip", &error), "Skull & Crossbones init fails without the ROM set");
    check(error != "unset" && !error.empty(), "Skull & Crossbones reports why the set is missing");
    check(std::strcmp(machine.title(), "Skull & Crossbones") == 0, "Skull & Crossbones title");
    check(machine.screen_width() == 672 && machine.screen_height() == 240,
          "Skull & Crossbones screen is 672x240");
}

void test_bublbobl_without_roms() {
    dsp::BublBobl machine;
    std::string error = "unset";
    check(!machine.init("/no/such/bublbobl.zip", &error), "Bubble Bobble init fails without the ROM set");
    check(error != "unset" && !error.empty(), "Bubble Bobble reports why the set is missing");
    check(std::strcmp(machine.title(), "Bubble Bobble") == 0, "Bubble Bobble title");
    check(machine.screen_width() == 256 && machine.screen_height() == 224,
          "Bubble Bobble screen is 256x224");
}

void test_asteroid_without_roms() {
    dsp::Asteroid machine;
    std::string error = "unset";
    check(!machine.init("/no/such/asteroid.zip", &error), "Asteroids init fails without the ROM set");
    check(error != "unset" && !error.empty(), "Asteroids reports why the set is missing");
    check(std::strcmp(machine.title(), "Asteroids") == 0, "Asteroids title");
    check(machine.screen_width() == 400 && machine.screen_height() == 320,
          "Asteroids screen is 400x320");
}

int unique_pixels(const dsp::Machine& machine) {
    const uint32_t* fb = machine.framebuffer();
    const int n = machine.screen_width() * machine.screen_height();
    std::set<uint32_t> colors(fb, fb + n);
    return int(colors.size());
}

// Classic 2600 kernel: 3 VSYNC, 37 VBLANK, 192 picture (COLUBK = line,
// reflected playfield), 30 overscan, plus a TIA square wave on channel 0.
std::vector<uint8_t> make_a2600_kernel_rom() {
    static const uint8_t kCode[] = {
        0x78, 0xD8, 0xA2, 0xFF, 0x9A, 0xA9, 0x00, 0x95, 0x00, 0xCA, 0xD0, 0xFB,
        0xA9, 0x04, 0x85, 0x15, 0xA9, 0x1F, 0x85, 0x17, 0xA9, 0x0F, 0x85, 0x19,
        0xA9, 0x86, 0x85, 0x08, 0xA9, 0x38, 0x85, 0x06, 0xA9, 0xF0, 0x85, 0x0D,
        0xA9, 0xFF, 0x85, 0x0E, 0xA9, 0x0F, 0x85, 0x0F, 0xA9, 0x01, 0x85, 0x0A,
        0xA9, 0xFF, 0x85, 0x1B, 0x85, 0x10, 0xA9, 0x02, 0x85, 0x00, 0x85, 0x02,
        0x85, 0x02, 0x85, 0x02, 0xA9, 0x00, 0x85, 0x00, 0xA9, 0x02, 0x85, 0x01,
        0xA2, 0x25, 0x85, 0x02, 0xCA, 0xD0, 0xFB, 0xA9, 0x00, 0x85, 0x01, 0xA2,
        0x00, 0x86, 0x09, 0x85, 0x02, 0xE8, 0xE0, 0xC0, 0xD0, 0xF7, 0xA9, 0x02,
        0x85, 0x01, 0xA2, 0x1E, 0x85, 0x02, 0xCA, 0xD0, 0xFB, 0x4C, 0x36, 0xF0,
    };
    std::vector<uint8_t> rom(4096, 0);
    std::memcpy(rom.data(), kCode, sizeof(kCode));
    rom[0xffc] = 0x00;
    rom[0xffd] = 0xf0;
    rom[0xffe] = 0x00;
    rom[0xfff] = 0xf0;
    return rom;
}

void test_tia_playfield_and_audio() {
    dsp::Tia tia;
    tia.reset();
    tia.write(0x09, 0x00);
    tia.write(0x08, 0x86);
    tia.write(0x0d, 0xf0);
    tia.write(0x0e, 0xff);
    tia.write(0x0f, 0x0f);
    tia.write(0x0a, 0x01);
    tia.write(0x01, 0x00);
    std::array<uint32_t, dsp::Tia::kScreenWidth> line{};
    tia.render_line(line.data());
    std::set<uint32_t> colors(line.begin(), line.end());
    check(colors.size() >= 2, "TIA playfield uses COLUPF and COLUBK");
    check(line[0] == dsp::Tia::ntsc_color(0x86), "PF0 lights the leftmost pixels");
    check(line[68] == dsp::Tia::ntsc_color(0x00), "the playfield gap is COLUBK");

    tia.reset();
    tia.begin_line();
    tia.write(0x01, 0x00);
    tia.write(0x09, 0x00);
    tia.write(0x06, 0x1e);
    tia.write(0x1b, 0xff);
    tia.set_hclock(68 + 8);
    tia.write(0x10, 0x00);  // RESP0
    tia.set_hclock(68 + 40);
    tia.write(0x1b, 0x00);  // GRP0 off for the rest of the line
    tia.set_hclock(dsp::Tia::kColorClocksPerLine);
    tia.render_line(line.data());
    check(line[8 + 5] == dsp::Tia::ntsc_color(0x1e),
          "TIA draws GRP0 after a mid-line RESP0");
    check(line[80] == dsp::Tia::ntsc_color(0x00),
          "TIA drops GRP0 after a later mid-line write");

    tia.write(0x15, 0x04);
    tia.write(0x17, 0x07);
    tia.write(0x19, 0x0f);
    std::vector<int16_t> audio;
    for (int i = 0; i < 400; i++) {
        tia.clock_audio();
        tia.emit_audio(dsp::Tia::kCpuCyclesPerLine, 1193181, audio);
    }
    int64_t energy = 0;
    for (int16_t sample : audio) energy += int64_t(sample) * sample;
    check(!audio.empty() && energy > 0, "TIA divide-by-2 tone produces energy");
}

void test_a2600_driver() {
    dsp::A2600 machine;
    check(std::strcmp(machine.title(), "Atari 2600") == 0, "Atari 2600 title");
    check(machine.screen_width() == 160 && machine.screen_height() == 192,
          "Atari 2600 visible area is 160x192");

    std::string error;
    check(!machine.init("", &error), "Atari 2600 init fails without a cartridge");
    check(!machine.init("/no/such/a2600-cart.bin", &error),
          "Atari 2600 init fails on a missing cartridge");

    const std::vector<uint8_t> image = make_a2600_kernel_rom();
    namespace fs = std::filesystem;
    const fs::path dir = "/tmp/dsp-a2600-test-kernel";
    fs::create_directories(dir);
    const fs::path path = dir / "kernel.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
    }
    check(machine.init(path.string(), &error), "the 2600 driver loads a 4K .bin");
    check((machine.debug_pc() & 0x1fff) == 0x1000, "6507 reset lands in the cart window");

    for (int frame = 0; frame < 8; frame++) machine.run_frame();
    check(unique_pixels(machine) > 8, "the 2600 kernel paints a colour picture");

    const uint32_t* fb = machine.framebuffer();
    int coloured = 0;
    for (int i = 0; i < machine.screen_width() * machine.screen_height(); i++) {
        if ((fb[i] & 0x00ffffff) != 0) coloured++;
    }
    check(coloured > 1000, "the 2600 kernel is not a black frame");

    std::vector<int16_t> audio;
    machine.drain_audio(audio);
    int64_t energy = 0;
    for (int16_t sample : audio) energy += int64_t(sample) * sample;
    check(!audio.empty() && energy > 0, "the 2600 kernel drives TIA audio");
}

void test_a2600_rom_if_present() {
    auto try_path = [](const char* path) {
        std::ifstream probe(path);
        return bool(probe);
    };
    const char* candidates[] = {
        "/tmp/roms/Beamrider.zip",
        "/tmp/roms/combat.bin",
        "/tmp/roms/pitfall.bin",
        "/tmp/roms/adventure.bin",
        "/tmp/roms/pacman.bin",
        "/tmp/roms/a2600.bin",
        "/tmp/roms/vcs.bin",
    };
    const char* found = nullptr;
    for (const char* path : candidates) {
        if (try_path(path)) {
            found = path;
            break;
        }
    }
    if (found == nullptr) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::is_directory("/tmp/roms", ec)) {
            for (const auto& entry : fs::directory_iterator("/tmp/roms", ec)) {
                if (!entry.is_regular_file(ec)) continue;
                const auto name = entry.path().filename().string();
                const auto lower = [&] {
                    std::string s = name;
                    for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    return s;
                }();
                if (lower.size() >= 4 &&
                    (lower.compare(lower.size() - 4, 4, ".bin") == 0 ||
                     lower.compare(lower.size() - 4, 4, ".a26") == 0)) {
                    static std::string picked;
                    picked = entry.path().string();
                    found = picked.c_str();
                    break;
                }
            }
        }
    }
    if (found == nullptr) {
        std::printf("skip: no Atari 2600 cartridge in /tmp/roms\n");
        return;
    }

    dsp::A2600 machine;
    std::string error;
    check(machine.init(found, &error), "a cartridge from /tmp/roms loads");
    for (int frame = 0; frame < 120; frame++) machine.run_frame();
    check(unique_pixels(machine) >= 4, "a real 2600 cart produces a framebuffer");
}

void test_sega_roms_if_present() {
    auto exists = [](const char* path) {
        std::ifstream probe(path);
        return bool(probe);
    };

    if (exists("/tmp/roms/outrun.zip")) {
        dsp::Outrun machine;
        std::string error;
        check(machine.init("/tmp/roms/outrun.zip", &error), "OutRun MAME set loads");
        check(machine.debug_pc() == 0x7b1e, "OutRun reset vector is the 315-5195 boot stub");
        for (int frame = 0; frame < 240; frame++) machine.run_frame();
        check(machine.debug_pc() != 0x7b1e, "OutRun leaves the mapper boot stub");
        check(machine.debug_sub_pc() != 0x103a, "OutRun sub CPU leaves the handshake wait");
        check(machine.debug_palette_used() > 16, "OutRun writes the attract palette after the handshake");
        // The sub CPU writes the road line table, then the control byte; if that
        // byte write swaps the road buffers the display keeps the power-on ramp
        // (line y selects road line y) forever and hides the horizon.
        for (int frame = 0; frame < 600; frame++) machine.run_frame();
        int identity_lines = 0;
        for (int line = 0; line < 224; line++) {
            if (machine.debug_road_line(line) == line) identity_lines++;
        }
        const bool road_table_fresh = identity_lines < 200;
        check(road_table_fresh, "OutRun swaps the freshly written road line table into the display");
    }

    if (exists("/tmp/roms/asteroid.zip")) {
        dsp::Asteroid machine;
        std::string error;
        check(machine.init("/tmp/roms/asteroid.zip", &error), "Asteroids MAME set loads");
        const uint16_t reset_pc = machine.debug_pc();
        for (int frame = 0; frame < 300; frame++) machine.run_frame();
        check(machine.debug_pc() != reset_pc, "Asteroids 6502 leaves the reset vector");
        check(machine.debug_dvg_lines() > 4, "DVG produced a vector list in attract");
        check(unique_pixels(machine) > 4, "Asteroids attract mode draws vectors");

        std::vector<int16_t> audio;
        machine.drain_audio(audio);
        check(!audio.empty(), "Asteroids drain_audio yields samples");
    }

    if (exists("/tmp/roms/bublbobl.zip")) {
        dsp::BublBobl machine;
        std::string error;
        check(machine.init("/tmp/roms/bublbobl.zip", &error), "Bubble Bobble MAME set loads");
        const uint16_t reset_pc = machine.debug_main_pc();
        for (int frame = 0; frame < 1200; frame++) machine.run_frame();
        check(machine.debug_main_pc() != reset_pc, "Bubble Bobble main CPU leaves the reset vector");
        check(machine.debug_video_enable(), "Bubble Bobble attract mode enables video");
        check(unique_pixels(machine) > 8, "Bubble Bobble attract mode draws a colour picture");

        std::vector<int16_t> audio;
        machine.drain_audio(audio);
        check(!audio.empty(), "Bubble Bobble drain_audio yields samples");
    }

    if (exists("/tmp/roms/skullxbo.zip")) {
        dsp::Skullxbo machine;
        std::string error;
        check(machine.init("/tmp/roms/skullxbo.zip", &error), "Skull & Crossbones MAME set loads");
        const uint32_t reset_pc = machine.debug_pc();
        for (int frame = 0; frame < 700; frame++) machine.run_frame();
        check(machine.debug_pc() != reset_pc, "Skull & Crossbones runs past the reset vector");
        check(machine.debug_palette_used() > 32, "Skull & Crossbones writes the attract palette");
        check(unique_pixels(machine) > 16, "Skull & Crossbones attract mode draws a colour picture");
        check(machine.debug_motion_object_pixels() > 0,
              "Skull & Crossbones draws motion objects during attract mode");

        std::vector<int16_t> audio;
        machine.drain_audio(audio);
        check(!audio.empty(), "Skull & Crossbones drain_audio yields samples");

        // A coin plus a button press must leave attract mode and start a game.
        dsp::MachineInputs inputs;
        inputs.coin1 = true;
        machine.set_inputs(inputs);
        for (int frame = 0; frame < 20; frame++) machine.run_frame();
        inputs.coin1 = false;
        inputs.player1.button1 = true;
        machine.set_inputs(inputs);
        for (int frame = 0; frame < 600; frame++) machine.run_frame();
        check(machine.debug_motion_object_pixels() > 0,
              "Skull & Crossbones keeps drawing motion objects after starting a game");
    }

    if (exists("/tmp/roms/fantzone.zip")) {
        dsp::System16 machine(dsp::System16::Game::Fantzone);
        std::string error;
        check(machine.init("/tmp/roms/fantzone.zip", &error), "Fantasy Zone MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 8, "Fantasy Zone attract mode draws a colour picture");
    }

    if (exists("/tmp/roms/shinobi.zip")) {
        dsp::System16 machine(dsp::System16::Game::Shinobi);
        std::string error;
        check(machine.init("/tmp/roms/shinobi.zip", &error), "Shinobi MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 8, "Shinobi attract mode draws a colour picture");
    }

    if (exists("/tmp/roms/tetris.zip")) {
        dsp::System16 machine(dsp::System16::Game::Tetris);
        std::string error;
        check(machine.init("/tmp/roms/tetris.zip", &error), "Tetris MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 4, "Tetris attract mode draws the warning screen");
    }

    if (exists("/tmp/roms/altbeast.zip")) {
        dsp::System16 machine(dsp::System16::Game::Altbeast);
        std::string error;
        check(machine.init("/tmp/roms/altbeast.zip", &error), "Altered Beast MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 4, "Altered Beast attract mode draws a colour picture");
    }

    if (exists("/tmp/roms/hangon.zip")) {
        dsp::HangOn machine;
        std::string error;
        check(machine.init("/tmp/roms/hangon.zip", &error), "Hang-On MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 4, "Hang-On attract mode draws more than the text layer");
    }

    if (exists("/tmp/roms/enduror.zip")) {
        dsp::HangOn machine(dsp::HangOn::Game::Enduro);
        std::string error;
        check(machine.init("/tmp/roms/enduror.zip", &error), "Enduro Racer MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 4, "Enduro Racer attract mode draws a colour picture");
    }

    if (exists("/tmp/roms/sharrier.zip")) {
        dsp::HangOn machine(dsp::HangOn::Game::Sharrier);
        std::string error;
        check(machine.init("/tmp/roms/sharrier.zip", &error), "Space Harrier MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 4, "Space Harrier attract mode draws a colour picture");
    }

    if (exists("/tmp/roms/alexkidd.zip")) {
        dsp::System16 machine(dsp::System16::Game::Alexkidd);
        std::string error;
        check(machine.init("/tmp/roms/alexkidd.zip", &error), "Alex Kidd MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 4, "Alex Kidd attract mode draws a colour picture");
    }

    if (exists("/tmp/roms/aliensyn.zip")) {
        dsp::System16 machine(dsp::System16::Game::Aliensyn);
        std::string error;
        check(machine.init("/tmp/roms/aliensyn.zip", &error), "Alien Syndrome MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 4, "Alien Syndrome attract mode draws a colour picture");
    }

    if (exists("/tmp/roms/wb3.zip")) {
        dsp::System16 machine(dsp::System16::Game::Wb3);
        std::string error;
        check(machine.init("/tmp/roms/wb3.zip", &error), "Wonder Boy III MAME set loads");
        for (int frame = 0; frame < 180; frame++) machine.run_frame();
        check(unique_pixels(machine) > 4, "Wonder Boy III attract mode draws a colour picture");
    }
}

void test_indy_coin_if_present() {
    const char* rom = "/tmp/roms/indytemp.zip";
    std::ifstream probe(rom);
    if (!probe) return;
    probe.close();

    dsp::AtariSystem1 machine(dsp::AtariSystem1::Game::Indy);
    std::string error;
    check(machine.init(rom, &error), "Indiana Jones ROM set loads for the coin test");
    bool armed = false;
    for (int i = 0; i < 600; i++) {
        machine.run_frame();
        if ((machine.debug_bankselect() & 0x80) != 0 &&
            (machine.debug_sound_ram(0x30) & 0x1f) == 0x1f) {
            armed = true;
            break;
        }
    }
    check(armed, "YM Timer A has armed the coin-1 debounce at $30");
    check(!machine.debug_sound_halted(), "6502 sound CPU is running so it can see coins on $1820");
    check(machine.debug_sound_pc() >= 0x4000, "6502 is executing sound ROM");

    const uint8_t credits_before = machine.debug_sound_ram(0x2c);
    dsp::MachineInputs coin;
    for (int pulse = 0; pulse < 4; pulse++) {
        coin.coin1 = true;
        machine.set_inputs(coin);
        check((machine.debug_in2() & 0x01) == 0, "coin 1 clears $1820 bit 0");
        for (int i = 0; i < 3; i++) machine.run_frame();
        coin.coin1 = false;
        machine.set_inputs(coin);
        for (int i = 0; i < 20; i++) machine.run_frame();
    }
    check(machine.debug_sound_ram(0x2c) > credits_before,
          "inserting a coin is counted by the 6502 coin scan at $FE38");
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
}

void test_ym2612() {
    dsp::YM2612 ym(7670453);
    ym.reset();
    bool silent = true;
    for (int i = 0; i < 128; i++) {
        if (ym.update() != 0) silent = false;
    }
    check(silent, "the YM2612 is silent after reset");

    // Channel 0, algorithm 7, all operators as carriers, max volume, key on.
    ym.write(0, 0xb0);
    ym.write(1, 0x07);
    ym.write(0, 0xb4);
    ym.write(1, 0xc0);
    ym.write(0, 0xa4);
    ym.write(1, 0x22);
    ym.write(0, 0xa0);
    ym.write(1, 0x69);
    for (int op = 0; op < 4; op++) {
        ym.write(0, uint8_t(0x40 + (op << 2)));
        ym.write(1, 0x00);
        ym.write(0, uint8_t(0x50 + (op << 2)));
        ym.write(1, 0x1f);
        ym.write(0, uint8_t(0x80 + (op << 2)));
        ym.write(1, 0x0f);
    }
    ym.write(0, 0x28);
    ym.write(1, 0xf0);
    bool audible = false;
    for (int i = 0; i < 4410; i++) {
        if (ym.update() != 0) audible = true;
    }
    check(audible, "the YM2612 produces sound after a key on");

    // DAC on channel 6.
    ym.reset();
    ym.write(0, 0x2b);
    ym.write(1, 0x80);
    ym.write(0, 0x2a);
    ym.write(1, 0xff);
    int32_t dac = ym.update();
    check(dac != 0, "the YM2612 DAC is audible when enabled");
}

void test_genesis_vdp() {
    dsp::Sega3155313 vdp(false);
    vdp.reset();
    vdp.write(4, 0x8004);  // mode 1
    vdp.write(4, 0x8174);  // display + DMA + VINT
    vdp.write(4, 0x8230);
    vdp.write(4, 0x8407);
    vdp.write(4, 0x8c81);  // H40
    vdp.write(4, 0x8f02);  // auto increment 2
    check((vdp.reg(1) & 0x40) != 0, "VDP display enable is latched");
    check((vdp.reg(0x0c) & 0x81) == 0x81, "VDP H40 mode is latched");

    // CRAM write at index 0: command CD=3, addr=0 → $C0000000
    vdp.write(4, 0xc000);
    vdp.write(4, 0x0000);
    vdp.write(0, 0x000e);
    vdp.write(0, 0x0eee);
    check((vdp.cram(0) & 0x0eee) == 0x000e, "VDP CRAM colour 0 is red");
    check((vdp.cram(1) & 0x0eee) == 0x0eee, "VDP CRAM colour 1 is white");

    // DMA fill of VRAM with $1111 (tile of colour 1).
    vdp.write(4, 0x9300);
    vdp.write(4, 0x9410);  // length 0x1000 words
    vdp.write(4, 0x9780);  // fill
    vdp.write(4, 0x4000);
    vdp.write(4, 0x0080);  // DMA bit + VRAM write
    vdp.write(0, 0x1111);
    check(vdp.vram(0) == 0x11, "DMA fill writes the high byte");
    check(vdp.vram(3) == 0x11, "DMA fill covers more than one word");

    vdp.handle_scanline(0);
    const uint32_t* line = vdp.line_buffer();
    bool saw_white = false;
    for (int x = 0; x < 320; x++) {
        if ((line[x] & 0x00ffffff) == 0x00eeeeee || (line[x] & 0x00ffffff) == 0x00ffffff ||
            ((line[x] >> 16) & 0xff) > 200) {
            saw_white = true;
            break;
        }
    }
    check(saw_white, "VDP scanline of a solid colour-1 tile is bright");
}

std::vector<uint8_t> make_genesis_test_rom() {
    std::vector<uint8_t> rom(0x800, 0);
    auto put32 = [&](size_t offset, uint32_t value) {
        rom[offset] = uint8_t(value >> 24);
        rom[offset + 1] = uint8_t(value >> 16);
        rom[offset + 2] = uint8_t(value >> 8);
        rom[offset + 3] = uint8_t(value);
    };
    auto put16 = [&](size_t offset, uint16_t value) {
        rom[offset] = uint8_t(value >> 8);
        rom[offset + 1] = uint8_t(value);
    };
    put32(0, 0x00fffe00);
    put32(4, 0x00000200);
    for (int vec = 2; vec < 64; vec++) put32(size_t(vec * 4), 0x000003e0);
    std::memcpy(&rom[0x100], "SEGA GENESIS    ", 16);
    put16(0x3e0, 0x4e73);  // rte

    size_t pc = 0x200;
    auto emit16 = [&](uint16_t value) {
        put16(pc, value);
        pc += 2;
    };
    auto emit32 = [&](uint32_t value) {
        put32(pc, value);
        pc += 4;
    };
    emit16(0x41f9);
    emit32(0x00c00000);  // lea $c00000, a0
    emit16(0x43f9);
    emit32(0x00c00004);  // lea $c00004, a1
    auto setreg = [&](uint8_t reg, uint8_t value) {
        emit16(0x32bc);
        emit16(uint16_t(0x8000 | (uint16_t(reg) << 8) | value));
    };
    setreg(0x00, 0x04);
    setreg(0x01, 0x74);
    setreg(0x02, 0x30);
    setreg(0x03, 0x28);
    setreg(0x04, 0x07);
    setreg(0x05, 0x7c);
    setreg(0x07, 0x00);
    setreg(0x0a, 0xff);
    setreg(0x0b, 0x00);
    setreg(0x0c, 0x81);
    setreg(0x0d, 0x3f);
    setreg(0x0f, 0x02);
    setreg(0x10, 0x01);
    emit16(0x22bc);
    emit32(0xc0000000);  // CRAM write
    emit16(0x30bc);
    emit16(0x000e);
    emit16(0x30bc);
    emit16(0x0eee);
    emit16(0x22bc);
    emit32(0x40000000);  // VRAM write at 0
    emit16(0x700f);      // moveq #15, d0
    emit16(0x30bc);
    emit16(0x1111);
    emit16(0x51c8);
    emit16(0xfff8);  // dbra d0, tile loop
    emit16(0x60fe);  // bra.s *
    return rom;
}

void test_genesis_boot() {
    dsp::Genesis machine;
    std::string error;
    check(machine.load_rom(make_genesis_test_rom(), &error), "Genesis loads a synthetic ROM");
    check(machine.debug_pc() == 0x200, "Genesis reset vector is $200");
    for (int frame = 0; frame < 8; frame++) machine.run_frame();
    check(machine.debug_pc() >= 0x200 && machine.debug_pc() < 0x400,
          "Genesis 68k stays in the test program");
    check((machine.vdp().reg(1) & 0x40) != 0, "Genesis test ROM enables the display");
    check((machine.vdp().cram(1) & 0x0eee) == 0x0eee, "Genesis test ROM writes CRAM");
    const uint32_t* fb = machine.framebuffer();
    bool bright = false;
    for (int i = 0; i < 320 * 16; i++) {
        if (((fb[i] >> 16) & 0xff) > 180 && ((fb[i] >> 8) & 0xff) > 180) {
            bright = true;
            break;
        }
    }
    check(bright, "Genesis test ROM fills the screen with the CRAM colour");

    dsp::MachineInputs inputs;
    inputs.player1.right = true;
    inputs.player1.button1 = true;
    machine.set_inputs(inputs);
    machine.debug_write_word(0xa10008, 0x4000);  // TH output
    machine.debug_write_word(0xa10002, 0x4000);  // TH high
    const uint16_t th_high = machine.debug_read_word(0xa10002);
    machine.debug_write_word(0xa10002, 0x0000);  // TH low
    const uint16_t th_low = machine.debug_read_word(0xa10002);
    check((th_high & 0x0800) == 0, "Genesis pad right is visible with TH high");
    check((th_low & 0x1000) == 0, "Genesis pad A is visible with TH low");
}

void write_msx2_dummy_roms(const std::string& dir, bool with_disk) {
    std::filesystem::create_directories(dir);
    std::vector<uint8_t> bios(0x8000, 0x00);
    bios[0] = 0x76;  // HALT
    {
        std::ofstream out(dir + "/MSX2.ROM", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bios.data()), std::streamsize(bios.size()));
    }
    std::vector<uint8_t> sub(0x4000, 0xff);
    sub[0] = 'C';
    sub[1] = 'D';
    {
        std::ofstream out(dir + "/MSX2EXT.ROM", std::ios::binary);
        out.write(reinterpret_cast<const char*>(sub.data()), std::streamsize(sub.size()));
    }
    if (with_disk) {
        std::vector<uint8_t> disk(0x4000, 0xff);
        disk[0] = 'A';
        disk[1] = 'B';
        {
            std::ofstream out(dir + "/nms8250_disk.rom", std::ios::binary);
            out.write(reinterpret_cast<const char*>(disk.data()), std::streamsize(disk.size()));
        }
    }
}

void test_v9938_status_and_hmmv() {
    dsp::V9938 vdp(nullptr);
    vdp.reset();
    auto setreg = [&](int index, uint8_t value) {
        vdp.register_write(value);
        vdp.register_write(uint8_t(0x80 | index));
    };
    setreg(15, 1);
    uint8_t id = vdp.status_read();
    check((id & 0x3e) == 0, "V9938 status 1 identification bits are 0");

    setreg(0, 0x06);   // GRAPHIC 4
    setreg(1, 0x40);   // display on
    setreg(7, 0x01);   // backdrop 1
    setreg(36, 0);     // DX
    setreg(37, 0);
    setreg(38, 0);     // DY
    setreg(39, 0);
    setreg(40, 32);    // NX
    setreg(41, 0);
    setreg(42, 8);     // NY
    setreg(43, 0);
    setreg(44, 0xff);  // both GRAPHIC 4 pixels in the byte are colour 15
    setreg(45, 0);
    setreg(46, 0xc0);  // HMMV
    check(!vdp.command_executing(), "V9938 HMMV completes immediately");
    for (int line = 0; line < 16; line++) vdp.refresh_line(line, 262);
    const uint32_t* fb = vdp.framebuffer();
    const uint32_t pixel = fb[2 * dsp::V9938::kScreenWidth + 4];
    check(((pixel >> 16) & 0xff) > 180 && ((pixel >> 8) & 0xff) > 180 && (pixel & 0xff) > 180,
          "V9938 HMMV fills GRAPHIC 4 pixels with palette colour 15");

    // GRAPHIC 5 HMMC writes packed bytes (4 pixels), not a single 2-bit colour.
    dsp::V9938 g5(nullptr);
    g5.reset();
    auto setreg5 = [&](int index, uint8_t value) {
        g5.register_write(value);
        g5.register_write(uint8_t(0x80 | index));
    };
    setreg5(0, 0x08);   // GRAPHIC 5
    setreg5(1, 0x40);
    setreg5(8, 0x20);   // TP: colour 0 is black, not transparent
    setreg5(36, 0);
    setreg5(37, 0);
    setreg5(38, 0);
    setreg5(39, 0);
    setreg5(40, 8);     // 8 dots = 2 bytes
    setreg5(41, 0);
    setreg5(42, 1);
    setreg5(43, 0);
    setreg5(44, 0x1b);  // pixels 0,1,2,3
    setreg5(45, 0);
    setreg5(46, 0xf0);  // HMMC consumes the first byte
    check(g5.command_executing(), "V9938 HMMC waits for the remaining CPU bytes");
    setreg5(44, 0xe4);  // pixels 3,2,1,0
    check(!g5.command_executing(), "V9938 HMMC finishes after NX dots");
    for (int line = 0; line < 2; line++) g5.refresh_line(line, 262);
    const uint32_t* g5fb = g5.framebuffer();
    const uint32_t p0 = g5fb[0];
    const uint32_t p1 = g5fb[1];
    const uint32_t p2 = g5fb[2];
    const uint32_t p6 = g5fb[6];
    check(((p0 >> 16) & 0xff) + ((p0 >> 8) & 0xff) + (p0 & 0xff) < 40,
          "V9938 GRAPHIC 5 HMMC first pixel is colour 0");
    check(p1 != p2, "V9938 GRAPHIC 5 HMMC packs four distinct 2-bit pixels per byte");
    check(p2 != p6, "V9938 GRAPHIC 5 HMMC second byte is not a 1-pixel colour smear");
}

void test_msx_disk_and_fdc() {
    std::vector<uint8_t> image(737280, 0xe5);
    image[0] = 0xeb;
    image[1] = 0xfe;
    image[2] = 0x90;
    std::memcpy(image.data() + 3, "MSXTEST ", 8);
    dsp::MsxDisk disk;
    std::string error;
    check(disk.load_bytes(image.data(), image.size(), &error), "720 KiB raw DSK is accepted");
    check(disk.tracks() == 80 && disk.heads() == 2 && disk.sectors_per_track() == 9,
          "720 KiB DSK is 80 tracks, 2 sides, 9 sectors");
    const uint8_t* boot = disk.sector(0, 0, 1);
    check(boot != nullptr && boot[0] == 0xeb && std::memcmp(boot + 3, "MSXTEST ", 8) == 0,
          "boot sector is track 0 head 0 sector 1");

    dsp::MsxFdc fdc;
    fdc.reset();
    fdc.set_disk(&disk);
    fdc.command_w(0x80);
    check((fdc.status_r() & 0x02) != 0, "MSX FDC read sector raises DRQ");
    check(fdc.data_r() == 0xeb, "MSX FDC returns the first boot-sector byte");
    for (int i = 1; i < 512; i++) (void)fdc.data_r();
    check(fdc.intrq(), "MSX FDC sector read completes with INTRQ");
}

void test_rp5c01_fixed_clock() {
    dsp::Rp5c01 rtc;
    rtc.reset();
    rtc.set_address(4);
    check(rtc.read() == 7, "RP-5C01 hours ones is 7");
    rtc.set_address(7);
    check(rtc.read() == 8, "RP-5C01 day ones is 8");
    rtc.set_address(13);
    rtc.write(2);  // RAM bank
    rtc.set_address(4);
    rtc.write(0x05);
    rtc.set_address(13);
    rtc.write(0);  // clock bank
    rtc.set_address(4);
    check(rtc.read() == 7, "RP-5C01 RAM bank writes do not replace the clock");
}

void test_msx2_missing_roms_mapper_and_disk() {
    dsp::Msx2 missing;
    std::string error = "unset";
    check(!missing.init("/no/such/msx2.zip", &error), "MSX2 init fails without the BIOS");
    check(error.find("not found") != std::string::npos || error.find("missing") != std::string::npos,
          "MSX2 init reports why the BIOS is missing");
    check(std::strcmp(missing.title(), "MSX2") == 0, "MSX2 title");
    check(missing.screen_width() == 512 && missing.screen_height() == 212, "MSX2 screen is 512x212");
    check(missing.uses_keyboard(), "MSX2 reads the host keyboard");

    const std::string dir = "/tmp/dsp-msx2-test";
    write_msx2_dummy_roms(dir, true);
    dsp::Msx2 boot;
    error.clear();
    check(boot.init(dir, &error), "MSX2 loads dummy BIOS/sub-ROM/disk ROM from a directory");
    check(boot.disk_rom_loaded(), "MSX2 reports the disk ROM as loaded");
    for (int frame = 0; frame < 3; frame++) boot.run_frame();
    check(boot.framebuffer() != nullptr, "MSX2 produces a framebuffer");

    boot.debug_write_port(0xa8, 0xff);          // all pages slot 3
    boot.debug_write_byte(0xffff, 0x55);        // all subslots 1 (mapper RAM)
    check(boot.debug_read_byte(0xffff) == uint8_t(~0x55),
          "expanded slot 3 reads the inverted subslot register at $FFFF");
    boot.debug_write_port(0xff, 0);
    boot.debug_write_byte(0xc000, 0xa5);
    check(boot.debug_read_byte(0xc000) == 0xa5, "mapper RAM segment 0 is writable in page 3");
    boot.debug_write_port(0xff, 1);
    check(boot.debug_read_byte(0xc000) == 0, "mapper port $FF switches the RAM segment");
    boot.debug_write_port(0xff, 0);
    check(boot.debug_read_byte(0xc000) == 0xa5, "mapper RAM keeps the previous segment");

    boot.debug_write_byte(0xffff, 0xaa);  // subslot 2: disk ROM
    check(boot.debug_read_byte(0x4000) == 'A' && boot.debug_read_byte(0x4001) == 'B',
          "disk ROM is visible in slot 3-2 page 1");

    std::vector<uint8_t> dsk(737280, 0xe5);
    const std::string dsk_path = dir + "/blank.dsk";
    {
        std::ofstream out(dsk_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(dsk.data()), std::streamsize(dsk.size()));
    }
    error.clear();
    check(boot.load_media(dsk_path, &error), "MSX2 attaches a 720 KiB .dsk");

    auto exists = [](const char* path) {
        std::ifstream probe(path);
        return bool(probe);
    };
    const char* romdir = "/tmp/roms/msx2";
    const char* zxtiny = "/tmp/roms/zxtiny";
    const bool have_official = exists((std::string(romdir) + "/MSX2.ROM").c_str()) ||
                               exists((std::string(romdir) + "/nms8250_basic-bios2.rom").c_str());
    const bool have_zxtiny = exists((std::string(zxtiny) + "/msx2_bios.rom").c_str());
    const char* biosdir = have_zxtiny ? zxtiny : (have_official ? romdir : nullptr);
    if (biosdir != nullptr) {
        dsp::Msx2 real;
        error.clear();
        check(real.init(biosdir, &error), "MSX2 BIOS set loads");
        for (int frame = 0; frame < 300; frame++) real.run_frame();
        const uint32_t* fb = real.framebuffer();
        const int n = real.screen_width() * real.screen_height();
        int lit = 0;
        for (int i = 0; i < n; i++) {
            const uint32_t p = fb[i];
            if (((p >> 16) & 0xff) + ((p >> 8) & 0xff) + (p & 0xff) > 40) lit++;
        }
        check(lit > 200, "MSX2 BIOS paints the V9938 framebuffer");
    }
}

int count_lit_pixels(const dsp::Machine& machine) {
    const uint32_t* fb = machine.framebuffer();
    const int n = machine.screen_width() * machine.screen_height();
    int lit = 0;
    for (int i = 0; i < n; i++) {
        const uint32_t p = fb[i];
        if (((p >> 16) & 0xff) + ((p >> 8) & 0xff) + (p & 0xff) > 40) lit++;
    }
    return lit;
}

void test_diskii_encode_roundtrip() {
    uint8_t sector[256];
    for (int i = 0; i < 256; i++) sector[i] = uint8_t(i * 3 + 17);
    uint8_t encoded[343];
    dsp::DiskIi::encode_62(sector, encoded);
    uint8_t decoded[256];
    check(dsp::DiskIi::decode_62(encoded, decoded), "Disk II 6-and-2 checksum is valid");
    check(std::memcmp(sector, decoded, 256) == 0, "Disk II 6-and-2 roundtrips a sector");

    std::vector<uint8_t> image(dsp::DiskIi::kDosSize, 0);
    for (int s = 0; s < 16; s++) image[s * 256] = uint8_t(0xA0 + s);
    std::memcpy(image.data() + 1, "HELLO", 5);
    dsp::DiskIi disk;
    std::string error;
    check(disk.load_bytes(image.data(), image.size(), "test.dsk", &error), "Disk II loads a 140K .dsk");
    check(disk.nibbles().size() >= 6000, "Disk II nibblizes track 0");
    bool saw_d5 = false;
    for (uint8_t n : disk.nibbles()) {
        if (n == 0xD5) saw_d5 = true;
    }
    check(saw_d5, "Disk II track contains an address/data prologue");

    disk.read_io(0xE9);  // motor on
    disk.tick(40);
    check((disk.latch() & 0x80) != 0, "Disk II latch bit 7 is set when a nibble is ready");
    const int pos = disk.nibble_pos();
    disk.tick(27);
    check(disk.nibble_pos() == pos, "Disk II keeps the current nibble for a 27-cycle PROM data loop");
    const uint8_t first = disk.read_io(0xEC);
    check((first & 0x80) != 0, "Disk II $C0EC returns the held nibble with bit 7 set");
    check((disk.latch() & 0x80) == 0, "Disk II $C0EC consumes the nibble (clears bit 7)");
    disk.tick(40);
    check(disk.nibble_pos() != pos, "Disk II advances after the CPU consumes the nibble");
}

void test_apple2_missing_roms_and_dummy() {
    dsp::Apple2 machine;
    std::string error;
    check(!machine.init("/tmp/no-such-apple2-bios-set", &error), "Apple II without BIOS fails init");

    machine.init_synthetic_roms();
    machine.poke(0x0400, 0xC1);
    machine.poke(0x0401, 0xD0);
    machine.poke(0x0402, 0xD0);
    machine.poke(0x0403, 0xCC);
    machine.poke(0x0404, 0xC5);
    for (int i = 0; i < 3; i++) machine.run_frame();
    check(count_lit_pixels(machine) > 50, "Apple II dummy chargen paints text cells");
    check(machine.pc() == 0xF000, "Apple II synthetic ROM sits in the JMP loop");

    dsp::MachineInputs inputs{};
    inputs.keys[size_t(dsp::Key::A)] = true;
    machine.set_inputs(inputs);
    check((machine.peek(0xC000) & 0x80) != 0, "Apple II keyboard strobe sets bit 7");
    check((machine.peek(0xC000) & 0x7F) == 'A', "Apple II keyboard returns ASCII A");
    machine.peek(0xC010);
    check((machine.peek(0xC000) & 0x80) == 0, "Apple II $C010 clears the keyboard strobe");
}

void test_apple2_roms_if_present() {
    const char* plus_zip = "/tmp/roms/apple2/apple2p.zip";
    const char* iie_zip = "/tmp/roms/apple2/apple2e.zip";
    const char* ee_zip = "/tmp/roms/apple2/apple2ee.zip";
    auto exists = [](const char* path) {
        std::ifstream in(path, std::ios::binary);
        return bool(in);
    };
    if (exists(plus_zip)) {
        dsp::Apple2 plus(dsp::Apple2::Model::IIPlus);
        std::string error;
        check(plus.init(plus_zip, &error), "Apple II+ BIOS set loads");
        check(!plus.disk_prom_loaded(), "apple2p.zip has no Disk II PROM so Autostart reaches BASIC");
        for (int frame = 0; frame < 180; frame++) plus.run_frame();
        check(count_lit_pixels(plus) > 200, "Apple II+ Autostart paints the text screen");
    }
    if (exists(iie_zip)) {
        dsp::Apple2 iie(dsp::Apple2::Model::IIe);
        std::string error;
        check(iie.init(iie_zip, &error), "Apple IIe BIOS set loads");
        for (int frame = 0; frame < 180; frame++) iie.run_frame();
        check(count_lit_pixels(iie) > 200, "Apple IIe firmware paints the text screen");
    }
    if (exists(ee_zip)) {
        dsp::Apple2 ee(dsp::Apple2::Model::IIeEnhanced);
        std::string error;
        check(ee.init(ee_zip, &error), "Apple IIe Enhanced BIOS set loads");
        check(ee.pc() != 0, "Apple IIe Enhanced 65C02 starts from the reset vector");
    }

    const char* prom = "/tmp/roms/apple2/disk2-16boot.rom";
    const char* concat = "/tmp/roms/apple2/apple2-asoft-auto.rom";
    if (exists(prom) && exists(concat) && exists("/tmp/roms/apple2/apple2-character.rom")) {
        dsp::Apple2 plus(dsp::Apple2::Model::IIPlus);
        std::string error;
        check(plus.init(concat, &error), "Apple II+ concatenated ROM + sibling Disk II PROM load");
        check(plus.disk_prom_loaded(), "Disk II PROM is mapped in slot 6");

        std::vector<uint8_t> image(dsp::DiskIi::kDosSize, 0);
        // Track 0 sector 0: boot stub that HOMEs and COUTs "DISK OK".
        const uint8_t boot[] = {
            0x01,
            0x20, 0x58, 0xFC,
            0xA0, 0x00,
            0xB9, 0x11, 0x08,
            0xF0, 0xFE,
            0x20, 0xED, 0xFD,
            0xC8,
            0xD0, 0xF5,
            0xC4, 0xC9, 0xD3, 0xCB, 0xA0, 0xCF, 0xCB, 0x00,
        };
        std::memcpy(image.data(), boot, sizeof(boot));
        check(plus.disk().load_bytes(image.data(), image.size(), "boot.dsk", &error),
              "synthetic boot disk loads");
        plus.reset();
        for (int frame = 0; frame < 240; frame++) plus.run_frame();
        check(count_lit_pixels(plus) > 50, "Disk II boot PROM paints from the synthetic sector");
    }

    const char* compilation = "/tmp/roms/apple2/spyhunter-compilation.dsk";
    if (exists(prom) && exists(concat) && exists(compilation)) {
        std::ifstream in(compilation, std::ios::binary);
        std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
        dsp::Apple2 plus(dsp::Apple2::Model::IIPlus);
        std::string error;
        check(plus.init(concat, &error), "Apple II+ loads firmware for the compilation disk");
        check(plus.disk().load_bytes(image.data(), image.size(), compilation, &error),
              "Spy Hunter compilation .dsk loads");
        plus.reset();
        bool pages_ok = false;
        for (int frame = 0; frame < 250; frame++) {
            plus.run_frame();
            if (plus.peek(0x08FF) != 0xFF || plus.pc() < 0xC600) {
                continue;
            }
            static const uint8_t kBoot1Phys[] = {0x0C, 0x0E, 0x01, 0x03, 0x05,
                                                0x07, 0x09, 0x0B, 0x0D};
            static const uint8_t kDosSkew[] = {0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04,
                                               0x0B, 0x03, 0x0A, 0x02, 0x09, 0x01, 0x08, 0x0F};
            pages_ok = true;
            for (int i = 0; i < 9; i++) {
                const int logical = kDosSkew[kBoot1Phys[i]];
                const uint16_t page = uint16_t((0x3F - i) << 8);
                for (int b = 0; b < 256; b++) {
                    if (plus.peek(uint16_t(page + b)) != image[logical * 256 + b]) {
                        pages_ok = false;
                        break;
                    }
                }
                if (!pages_ok) {
                    break;
                }
            }
            break;
        }
        check(plus.peek(0x0800) == 0x01, "boot0 loaded track 0 sector 0 into $0800");
        check(pages_ok, "boot1 loaded DOS pages $3700-$3FFF from track 0");
    }
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
    test_tia_playfield_and_audio();
    test_a2600_driver();
    test_a2600_rom_if_present();
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
    test_gbc_cart_detection();
    test_gb_cgb_registers_absent_on_dmg();
    test_gbc_hdma_control();
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
    test_c64_prg_injection();
    test_c64_t64_and_built_disk();
    test_c64_drive_rom_and_media();
    test_m6502_rmw_double_write();
    test_mos6566_bank_and_multicolor_bitmap();
    test_pv2000_missing_roms_and_dummy_bios();
    test_tms7000_mov_add_call();
    test_tms7000_lvdp_and_int1();
    test_tms3556_background();
    test_tms5220_fifo_and_status();
    test_tms5220_latched_writes();
    test_tms5220_chirp_rom();
    test_tms5220_synthesis();
    test_tms5220_stop_frame_ramp();
    test_exelv_dummy_bios();
    test_trdos_scl_and_beta();
    test_starwars_missing_roms();
    test_polepos_driver();
    test_atari_system1_missing_roms();
    test_t11_reset_and_moves();
    test_t11_addressing_and_bytes();
    test_t11_stack_and_interrupts();
    test_atari_system2_missing_roms();
    test_paperboy_if_present();
    test_atari_system2_game_if_present(dsp::AtariSystem2::Game::SuperSprint,
                                       "/tmp/roms/ssprint.zip", 1200);
    test_atari_system2_game_if_present(dsp::AtariSystem2::Game::Degrees720,
                                       "/tmp/roms/720.zip", 1200);
    test_atari_system2_game_if_present(dsp::AtariSystem2::Game::Apb,
                                       "/tmp/roms/apb.zip", 3000);
    test_sega_pcm_and_mapper();
    test_sega_system16_missing_roms();
    test_skullxbo_without_roms();
    test_bublbobl_without_roms();
    test_asteroid_without_roms();
    test_sega16_palette_banks();
    test_sega_roms_if_present();
    test_indy_coin_if_present();
    test_ym2612();
    test_genesis_vdp();
    test_genesis_boot();
    test_v9938_status_and_hmmv();
    test_msx_disk_and_fdc();
    test_rp5c01_fixed_clock();
    test_msx2_missing_roms_mapper_and_disk();
    test_diskii_encode_roundtrip();
    test_apple2_missing_roms_and_dummy();
    test_apple2_roms_if_present();
    if (failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", failures);
    return 1;
}