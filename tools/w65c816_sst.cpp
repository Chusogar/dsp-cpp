// Runs the SingleStepTests 65816 vectors (https://github.com/SingleStepTests/65816)
// against the W65C816 core: usage  w65c816_sst DIR [opcode-hex...]
// Checks registers and memory after one instruction; reports cycle-count
// mismatches separately (the core's timing is approximate).
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cpu/w65c816.h"

namespace {

struct Json {
    enum Type { Null, Num, Str, Arr, Obj } type = Null;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
    const Json& operator[](const char* k) const {
        for (auto& kv : obj) if (kv.first == k) return kv.second;
        static Json null;
        return null;
    }
};

struct Parser {
    const char* p;
    void ws() { while (*p && std::isspace(uint8_t(*p))) p++; }
    Json parse() {
        ws();
        Json j;
        if (*p == '{') {
            j.type = Json::Obj; p++; ws();
            if (*p == '}') { p++; return j; }
            while (true) {
                ws(); Json k = parse(); ws(); p++;  // ':'
                Json v = parse();
                j.obj.emplace_back(k.str, std::move(v));
                ws();
                if (*p == ',') { p++; continue; }
                p++; break;
            }
        } else if (*p == '[') {
            j.type = Json::Arr; p++; ws();
            if (*p == ']') { p++; return j; }
            while (true) {
                j.arr.push_back(parse()); ws();
                if (*p == ',') { p++; continue; }
                p++; break;
            }
        } else if (*p == '"') {
            j.type = Json::Str; p++;
            while (*p != '"') j.str += *p++;
            p++;
        } else if (*p == 'n' || *p == 't' || *p == 'f') {
            while (std::isalpha(uint8_t(*p))) p++;
        } else {
            j.type = Json::Num;
            char* end;
            j.num = std::strtod(p, &end);
            p = end;
        }
        return j;
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s DIR [op...]\n", argv[0]); return 1; }
    std::string dir = argv[1];
    std::vector<int> ops;
    for (int i = 2; i < argc; i++) ops.push_back(int(std::strtol(argv[i], nullptr, 16)));
    if (ops.empty()) for (int i = 0; i < 256; i++) ops.push_back(i);

    std::unordered_map<uint32_t, uint8_t> mem;
    dsp::W65C816 cpu(1000000);
    cpu.set_memory_handlers(
        [&](uint32_t a) -> uint8_t { auto it = mem.find(a); return it == mem.end() ? 0 : it->second; },
        [&](uint32_t a, uint8_t v) { mem[a] = v; });
    int cyc = 0;
    cpu.set_cycle_handler([&](int c) { cyc += c; });

    int total_fail = 0, total_cycle = 0;
    for (int op : ops) {
        for (const char* mode : {"e", "n"}) {
            char name[64];
            std::snprintf(name, sizeof name, "%s/%02x.%s.json", dir.c_str(), op, mode);
            std::ifstream in(name);
            if (!in) continue;
            std::stringstream ss; ss << in.rdbuf();
            std::string text = ss.str();
            Parser parser{text.c_str()};
            Json tests = parser.parse();
            int fails = 0, cycle_fails = 0;
            std::string first;
            for (const Json& t : tests.arr) {
                const Json& in0 = t["initial"];
                const Json& fin = t["final"];
                mem.clear();
                for (const Json& r : in0["ram"].arr) mem[uint32_t(r.arr[0].num)] = uint8_t(r.arr[1].num);
                cpu.a = uint16_t(in0["a"].num);
                cpu.x = uint16_t(in0["x"].num);
                cpu.y = uint16_t(in0["y"].num);
                cpu.sp = uint16_t(in0["s"].num);
                cpu.d = uint16_t(in0["d"].num);
                cpu.dbr = uint8_t(in0["dbr"].num);
                cpu.set_pc((uint32_t(in0["pbr"].num) << 16) | uint32_t(in0["pc"].num));
                cpu.clear_halt();
                cpu.set_emulation(in0["e"].num != 0);
                cpu.set_p(uint8_t(in0["p"].num));
                // set_p may have truncated X/Y; restore raw test values if x=0
                cpu.x = uint16_t(in0["x"].num);
                cpu.y = uint16_t(in0["y"].num);
                if (cpu.p.x) { cpu.x &= 0xff; cpu.y &= 0xff; }
                cyc = 0;
                cpu.step();
                if (op == 0x44 || op == 0x54) {
                    while (cyc + 7 <= int(t["cycles"].arr.size()) && (cpu.pc() & 0xffff) == uint32_t(in0["pc"].num)) cpu.step();
                    cpu.set_pc((uint32_t(cpu.pbr) << 16) | uint32_t(fin["pc"].num));  // mid-instruction snapshot
                }
                std::string err;
                char buf[256];
                auto chk = [&](const char* nm, uint32_t got, uint32_t want) {
                    if (got != want) { std::snprintf(buf, sizeof buf, " %s=%x(want %x)", nm, got, want); err += buf; }
                };
                chk("a", cpu.a, uint32_t(fin["a"].num));
                chk("x", cpu.x, uint32_t(fin["x"].num));
                chk("y", cpu.y, uint32_t(fin["y"].num));
                chk("s", cpu.sp, uint32_t(fin["s"].num));
                chk("d", cpu.d, uint32_t(fin["d"].num));
                chk("dbr", cpu.dbr, uint32_t(fin["dbr"].num));
                chk("pbr", cpu.pbr, uint32_t(fin["pbr"].num));
                chk("pc", cpu.pc() & 0xffff, uint32_t(fin["pc"].num));
                chk("p", cpu.get_p(), uint32_t(fin["p"].num));
                chk("e", cpu.emulation(), uint32_t(fin["e"].num));
                for (const Json& r : fin["ram"].arr) {
                    uint32_t addr = uint32_t(r.arr[0].num);
                    uint8_t want = uint8_t(r.arr[1].num);
                    auto it = mem.find(addr);
                    uint8_t got = it == mem.end() ? 0 : it->second;
                    if (got != want) { std::snprintf(buf, sizeof buf, " [%06x]=%02x(want %02x)", addr, got, want); err += buf; }
                }
                if (!err.empty()) {
                    if (fails == 0) first = t["name"].str + ":" + err;
                    fails++;
                } else if (cyc != int(t["cycles"].arr.size())) {
                    cycle_fails++;
                }
            }
            if (fails || cycle_fails) {
                std::printf("%02x.%s: %d/%zu fail, %d cycle mismatches %s\n", op, mode, fails, tests.arr.size(),
                            cycle_fails, first.c_str());
            }
            total_fail += fails;
            total_cycle += cycle_fails;
        }
    }
    std::printf("TOTAL failures: %d, cycle mismatches: %d\n", total_fail, total_cycle);
    return total_fail ? 1 : 0;
}
