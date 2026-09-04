#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "drivers/computers/c64.h"

using namespace dsp;

namespace {

void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

// A minimal 6502 program: increments $C400 forever (a visible "is it
// running" heartbeat) - loaded as the payload for PRG/T64/D64 tests.
std::vector<uint8_t> HeartbeatProgram() {
    return {
        0xEE, 0x00, 0xC4,  // INC $C400
        0x4C, 0x00, 0xC0,  // JMP $C000
    };
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const std::string rom_dir = argv[1];

    // --- PRG ---
    {
        std::vector<uint8_t> prg = {0x00, 0xC0};  // load address $C000
        auto code = HeartbeatProgram();
        prg.insert(prg.end(), code.begin(), code.end());
        write_file("/tmp/test.prg", prg);

        C64 machine;
        std::string error;
        if (!machine.init(rom_dir, &error)) {
            std::fprintf(stderr, "init failed: %s\n", error.c_str());
            return 1;
        }
        if (!machine.load_media("/tmp/test.prg", &error)) {
            std::fprintf(stderr, "PRG load failed: %s\n", error.c_str());
            return 1;
        }
        machine.set_pc(0xC000);
        for (int i = 0; i < 5; i++) machine.run_frame();
        std::printf("PRG: $C400 = %d (expect > 0)\n", machine.peek(0xC400));
    }

    // --- T64 ---
    {
        std::vector<uint8_t> t64(64, 0);
        std::memcpy(t64.data(), "C64", 3);
        t64[34] = 1;  // max_entries = 1
        t64[35] = 0;
        std::vector<uint8_t> entry(32, 0);
        entry[0] = 1;                       // entry type: normal file
        entry[1] = 1;                       // C64s file type: PRG
        entry[2] = 0x00; entry[3] = 0xC0;   // start address $C000
        auto code = HeartbeatProgram();
        const uint16_t end = uint16_t(0xC000 + code.size());
        entry[4] = uint8_t(end & 0xff); entry[5] = uint8_t(end >> 8);
        entry[8] = 96;  // file offset (64 header + 32 entry)
        std::memcpy(&entry[16], "HEARTBEAT       ", 16);
        t64.insert(t64.end(), entry.begin(), entry.end());
        t64.insert(t64.end(), code.begin(), code.end());
        write_file("/tmp/test.t64", t64);

        C64 machine;
        std::string error;
        machine.init(rom_dir, &error);
        if (!machine.load_media("/tmp/test.t64", &error)) {
            std::fprintf(stderr, "T64 load failed: %s\n", error.c_str());
            return 1;
        }
        machine.set_pc(0xC000);
        for (int i = 0; i < 5; i++) machine.run_frame();
        std::printf("T64: $C400 = %d (expect > 0)\n", machine.peek(0xC400));
    }

    // --- TAP ---
    {
        std::vector<uint8_t> tap;
        const char header[12] = {'C', '6', '4', '-', 'T', 'A', 'P', 'E', '-', 'R', 'A', 'W'};
        tap.insert(tap.end(), header, header + 12);
        tap.push_back(1);           // version 1
        tap.push_back(0); tap.push_back(0); tap.push_back(0);  // reserved
        // A handful of pulses; size filled in after.
        std::vector<uint8_t> pulses;
        for (int i = 0; i < 100; i++) pulses.push_back(0x30);  // 0x30*8=384 cycles each
        const uint32_t size = uint32_t(pulses.size());
        tap.push_back(uint8_t(size & 0xff));
        tap.push_back(uint8_t((size >> 8) & 0xff));
        tap.push_back(uint8_t((size >> 16) & 0xff));
        tap.push_back(uint8_t((size >> 24) & 0xff));
        tap.insert(tap.end(), pulses.begin(), pulses.end());
        write_file("/tmp/test.tap", tap);

        C64 machine;
        std::string error;
        machine.init(rom_dir, &error);
        if (!machine.load_media("/tmp/test.tap", &error)) {
            std::fprintf(stderr, "TAP load failed: %s\n", error.c_str());
            return 1;
        }
        std::printf("TAP: loaded ok, %zu pulses accepted\n", pulses.size());
    }

    // --- D64 ---
    {
        constexpr int kSectorsPerTrack[35] = {21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
                                               21, 21, 19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18,
                                               17, 17, 17, 17, 17};
        size_t total = 0;
        for (int s : kSectorsPerTrack) total += size_t(s);
        std::vector<uint8_t> d64(total * 256, 0);
        auto sector_off = [&](int track, int sector) {
            size_t off = 0;
            for (int t = 1; t < track; t++) off += size_t(kSectorsPerTrack[t - 1]) * 256;
            return off + size_t(sector) * 256;
        };
        // Directory sector 18/1: one entry pointing at data starting 19/0.
        // Directory entries are 32 bytes each starting at the very top of
        // the sector; entry 0's own first two bytes double as the sector's
        // next-directory-sector link (0/0 here, since this is the only
        // directory sector we need).
        size_t dir = sector_off(18, 1);
        d64[dir + 0] = 0;  // no next directory sector
        d64[dir + 1] = 0;
        d64[dir + 2] = 0x82;  // entry0: closed PRG file type
        d64[dir + 3] = 19;    // first data track
        d64[dir + 4] = 0;     // first data sector
        std::memset(&d64[dir + 5], 0xa0, 16);
        std::memcpy(&d64[dir + 5], "HEARTBEAT", 9);
        // Data sector 19/0: address + code, single sector (track=0 => last).
        std::vector<uint8_t> payload = {0x00, 0xC0};
        auto code = HeartbeatProgram();
        payload.insert(payload.end(), code.begin(), code.end());
        size_t data = sector_off(19, 0);
        d64[data + 0] = 0;                                   // last sector
        d64[data + 1] = uint8_t(payload.size() + 1);          // bytes used + 1, per spec
        std::memcpy(&d64[data + 2], payload.data(), payload.size());
        write_file("/tmp/test.d64", d64);

        C64 machine;
        std::string error;
        machine.init(rom_dir, &error);
        if (!machine.load_media("/tmp/test.d64", &error)) {
            std::fprintf(stderr, "D64 load failed: %s\n", error.c_str());
            return 1;
        }
        // Simulate: SETNAM "HEARTBEAT", SETLFS 1,8,1, JSR $FFD5 (LOAD).
        const char* name = "HEARTBEAT";
        for (size_t i = 0; i < std::strlen(name); i++) machine.poke(uint16_t(0xC800 + i), uint8_t(name[i]));
        machine.poke(0xb7, uint8_t(std::strlen(name)));  // FNLEN
        machine.poke(0xbb, 0x00); machine.poke(0xbc, 0xC8);  // FNAM ptr
        machine.poke(0xb9, 1);  // secondary address 1: use embedded address
        machine.poke(0xba, 8);  // device 8

        // Program: JSR $FFD5, then jump into the freshly-loaded program.
        machine.poke(0xC100, 0x20); machine.poke(0xC101, 0xD5); machine.poke(0xC102, 0xFF);  // JSR $FFD5
        machine.poke(0xC103, 0x4C); machine.poke(0xC104, 0x00); machine.poke(0xC105, 0xC0);  // JMP $C000
        machine.set_pc(0xC100);
        for (int i = 0; i < 3; i++) machine.run_frame();
        std::printf("D64: A=%d after LOAD; $C400 = %d (expect A=0, $C400 > 0)\n", -1, machine.peek(0xC400));
    }

    return 0;
}
