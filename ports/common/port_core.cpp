// Board-independent part of the microcontroller ports: ROM menu, driver start,
// frame loop. See port.h.
#include "port.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <sstream>

#include "core/fs_compat.h"
#include "font8x8.h"
#include "frontend/machine_registry.h"

namespace dsp {
namespace port {

namespace fs = dsp::fsx;

// --- helpers --------------------------------------------------------------------

bool ScaleMap::update(int src_width, int src_height, int display_width, int display_height,
                      int screen_w, int screen_h) {
    if (display_width <= 0) display_width = src_width;
    if (display_height <= 0) display_height = src_height;
    if (src_width == src_w && src_height == src_h && display_width == disp_w &&
        display_height == disp_h && int(xs.size()) == dst_w && int(ys.size()) == dst_h && dst_w > 0)
        return false;
    src_w = src_width;
    src_h = src_height;
    disp_w = display_width;
    disp_h = display_height;
    // Integer scale when the picture fits (crisp pixels), otherwise shrink to
    // fit; either way with the display proportions.
    int w = display_width, h = display_height;
    if (w <= screen_w && h <= screen_h) {
        const int k = std::max(1, std::min(screen_w / w, screen_h / h));
        w *= k;
        h *= k;
    } else if (int64_t(w) * screen_h > int64_t(h) * screen_w) {
        h = std::max(1, int(int64_t(h) * screen_w / w));
        w = screen_w;
    } else {
        w = std::max(1, int(int64_t(w) * screen_h / h));
        h = screen_h;
    }
    dst_w = w;
    dst_h = h;
    dst_x = (screen_w - w) / 2;
    dst_y = (screen_h - h) / 2;
    xs.resize(size_t(w));
    ys.resize(size_t(h));
    for (int x = 0; x < w; ++x) xs[size_t(x)] = uint16_t(int64_t(x) * src_w / w);
    for (int y = 0; y < h; ++y) ys[size_t(y)] = uint16_t(int64_t(y) * src_h / h);
    return true;
}

MachineInputs map_inputs(const HostInput& in, const Machine& machine) {
    MachineInputs m;
    const bool keyboard_machine = machine.uses_keyboard();
    m.player1.up = in.key(Key::Up) || in.pad_up;
    m.player1.down = in.key(Key::Down) || in.pad_down;
    m.player1.left = in.key(Key::Left) || in.pad_left;
    m.player1.right = in.key(Key::Right) || in.pad_right;
    m.player1.button1 = in.key(Key::LeftCtrl) || in.key(Key::Space) || in.pad_a;
    m.player1.button2 = in.left_alt || in.key(Key::Z) || in.pad_b;
    m.player1.button3 = in.key(Key::X) || in.pad_x;
    m.player1.button4 = in.key(Key::C) || in.pad_y;
    m.player1.start = in.key(Key::Num1) || in.pad_start;
    m.player1.select = in.key(Key::Num3) || (in.pad_select && !in.pad_start);

    m.player2.up = in.key(Key::R);
    m.player2.down = in.key(Key::F);
    m.player2.left = in.key(Key::D);
    m.player2.right = in.key(Key::G);
    m.player2.button1 = in.key(Key::A);
    m.player2.button2 = in.key(Key::S);
    m.player2.button3 = in.key(Key::Q);
    m.player2.button4 = in.key(Key::W);
    m.player2.start = in.key(Key::Num2);
    m.player2.select = in.key(Key::Num4);

    // On a gamepad, Select alone is the coin.
    m.coin1 = in.key(Key::Num5) || (in.pad_select && !in.pad_start);
    m.coin2 = in.key(Key::Num6);
    m.service = in.key(Key::F1);
    if (keyboard_machine) m.keys = in.keys;

    if (in.mouse_connected && machine.uses_relative_pointer()) {
        m.has_pointer = true;
        m.pointer_relative = true;
        m.pointer_dx = in.mouse_dx;
        m.pointer_dy = in.mouse_dy;
        m.pointer_button1 = in.mouse_left;
        m.pointer_button2 = in.mouse_right;
    }
    return m;
}

void Resampler::reset(int in_rate, int out_rate) {
    if (in_rate <= 0 || out_rate <= 0) in_rate = out_rate = 1;
    step_ = uint32_t((uint64_t(in_rate) << 16) / uint64_t(out_rate));
    if (step_ == 0) step_ = 1;
    pos_ = 0;
    prev_ = 0;
}

void Resampler::process(const std::vector<int16_t>& in, std::vector<int16_t>& out) {
    // Output sample k lies at pos_ (16.16) between prev_ (index -1) and in[0].
    size_t i = 0;
    while (i < in.size()) {
        while (pos_ < (1u << 16)) {
            const int32_t a = prev_, b = in[i];
            out.push_back(int16_t(a + (((b - a) * int32_t(pos_)) >> 16)));
            pos_ += step_;
        }
        pos_ -= 1u << 16;
        prev_ = in[i++];
    }
}

std::vector<std::string> split_args(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t p = line.find_first_not_of(" \t\r");
        if (p == std::string::npos || line[p] == '#') continue;
        while (p < line.size()) {
            while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) ++p;
            if (p >= line.size()) break;
            std::string arg;
            if (line[p] == '"') {
                const size_t end = line.find('"', p + 1);
                arg = line.substr(p + 1, end == std::string::npos ? std::string::npos : end - p - 1);
                p = end == std::string::npos ? line.size() : end + 1;
            } else {
                const size_t start = p;
                while (p < line.size() && !std::isspace(static_cast<unsigned char>(line[p]))) ++p;
                arg = line.substr(start, p - start);
            }
            out.push_back(arg);
        }
    }
    return out;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool read_text(const std::string& path, std::string* text) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *text = ss.str();
    return true;
}

// --- menu drawing -----------------------------------------------------------------

constexpr uint32_t kBack = 0xff101820, kText = 0xffd8d8d8, kDim = 0xff8090a0;
constexpr uint32_t kBar = 0xff203850, kSelect = 0xff3070b0, kTitle = 0xffffd060, kError = 0xffff7060;

// Menu screen: one byte per pixel (an index into a few colours), so a
// 320x240 menu takes 75 KB, which a board without PSRAM can still spare. It is
// sent to the board a few rows at a time (Hal::draw_rows).
class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h), px_(size_t(w) * size_t(h), 0) { palette_.push_back(kBack); }
    int width() const { return w_; }
    int height() const { return h_; }
    void clear(uint32_t c) { std::fill(px_.begin(), px_.end(), index(c)); }
    void rect(int x, int y, int w, int h, uint32_t c) {
        const uint8_t i = index(c);
        for (int j = std::max(0, y); j < std::min(h_, y + h); ++j)
            for (int k = std::max(0, x); k < std::min(w_, x + w); ++k) px_[size_t(j) * w_ + k] = i;
    }
    // Draws up to `max_chars` characters; returns the x after the text.
    int text(int x, int y, const std::string& s, uint32_t c, size_t max_chars = 1000) {
        const uint8_t i = index(c);
        for (size_t n = 0; n < s.size() && n < max_chars; ++n, x += 8) {
            const unsigned char ch = static_cast<unsigned char>(s[n]);
            const uint8_t* g = kFont8x8[(ch >= 0x20 && ch < 0x7f) ? ch - 0x20 : '?' - 0x20];
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 8; ++col)
                    if ((g[row] >> col) & 1) {
                        const int px = x + col, py = y + row;
                        if (px >= 0 && px < w_ && py >= 0 && py < h_) px_[size_t(py) * w_ + px] = i;
                    }
        }
        return x;
    }
    void show(Hal& hal) const {
        constexpr int kBand = 8;
        std::vector<uint32_t> rows(size_t(w_) * kBand);
        for (int y = 0; y < h_; y += kBand) {
            const int n = std::min(kBand, h_ - y);
            const uint8_t* src = &px_[size_t(y) * w_];
            for (size_t k = 0; k < size_t(w_) * size_t(n); ++k) rows[k] = palette_[src[k]];
            hal.draw_rows(y, n, rows.data());
        }
    }

private:
    uint8_t index(uint32_t c) {
        for (size_t i = 0; i < palette_.size(); ++i)
            if (palette_[i] == c) return uint8_t(i);
        if (palette_.size() >= 255) return 0;
        palette_.push_back(c);
        return uint8_t(palette_.size() - 1);
    }
    int w_, h_;
    std::vector<uint8_t> px_;
    std::vector<uint32_t> palette_;
};

// Word-wraps `text` to `cols` characters a line.
std::vector<std::string> wrap(const std::string& text, size_t cols) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string para;
    while (std::getline(in, para)) {
        std::string line;
        std::istringstream words(para);
        std::string w;
        while (words >> w) {
            while (w.size() > cols) {
                if (!line.empty()) { lines.push_back(line); line.clear(); }
                lines.push_back(w.substr(0, cols));
                w.erase(0, cols);
            }
            if (line.size() + w.size() + (line.empty() ? 0 : 1) > cols) {
                lines.push_back(line);
                line.clear();
            }
            if (!line.empty()) line += ' ';
            line += w;
        }
        lines.push_back(line);
    }
    return lines;
}

// Edge-triggered menu buttons with auto-repeat for up/down.
class MenuKeys {
public:
    enum Button { Up, Down, PageUp, PageDown, Accept, Back, Count };
    void update(const HostInput& in, uint64_t now_us) {
        const bool state[Count] = {
            in.key(Key::Up) || in.pad_up,
            in.key(Key::Down) || in.pad_down,
            in.key(Key::Left) || in.pad_left,
            in.key(Key::Right) || in.pad_right,
            in.key(Key::Enter) || in.key(Key::Space) || in.key(Key::LeftCtrl) || in.pad_a || in.pad_start,
            in.key(Key::Escape) || in.key(Key::F12) || in.pad_b,
        };
        for (int b = 0; b < Count; ++b) {
            pressed_[b] = false;
            if (state[b] && !held_[b]) {
                pressed_[b] = true;
                repeat_at_[b] = now_us + 400000;
            } else if (state[b] && b <= PageDown && now_us >= repeat_at_[b]) {
                pressed_[b] = true;
                repeat_at_[b] = now_us + 70000;
            }
            held_[b] = state[b];
        }
    }
    bool pressed(Button b) const { return pressed_[b]; }
    bool any_held() const {
        for (bool h : held_) if (h) return true;
        return false;
    }

private:
    bool held_[Count] = {}, pressed_[Count] = {};
    uint64_t repeat_at_[Count] = {};
};

struct MenuEntry {
    std::string label;
    std::vector<std::string> args;
};

// --- the port ---------------------------------------------------------------------

class Port {
public:
    explicit Port(Hal& hal) : hal_(hal) {}
    int run(int max_frames);

private:
    std::vector<MenuEntry> scan_entries(std::string* note);
    bool menu(std::vector<std::string>* args);
    void message(const std::string& title, const std::string& text, bool wait);
    void draw_menu(Canvas& c, const std::vector<MenuEntry>& entries, size_t selected, size_t top,
                   const std::string& note);
    std::string resolve(const std::string& path) const;
    int play(const std::vector<std::string>& args, int max_frames, std::string* error);
    void wait_release();

    Hal& hal_;
    std::string root_;
    MenuKeys keys_;
};

std::string Port::resolve(const std::string& path) const {
    if (path.empty() || path[0] == '/' || root_.empty()) return path;
    return (fs::path(root_) / path).string();
}

void Port::wait_release() {
    HostInput in;
    for (int i = 0; i < 2000; ++i) {
        hal_.poll(in);
        keys_.update(in, hal_.micros());
        if (!keys_.any_held()) return;
        hal_.idle();
        hal_.sleep_us(10000);
    }
}

void Port::message(const std::string& title, const std::string& text, bool wait) {
    Canvas c(hal_.screen_width(), hal_.screen_height());
    c.rect(0, 0, c.width(), 14, kBar);
    c.text(4, 3, title, kTitle, size_t(c.width() / 8 - 1));
    int y = 22;
    for (const std::string& line : wrap(text, size_t(c.width() / 8 - 2))) {
        if (y > c.height() - 20) break;
        c.text(8, y, line, kText);
        y += 10;
    }
    if (wait) c.text(8, c.height() - 12, "Press a button", kDim);
    c.show(hal_);
    hal_.log((title + ": " + text).c_str());
    if (!wait) return;
    wait_release();
    HostInput in;
    for (;;) {
        hal_.poll(in);
        keys_.update(in, hal_.micros());
        if (keys_.pressed(MenuKeys::Accept) || keys_.pressed(MenuKeys::Back)) break;
        hal_.idle();
        hal_.sleep_us(10000);
    }
    wait_release();
}

// Menu entries: <root>/dsp/*.txt command files, then <root>/roms/<name>.zip
// (or folder) for each built-in driver name or alias.
std::vector<MenuEntry> Port::scan_entries(std::string* note) {
    std::vector<MenuEntry> entries;
    const auto& drivers = list_drivers();
    if (root_.empty()) {
        *note = "No SD card.";
        return entries;
    }
    std::error_code ec;
    std::vector<std::string> names;
    for (const auto& item : fs::directory_iterator(fs::path(root_) / "dsp", ec)) {
        const std::string name = item.path().filename().string();
        if (lower(item.path().extension().string()) == ".txt" && lower(name) != "autostart.txt")
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        std::string text;
        if (!read_text((fs::path(root_) / "dsp" / name).string(), &text)) continue;
        MenuEntry e;
        e.args = split_args(text);
        e.label = fs::path(name).stem().string() + " (dsp/" + name + ")";
        if (!e.args.empty()) entries.push_back(e);
    }
    std::vector<std::pair<std::string, std::string>> roms;  // stem, file name
    for (const auto& item : fs::directory_iterator(fs::path(root_) / "roms", ec)) {
        const fs::path p = item.path();
        std::string stem = lower(p.filename().string());
        if (lower(p.extension().string()) == ".zip") stem = lower(p.stem().string());
        else if (!item.is_directory()) continue;
        roms.emplace_back(stem, p.filename().string());
    }
    std::sort(roms.begin(), roms.end());
    for (const auto& rom : roms) {
        for (const DriverInfo& d : drivers) {
            bool match = lower(d.name) == rom.first;
            for (const std::string& a : d.aliases) match = match || lower(a) == rom.first;
            if (!match) continue;
            MenuEntry e;
            e.label = d.title + " (" + rom.second + ")";
            e.args = {"--game", rom.first, "roms/" + rom.second};
            entries.push_back(e);
            break;
        }
    }
    if (entries.empty()) {
        std::string list;
        for (const DriverInfo& d : drivers) list += (list.empty() ? "" : ", ") + d.name;
        *note = "Nothing to start. Copy ROM sets to " + root_ + "/roms/NAME.zip (or a folder) or "
                "command lines to " + root_ + "/dsp/*.txt. Built-in drivers: " + list;
    }
    return entries;
}

void Port::draw_menu(Canvas& c, const std::vector<MenuEntry>& entries, size_t selected, size_t top,
                     const std::string& note) {
    c.clear(kBack);
    c.rect(0, 0, c.width(), 14, kBar);
    c.text(4, 3, std::string("dsp-cpp  ") + hal_.board_name(), kTitle);
    const size_t cols = size_t(c.width() / 8 - 2);
    if (entries.empty()) {
        int y = 22;
        for (const std::string& line : wrap(note, cols)) {
            if (y > c.height() - 10) break;
            c.text(8, y, line, kText);
            y += 10;
        }
        return;
    }
    const size_t rows = size_t((c.height() - 30) / 10);
    for (size_t i = 0; i < rows && top + i < entries.size(); ++i) {
        const int y = 18 + int(i) * 10;
        if (top + i == selected) c.rect(0, y - 1, c.width(), 10, kSelect);
        c.text(8, y, entries[top + i].label, top + i == selected ? 0xffffffff : kText, cols);
    }
    char status[64];
    std::snprintf(status, sizeof status, "%u/%u   Enter/A: start   F12: back",
                  unsigned(selected + 1), unsigned(entries.size()));
    c.text(4, c.height() - 10, status, kDim, cols + 1);
}

bool Port::menu(std::vector<std::string>* args) {
    std::string note;
    std::vector<MenuEntry> entries = scan_entries(&note);
    Canvas c(hal_.screen_width(), hal_.screen_height());
    const size_t rows = size_t((c.height() - 30) / 10);
    size_t selected = 0, top = 0;
    bool dirty = true;
    wait_release();
    HostInput in;
    for (;;) {
        hal_.poll(in);
        keys_.update(in, hal_.micros());
        if (!entries.empty()) {
            const size_t n = entries.size();
            if (keys_.pressed(MenuKeys::Up)) { selected = (selected + n - 1) % n; dirty = true; }
            if (keys_.pressed(MenuKeys::Down)) { selected = (selected + 1) % n; dirty = true; }
            if (keys_.pressed(MenuKeys::PageUp)) { selected = selected > rows ? selected - rows : 0; dirty = true; }
            if (keys_.pressed(MenuKeys::PageDown)) { selected = std::min(n - 1, selected + rows); dirty = true; }
            if (keys_.pressed(MenuKeys::Accept)) {
                *args = entries[selected].args;
                return true;
            }
        } else if (keys_.pressed(MenuKeys::Accept)) {
            entries = scan_entries(&note);  // card inserted since?
            dirty = true;
        }
        if (dirty) {
            if (selected < top) top = selected;
            if (selected >= top + rows) top = selected - rows + 1;
            draw_menu(c, entries, selected, top, note);
            c.show(hal_);
            dirty = false;
        }
        hal_.idle();
        hal_.sleep_us(15000);
    }
}

int Port::play(const std::vector<std::string>& args, int max_frames, std::string* error) {
    std::string game, rom;
    std::vector<std::string> media;
    std::vector<std::pair<int, uint8_t>> dips;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool has_value = i + 1 < args.size();
        if (a == "--game" && has_value) {
            game = args[++i];
        } else if ((a == "--tape" || a == "--disk" || a == "--cart") && has_value) {
            media.push_back(resolve(args[++i]));
        } else if (a == "--dip" && has_value) {
            const std::string v = args[++i];
            const size_t sep = v.find(':');
            if (sep == std::string::npos) dips.emplace_back(0, uint8_t(std::strtoul(v.c_str(), nullptr, 0)));
            else dips.emplace_back(std::atoi(v.substr(0, sep).c_str()),
                                   uint8_t(std::strtoul(v.c_str() + sep + 1, nullptr, 0)));
        } else if (a == "--scale" || a == "--screenshot" || a == "--frames") {
            ++i;  // desktop options, ignored here
        } else if (!a.empty() && a[0] == '-') {
            // --mute, --fullscreen...: nothing to do on a board
        } else {
            rom = resolve(a);
        }
    }
    if (game.empty() || rom.empty()) {
        *error = "the command line needs --game NAME and a ROM path";
        return -1;
    }
    std::unique_ptr<Machine> machine = create_machine(game);
    if (!machine) {
        *error = "driver '" + game + "' is not built into this firmware";
        return -1;
    }
    message("Loading", machine->title(), false);
    if (!machine->init(rom, error)) return -1;
    for (const std::string& m : media)
        if (!machine->load_media(m, error)) {
            *error = m + ": " + *error;
            return -1;
        }
    for (const auto& d : dips) machine->set_dip_switch(d.first, d.second);
    for (const std::string& w : machine->warnings()) hal_.log(("warning: " + w).c_str());

    wait_release();
    Resampler resampler;
    const int out_rate = hal_.output_rate();
    resampler.reset(machine->sample_rate(), out_rate);
    std::vector<int16_t> samples, resampled;
    samples.reserve(4096);
    resampled.reserve(4096);
    const double fps = machine->frames_per_second() > 1 ? machine->frames_per_second() : 60.0;
    const uint64_t period = uint64_t(1000000.0 / fps);
    const size_t max_queue = size_t(out_rate) / 6;  // ~170 ms of sound
    uint64_t next = hal_.micros();
    uint64_t exit_hold = 0;
    int frames = 0, skipped = 0;
    uint64_t stats_start = next;
    int stats_frames = 0;
    HostInput in;
    for (;;) {
        hal_.poll(in);
        const bool exit_combo = in.key(Key::F12) || (in.pad_start && in.pad_select);
        if (exit_combo) {
            if (exit_hold == 0) exit_hold = hal_.micros();
            else if (hal_.micros() - exit_hold > 500000) break;
        } else {
            exit_hold = 0;
        }
        machine->set_inputs(map_inputs(in, *machine));
        machine->run_frame();

        samples.clear();
        machine->drain_audio(samples);
        resampled.clear();
        resampler.process(samples, resampled);
        if (!resampled.empty() && hal_.audio_queued() < max_queue)
            hal_.push_audio(resampled.data(), resampled.size());

        next += period;
        const uint64_t now = hal_.micros();
        // Late by more than a frame: skip drawing (up to 3 in a row) so the
        // emulation keeps its speed if the board can manage the CPU part.
        if (now > next + period && skipped < 3) {
            ++skipped;
        } else {
            skipped = 0;
            hal_.present(machine->framebuffer(), machine->screen_width(), machine->screen_height(),
                         machine->display_width(), machine->display_height());
        }
        const uint64_t after = hal_.micros();
        if (after < next) hal_.sleep_us(uint32_t(next - after));
        else if (after > next + 8 * period) next = after;  // far behind: start again from now
        ++frames;
        ++stats_frames;
        if (after - stats_start >= 5000000) {
            char text[96];
            std::snprintf(text, sizeof text, "%s: %.1f fps (target %.1f)", game.c_str(),
                          stats_frames * 1e6 / double(after - stats_start), fps);
            hal_.log(text);
            stats_start = after;
            stats_frames = 0;
        }
        if (max_frames > 0 && frames >= max_frames) return 1;
    }
    return 0;
}

int Port::run(int max_frames) {
    root_ = hal_.storage_root();
    std::vector<std::string> args;
    bool from_start = false;
    std::string error;
    if (dsp_embedded_file_count > 0) {
        std::string dir;
        if (!hal_.mount_embedded(dsp_embedded_files, dsp_embedded_file_count, &dir, &error)) {
            message("Embedded media", error, true);
        } else {
            for (const char* const* a = dsp_embedded_args; a && *a; ++a) {
                std::string v = *a;
                if (v.compare(0, 2, "@/") == 0) v = (fs::path(dir) / v.substr(2)).string();
                args.push_back(v);
            }
            from_start = true;
        }
    }
    if (!from_start && !root_.empty()) {
        std::string text;
        if (read_text((fs::path(root_) / "dsp" / "autostart.txt").string(), &text)) {
            args = split_args(text);
            from_start = !args.empty();
        }
    }
    for (;;) {
        if (!from_start && !menu(&args)) return 1;
        from_start = false;
        error.clear();
        int r;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
        try {
            r = play(args, max_frames, &error);
        } catch (const std::bad_alloc&) {
            // Boards without PSRAM: the driver's RAM/ROM arrays did not fit.
            error = "not enough memory for this driver on this board";
            r = -1;
        }
#else
        r = play(args, max_frames, &error);
#endif
        if (r < 0) {
            message("Cannot start", error, max_frames <= 0);
            if (max_frames > 0) return 2;
        }
        if (max_frames > 0 && r == 1) return 0;
    }
}

}  // namespace

int run(Hal& hal, int max_frames) {
    Port port(hal);
    return port.run(max_frames);
}

}  // namespace port
}  // namespace dsp
