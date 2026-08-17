#include "drivers/zx_clone.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace dsp {
namespace {

const uint32_t kPalette[16] = {
    0xff000000, 0xffc00000, 0xff0000c0, 0xffc000c0, 0xff00c000, 0xffc0c000, 0xff00c0c0, 0xffc0c0c0,
    0xff000000, 0xffff0000, 0xff0000ff, 0xffff00ff, 0xff00ff00, 0xffffff00, 0xff00ffff, 0xffffffff,
};

const Key kMatrix[8][5] = {
    {Key::LeftShift, Key::Z, Key::X, Key::C, Key::V},
    {Key::A, Key::S, Key::D, Key::F, Key::G},
    {Key::Q, Key::W, Key::E, Key::R, Key::T},
    {Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5},
    {Key::Num0, Key::Num9, Key::Num8, Key::Num7, Key::Num6},
    {Key::P, Key::O, Key::I, Key::U, Key::Y},
    {Key::Enter, Key::L, Key::K, Key::J, Key::H},
    {Key::Space, Key::RightCtrl, Key::M, Key::N, Key::B},
};

bool load_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return bool(f);
}

bool try_rom(const std::string& dir, const char* name, std::vector<uint8_t>& out) {
    namespace fs = std::filesystem;
    if (load_file((fs::path(dir) / name).string(), out)) return true;
    std::string upper = name;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    return load_file((fs::path(dir) / upper).string(), out);
}

bool ends_ci(const std::string& path, const char* ext) {
    std::string lower = path;
    for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    const size_t n = std::strlen(ext);
    return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
}

uint32_t to_argb(uint32_t bgr) {
    const uint32_t blue = (bgr >> 16) & 0xff;
    const uint32_t green = (bgr >> 8) & 0xff;
    const uint32_t red = bgr & 0xff;
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

}  // namespace

ZxClone::ZxClone(ZxCloneModel model) : model_(model), cpu_(kClock), ay_(kAyClock) {
    ram_pages_ = (model_ == ZxCloneModel::Pentagon1024) ? 64 : 16;
    for (int i = 0; i < 16; ++i) palette_[i] = to_argb(kPalette[i] | 0xff000000);
}

const char* ZxClone::title() const {
    return model_ == ZxCloneModel::Scorpion256 ? "Scorpion ZS-256" : "Pentagon 1024";
}

bool ZxClone::load_roms(const std::string& path, std::string* error) {
    namespace fs = std::filesystem;
    for (auto& page : rom_) page.fill(0);
    std::string dir = path;
    if (ends_ci(path, ".trd") || ends_ci(path, ".scl") || ends_ci(path, ".tap") ||
        ends_ci(path, ".tzx") || ends_ci(path, ".sna")) {
        dir = fs::path(path).parent_path().string();
        if (dir.empty()) dir = ".";
    }

    auto copy_page = [&](int page, const std::vector<uint8_t>& blob, size_t off) {
        if (page < 0 || page > 3) return;
        const size_t n = std::min(size_t(0x4000), blob.size() - std::min(off, blob.size()));
        std::memcpy(rom_[size_t(page)].data(), blob.data() + off, n);
    };

    std::vector<uint8_t> blob;
    bool have128 = false;
    bool have_dos = false;
    gluk_present_ = false;

    const char* joined64[] = {"scorpion.rom", "scorp294.rom", "scorpion.rom.bin", "scorpio.rom"};
    const char* joined32[] = {"pentagon.rom", "128.rom", "zx128.rom", "spectrum128.rom", "128p.rom"};
    if (model_ == ZxCloneModel::Scorpion256) {
        for (const char* n : joined64) {
            if (try_rom(dir, n, blob) && blob.size() >= 0x10000) {
                for (int p = 0; p < 4; p++) copy_page(p, blob, size_t(p) * 0x4000);
                have128 = have_dos = true;
                break;
            }
        }
        if (!have128) {
            std::vector<uint8_t> p0, p1, p2, p3;
            if (try_rom(dir, "scorp0.rom", p0) && try_rom(dir, "scorp1.rom", p1) &&
                try_rom(dir, "scorp2.rom", p2) && try_rom(dir, "scorp3.rom", p3) &&
                p0.size() >= 0x4000 && p1.size() >= 0x4000 && p2.size() >= 0x4000 &&
                p3.size() >= 0x4000) {
                copy_page(0, p0, 0);
                copy_page(1, p1, 0);
                copy_page(2, p2, 0);
                copy_page(3, p3, 0);
                have128 = have_dos = true;
            }
        }
    }

    if (!have128) {
        for (const char* n : joined32) {
            if (try_rom(dir, n, blob) && blob.size() >= 0x8000) {
                copy_page(0, blob, 0);
                copy_page(1, blob, 0x4000);
                have128 = true;
                break;
            }
        }
    }
    if (!have128) {
        std::vector<uint8_t> r0, r1;
        const char* n0[] = {"128p-0.rom", "128-0.rom", "plus2-0.rom"};
        const char* n1[] = {"128p-1.rom", "128-1.rom", "plus2-1.rom"};
        for (const char* a : n0) {
            if (try_rom(dir, a, r0) && r0.size() >= 0x4000) break;
        }
        for (const char* a : n1) {
            if (try_rom(dir, a, r1) && r1.size() >= 0x4000) break;
        }
        if (r0.size() >= 0x4000 && r1.size() >= 0x4000) {
            copy_page(0, r0, 0);
            copy_page(1, r1, 0);
            have128 = true;
        }
    }
    if (!have128 && load_file(path, blob) && blob.size() >= 0x8000) {
        copy_page(0, blob, 0);
        copy_page(1, blob, 0x4000);
        if (blob.size() >= 0x10000) {
            copy_page(2, blob, 0x8000);
            copy_page(3, blob, 0xc000);
            have_dos = true;
        }
        have128 = true;
    }
    if (!have128) {
        if (error) *error = "128K ROM (32 KB) not found in " + dir;
        return false;
    }

    if (!have_dos) {
        const char* dos_names[] = {"trdos.rom", "trd503.rom", "trd504.rom", "trd505.rom",
                                   "dos.rom",   "128-3.rom",  "scorp3.rom", "beta128.rom"};
        std::vector<uint8_t> dos;
        for (const char* n : dos_names) {
            if (try_rom(dir, n, dos) && dos.size() >= 0x4000) {
                copy_page(3, dos, 0);
                have_dos = true;
                break;
            }
        }
    }
    if (!have_dos) {
        if (error) *error = "TR-DOS ROM (16 KB) not found in " + dir;
        return false;
    }

    const char* gluk_names[] = {"gluk63r.rom", "gluk.rom", "gluk54r.rom", "gluk60r.rom",
                                "scorp2.rom", "service.rom"};
    std::vector<uint8_t> gluk;
    if (model_ == ZxCloneModel::Pentagon1024) {
        for (const char* n : gluk_names) {
            if (try_rom(dir, n, gluk) && gluk.size() >= 0x4000) {
                copy_page(2, gluk, 0);
                gluk_present_ = true;
                break;
            }
        }
        if (!gluk_present_) copy_page(2, std::vector<uint8_t>(rom_[3].begin(), rom_[3].end()), 0);
    } else if (rom_[2][0] == 0 && rom_[2][1] == 0) {
        const char* svc[] = {"scorp2.rom", "service.rom"};
        for (const char* n : svc) {
            if (try_rom(dir, n, gluk) && gluk.size() >= 0x4000) {
                copy_page(2, gluk, 0);
                break;
            }
        }
    }
    return true;
}

bool ZxClone::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    cpu_.set_memory_handlers([this](uint16_t a) { return mem_read(a); },
                             [this](uint16_t a, uint8_t v) { mem_write(a, v); });
    cpu_.set_io_handlers([this](uint16_t p) { return io_in(p); },
                         [this](uint16_t p, uint8_t v) { io_out(p, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });
    cpu_.set_instruction_hook([this](uint16_t pc) { on_m1(pc); });

    reset();

    if (ends_ci(rom_path, ".trd") || ends_ci(rom_path, ".scl")) {
        std::string disk_error;
        if (!beta_.load_disk(rom_path, &disk_error)) warnings_.push_back(disk_error);
    }
    return true;
}

void ZxClone::reset() {
    for (int i = 0; i < ram_pages_; i++) ram_[size_t(i)].fill(0);
    port_7ffd_ = 0;
    port_1ffd_ = 0;
    port_dffd_ = 0;
    paging_locked_ = false;
    nmi_pending_ = false;
    magic_down_ = false;
    beta_.reset();
    if (model_ == ZxCloneModel::Pentagon1024 && gluk_present_) beta_.enable();
    update_memory();
    cpu_.reset();
    ay_.reset();
    border_ = 7;
    speaker_ = ear_ = 0;
    keys_.fill(0xff);
    joy_ = 0;
    flash_ = false;
    flash_count_ = 0;
    line_ = t_in_line_ = frame_t_ = 0;
    audio_.clear();
    audio_acc_ = 0;
    beeper_level_ = 0;
    tape_.stop();
    std::fill(framebuffer_.begin(), framebuffer_.end(), palette_[7]);
}

void ZxClone::update_memory() {
    pantalla_ = uint8_t((port_7ffd_ & 0x08) ? 7 : 5);
    page0_ram_ = false;
    if (model_ == ZxCloneModel::Pentagon1024) {
        ram3_ = uint8_t(((port_7ffd_ & 7) | ((port_7ffd_ & 0xc0) >> 3) | ((port_dffd_ & 1) << 5)) &
                        (ram_pages_ - 1));
        const uint8_t rom1 = uint8_t((port_7ffd_ >> 4) & 1);
        if (beta_.active()) {
            rom_page_ = (gluk_present_ && rom1 == 0) ? 2 : 3;
        } else {
            rom_page_ = rom1;
        }
    } else {
        ram3_ = uint8_t(((port_7ffd_ & 7) | ((port_1ffd_ & 0x10) >> 1)) & (ram_pages_ - 1));
        if ((port_1ffd_ & 0x01) && !nmi_pending_) {
            page0_ram_ = true;
            rom_page_ = 0;
        } else if (port_1ffd_ & 0x02) {
            rom_page_ = 2;
        } else {
            const uint8_t rom1 = uint8_t((port_7ffd_ >> 4) & 1);
            rom_page_ = uint8_t((((nmi_pending_ || beta_.active()) ? 1 : 0) << 1) | rom1);
        }
    }
}

void ZxClone::on_m1(uint16_t pc) {
    if (pc >= 0x3d00 && pc < 0x3e00) {
        const bool rom48 = (port_7ffd_ & 0x10) != 0;
        if (rom48 && !page0_ram_) beta_.enable();
        update_memory();
    } else if (pc >= 0x4000) {
        if (nmi_pending_) {
            nmi_pending_ = false;
            update_memory();
        }
        if (beta_.active()) {
            beta_.disable();
            update_memory();
        }
    }
}

uint8_t ZxClone::mem_read(uint16_t addr) {
    if (addr < 0x4000) {
        if (page0_ram_) return ram_[0][addr];
        return rom_[rom_page_][addr];
    }
    if (addr < 0x8000) return ram_[5][addr & 0x3fff];
    if (addr < 0xc000) return ram_[2][addr & 0x3fff];
    return ram_[ram3_ % ram_pages_][addr & 0x3fff];
}

void ZxClone::mem_write(uint16_t addr, uint8_t value) {
    if (addr < 0x4000) {
        if (page0_ram_) ram_[0][addr] = value;
        return;
    }
    if (addr < 0x8000) {
        ram_[5][addr & 0x3fff] = value;
        return;
    }
    if (addr < 0xc000) {
        ram_[2][addr & 0x3fff] = value;
        return;
    }
    ram_[ram3_ % ram_pages_][addr & 0x3fff] = value;
}

uint8_t ZxClone::io_in(uint16_t port) {
    if ((port & 1) == 0) {
        uint8_t keys = 0x1f;
        if ((port & 0x8000) == 0) keys &= keys_[7];
        if ((port & 0x4000) == 0) keys &= keys_[6];
        if ((port & 0x2000) == 0) keys &= keys_[5];
        if ((port & 0x1000) == 0) keys &= keys_[4];
        if ((port & 0x0800) == 0) keys &= keys_[3];
        if ((port & 0x0400) == 0) keys &= keys_[2];
        if ((port & 0x0200) == 0) keys &= keys_[1];
        if ((port & 0x0100) == 0) keys &= keys_[0];
        return uint8_t((keys & 0x1f) | 0xa0 | ear_ | speaker_);
    }

    if (beta_.active()) {
        switch (port & 0xff) {
            case 0x1f: return beta_.status_r();
            case 0x3f: return beta_.track_r();
            case 0x5f: return beta_.sector_r();
            case 0x7f: return beta_.data_r();
            case 0xff: return beta_.state_r();
            default: break;
        }
    } else if ((port & 0x21) == 0x01) {
        return uint8_t(joy_ & 0x1f);
    }

    if (model_ == ZxCloneModel::Pentagon1024) {
        if ((port & 0xc002) == 0xc000) return uint8_t(ay_.read());
    } else {
        if ((port & 0xe023) == 0xe021) return uint8_t(ay_.read());
    }
    return 0xff;
}

void ZxClone::io_out(uint16_t port, uint8_t value) {
    if ((port & 1) == 0) {
        border_ = value & 7;
        speaker_ = (value & 0x10) ? 0x10 : 0x00;
        beeper_level_ = (value & 0x10) ? int16_t(4096) : int16_t(-4096);
    }

    if (beta_.active()) {
        switch (port & 0xff) {
            case 0x1f: beta_.command_w(value); break;
            case 0x3f: beta_.track_w(value); break;
            case 0x5f: beta_.sector_w(value); break;
            case 0x7f: beta_.data_w(value); break;
            case 0xff: beta_.param_w(value); break;
            default: break;
        }
    }

    if (model_ == ZxCloneModel::Pentagon1024) {
        if ((port & 0x8002) == 0 && (port & 1) != 0) {
            if (!paging_locked_) {
                port_7ffd_ = value;
                paging_locked_ = (value & 0x20) != 0;
                update_memory();
            }
        }
        if ((port & 0xf002) == 0xd000) {
            port_dffd_ = value;
            update_memory();
        } else if ((port & 0xc002) == 0xc000) {
            ay_.control(value);
        } else if ((port & 0xc002) == 0x8000) {
            ay_.write(value);
        }
    } else {
        if ((port & 0xc023) == 0x4021) {
            if (!paging_locked_) {
                port_7ffd_ = value;
                paging_locked_ = (value & 0x20) != 0;
                update_memory();
            }
        }
        if ((port & 0xc023) == 0x0021) {
            port_1ffd_ = value;
            update_memory();
        }
        if ((port & 0xe023) == 0xe021) ay_.control(value);
        if ((port & 0xe023) == 0xa021) ay_.write(value);
    }
}

void ZxClone::on_cycles(int cycles) {
    t_in_line_ += cycles;
    frame_t_ += cycles;
    while (t_in_line_ >= kTstatesPerLine) {
        t_in_line_ -= kTstatesPerLine;
        render_line(line_);
        ++line_;
        if (line_ >= kLinesPerFrame) line_ = 0;
    }
    if (tape_.is_playing()) ear_ = tape_.advance(cycles) ? 0x40 : 0x00;
    else if (tape_.is_loaded()) {
        const uint16_t pc = cpu_.pc();
        if ((pc >= 0x04c2 && pc < 0x0800) || (pc >= 0x056c && pc < 0x0600)) tape_.play(true);
    }
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kClock)) {
        audio_acc_ -= int64_t(kClock);
        const int32_t mixed = ay_.update() + beeper_level_;
        audio_.push_back(int16_t(std::clamp(mixed, int32_t(-32768), int32_t(32767))));
    }
}

void ZxClone::render_line(int line) {
    if (line < 32 || line > 311) return;
    const int sy = line - 32;
    if (sy < 0 || sy >= kScreenHeight) return;
    uint32_t* dst = framebuffer_.data() + size_t(sy) * kScreenWidth;
    const uint32_t border = palette_[border_ & 7];
    for (int x = 0; x < kScreenWidth; x++) dst[x] = border;

    if (line < 80 || line > 271) return;
    const int y = line - 80;
    const auto& vram = ram_[pantalla_];
    const uint16_t pix_base = kScrTable[y];
    const int attr_row = (y >> 3) << 5;
    for (int col = 0; col < 32; ++col) {
        const uint8_t attrib = vram[0x1800 + attr_row + col];
        const uint8_t pixels = vram[pix_base + col];
        int ink = attrib & 7;
        int paper = (attrib >> 3) & 7;
        if (attrib & 0x40) {
            ink += 8;
            paper += 8;
        }
        if ((attrib & 0x80) && flash_) std::swap(ink, paper);
        const uint32_t c_ink = palette_[ink];
        const uint32_t c_paper = palette_[paper];
        uint8_t pix = pixels;
        for (int b = 0; b < 8; ++b) {
            dst[48 + col * 8 + b] = (pix & 0x80) ? c_ink : c_paper;
            pix = uint8_t(pix << 1);
        }
    }
}

void ZxClone::run_frame() {
    line_ = 0;
    t_in_line_ = 0;
    frame_t_ = 0;
    cpu_.set_irq(IrqLine::Hold);
    int remaining = kTstatesPerFrame;
    while (remaining > 0) {
        const int ran = cpu_.run(std::min(remaining, kTstatesPerLine));
        if (ran <= 0) break;
        remaining -= ran;
        if (remaining < kTstatesPerFrame - 32) cpu_.set_irq(IrqLine::Clear);
    }
    if (line_ != 0 || t_in_line_ != 0) {
        while (line_ < kLinesPerFrame) {
            render_line(line_);
            ++line_;
        }
    }
    line_ = 0;
    t_in_line_ = 0;
    frame_t_ = 0;
    flash_count_ = (flash_count_ + 1) & 0x0f;
    if (flash_count_ == 0) flash_ = !flash_;
}

void ZxClone::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

void ZxClone::set_dip_switch(int, uint8_t) {}

void ZxClone::set_inputs(const MachineInputs& inputs) {
    apply_keyboard(inputs);
    const bool magic = inputs.key(Key::F5);
    if (magic && !magic_down_) {
        nmi_pending_ = true;
        if (model_ == ZxCloneModel::Scorpion256) {
            beta_.enable();
            update_memory();
        }
        cpu_.set_nmi(IrqLine::Pulse);
    }
    magic_down_ = magic;
}

void ZxClone::apply_keyboard(const MachineInputs& in) {
    keys_.fill(0xff);
    for (int row = 0; row < 8; ++row) {
        for (int bit = 0; bit < 5; ++bit) {
            if (in.key(kMatrix[row][bit])) keys_[row] &= uint8_t(~(1u << bit));
        }
    }
    joy_ = 0;
    if (in.player1.right) joy_ |= 0x01;
    if (in.player1.left) joy_ |= 0x02;
    if (in.player1.down) joy_ |= 0x04;
    if (in.player1.up) joy_ |= 0x08;
    if (in.player1.button1) joy_ |= 0x10;
    auto press_bit = [&](int row, int bit) { keys_[row] = uint8_t(keys_[row] & ~(1u << bit)); };
    if (in.player1.left) press_bit(4, 4);
    if (in.player1.right) press_bit(4, 3);
    if (in.player1.down) press_bit(4, 2);
    if (in.player1.up) press_bit(4, 1);
    if (in.player1.button1) press_bit(4, 0);
}

bool ZxClone::load_media(const std::string& path, std::string* error) {
    if (ends_ci(path, ".trd") || ends_ci(path, ".scl")) return beta_.load_disk(path, error);
    if (ends_ci(path, ".tzx") || ends_ci(path, ".tap") || ends_ci(path, ".cdt")) {
        if (!tape_.load_file(path, error)) return false;
        tape_.stop();
        return true;
    }
    if (error) *error = "unsupported media (use .trd/.scl disk or .tap/.tzx tape): " + path;
    return false;
}

void ZxClone::tape_toggle_play() {
    if (tape_.is_loaded()) tape_.play(!tape_.is_playing());
}

}  // namespace dsp
