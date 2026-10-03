// ZX Spectrum Next driver.
//
// Hardware behaviour follows the TBBlue/ZX Next core (zxnext.vhd) as modelled
// by MAME's specnext driver (BSD-3-Clause, Andrei I. Holub, thanks to Kev
// Brady and Peter Ped Helcmanovsky): memory decoding, DivMMC automap, the
// Next register file, reset values and video timings come from there.
#include "drivers/computers/specnext.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

constexpr uint8_t kMachineId = 0x08;    // "Emulators"
constexpr uint8_t kCoreVersion = 0x32;  // 3.02
constexpr uint8_t kCoreSubVersion = 0x04;
constexpr uint8_t kBoardIssue = 0;

// Standard Spectrum colours as RRRGGGBB, used for the ULA palette at reset.
constexpr uint8_t kUlaDefault[16] = {0x00, 0x02, 0xa0, 0xa2, 0x14, 0x16, 0xb4, 0xb6,
                                     0x00, 0x03, 0xe0, 0xe7, 0x1c, 0x1f, 0xfc, 0xff};

inline uint16_t rgb8_to_9(uint8_t v) { return uint16_t((v << 1) | ((v & 3) ? 1 : 0)); }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Picks the main boot ROM out of a tbblue distribution: boot-30204.bin, else
// any boot-*.bin that is not the anti-brick one, else tbblue_loader.rom.
bool find_boot_rom(const RomLoader& loader, std::vector<uint8_t>& out) {
    if (loader.try_read("boot-30204.bin", out) && out.size() >= 0x2000) return true;
    std::vector<std::string> names = loader.filenames();
    std::sort(names.rbegin(), names.rend());
    for (const std::string& n : names) {
        if (n.rfind("boot-", 0) == 0 && n.find("-ab") == std::string::npos &&
            n.size() > 4 && n.substr(n.size() - 4) == ".bin" && loader.try_read(n, out) &&
            out.size() >= 0x2000)
            return true;
    }
    for (const char* n : {"tbblue_loader.rom", "tbblue.rom", "specnext.rom", "bootrom.bin"})
        if (loader.try_read(n, out) && out.size() >= 0x2000) return true;
    return false;
}

bool is_sd_image(const std::string& path) {
    const std::string ext = lower(std::filesystem::path(path).extension().string());
    return ext == ".img" || ext == ".mmc" || ext == ".hdf" || ext == ".sd";
}

}  // namespace

// ---------------------------------------------------------------------------
// DivMMC automap state machine (specnext_divmmc)

void SpecNext::DivMmc::clock() {
    if (automap_reset || retn_seen)
        button_nmi = false;
    else if (button)
        button_nmi = true;
    else if (held)
        button_nmi = false;

    if (automap_reset || retn_seen) {
        hold = false;
    } else if (!mreq_n && !m1_n) {
        hold = (automap_active && (instant_on || delayed_on || (nmi_instant_on && button_nmi) ||
                                   (nmi_delayed_on && button_nmi))) ||
               (automap_rom3_active && (rom3_instant_on || rom3_delayed_on)) ||
               (held && !(automap_active && delayed_off));
    }

    if (automap_reset || retn_seen)
        held = false;
    else if (mreq_n)
        held = hold;
}

void SpecNext::DivMmc::reset_lines() {
    mreq_n = m1_n = false;
    retn_seen = button = false;
    instant_on = delayed_on = delayed_off = false;
    rom3_instant_on = rom3_delayed_on = false;
    nmi_instant_on = nmi_delayed_on = false;
    hold = held = button_nmi = false;
}

void SpecNext::Multiface::clock() {
    const bool port_io_dly = en_rd || en_wr || dis_rd || dis_wr;
    if (button_pulse())
        invisible = false;
    else if (((dis_wr && !mode_p3()) || (en_wr && mode_p3())) && !port_io_dly)
        invisible = true;
    if (button_pulse())
        nmi_active = true;
    else if (retn_seen || ((en_wr || dis_wr || (dis_rd && mode_p3())) && !port_io_dly))
        nmi_active = false;
    if (fetch_66() && !mreq_n)
        mf_enable = true;
    else if (dis_rd || retn_seen)
        mf_enable = false;
    else if (en_rd)
        mf_enable = !invisible_eff();
}

// ---------------------------------------------------------------------------

SpecNext::SpecNext() : cpu_(3500000) {
    for (auto& ay : ay_) ay = std::make_unique<AY8910>(1750000, 1.0f);
    for (int c = 0; c < 512; ++c) {
        auto x3 = [](int v) { return uint32_t((v << 5) | (v << 2) | (v >> 1)); };
        rgb_lut_[size_t(c)] = 0xff000000u | (x3((c >> 6) & 7) << 16) | (x3((c >> 3) & 7) << 8) |
                              x3(c & 7);
    }
}

SpecNext::~SpecNext() = default;

bool SpecNext::init(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    // The boot ROM: rom_path may be the tbblue zip, a directory holding it
    // (or holding the loose files), or the ROM file itself.
    std::vector<std::string> candidates;
    std::string dir = rom_path;
    if (fs::is_directory(rom_path)) {
        for (const char* z : {"tbblue.zip", "specnext.zip", "tbblue", "specnext"})
            candidates.push_back((fs::path(rom_path) / z).string());
        candidates.push_back(rom_path);
    } else {
        candidates.push_back(rom_path);
        dir = fs::path(rom_path).parent_path().string();
        if (dir.empty()) dir = ".";
    }
    rom_dir_ = dir;
    bool found = false;
    for (const std::string& c : candidates) {
        if (!fs::exists(c)) continue;
        if (fs::is_regular_file(c) && lower(fs::path(c).extension().string()) != ".zip") {
            if (read_file(c, boot_rom_) && boot_rom_.size() >= 0x2000) {
                found = true;
                break;
            }
            continue;
        }
        RomLoader loader;
        std::string err;
        if (loader.open(c, &err) && find_boot_rom(loader, boot_rom_)) {
            found = true;
            break;
        }
    }
    if (!found) {
        if (error)
            *error = "ZX Spectrum Next boot ROM (boot-30204.bin, tbblue.zip) not found in " +
                     rom_path;
        return false;
    }
    boot_rom_.resize(0x2000);

    sram_.assign(size_t(kRamPages) * 0x2000, 0);

    cpu_.set_memory_handlers([this](uint16_t a) { return mem_read(a); },
                             [this](uint16_t a, uint8_t v) { mem_write(a, v); });
    cpu_.set_opcode_read([this](uint16_t a) { return opcode_read(a); });
    cpu_.set_io_handlers([this](uint16_t p) { return io_in(p); },
                         [this](uint16_t p, uint8_t v) { io_out(p, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });
    cpu_.set_irq_ack_callback([this]() { irq_acknowledge(); });
    cpu_.set_return_callback([this](bool reti) {
        if (reti) {
            irq_reti();
        } else {
            leave_nmi();  // RETN
        }
    });
    cpu_.enable_z80n([this](uint8_t reg, uint8_t value) { reg_w(reg, value); },
                     [this](uint8_t reg) { return reg_r(reg); });

    dma_.mem_read = [this](uint16_t a) { return mem_read(a); };
    dma_.mem_write = [this](uint16_t a, uint8_t v) { mem_write(a, v); };
    dma_.io_read = [this](uint16_t p) { return io_in(p); };
    dma_.io_write = [this](uint16_t p, uint8_t v) { io_out(p, v); };

    // An SD card image next to the ROMs is inserted automatically.
    if (!sd_.inserted() && fs::is_directory(dir)) {
        std::vector<std::string> images;
        for (const auto& e : fs::directory_iterator(dir)) {
            if (e.is_regular_file() && is_sd_image(e.path().string()))
                images.push_back(e.path().string());
        }
        for (const char* sub : {"2gb", "sd", "tbblue"}) {
            const fs::path p = fs::path(dir) / sub;
            if (!fs::is_directory(p)) continue;
            for (const auto& e : fs::directory_iterator(p))
                if (e.is_regular_file() && is_sd_image(e.path().string()))
                    images.push_back(e.path().string());
        }
        std::sort(images.begin(), images.end(), [](const std::string& a, const std::string& b) {
            auto score = [](const std::string& s) {
                const std::string l = lower(fs::path(s).filename().string());
                return int(l.find("next") != std::string::npos) +
                       int(l.find("tbblue") != std::string::npos) +
                       int(l.find("cspect") != std::string::npos);
            };
            return score(a) > score(b);
        });
        if (!images.empty()) {
            std::string err;
            if (!sd_.open(images.front(), false, &err)) warnings_.push_back(err);
        }
    }
    if (!sd_.inserted())
        warnings_.push_back(
            "No SD card image: the Next boot ROM needs one with TBBLUE.FW and NextZXOS "
            "(--disk next.img)");

    hard_reset();
    return true;
}

bool SpecNext::insert_sd(const std::string& path, bool read_only, std::string* error) {
    if (!sd_.open(path, read_only, error)) return false;
    hard_reset();
    return true;
}

bool SpecNext::load_media(const std::string& path, std::string* error) {
    if (is_sd_image(path)) return insert_sd(path, false, error);
    const std::string ext = lower(std::filesystem::path(path).extension().string());
    if (ext == ".nex") {
        std::vector<uint8_t> data;
        if (!read_file(path, data) || data.size() < 512 || std::memcmp(data.data(), "Next", 4) != 0) {
            if (error) *error = "not a NEX file: " + path;
            return false;
        }
        if (!sd_.inserted()) return load_nex(data, error);
        pending_nex_ = std::move(data);  // loaded once NextZXOS has started
        return true;
    }
    if (error) *error = "unsupported Spectrum Next media: " + path;
    return false;
}

void SpecNext::reset() { hard_reset(); }

void SpecNext::hard_reset() {
    hard_reset_pending_ = true;
    machine_reset();
    cpu_.reset();
    os_frames_ = -1;
}

void SpecNext::soft_reset() {
    machine_reset();
    cpu_.reset();
}

// Values the FPGA sets on power up / hard reset only (reset_hard()).
void SpecNext::reset_hard_registers() {
    hard_reset_pending_ = false;
    bootrom_en_ = true;
    nr_80_ = 0;
    nr_8c_altrom_ = 0;
    nr_8f_mapping_mode_ = 0;
    nr_03_machine_timing_ = 3;
    nr_03_user_dt_lock_ = false;
    nr_03_config_mode_ = true;
    nr_03_machine_type_ = 3;
    nr_04_romram_bank_ = 0;
    nr_05_ = 0x40;  // joystick 0 = Kempston 1, joystick 1 = Sinclair 2, 50 Hz
    nr_06_ = 0xa0;  // hotkeys enabled
    nr_08_ = 0x10;  // internal speaker
    nr_09_ = 0;
    nr_0a_ = 0x11;  // DivMMC automap, mouse dpi 1
    nr_10_coreid_ = 0;
    mf_.mode = 0;
    nr_7f_ = 0xff;
    nr_82_ = nr_83_ = nr_84_ = 0xff;
    nr_85_ = 0x8f;
    nr_86_ = nr_87_ = nr_88_ = 0xff;
    nr_89_ = 0x8f;
    nr_02_reset_type_ = 4;
    eff_machine_timing_ = 3;
    eff_5060_ = false;
    nr_raw_.fill(0);

    // Palettes: ULA = Spectrum colours, the others RRRGGGBB = index.
    for (int p = 0; p < 8; ++p) {
        for (int i = 0; i < 256; ++i) {
            const bool ula = (p & 3) == 0;
            palette_[size_t(p)][size_t(i)] = rgb8_to_9(ula ? kUlaDefault[i & 15] : uint8_t(i));
        }
    }
    std::fill(sram_.begin(), sram_.end(), 0);
}

void SpecNext::machine_reset() {
    if (hard_reset_pending_) reset_hard_registers();

    for (int i = 0; i < 3; ++i) {
        ay_[size_t(i)]->reset();
        ay_[size_t(i)]->control(7);
        ay_[size_t(i)]->write(0xff);
    }
    dac_.fill(0x80);
    port_ff_w(0);
    port_fe_ = 0;
    divmmc_delayed_check_ = false;
    divmmc_.reset_lines();
    {
        const uint8_t mode = mf_.mode;
        mf_ = Multiface{};
        mf_.mode = mode;
    }
    nr_02_mf_nmi_ = nr_02_divmmc_nmi_ = false;
    cpu_.set_nmi_stackless(false);
    dma_.reset();
    int_pending_.fill(false);
    int_service_.fill(false);
    pulse_left_ = 0;
    irq_asserted_ = false;
    cpu_.set_irq(IrqLine::Clear);
    cpu_.set_nmi(IrqLine::Clear);
    nr_c2_ = nr_c3_ = 0;
    nr_80_ = uint8_t((nr_80_ << 4) | (nr_80_ & 0x0f));
    nr_8c_altrom_ = uint8_t((nr_8c_altrom_ << 4) | (nr_8c_altrom_ & 0x0f));
    port_e7_w(0xff);

    port_7ffd_ = 0;
    port_1ffd_ = 0;
    port_1ffd_special_old_ = false;
    port_dffd_ = 0;
    port_eff7_ = 0;
    layer2_en_ = layer2_map_wr_ = layer2_map_rd_ = layer2_map_shadow_ = false;
    layer2_map_segment_ = layer2_offset_ = 0;
    port_e3_ = 0;
    ulap_mode_ = ulap_index_ = 0;
    ulap_en_ = false;
    nr_register_ = 0x24;
    nr_07_w(0);
    nr_06_ |= 0xa0;
    nr_08_ &= uint8_t(~0x40);
    nr_09_ &= uint8_t(~0x10);
    nr_0b_ = 0x01;
    nr_12_layer2_bank_ = 8;
    nr_13_layer2_shadow_bank_ = 11;
    nr_14_global_transparent_ = 0xe3;
    nr_15_ = 0;
    nr_16_l2_scrollx_ = nr_17_l2_scrolly_ = 0;
    clip_l2_ = {0x00, 0xff, 0x00, 0xbf};
    clip_spr_ = {0x00, 0xff, 0x00, 0xbf};
    clip_ula_ = {0x00, 0xff, 0x00, 0xbf};
    clip_tm_ = {0x00, 0x9f, 0x00, 0xff};
    clip_idx_l2_ = clip_idx_spr_ = clip_idx_ula_ = clip_idx_tm_ = 0;
    nr_22_line_int_en_ = false;
    nr_23_line_int_ = 0;
    nr_26_ula_scrollx_ = nr_27_ula_scrolly_ = 0;
    nr_30_tm_scrollx_ = 0;
    nr_31_tm_scrolly_ = nr_32_lores_scrollx_ = nr_33_lores_scrolly_ = 0;
    nr_palette_idx_ = 0;
    nr_palette_sub_idx_ = false;
    nr_42_ulanext_format_ = 7;
    nr_43_ = 0;
    nr_stored_palette_value_ = 0;
    nr_4a_fallback_ = 0xe3;
    nr_4b_sprite_transparent_ = 0xe3;
    nr_4c_tm_transparent_ = 0x0f;
    copper_mode_ = 0;
    copper_pc_ = 0;
    copper_budget_ = 0;
    nr_copper_addr_ = 0;
    nr_copper_data_stored_ = 0;
    nr_64_copper_offset_ = 0;
    nr_68_ = 0;
    nr_6a_ = 0;
    nr_6b_ = 0;
    nr_6c_ = 0;
    nr_6e_ = 0x2c;
    nr_6f_ = 0x0c;
    nr_70_ = 0;
    nr_71_ = 0;
    if (nr_85_ & 0x80) {
        nr_82_ = nr_83_ = nr_84_ = 0xff;
        nr_85_ = uint8_t((nr_85_ & 0x80) | 0x0f);
    }
    if (!(nr_89_ & 0x80)) {
        nr_86_ = nr_87_ = nr_88_ = 0xff;
        nr_89_ = uint8_t((nr_89_ & 0x80) | 0x0f);
    }
    nr_b8_ = 0x83;
    nr_b9_ = 0x01;
    nr_ba_ = 0x00;
    nr_bb_ = 0xcd;
    nr_c0_ = 0;
    nr_c4_ = 0x80;
    nr_c6_ = 0;
    nr_cc_ = nr_cd_ = nr_ce_ = 0;
    if (nr_03_config_mode_) bootrom_en_ = true;

    // Sprites
    sprite_attr_.fill(0);
    sprite_attr_index_ = sprite_pattern_index_ = 0;
    sprite_mirror_q_ = 0;
    sprite_mirror_index_ = 7;
    sprite_mirror_inc_ = false;
    sprite_cache_valid_ = false;
    copper_ram_.fill(0);
    for (auto& ch : ctc_) ch = CtcChannel{};

    page_shadow_.fill(-1);
    mmu_x2_w(0, 0xff);
    mmu_x2_w(2, 0x0a);
    mmu_x2_w(4, 0x04);
    mmu_x2_w(6, 0x00);
    bank_update_range(0, 8);

    ay_select_ = 0;
    update_video_mode();
    frame_tick_ = 0;
    render_vc_ = 0;
    ula_int_done_ = false;
    copper_frame_pending_ = false;
}

// ---------------------------------------------------------------------------
// Memory

void SpecNext::mmu_w(int bank, uint8_t data) {
    mmu_[size_t(bank)] = data;
    bank_update(bank);
}

void SpecNext::mmu_x2_w(int bank, uint8_t data) {
    mmu_w(bank, data);
    mmu_w(bank | 1, data == 0xff ? 0xff : uint8_t(data | 1));
}

uint16_t SpecNext::port_7ffd_bank() const {
    const bool pent = mapping_pentagon();
    return uint16_t(((pent ? 0 : ((port_dffd_ >> 3) & 1)) << 6) |
                    ((!pent ? ((port_dffd_ >> 2) & 1) : (pentagon_1024_en() && (port_7ffd_ & 0x20))) << 5) |
                    ((pent ? ((port_7ffd_ >> 6) & 3) : (port_dffd_ & 3)) << 3) | (port_7ffd_ & 7));
}

bool SpecNext::port_7ffd_locked() const {
    return pentagon_1024_en() ? false : (port_7ffd_ & 0x20) != 0;
}

uint32_t SpecNext::internal_port_enable() const {
    uint32_t en = uint32_t(nr_82_) | (uint32_t(nr_83_) << 8) | (uint32_t(nr_84_) << 16) |
                  (uint32_t(nr_85_ & 0x0f) << 24);
    if (nr_80_ & 0x80)
        en &= uint32_t(nr_86_) | (uint32_t(nr_87_) << 8) | (uint32_t(nr_88_) << 16) |
              (uint32_t(nr_89_ & 0x0f) << 24);
    return en;
}

// Decodes one 8 KB slot: boot ROM, ROM/altrom/config mode, DivMMC ROM and
// RAM, Layer 2 mapping and the MMU pages, separately for reads and writes.
void SpecNext::bank_update(int bank) {
    const bool is_rom = (bank >> 1) == 0;
    if (bootrom_en_ && is_rom) {
        rd_[size_t(bank)] = boot_rom_.data();
        wr_[size_t(bank)] = nullptr;
        return;
    }

    const bool altrom_en = nr_8c_altrom_ & 0x80;
    const bool altrom_rw = nr_8c_altrom_ & 0x40;
    const bool lock_rom1 = nr_8c_altrom_ & 0x20;
    const bool lock_rom0 = nr_8c_altrom_ & 0x10;
    const int rom_1ffd = (((port_1ffd_ >> 2) & 1) << 1) | ((port_7ffd_ >> 4) & 1);
    int sram_rom;
    bool sram_rom3, alt_128_n;
    if (machine_type_48()) {
        sram_rom = 0;
        sram_rom3 = true;
        alt_128_n = !(!lock_rom1 && lock_rom0);
    } else if (machine_type_p3()) {
        if (lock_rom1 || lock_rom0) {
            sram_rom = (int(lock_rom1) << 1) | int(lock_rom0);
            sram_rom3 = lock_rom1 && lock_rom0;
            alt_128_n = lock_rom1;
        } else {
            sram_rom = rom_1ffd;
            sram_rom3 = rom_1ffd == 3;
            alt_128_n = rom_1ffd & 1;
        }
    } else {
        if (lock_rom1 || lock_rom0) {
            sram_rom = lock_rom1;
            sram_rom3 = lock_rom1;
            alt_128_n = lock_rom1;
        } else {
            sram_rom = rom_1ffd & 1;
            sram_rom3 = rom_1ffd & 1;
            alt_128_n = rom_1ffd & 1;
        }
    }

    const uint8_t page = mmu_[size_t(bank)];
    const bool mem_bank5 = page == 0x0a || page == 0x0b;
    const bool mem_bank7 = page == 0x0e;
    const int mmu_a = ((1 + (page >> 5)) << 5) | (page & 0x1f);

    const int l2_off_pre = layer2_map_segment_ == 3 ? ((bank >> 1) & 3) : layer2_map_segment_;
    const int l2_bank =
        (layer2_map_shadow_ ? nr_13_layer2_shadow_bank_ : nr_12_layer2_bank_) + l2_off_pre + layer2_offset_;
    const int l2_page = ((l2_bank << 1) | (bank & 1)) & 0xff;
    const int l2_a = ((1 + (l2_page >> 5)) << 5) | (l2_page & 0x1f);

    int pre_a;
    int pre_override;
    bool pre_active, pre_bank5, pre_bank7, pre_rdonly;
    if (is_rom) {
        const int a13 = bank & 1;
        mf_.enable = port_en(9);
        if (mf_.enabled()) {  // Multiface ROM (page 0x0A) / RAM (0x0B)
            pre_a = 0x0a | a13;
            pre_active = true;
            pre_bank5 = pre_bank7 = false;
            pre_rdonly = !a13;
            pre_override = 0;
        } else if (!(mmu_a & 0x100)) {
            pre_a = mmu_a & 0xff;
            pre_active = !mem_bank5 && !mem_bank7;
            pre_bank5 = mem_bank5;
            pre_bank7 = mem_bank7;
            pre_rdonly = false;
            pre_override = 6;
        } else if (nr_03_config_mode_) {
            pre_a = (nr_04_romram_bank_ << 1) | a13;
            pre_active = true;
            pre_bank5 = pre_bank7 = false;
            pre_rdonly = false;
            pre_override = 6;
        } else {
            pre_a = (sram_rom << 1) | a13;
            pre_active = true;
            pre_bank5 = pre_bank7 = false;
            pre_rdonly = !(altrom_en && altrom_rw);
            pre_override = 7;
        }
    } else {
        pre_a = mmu_a & 0xff;
        pre_active = !(mmu_a & 0x100) && !mem_bank5 && !mem_bank7;
        pre_bank5 = mem_bank5;
        pre_bank7 = mem_bank7;
        pre_rdonly = false;
        pre_override = (((bank & 6) != 6) && layer2_map_segment_ == 3) ? 2 : 0;
    }

    divmmc_.cpu_a_15_13 = bank;
    divmmc_.en = port_en(8);
    divmmc_.automap_reset = !port_en(8) || !(nr_0a_ & 0x10);
    divmmc_.automap_active = (pre_override & 4) != 0;

    int page_for[2] = {-1, -1};  // [0] = read, [1] = write
    for (int cpu_rd_n = 1; cpu_rd_n >= 0; --cpu_rd_n) {
        const bool l2_map_en = (pre_override & 2) &&
                               ((layer2_map_wr_ && cpu_rd_n) || (layer2_map_rd_ && !cpu_rd_n));
        const bool sram_altrom_en = !(!(pre_override & 1) || !altrom_en || (pre_rdonly && cpu_rd_n) ||
                                      (!pre_rdonly && !cpu_rd_n));
        divmmc_.automap_rom3_active = ((pre_override & 5) == 5) && !l2_map_en &&
                                      ((sram_altrom_en && alt_128_n) || (sram_rom3 && !sram_altrom_en));
        divmmc_.clock();

        int a;
        bool active, rdonly;
        if ((pre_override & 4) && divmmc_.rom_en()) {
            a = 8;
            active = true;
            rdonly = true;
        } else if ((pre_override & 4) && divmmc_.ram_en()) {
            a = 0x10 | divmmc_.ram_bank();
            active = true;
            rdonly = divmmc_.rdonly();
        } else if (l2_map_en) {
            a = l2_a & 0xff;
            active = !(l2_a & 0x100);
            rdonly = false;
        } else if (sram_altrom_en) {
            a = 0x0c | (int(alt_128_n) << 1) | (pre_a & 1);
            active = true;
            rdonly = pre_rdonly;
        } else {
            a = pre_a;
            active = pre_active || pre_bank5 || pre_bank7;
            rdonly = pre_rdonly;
        }
        if (cpu_rd_n)
            page_for[1] = (active && !rdonly && a < kRamPages) ? a : -1;
        else
            page_for[0] = (active && a < kRamPages) ? a : -1;
    }
    rd_[size_t(bank)] = page_for[0] >= 0 ? &sram_[size_t(page_for[0]) * 0x2000] : nullptr;
    wr_[size_t(bank)] = page_for[1] >= 0 ? &sram_[size_t(page_for[1]) * 0x2000] : nullptr;
}

void SpecNext::memory_change(uint16_t port, uint8_t data) {
    if (port_1ffd_ & 1) {  // +3 special (all RAM) paging
        uint8_t b3 = uint8_t((((port_1ffd_ >> 2) & 1) || ((port_1ffd_ >> 1) & 1)) << 3);
        mmu_x2_w(0, b3);
        const uint8_t b2 = uint8_t((((port_1ffd_ >> 2) & 1) && ((port_1ffd_ >> 1) & 1)) << 2);
        mmu_x2_w(2, uint8_t(b3 | b2 | 2));
        mmu_x2_w(4, uint8_t(b3 | 4));
        b3 = uint8_t((!((port_1ffd_ >> 2) & 1) && ((port_1ffd_ >> 1) & 1)) << 3);
        mmu_x2_w(6, uint8_t(b3 | 6));
    } else {
        mmu_x2_w(0, (port_eff7_ & 8) ? 0x00 : 0xff);
        if (port_1ffd_special_old_) mmu_x2_w(2, 0x0a);
        if (port_1ffd_special_old_) mmu_x2_w(4, 0x04);
        const bool ram_change = !(port == 0x8e && !(data & 8));
        if (port_1ffd_special_old_ || ram_change) mmu_x2_w(6, uint8_t(port_7ffd_bank() << 1));
    }
    port_1ffd_special_old_ = port_1ffd_ & 1;
}

uint8_t SpecNext::do_m1(uint16_t address) {
    const int bank = address >> 13;
    divmmc_.mreq_n = true;
    divmmc_.clock();
    divmmc_.mreq_n = false;
    divmmc_.m1_n = false;
    divmmc_.clock();
    if (bank < 2) bank_update(bank);
    const uint8_t data = mem_read(address);
    divmmc_.instant_on = divmmc_.delayed_on = divmmc_.delayed_off = false;
    divmmc_.rom3_instant_on = divmmc_.rom3_delayed_on = false;
    divmmc_.nmi_instant_on = divmmc_.nmi_delayed_on = false;
    divmmc_.m1_n = true;
    divmmc_.mreq_n = true;
    divmmc_.clock();
    bank_update_range(0, 2);
    divmmc_delayed_check_ = true;
    return data;
}

// Opcode fetches: the DivMMC watches the M1 cycles at its entry points.
uint8_t SpecNext::opcode_read(uint16_t a) {
    if (a <= 0x38 && (a & 7) == 0) {
        const int b = a >> 3;
        const bool ep = (nr_b8_ >> b) & 1, valid = (nr_b9_ >> b) & 1, timing = (nr_ba_ >> b) & 1;
        divmmc_.instant_on = ep && valid && timing;
        divmmc_.delayed_on = ep && valid && !timing;
        divmmc_.rom3_instant_on = ep && !valid && timing;
        divmmc_.rom3_delayed_on = ep && !valid && !timing;
        return do_m1(a);
    }
    if (a >= 0x1ff8 && a <= 0x1fff) {
        divmmc_.delayed_off = (nr_bb_ >> 6) & 1;
        return do_m1(a);
    }
    if (a >= 0x3d00 && a <= 0x3dff) {
        divmmc_.rom3_instant_on = (nr_bb_ >> 7) & 1;
        return do_m1(a);
    }
    switch (a) {
        case 0x04c6: divmmc_.rom3_delayed_on = (nr_bb_ >> 2) & 1; return do_m1(a);
        case 0x0562: divmmc_.rom3_delayed_on = (nr_bb_ >> 3) & 1; return do_m1(a);
        case 0x04d7: divmmc_.rom3_delayed_on = (nr_bb_ >> 4) & 1; return do_m1(a);
        case 0x056a: divmmc_.rom3_delayed_on = (nr_bb_ >> 5) & 1; return do_m1(a);
        case 0x0066: {
            mf_.m1_n = false;
            mf_.a_0066 = true;
            mf_.mreq_n = false;
            mf_.clock();
            divmmc_.nmi_instant_on = (nr_bb_ >> 1) & 1;
            divmmc_.nmi_delayed_on = nr_bb_ & 1;
            const uint8_t data = do_m1(a);
            mf_.a_0066 = false;
            mf_.m1_n = true;
            mf_.clock();
            return data;
        }
        default: break;
    }
    if (divmmc_delayed_check_) {
        do_m1(a);
        divmmc_delayed_check_ = false;
    }
    return mem_read(a);
}

// ---------------------------------------------------------------------------
// I/O ports

void SpecNext::port_ff_w(uint8_t data) { port_ff_ = data; }

void SpecNext::port_7ffd_w(uint8_t data) { port_7ffd_ = data; }

void SpecNext::port_e3_w(uint8_t data) {
    port_e3_ = uint8_t(data & ~0x30);
    divmmc_.reg = port_e3_;
    bank_update_range(0, 2);
}

void SpecNext::port_e7_w(uint8_t data) {
    const bool swap = nr_0a_ & 0x20;
    if ((data & 3) == 2)
        port_e7_ = uint8_t(0xfc | (int(!swap) << 1) | int(swap));
    else if ((data & 3) == 1)
        port_e7_ = uint8_t(0xfc | (int(swap) << 1) | int(!swap));
    else if (data == 0xfb || data == 0xf7)
        port_e7_ = data;
    else if (data == 0x7f && (nr_03_config_mode_ || (nr_02_reset_type_ & 4)))
        port_e7_ = 0x7f;
    else
        port_e7_ = 0xff;
    sd_.select(!(port_e7_ & 1));  // SD card 0 (the only one fitted)
}

void SpecNext::turbosound_address_w(uint8_t data) {
    const bool ts = nr_08_ & 0x02;
    if (ts && (data & 0x9c) == 0x9c)
        ay_select_ = (data & 0x02) ? ((data & 0x01) ? 0 : 1) : ((data & 0x01) ? 2 : 0);
    else if ((data & 0xe0) == 0)
        ay_[ts ? ay_select_ : 0]->control(data);
}

void SpecNext::dac_w(int mask, uint8_t value) {
    for (int i = 0; i < 4; ++i)
        if (mask & (1 << i)) dac_[size_t(i)] = value;
}

uint8_t SpecNext::kempston_read(int port_37) const {
    const uint8_t joy0 = uint8_t((((nr_05_ >> 3) & 1) << 2) | ((nr_05_ >> 6) & 3));
    const uint8_t joy1 = uint8_t((((nr_05_ >> 1) & 1) << 2) | ((nr_05_ >> 4) & 3));
    auto value = [&](uint8_t type, uint8_t joy) -> uint8_t {
        const bool md = port_37 ? type == 6 : type == 5;
        const bool kemp = port_37 ? (type == 4 || type == 6) : (type == 1 || type == 5);
        if (!kemp) return 0;
        return md ? joy : uint8_t(joy & 0x3f);
    };
    if (!port_en(port_37 ? 7 : 6)) return 0;
    return uint8_t(value(joy0, joy_left_) | value(joy1, joy_right_));
}

uint8_t SpecNext::ula_read(uint16_t port) {
    uint8_t keys = 0x1f;
    for (int row = 0; row < 8; ++row)
        if (!(port & (0x100 << row))) keys &= key_rows_[size_t(row)];
    const bool issue2 = nr_08_ & 1;
    const bool ear = issue2 ? (port_fe_ & 0x18) != 0 : (port_fe_ & 0x10) != 0;
    return uint8_t(0xa0 | keys | (ear ? 0x40 : 0));
}

uint8_t SpecNext::io_in(uint16_t port) {
    const uint8_t lo = uint8_t(port);
    if (!(port & 1)) return ula_read(port);
    switch (port & 0x0fff) {
        case 0x0adf: return 0xff;  // Kempston mouse buttons (none pressed)
        case 0x0bdf: return 0x00;  // mouse X
        case 0x0fdf: return 0x00;  // mouse Y
        default: break;
    }
    switch (lo) {
        case 0x37: return kempston_read(1);
        case 0x6b: return dma_.read();
        case 0x0b: return dma_.read();
        case 0xeb: {
            const uint8_t v = spi_miso_;
            spi_miso_ = port_en(11) ? sd_.exchange(0xff) : 0xff;
            return v;
        }
        case 0xe3: return port_en(8) ? uint8_t(port_e3_ & ~0x30) : 0x00;
        case 0x1f: case 0x3f: case 0x9f: case 0xbf: return mf_port_read(port, lo);
        case 0xff:
            return ((nr_08_ & 0x04) && port_en(0)) ? port_ff_ : 0xff;
        default: break;
    }
    switch (port) {
        case 0xff3b:
            if (!port_en(24)) return 0;
            if (ulap_mode_ == 0) {
                const uint16_t v = palette_[0 + ((nr_43_ & 0x40) ? 4 : 0)][size_t(0xc0 | ulap_index_)];
                // GGGRRRBB from the 9-bit RGB333
                return uint8_t(((v >> 3) & 7) << 5 | ((v >> 6) & 7) << 2 | ((v >> 1) & 3));
            }
            return ulap_en_ ? 1 : 0;
        case 0x303b: return 0x00;  // sprite status: no collision / overflow reported
        case 0x253b: return reg_r(nr_register_);
        case 0x243b: return nr_register_;
        case 0x123b:
            return uint8_t((layer2_map_segment_ << 6) | (int(layer2_map_shadow_) << 3) |
                           (int(layer2_map_rd_) << 2) | (int(layer2_en_) << 1) | int(layer2_map_wr_));
        case 0x133b: return 0x00;  // UART: nothing received, transmitter idle
        case 0x143b: case 0x153b: case 0x163b: return 0x00;
        case 0x103b: case 0x113b: return port_en(10) ? 0xff : 0x00;  // I2C lines idle high
        case 0xbff5: return (nr_08_ & 0x02) ? ay_select_ : 0;
        default: break;
    }
    if ((port & 0xf8ff) == 0x183b) {
        const int ch = (port >> 8) & 7;
        return (port_en(27) && ch < 4) ? ctc_read(ch) : 0x00;
    }
    if (lo == 0xfd) {
        if ((port & 0xc000) == 0xc000 && (port & 0xf000) != 0xd000)
            return ay_[(nr_08_ & 0x02) ? ay_select_ : 0]->read();
        return 0xff;
    }
    return 0xff;
}

void SpecNext::io_out(uint16_t port, uint8_t v) {
    const uint8_t lo = uint8_t(port);
    if (!(port & 1)) {
        port_fe_ = v;  // border, MIC, speaker
        return;
    }
    switch (lo) {
        case 0x6b: dma_.write(v, false); return;
        case 0x0b: dma_.write(v, true); return;
        case 0xeb:
            spi_miso_ = port_en(11) ? sd_.exchange(v) : 0xff;
            return;
        case 0xe3:
            if (port_en(8)) port_e3_w(uint8_t((port_e3_ & 0x40) | v));
            return;
        case 0xe7: port_e7_w(v); return;
        case 0x5b:
        case 0x57:
            if (port_en(14)) sprite_io_w(lo, v);
            return;
        case 0x1f:
            mf_port_write(lo);
            if ((nr_08_ & 0x08) && port_en(17)) dac_w(0xf, v);
            return;
        case 0x3f:
            mf_port_write(lo);
            if ((nr_08_ & 0x08) && port_en(19)) dac_w(0x9, v);
            return;
        case 0x9f: case 0xbf:
            mf_port_write(lo);
            return;
        case 0x0f: case 0xf3: if (nr_08_ & 0x08) dac_w(0x2, v); return;
        case 0x4f: case 0xf9: if (nr_08_ & 0x08) dac_w(0x4, v); return;
        case 0x5f: if (nr_08_ & 0x08) dac_w(0x8, v); return;
        case 0xf1: if (nr_08_ & 0x08) dac_w(0x1, v); return;
        case 0xb3: if ((nr_08_ & 0x08) && port_en(22)) dac_w(0x6, v); return;
        case 0xdf: if ((nr_08_ & 0x08) && port_en(23)) dac_w(0x9, v); return;
        case 0xfb: if ((nr_08_ & 0x08) && port_en(21)) dac_w(0x9, v); return;
        case 0xff:
            if (port_en(0)) port_ff_w(v);
            return;
        default: break;
    }
    switch (port) {
        case 0xff3b:
            if (!port_en(24)) return;
            if (ulap_mode_ == 0) {
                // ULA+ palette entry GGGRRRBB into the ULA palette at 0xC0+
                const uint16_t c9 = uint16_t(((v >> 2) & 7) << 6 | ((v >> 5) & 7) << 3 | ((v & 3) << 1) |
                                             ((v & 3) ? 1 : 0));
                palette_[(nr_43_ & 0x40) ? 4 : 0][size_t(0xc0 | ulap_index_)] = c9;
            } else if (ulap_mode_ == 1) {
                ulap_en_ = v & 1;
            }
            return;
        case 0xbf3b:
            if (port_en(24)) {
                ulap_mode_ = uint8_t((v >> 6) & 3);
                if (ulap_mode_ == 0) ulap_index_ = uint8_t(v & 0x3f);
            }
            return;
        case 0x303b:
            if (port_en(14)) sprite_io_w(0x303b, v);
            return;
        case 0x253b: reg_w(nr_register_, v); return;
        case 0x243b: nr_register_ = v; return;
        case 0x123b:
            if (!(v & 0x10)) {
                layer2_en_ = v & 0x02;
                layer2_map_wr_ = v & 0x01;
                layer2_map_rd_ = v & 0x04;
                layer2_map_shadow_ = v & 0x08;
                layer2_map_segment_ = uint8_t((v >> 6) & 3);
            } else {
                layer2_offset_ = uint8_t(v & 7);
            }
            bank_update_range(0, 6);
            return;
        case 0x133b: case 0x143b: case 0x153b: case 0x163b: return;  // UART
        case 0x103b: case 0x113b: return;                             // I2C
        default: break;
    }
    if ((port & 0xf8ff) == 0x183b) {
        const int ch = (port >> 8) & 7;
        if (port_en(27) && ch < 4) ctc_write(ch, v);
        return;
    }
    if ((port & 0xf0ff) == 0xe0f7) {
        if (port_en(26)) {
            port_eff7_ = v;
            memory_change(0xeff7, v);
        }
        return;
    }
    if (lo == 0xfd) {
        const int hi = port >> 12;
        if (port & 0x8000) {
            if (hi == 0xd) {  // $DFFD
                if (port_en(2)) {
                    if (!port_7ffd_locked()) {
                        port_dffd_ = v;
                        memory_change(0xdffd, v);
                    }
                } else {
                    turbosound_address_w(v);
                }
            } else if (port & 0x4000) {  // $FFFD
                if (port_en(16)) turbosound_address_w(v);
            } else {  // $BFFD
                if (port_en(16)) ay_[(nr_08_ & 0x02) ? ay_select_ : 0]->write(v);
            }
            return;
        }
        if (hi == 1) {  // $1FFD
            if (port_en(3) && !port_7ffd_locked()) {
                port_1ffd_ = v;
                memory_change(0x1ffd, v);
            }
            return;
        }
        if (hi == 2 || hi == 3) return;  // +3 FDC: not fitted
        const bool p3_timing = (nr_03_machine_timing_ & 3) == 3;
        if (port_en(1) && ((port & 0x4000) || !p3_timing) && !port_7ffd_locked()) {
            port_7ffd_w(v);
            memory_change(0x7ffd, v);
        }
        return;
    }
}

// Multiface paging ports: $3F/$BF (+3 mode), $BF/$3F (128), $9F/$1F (48).
static void mf_ports(uint8_t type, uint8_t& enable, uint8_t& disable) {
    enable = 0x3f;
    disable = 0xbf;
    if (type & 2) {
        enable = 0x9f;
        disable = 0x1f;
    } else if (type & 1) {
        enable = 0xbf;
        disable = 0x3f;
    }
}

uint8_t SpecNext::mf_port_read(uint16_t port, uint8_t lsb) {
    uint8_t en_port, dis_port;
    mf_ports(mf_.mode, en_port, dis_port);
    mf_.en_rd = port_en(9) && lsb == en_port;
    mf_.dis_rd = port_en(9) && lsb == dis_port;
    mf_.en_wr = mf_.dis_wr = false;
    mf_.clock();
    bank_update_range(0, 2);
    uint8_t data;
    if (!mf_.port_en()) {
        data = lsb == 0x1f ? kempston_read(0) : 0x00;
    } else if (mf_.mode != 0) {
        data = uint8_t(((port_7ffd_ >> 3) & 1) << 7 | 0x7f);
    } else {
        switch (port >> 12) {
            case 0x1: data = port_1ffd_; break;
            case 0x7: data = port_7ffd_; break;
            case 0xd: data = port_dffd_; break;
            case 0xe: data = uint8_t(port_eff7_ & 0xc0); break;
            default: data = uint8_t(port_fe_ & 7); break;
        }
    }
    mf_.en_rd = mf_.dis_rd = false;
    return data;
}

void SpecNext::mf_port_write(uint8_t lsb) {
    uint8_t en_port, dis_port;
    mf_ports(mf_.mode, en_port, dis_port);
    mf_.en_rd = mf_.dis_rd = false;
    mf_.en_wr = port_en(9) && lsb == en_port;
    mf_.dis_wr = port_en(9) && lsb == dis_port;
    mf_.clock();
    bank_update_range(0, 2);
}

// The Next's two NMI buttons (Multiface "M1" and DivMMC "drive") and the
// software NMIs of nextreg 02.
void SpecNext::nmi_request() {
    if (nr_03_config_mode_) return;
    const bool mf_nmi = (mf_button_ || nr_02_mf_nmi_) && (nr_06_ & 0x08);
    const bool div_nmi = (drive_button_ || nr_02_divmmc_nmi_) && (nr_06_ & 0x10);
    mf_.button = mf_nmi;
    mf_.clock();
    divmmc_.button = div_nmi;
    divmmc_.clock();
    if (mf_nmi || div_nmi) cpu_.set_nmi(IrqLine::Assert);
}

void SpecNext::leave_nmi() {
    mf_.retn_seen = true;
    mf_.clock();
    mf_.retn_seen = false;
    mf_.clock();
    divmmc_.retn_seen = true;
    divmmc_.clock();
    divmmc_.retn_seen = false;
    divmmc_.clock();
    nr_02_mf_nmi_ = nr_02_divmmc_nmi_ = false;
    bank_update_range(0, 2);
    cpu_.set_nmi(IrqLine::Clear);
}

// ---------------------------------------------------------------------------
// Next registers

// Palette being written: nr_43 bits 6-4 select ULA, Layer 2, sprites,
// tilemap (first palettes 0-3, second palettes 4-7), the order of palette_.
static int palette_select(uint8_t nr_43) { return (nr_43 >> 4) & 7; }

void SpecNext::palette_write(uint8_t priority, uint16_t value9) {
    const int p = palette_select(nr_43_);
    uint16_t v = uint16_t(value9 & 0x1ff);
    if ((p & 3) == 1 && (priority & 2)) v |= 0x8000;  // Layer 2 priority colour
    palette_[size_t(p)][nr_palette_idx_] = v;
}

uint16_t SpecNext::palette_read() const {
    return palette_[size_t(palette_select(nr_43_))][nr_palette_idx_];
}

int SpecNext::irq_pulse_cycles() const {
    const uint8_t t = eff_machine_timing_;
    return ((t & 4) || ((t & 2) && !(t & 1))) ? 36 : 32;
}

uint8_t SpecNext::reg_r(uint8_t reg) {
    switch (reg) {
        case 0x00: return kMachineId;
        case 0x01: return kCoreVersion;
        case 0x02:
            return uint8_t((int(nr_02_mf_nmi_) << 3) | (int(nr_02_divmmc_nmi_) << 2) | (nr_02_reset_type_ & 3));
        case 0x03:
            return uint8_t((int(nr_palette_sub_idx_) << 7) | (nr_03_machine_timing_ << 4) |
                           (int(nr_03_user_dt_lock_) << 3) | nr_03_machine_type_);
        case 0x04: return nr_04_romram_bank_;
        case 0x05: return uint8_t((nr_05_ & ~0x05) | (eff_5060_ ? 4 : 0));
        case 0x06: return nr_06_;
        case 0x07: return uint8_t((nr_07_cpu_speed_ << 4) | nr_07_cpu_speed_);
        case 0x08: return uint8_t((nr_08_ & 0x7f) | (port_7ffd_locked() ? 0 : 0x80));
        case 0x09: return nr_09_;
        case 0x0a: return nr_0a_;
        case 0x0b: return nr_0b_;
        case 0x0e: return kCoreSubVersion;
        case 0x0f: return kBoardIssue;
        case 0x10: return uint8_t(nr_10_coreid_ << 2);
        case 0x11: return nr_11_video_timing_;
        case 0x12: return nr_12_layer2_bank_;
        case 0x13: return nr_13_layer2_shadow_bank_;
        case 0x14: return nr_14_global_transparent_;
        case 0x15: return nr_15_;
        case 0x16: return nr_16_l2_scrollx_;
        case 0x17: return nr_17_l2_scrolly_;
        case 0x18: return clip_l2_[clip_idx_l2_];
        case 0x19: return clip_spr_[clip_idx_spr_];
        case 0x1a: return clip_ula_[clip_idx_ula_];
        case 0x1b: return clip_tm_[clip_idx_tm_];
        case 0x1c:
            return uint8_t((clip_idx_tm_ << 6) | (clip_idx_ula_ << 4) | (clip_idx_spr_ << 2) | clip_idx_l2_);
        case 0x1e:
        case 0x1f: {
            const int vc = std::min(frame_tick_ / std::max(1, line_ticks_), vt_.max_vc);
            const int cvc = vpos_to_cvc(vc);
            return reg == 0x1e ? uint8_t(cvc >> 8) : uint8_t(cvc);
        }
        case 0x20:
            return uint8_t((int(int_pending_[kIntLine]) << 7) | (int(int_pending_[kIntUla]) << 6) |
                           (int(int_pending_[kIntCtc0]) | (int(int_pending_[kIntCtc0 + 1]) << 1) |
                            (int(int_pending_[kIntCtc0 + 2]) << 2) | (int(int_pending_[kIntCtc0 + 3]) << 3)));
        case 0x22:
            return uint8_t((int(int_pending_[kIntUla] && !(nr_c0_ & 1)) << 7) |
                           (int((port_ff_ & 0x40) != 0) << 2) | (int(nr_22_line_int_en_) << 1) |
                           ((nr_23_line_int_ >> 8) & 1));
        case 0x23: return uint8_t(nr_23_line_int_);
        case 0x26: return nr_26_ula_scrollx_;
        case 0x27: return nr_27_ula_scrolly_;
        case 0x28: return nr_stored_palette_value_;
        case 0x2f: return uint8_t((nr_30_tm_scrollx_ >> 8) & 3);
        case 0x30: return uint8_t(nr_30_tm_scrollx_);
        case 0x31: return nr_31_tm_scrolly_;
        case 0x32: return nr_32_lores_scrollx_;
        case 0x33: return nr_33_lores_scrolly_;
        case 0x34: return uint8_t(sprite_mirror_q_ & 0x7f);
        case 0x40: return nr_palette_idx_;
        case 0x41: return uint8_t(palette_read() >> 1);
        case 0x42: return nr_42_ulanext_format_;
        case 0x43: return nr_43_;
        case 0x44: {
            const uint16_t v = palette_read();
            return uint8_t(((v & 0x8000) ? 0x80 : 0) | (v & 1));
        }
        case 0x4a: return nr_4a_fallback_;
        case 0x4b: return nr_4b_sprite_transparent_;
        case 0x4c: return nr_4c_tm_transparent_;
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            return mmu_[size_t(reg - 0x50)];
        case 0x61: return uint8_t(nr_copper_addr_);
        case 0x62: return uint8_t((copper_mode_ << 6) | ((nr_copper_addr_ >> 8) & 7));
        case 0x64: return nr_64_copper_offset_;
        case 0x68: return uint8_t((nr_68_ & ~0x08) | (ulap_en_ ? 0x08 : 0));
        case 0x69:
            return uint8_t((int(layer2_en_) << 7) | (((port_7ffd_ >> 3) & 1) << 6) | (port_ff_ & 0x3f));
        case 0x6a: return nr_6a_;
        case 0x6b: return nr_6b_;
        case 0x6c: return nr_6c_;
        case 0x6e: return nr_6e_;
        case 0x6f: return nr_6f_;
        case 0x70: return nr_70_;
        case 0x71: return nr_71_;
        case 0x7f: return nr_7f_;
        case 0x80: return nr_80_;
        case 0x82: return nr_82_;
        case 0x83: return nr_83_;
        case 0x84: return nr_84_;
        case 0x85: return nr_85_;
        case 0x86: return nr_86_;
        case 0x87: return nr_87_;
        case 0x88: return nr_88_;
        case 0x89: return nr_89_;
        case 0x8c: return nr_8c_altrom_;
        case 0x8e:
            return uint8_t(((port_dffd_ & 1) << 7) | ((port_7ffd_ & 7) << 4) | 0x08 | ((port_1ffd_ & 1) << 2) |
                           (((port_1ffd_ >> 2) & 1) << 1) |
                           int(((port_7ffd_ & 0x10) && !(port_1ffd_ & 1)) || ((port_1ffd_ & 2) && (port_1ffd_ & 1))));
        case 0x8f: return nr_8f_mapping_mode_;
        case 0xb0: return ext_keys_b0_;
        case 0xb1: return ext_keys_b1_;
        case 0xb8: return nr_b8_;
        case 0xb9: return nr_b9_;
        case 0xba: return nr_ba_;
        case 0xbb: return nr_bb_;
        case 0xc0: return uint8_t((nr_c0_ & 0xe9) | ((cpu_.im & 3) << 1));
        case 0xc2: return nr_c2_;
        case 0xc3: return nr_c3_;
        case 0xc4:
            return uint8_t((nr_c4_ & 0x80) | (int(nr_22_line_int_en_) << 1) | int(!(port_ff_ & 0x40)));
        case 0xc5: {
            uint8_t v = 0;
            for (int ch = 0; ch < 4; ++ch)
                if (ctc_[size_t(ch)].control & 0x80) v |= uint8_t(1 << ch);
            return v;
        }
        case 0xc6: return nr_c6_;
        case 0xc8: return uint8_t((int(int_pending_[kIntLine]) << 1) | int(int_pending_[kIntUla]));
        case 0xc9: {
            uint8_t v = 0;
            for (int ch = 0; ch < 4; ++ch)
                if (int_pending_[size_t(kIntCtc0 + ch)]) v |= uint8_t(1 << ch);
            return v;
        }
        case 0xca: return 0;
        case 0xcc: return nr_cc_;
        case 0xcd: return nr_cd_;
        case 0xce: return nr_ce_;
        default: return nr_raw_[reg];
    }
}

void SpecNext::nr_02_w(uint8_t v) {
    nr_02_mf_nmi_ = v & 0x08;
    nr_02_divmmc_nmi_ = v & 0x04;
    nmi_request();
    if (v & 0x02) {
        hard_reset_pending_ = true;
        machine_reset();
        cpu_.reset();
    } else if (v & 0x01) {
        nr_02_reset_type_ = uint8_t((((nr_02_reset_type_ >> 2) & 1) << 1) | ((nr_02_reset_type_ >> 1) & 1) |
                                    (nr_02_reset_type_ & 1));
        machine_reset();
        cpu_.reset();
    }
}

void SpecNext::nr_07_w(uint8_t v) {
    nr_07_cpu_speed_ = uint8_t(v & 3);
    dma_.ticks_per_t = ticks_per_t();
}

void SpecNext::reg_w(uint8_t reg, uint8_t v) {
    switch (reg) {
        case 0x02: nr_02_w(v); break;
        case 0x03:
            bootrom_en_ = false;
            if ((v & 0x80) && !nr_03_user_dt_lock_ && !(v & 0x08)) {
                static const uint8_t timing[8] = {1, 1, 2, 3, 4, 3, 3, 3};
                nr_03_machine_timing_ = timing[(v >> 4) & 7];
            }
            nr_03_user_dt_lock_ = nr_03_user_dt_lock_ ^ ((v >> 3) & 1);
            if (nr_03_config_mode_ && (v & 7) >= 1 && (v & 7) <= 4) nr_03_machine_type_ = uint8_t(v & 7);
            if ((v & 7) == 7)
                nr_03_config_mode_ = true;
            else if ((v & 7) != 0)
                nr_03_config_mode_ = false;
            bank_update_range(0, 8);
            break;
        case 0x04:
            nr_04_romram_bank_ = uint8_t(v & 0x7f);
            bank_update_range(0, 2);
            break;
        case 0x05: nr_05_ = v; break;
        case 0x06:
            nr_06_ = nr_03_config_mode_ ? v : uint8_t((v & ~0x04) | (nr_06_ & 0x04));
            break;
        case 0x07: nr_07_w(v); break;
        case 0x08:
            nr_08_ = uint8_t(v & 0x7f);
            if (v & 0x80) port_7ffd_ &= uint8_t(~0x20);
            break;
        case 0x09:
            nr_09_ = v;
            if (v & 0x08) port_e3_w(uint8_t(port_e3_ & ~0x40));
            break;
        case 0x0a:
            if (nr_03_config_mode_) {
                nr_0a_ = uint8_t((v & 0xc0) | ((v & 0x04) ? (v & 0x20) : (nr_0a_ & 0x20)) | (v & 0x1f));
                mf_.mode = uint8_t(v >> 6);
            } else
                nr_0a_ = uint8_t((nr_0a_ & 0xe0) | (v & 0x1f));
            bank_update_range(0, 2);
            break;
        case 0x0b: nr_0b_ = v; break;
        case 0x10:
            if (nr_03_config_mode_ && !(v & 0x10) && (v & 0x0f) != 0x0f) nr_10_coreid_ = uint8_t(v & 0x0f);
            if (v & 0x80) {  // flash boot: restart the FPGA (hard reset)
                hard_reset_pending_ = true;
                machine_reset();
                cpu_.reset();
            }
            break;
        case 0x11:
            if (nr_03_config_mode_) nr_11_video_timing_ = 0;  // VGA 0 only
            break;
        case 0x12:
            nr_12_layer2_bank_ = uint8_t(v & 0x7f);
            bank_update_range(0, 6);
            break;
        case 0x13:
            nr_13_layer2_shadow_bank_ = uint8_t(v & 0x7f);
            bank_update_range(0, 6);
            break;
        case 0x14: nr_14_global_transparent_ = v; break;
        case 0x15:
            nr_15_ = v;
            sprite_cache_valid_ = false;
            break;
        case 0x16: nr_16_l2_scrollx_ = v; break;
        case 0x17: nr_17_l2_scrolly_ = v; break;
        case 0x18: clip_l2_[clip_idx_l2_] = v; clip_idx_l2_ = uint8_t((clip_idx_l2_ + 1) & 3); break;
        case 0x19: clip_spr_[clip_idx_spr_] = v; clip_idx_spr_ = uint8_t((clip_idx_spr_ + 1) & 3); break;
        case 0x1a: clip_ula_[clip_idx_ula_] = v; clip_idx_ula_ = uint8_t((clip_idx_ula_ + 1) & 3); break;
        case 0x1b: clip_tm_[clip_idx_tm_] = v; clip_idx_tm_ = uint8_t((clip_idx_tm_ + 1) & 3); break;
        case 0x1c:
            if (v & 1) clip_idx_l2_ = 0;
            if (v & 2) clip_idx_spr_ = 0;
            if (v & 4) clip_idx_ula_ = 0;
            if (v & 8) clip_idx_tm_ = 0;
            break;
        case 0x22:
            nr_22_line_int_en_ = v & 0x02;
            nr_23_line_int_ = uint16_t((nr_23_line_int_ & 0xff) | ((v & 1) << 8));
            port_ff_w(uint8_t((port_ff_ & 0xbf) | ((v & 0x04) << 4)));
            break;
        case 0x23: nr_23_line_int_ = uint16_t((nr_23_line_int_ & 0x100) | v); break;
        case 0x26: nr_26_ula_scrollx_ = v; break;
        case 0x27: nr_27_ula_scrolly_ = v; break;
        case 0x2c: dac_w(0x2, v); break;
        case 0x2d: dac_w(0x9, v); break;
        case 0x2e: dac_w(0x4, v); break;
        case 0x2f: nr_30_tm_scrollx_ = uint16_t((nr_30_tm_scrollx_ & 0xff) | ((v & 3) << 8)); break;
        case 0x30: nr_30_tm_scrollx_ = uint16_t((nr_30_tm_scrollx_ & 0x300) | v); break;
        case 0x31: nr_31_tm_scrolly_ = v; break;
        case 0x32: nr_32_lores_scrollx_ = v; break;
        case 0x33: nr_33_lores_scrolly_ = v; break;
        case 0x34:
            sprite_mirror_w(v);
            break;
        case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
        case 0x75: case 0x76: case 0x77: case 0x78: case 0x79:
            sprite_mirror_inc_ = (reg & 0x40) != 0;
            sprite_mirror_index_ = uint8_t((reg & 0x3f) - 0x35);
            sprite_mirror_w(v);
            break;
        case 0x40:
            nr_palette_idx_ = v;
            nr_palette_sub_idx_ = false;
            break;
        case 0x41:
            palette_write(0, rgb8_to_9(v));
            if (!(nr_43_ & 0x80)) ++nr_palette_idx_;
            nr_palette_sub_idx_ = false;
            break;
        case 0x42: nr_42_ulanext_format_ = v; break;
        case 0x43:
            nr_43_ = v;
            nr_palette_sub_idx_ = false;
            break;
        case 0x44:
            if (!nr_palette_sub_idx_) {
                nr_stored_palette_value_ = v;
            } else {
                palette_write(uint8_t((v >> 6) & 3), uint16_t((nr_stored_palette_value_ << 1) | (v & 1)));
                if (!(nr_43_ & 0x80)) ++nr_palette_idx_;
            }
            nr_palette_sub_idx_ = !nr_palette_sub_idx_;
            break;
        case 0x4a: nr_4a_fallback_ = v; break;
        case 0x4b: nr_4b_sprite_transparent_ = v; break;
        case 0x4c: nr_4c_tm_transparent_ = uint8_t(v & 0x0f); break;
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            mmu_w(reg - 0x50, v);
            break;
        case 0x60:
        case 0x63: {
            const bool write8 = reg == 0x60;
            if (!(nr_copper_addr_ & 1)) {
                if (write8) copper_ram_[nr_copper_addr_] = v;
                nr_copper_data_stored_ = v;
            } else {
                if (!write8) copper_ram_[nr_copper_addr_ & 0x7fe] = nr_copper_data_stored_;
                copper_ram_[nr_copper_addr_] = v;
            }
            nr_copper_addr_ = uint16_t((nr_copper_addr_ + 1) & 0x7ff);
            break;
        }
        case 0x61: nr_copper_addr_ = uint16_t((nr_copper_addr_ & 0x700) | v); break;
        case 0x62:
            copper_mode_w(uint8_t((v >> 6) & 3));
            nr_copper_addr_ = uint16_t((nr_copper_addr_ & 0xff) | ((v & 7) << 8));
            break;
        case 0x64: nr_64_copper_offset_ = v; break;
        case 0x68:
            nr_68_ = v;
            ulap_en_ = v & 0x08;
            break;
        case 0x69:
            port_ff_w(uint8_t((port_ff_ & 0xc0) | (v & 0x3f)));
            port_7ffd_ = uint8_t((port_7ffd_ & ~0x08) | ((v >> 3) & 0x08));
            layer2_en_ = v & 0x80;
            break;
        case 0x6a: nr_6a_ = uint8_t(v & 0x3f); break;
        case 0x6b: nr_6b_ = v; break;
        case 0x6c: nr_6c_ = v; break;
        case 0x6e: nr_6e_ = uint8_t(v & 0xbf); break;
        case 0x6f: nr_6f_ = uint8_t(v & 0xbf); break;
        case 0x70: nr_70_ = uint8_t(v & 0x3f); break;
        case 0x71: nr_71_ = uint8_t(v & 1); break;
        case 0x7f: nr_7f_ = v; break;
        case 0x80: nr_80_ = v; break;
        case 0x82: nr_82_ = v; break;
        case 0x83: nr_83_ = v; bank_update_range(0, 2); break;
        case 0x84: nr_84_ = v; break;
        case 0x85: nr_85_ = uint8_t(v & 0x8f); break;
        case 0x86: nr_86_ = v; break;
        case 0x87: nr_87_ = v; break;
        case 0x88: nr_88_ = v; break;
        case 0x89: nr_89_ = uint8_t(v & 0x8f); break;
        case 0x8c:
            nr_8c_altrom_ = v;
            bank_update_range(0, 2);
            break;
        case 0x8e:
            if (v & 0x08) {
                port_dffd_ = uint8_t((port_dffd_ & ~0x08 & ~0x07) | ((v >> 7) & 1));
                port_7ffd_ = uint8_t((port_7ffd_ & ~0x07) | ((v >> 4) & 7));
            }
            if (!(v & 0x04)) port_7ffd_ = uint8_t((port_7ffd_ & ~0x10) | ((v & 1) << 4));
            port_1ffd_ = uint8_t((port_1ffd_ & ~0x07) | (((v >> 1) & 1) << 2) | ((v & 1) << 1) | ((v >> 2) & 1));
            memory_change(0x8e, v);
            bank_update_range(0, 2);
            break;
        case 0x8f:
            nr_8f_mapping_mode_ = uint8_t(v & 3);
            memory_change(0x8f, v);
            break;
        case 0xb8: nr_b8_ = v; break;
        case 0xb9: nr_b9_ = v; break;
        case 0xba: nr_ba_ = v; break;
        case 0xbb: nr_bb_ = v; break;
        case 0xc0:
            nr_c0_ = v;
            cpu_.set_nmi_stackless(v & 0x08);
            break;
        case 0xc2: nr_c2_ = v; break;
        case 0xc3: nr_c3_ = v; break;
        case 0xc4:
            nr_c4_ = v;
            nr_22_line_int_en_ = v & 0x02;
            port_ff_w(uint8_t((port_ff_ & 0xbf) | ((~v & 1) << 6)));
            break;
        case 0xc5:
            for (int ch = 0; ch < 4; ++ch) {
                auto& c = ctc_[size_t(ch)];
                c.control = uint8_t((v >> ch) & 1 ? (c.control | 0x80) : (c.control & ~0x80));
            }
            break;
        case 0xc6: nr_c6_ = v; break;
        case 0xc8:  // writing 1s clears pending interrupts
            if (v & 1) int_clear(kIntUla);
            if (v & 2) int_clear(kIntLine);
            break;
        case 0xc9:
            for (int ch = 0; ch < 4; ++ch)
                if (v & (1 << ch)) int_clear(kIntCtc0 + ch);
            break;
        case 0xcc: nr_cc_ = v; break;
        case 0xcd: nr_cd_ = v; break;
        case 0xce: nr_ce_ = v; break;
        default:
            nr_raw_[reg] = v;
            break;
    }
    if (reg != 0x34 && !(reg >= 0x35 && reg <= 0x39) && !(reg >= 0x75 && reg <= 0x79)) {
        sprite_mirror_index_ = 7;
        sprite_mirror_inc_ = false;
    }
}

// ---------------------------------------------------------------------------
// Sprites: port $303B/$57/$5B and the nextreg attribute mirror

void SpecNext::sprite_io_w(uint16_t port, uint8_t v) {
    bool attr_num_change = false;
    if (port == 0x5b) {
        sprite_pattern_[sprite_pattern_index_] = v;
        sprite_pattern_index_ = uint16_t((sprite_pattern_index_ + 1) & 0x3fff);
    } else if (port == 0x303b) {
        sprite_pattern_index_ = uint16_t(((v & 0x3f) << 8) | (v & 0x80));
        sprite_attr_index_ = uint16_t((v & 0x7f) << 3);
        attr_num_change = true;
    } else if (port == 0x57) {
        sprite_cache_valid_ = false;
        sprite_attr_[sprite_attr_index_] = v;
        const bool by8 = (sprite_attr_index_ & 4) || ((sprite_attr_index_ & 7) == 3 && !(v & 0x40));
        if (!by8) {
            sprite_attr_index_ = uint16_t((sprite_attr_index_ + 1) & 0x3ff);
        } else {
            sprite_attr_index_ = uint16_t((((sprite_attr_index_ >> 3) + 1) & 0x7f) << 3);
            attr_num_change = true;
        }
    }
    if (attr_num_change && (nr_09_ & 0x10)) {
        sprite_mirror_q_ = uint16_t((sprite_attr_index_ >> 3) | (sprite_pattern_index_ & 0x80));
    }
}

void SpecNext::sprite_mirror_w(uint8_t v) {
    if (sprite_mirror_index_ <= 4) {
        sprite_cache_valid_ = false;
        sprite_attr_[size_t(((sprite_mirror_q_ & 0x7f) << 3) | sprite_mirror_index_)] = v;
    }
    bool changed = false;
    if (sprite_mirror_index_ == 7) {
        sprite_mirror_q_ = v;
        changed = true;
    } else if (sprite_mirror_inc_) {
        sprite_mirror_q_ = uint16_t(((sprite_mirror_q_ + 1) & 0x7f) | (sprite_pattern_index_ & 0x80));
        changed = true;
    }
    if (changed && (nr_09_ & 0x10)) {
        sprite_pattern_index_ = uint16_t(((sprite_mirror_q_ & 0x3f) << 8) | (sprite_mirror_q_ & 0x80));
        sprite_attr_index_ = uint16_t((sprite_mirror_q_ & 0x7f) << 3);
    }
}

// ---------------------------------------------------------------------------
// Video timing and the per-instruction event loop

void SpecNext::update_video_mode() {
    const uint8_t t = nr_03_machine_timing_;
    const bool hz60 = (nr_05_ & 0x04) != 0;
    if (t & 4) {  // Pentagon (always 50 Hz)
        vt_ = {448 + 3 - 12, 128, 447, 319, 80, 319};
    } else if (t & 2) {  // 128K / +3
        const int int_h = (t & 1) ? 136 + 2 - 12 : 136 + 4 - 12;
        vt_ = hz60 ? VideoTimings{int_h, 136, 455, 0, 40, 263} : VideoTimings{int_h, 136, 455, 1, 64, 310};
    } else {  // 48K
        vt_ = hz60 ? VideoTimings{116, 128, 447, 0, 40, 263} : VideoTimings{116, 128, 447, 0, 64, 311};
    }
    eff_machine_timing_ = t;
    eff_5060_ = hz60 && !(t & 4);
    line_ticks_ = (vt_.max_hc + 1) * 4;
    frame_ticks_ = line_ticks_ * (vt_.max_vc + 1);
}

double SpecNext::frames_per_second() const {
    return frame_ticks_ > 0 ? double(kMasterClock) / double(frame_ticks_) : 50.0;
}

int SpecNext::vpos_to_cvc(int vpos) const {
    const int lines = vt_.max_vc + 1;
    return (vpos - vt_.min_vactive + nr_64_copper_offset_ + lines) % lines;
}

int SpecNext::cvc_to_vpos(int cvc) const {
    const int lines = vt_.max_vc + 1;
    return ((cvc + vt_.min_vactive - nr_64_copper_offset_) % lines + lines) % lines;
}

void SpecNext::on_cycles(int t_states) {
    if (pulse_left_ > 0) {
        pulse_left_ -= t_states;
        if (pulse_left_ <= 0 && !(nr_c0_ & 1)) {
            int_clear(kIntUla);
            int_clear(kIntLine);
            int_service_[kIntUla] = int_service_[kIntLine] = false;
        }
    }
    advance(t_states * ticks_per_t());
    // A running DMA owns the bus: the CPU waits until the burst is done.
    for (int guard = 0; dma_.active() && guard < 4096; ++guard) {
        dma_.ticks_per_t = ticks_per_t();
        const int used = dma_.run(line_ticks_);
        if (used <= 0) break;
        advance(used);
    }
    update_irq();
}

void SpecNext::advance(int ticks) {
    frame_tick_ += ticks;
    for (;;) {
        const int render_tick = render_vc_ * line_ticks_ + (vt_.min_hactive + 256) * 4;
        if (render_vc_ > vt_.max_vc || frame_tick_ < render_tick) break;
        line_event(render_vc_);
        ++render_vc_;
    }
    if (!ula_int_done_ && frame_tick_ >= vt_.int_v * line_ticks_ + vt_.int_h * 4) {
        ula_int_done_ = true;
        if (!(port_ff_ & 0x40)) {
            int_raise(kIntUla);
            if (!(nr_c0_ & 1)) pulse_left_ = irq_pulse_cycles();
        }
    }
    if (copper_frame_pending_ && frame_tick_ >= cvc_to_vpos(0) * line_ticks_ + vt_.int_h * 4) {
        copper_frame_pending_ = false;
        if (copper_mode_ == 3) {
            copper_pc_ = 0;
            copper_budget_ = 0;
        }
    }
    copper_run(ticks);
    ctc_tick(ticks);
    dma_.advance(ticks);
    mix_audio(ticks);
    if (frame_tick_ >= frame_ticks_) {
        frame_tick_ -= frame_ticks_;
        frame_done_ = true;
        frame_start();
    }
}

void SpecNext::line_event(int vc) {
    const int out_row = vc - (vt_.min_vactive - 32);
    if (out_row >= 0 && out_row < kHeight) render_line(out_row, vc);
    if (nr_22_line_int_en_ && nr_23_line_int_ <= vt_.max_vc) {
        const int target = cvc_to_vpos(nr_23_line_int_ ? nr_23_line_int_ - 1 : vt_.max_vc);
        if (vc == target) {
            int_raise(kIntLine);
            if (!(nr_c0_ & 1)) pulse_left_ = irq_pulse_cycles();
        }
    }
}

void SpecNext::frame_start() {
    ++frame_count_;
    if (++flash_counter_ >= 32) flash_counter_ = 0;
    if (nr_03_machine_timing_ != eff_machine_timing_ || ((nr_05_ & 4) != 0) != eff_5060_) {
        update_video_mode();
        frame_tick_ = std::min(frame_tick_, frame_ticks_ - 1);
    }
    render_vc_ = 0;
    ula_int_done_ = false;
    copper_frame_pending_ = true;
    check_os_started();
}

// ---------------------------------------------------------------------------
// Interrupt controller (hardware IM2 daisy chain: line, CTC 0-3, ULA)

uint8_t SpecNext::int_vector(int source) const {
    if (!(nr_c0_ & 1)) return 0xff;
    return uint8_t((nr_c0_ & 0xe0) | (source << 1));
}

void SpecNext::int_raise(int source) {
    if (int_service_[size_t(source)] && (nr_c0_ & 1)) return;  // being serviced
    int_pending_[size_t(source)] = true;
}

void SpecNext::int_clear(int source) { int_pending_[size_t(source)] = false; }

void SpecNext::update_irq() {
    int src = -1;
    for (int s = 0; s < kIntCount; ++s) {
        if (int_service_[size_t(s)] && (nr_c0_ & 1)) break;
        if (int_pending_[size_t(s)]) {
            src = s;
            break;
        }
    }
    const bool assert_line = src >= 0;
    if (assert_line != irq_asserted_ || assert_line) {
        irq_asserted_ = assert_line;
        cpu_.set_irq(assert_line ? IrqLine::Assert : IrqLine::Clear, assert_line ? int_vector(src) : 0xff);
    }
}

void SpecNext::irq_acknowledge() {
    for (int s = 0; s < kIntCount; ++s) {
        if (int_service_[size_t(s)] && (nr_c0_ & 1)) break;
        if (int_pending_[size_t(s)]) {
            int_pending_[size_t(s)] = false;
            if (nr_c0_ & 1) int_service_[size_t(s)] = true;
            cpu_.set_irq(IrqLine::Assert, int_vector(s));
            return;
        }
    }
}

void SpecNext::irq_reti() {
    for (int s = 0; s < kIntCount; ++s) {
        if (int_service_[size_t(s)]) {
            int_service_[size_t(s)] = false;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// CTC: four channels clocked at 28 MHz, ZC/TO of channel n triggers n+1

void SpecNext::ctc_write(int ch, uint8_t v) {
    auto& c = ctc_[size_t(ch)];
    if (c.waiting_tc) {
        c.tconst = v;
        c.waiting_tc = false;
        c.counter = v ? v : 256;
        c.prescale_acc = 0;
        if (!(c.control & 0x40) && (c.control & 0x08))
            c.waiting_trigger = true;  // timer started by a trigger edge
        else
            c.running = true;
        return;
    }
    if (v & 1) {  // control word
        c.control = v;
        if (v & 0x04) c.waiting_tc = true;
        if (v & 0x02) {
            c.running = false;
            if (!(v & 0x04)) c.waiting_tc = false;
        }
        if (!(v & 0x80)) int_clear(kIntCtc0 + ch);
    }
    // Vector words are ignored: the Next supplies the IM2 vectors (nr_C0).
}

uint8_t SpecNext::ctc_read(int ch) const { return uint8_t(ctc_[size_t(ch)].counter & 0xff); }

void SpecNext::ctc_count(int ch) {
    auto& c = ctc_[size_t(ch)];
    if (--c.counter == 0) {
        c.counter = c.tconst ? c.tconst : 256;
        if (c.control & 0x80) int_raise(kIntCtc0 + ch);
        if (ch < 3) {
            auto& n = ctc_[size_t(ch + 1)];
            if (n.running && (n.control & 0x40))
                ctc_count(ch + 1);
            else if (n.waiting_trigger) {
                n.waiting_trigger = false;
                n.running = true;
            }
        }
    }
}

void SpecNext::ctc_tick(int ticks) {
    for (int ch = 0; ch < 4; ++ch) {
        auto& c = ctc_[size_t(ch)];
        if (!c.running || (c.control & 0x40)) continue;  // counter mode counts triggers
        const int pre = (c.control & 0x20) ? 256 : 16;
        c.prescale_acc += ticks;
        while (c.prescale_acc >= pre) {
            c.prescale_acc -= pre;
            ctc_count(ch);
        }
    }
}

// ---------------------------------------------------------------------------
// Copper

void SpecNext::copper_mode_w(uint8_t mode) {
    if (mode == copper_mode_) return;
    copper_mode_ = mode;
    copper_budget_ = 0;
    if (mode == 1 || mode == 3) copper_pc_ = 0;
}

void SpecNext::copper_run(int ticks) {
    if (copper_mode_ == 0) return;
    copper_budget_ += ticks;
    const int vc = std::min(frame_tick_ / line_ticks_, vt_.max_vc);
    const int hc = (frame_tick_ % line_ticks_) / 4;
    for (int guard = 0; copper_budget_ >= 2 && guard < 1024; ++guard) {
        const uint16_t ins = uint16_t((copper_ram_[size_t(copper_pc_ << 1)] << 8) |
                                      copper_ram_[size_t((copper_pc_ << 1) | 1)]);
        if (ins & 0x8000) {  // WAIT line, h*8
            const int wv = ins & 0x1ff;
            const int wh = ((ins >> 9) & 0x3f) << 3;
            if (wv > vt_.max_vc || wh > vt_.max_hc) {
                copper_budget_ = 0;  // never satisfied
                return;
            }
            // Copper coordinates: line counted from the first paper line
            // (plus nr_64), horizontal from the left edge of the paper.
            int cur_h = hc - vt_.min_hactive;
            int cur_v = vpos_to_cvc(vc);
            if (cur_h < 0) {
                cur_h += vt_.max_hc + 1;
                cur_v = (cur_v + vt_.max_vc) % (vt_.max_vc + 1);
            }
            if (cur_v == wv && cur_h >= wh) {
                copper_pc_ = uint16_t((copper_pc_ + 1) & 0x3ff);
                copper_budget_ -= 1;
                continue;
            }
            copper_budget_ = 0;
            return;
        }
        copper_pc_ = uint16_t((copper_pc_ + 1) & 0x3ff);
        copper_budget_ -= 2;
        if (ins & 0x7f00) reg_w(uint8_t((ins >> 8) & 0x7f), uint8_t(ins));
        if (copper_pc_ == 0 && copper_mode_ != 2) {
            // End of the list: modes 01/11 stop here until restarted.
        }
    }
    if (copper_budget_ > 64) copper_budget_ = 64;
}

// ---------------------------------------------------------------------------
// Input

void SpecNext::set_inputs(const MachineInputs& inputs) {
    // F4: the Next's reset button (soft reset). F9 / F10: its NMI buttons,
    // Multiface (the NextZXOS NMI menu) and DivMMC ("drive").
    if (inputs.key(Key::F4) && !inputs_.key(Key::F4)) soft_reset();
    const bool mf = inputs.key(Key::F9), drive = inputs.key(Key::F10);
    if (mf != mf_button_ || drive != drive_button_) {
        mf_button_ = mf;
        drive_button_ = drive;
        nmi_request();
    }
    inputs_ = inputs;
}

void SpecNext::apply_inputs() {
    const MachineInputs& in = inputs_;
    key_rows_.fill(0x1f);
    auto press = [this](int row, int bit) { key_rows_[size_t(row)] &= uint8_t(~(1 << bit)); };
    static const Key kMatrix[8][5] = {
        {Key::LeftShift, Key::Z, Key::X, Key::C, Key::V},
        {Key::A, Key::S, Key::D, Key::F, Key::G},
        {Key::Q, Key::W, Key::E, Key::R, Key::T},
        {Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5},
        {Key::Num0, Key::Num9, Key::Num8, Key::Num7, Key::Num6},
        {Key::P, Key::O, Key::I, Key::U, Key::Y},
        {Key::Enter, Key::L, Key::K, Key::J, Key::H},
        {Key::Space, Key::LeftCtrl, Key::M, Key::N, Key::B},
    };
    for (int r = 0; r < 8; ++r)
        for (int b = 0; b < 5; ++b)
            if (in.key(kMatrix[r][b])) press(r, b);
    if (in.key(Key::RightShift)) press(0, 0);
    if (in.key(Key::RightCtrl)) press(7, 1);

    // PC keys that the Next keyboard maps onto Spectrum key combinations
    // (and reports in nr_B0/nr_B1 as extended keys).
    const bool cancel = nr_68_ & 0x10;
    ext_keys_b0_ = ext_keys_b1_ = 0;
    auto ext = [&](bool down, uint8_t& reg, int bit, int r1, int b1, int r2, int b2) {
        if (!down) return;
        reg |= uint8_t(1 << bit);
        if (cancel) return;
        press(r1, b1);
        if (r2 >= 0) press(r2, b2);
    };
    ext(in.key(Key::Semicolon), ext_keys_b0_, 7, 7, 1, 5, 1);  // ;  = SS+O
    ext(in.key(Key::Quote), ext_keys_b0_, 6, 7, 1, 5, 0);      // "  = SS+P
    ext(in.key(Key::Comma), ext_keys_b0_, 5, 7, 1, 7, 3);      // ,  = SS+N
    ext(in.key(Key::Period), ext_keys_b0_, 4, 7, 1, 7, 2);     // .  = SS+M
    ext(in.key(Key::Up), ext_keys_b0_, 3, 0, 0, 4, 3);         // CS+7
    ext(in.key(Key::Down), ext_keys_b0_, 2, 0, 0, 4, 4);       // CS+6
    ext(in.key(Key::Left), ext_keys_b0_, 1, 0, 0, 3, 4);       // CS+5
    ext(in.key(Key::Right), ext_keys_b0_, 0, 0, 0, 4, 2);      // CS+8
    ext(in.key(Key::Backspace), ext_keys_b1_, 7, 0, 0, 4, 0);  // DELETE = CS+0
    ext(in.key(Key::Backquote), ext_keys_b1_, 6, 0, 0, 3, 0);  // EDIT = CS+1
    ext(in.key(Key::Escape), ext_keys_b1_, 5, 0, 0, 7, 0);     // BREAK = CS+SPACE
    ext(in.key(Key::CapsLock), ext_keys_b1_, 1, 0, 0, 3, 1);   // CAPS LOCK = CS+2
    ext(in.key(Key::Tab), ext_keys_b1_, 0, 0, 0, 7, 1);        // EXTEND = CS+SS
    if (!cancel) {
        if (in.key(Key::Minus)) { press(7, 1); press(6, 3); }    // - = SS+J
        if (in.key(Key::Equals)) { press(7, 1); press(6, 1); }   // = = SS+L
        if (in.key(Key::Plus)) { press(7, 1); press(6, 2); }     // + = SS+K
        if (in.key(Key::Slash)) { press(7, 1); press(0, 4); }    // / = SS+V
        if (in.key(Key::Asterisk)) { press(7, 1); press(7, 4); } // * = SS+B
    }

    // Joysticks: bits right, left, down, up, fire(B), C, A, start.
    auto joy = [](const InputState& p) {
        return uint8_t((p.right ? 0x01 : 0) | (p.left ? 0x02 : 0) | (p.down ? 0x04 : 0) |
                       (p.up ? 0x08 : 0) | (p.button1 ? 0x10 : 0) | (p.button2 ? 0x20 : 0) |
                       (p.button3 ? 0x40 : 0) | (p.start ? 0x80 : 0));
    };
    joy_left_ = joy(in.player1);
    joy_right_ = joy(in.player2);
    // Sinclair / cursor joystick modes press keys instead.
    auto keyjoy = [&](uint8_t type, uint8_t j) {
        if (!j) return;
        switch (type) {
            case 0:  // Sinclair 2: 6 left, 7 right, 8 down, 9 up, 0 fire
                if (j & 0x02) press(4, 4);
                if (j & 0x01) press(4, 3);
                if (j & 0x04) press(4, 2);
                if (j & 0x08) press(4, 1);
                if (j & 0x10) press(4, 0);
                break;
            case 3:  // Sinclair 1: 1 left, 2 right, 3 down, 4 up, 5 fire
                if (j & 0x02) press(3, 0);
                if (j & 0x01) press(3, 1);
                if (j & 0x04) press(3, 2);
                if (j & 0x08) press(3, 3);
                if (j & 0x10) press(3, 4);
                break;
            case 2:  // Cursor: 5 left, 6 down, 7 up, 8 right, 0 fire
                if (j & 0x02) press(3, 4);
                if (j & 0x04) press(4, 4);
                if (j & 0x08) press(4, 3);
                if (j & 0x01) press(4, 2);
                if (j & 0x10) press(4, 0);
                break;
            default: break;
        }
    };
    keyjoy(uint8_t((((nr_05_ >> 3) & 1) << 2) | ((nr_05_ >> 6) & 3)), joy_left_);
    keyjoy(uint8_t((((nr_05_ >> 1) & 1) << 2) | ((nr_05_ >> 4) & 3)), joy_right_);
}

// ---------------------------------------------------------------------------
// Audio: 3 x AY + beeper + 4 DACs, mixed to mono at 44.1 kHz

void SpecNext::mix_audio(int ticks) {
    audio_acc_ += int64_t(ticks) * kSampleRate;
    while (audio_acc_ >= int64_t(kMasterClock)) {
        audio_acc_ -= int64_t(kMasterClock);
        // Three AYs (the second and third only with TurboSound enabled),
        // the beeper/MIC bits and the four DACs, then a DC-blocking filter.
        int32_t s = ay_[0]->update();
        if (nr_08_ & 0x02) s += ay_[1]->update() + ay_[2]->update();
        s = s * 3 / 4;
        s += ((port_fe_ & 0x10) ? 5000 : 0) + ((port_fe_ & 0x08) ? 800 : 0);
        if (nr_08_ & 0x08)
            for (uint8_t d : dac_) s += (int(d) - 0x80) * 24;
        const double x = double(s);
        dc_y_ = x - dc_x_ + 0.9975 * dc_y_;
        dc_x_ = x;
        audio_.push_back(int16_t(std::clamp(int32_t(dc_y_), int32_t(-32768), int32_t(32767))));
    }
}

void SpecNext::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Frame loop

void SpecNext::run_frame() {
    apply_inputs();
    frame_done_ = false;
    for (int guard = 0; !frame_done_ && guard < 1000000; ++guard) {
        const int remaining = (frame_ticks_ - frame_tick_) / std::max(1, ticks_per_t());
        cpu_.run(std::clamp(remaining, 1, 4096));
    }
}

void SpecNext::check_os_started() {
    if (bootrom_en_ || nr_03_config_mode_) return;
    ++os_frames_;
    if (!pending_nex_.empty() && os_frames_ >= 250 && !divmmc_.held && !(port_e3_ & 0x80)) {
        std::vector<uint8_t> nex;
        nex.swap(pending_nex_);
        std::string err;
        if (!load_nex(nex, &err)) warnings_.push_back(err);
    }
}

// ---------------------------------------------------------------------------
// NEX programs (https://wiki.specnext.dev/NEX_file_format)

bool SpecNext::load_nex(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 512 || std::memcmp(data.data(), "Next", 4) != 0) {
        if (error) *error = "not a NEX file";
        return false;
    }
    const uint8_t* h = data.data();
    size_t pos = 512;
    auto take = [&](size_t n) -> const uint8_t* {
        if (pos + n > data.size()) return nullptr;
        const uint8_t* p = &data[pos];
        pos += n;
        return p;
    };
    auto bank_ptr = [this](int bank) { return &sram_[0x40000 + size_t(bank) * 0x4000]; };

    // The machine as NEXLOAD leaves it: Next registers back to their reset
    // values (unless the file asks to keep them), ROM 3 (48K BASIC) paged in,
    // DivMMC paged out.
    const uint8_t screen = h[10];
    const uint8_t border = h[11];
    const uint16_t sp = uint16_t(h[12] | (h[13] << 8));
    const uint16_t pc = uint16_t(h[14] | (h[15] << 8));
    const uint8_t* banks = h + 18;
    const bool preserve = h[134] != 0;
    const uint8_t entry_bank = h[139];
    const uint16_t file_handle = uint16_t(h[140] | (h[141] << 8));
    const uint8_t screen2 = h[152];
    const bool has_copper = h[153] != 0;

    if (!preserve) {
        layer2_en_ = false;
        nr_15_ = 0;
        nr_16_l2_scrollx_ = nr_17_l2_scrolly_ = 0;
        nr_14_global_transparent_ = 0xe3;
        nr_4b_sprite_transparent_ = 0xe3;
        nr_4c_tm_transparent_ = 0x0f;
        clip_l2_ = {0x00, 0xff, 0x00, 0xbf};
        clip_spr_ = {0x00, 0xff, 0x00, 0xbf};
        clip_ula_ = {0x00, 0xff, 0x00, 0xbf};
        clip_tm_ = {0x00, 0x9f, 0x00, 0xff};
        nr_26_ula_scrollx_ = nr_27_ula_scrolly_ = 0;
        nr_30_tm_scrollx_ = 0;
        nr_31_tm_scrolly_ = nr_32_lores_scrollx_ = nr_33_lores_scrolly_ = 0;
        nr_42_ulanext_format_ = 7;
        nr_43_ = 0;
        nr_68_ = 0;
        nr_6a_ = 0;
        nr_6b_ = 0;
        nr_6c_ = 0;
        nr_6e_ = 0x2c;
        nr_6f_ = 0x0c;
        nr_70_ = 0;
        nr_71_ = 0;
        copper_mode_ = 0;
        ulap_en_ = false;
        nr_22_line_int_en_ = false;
        nr_c0_ = 0;
        for (auto& c : ctc_) c = CtcChannel{};
        for (int p = 0; p < 8; ++p)
            for (int i = 0; i < 256; ++i)
                palette_[size_t(p)][size_t(i)] = rgb8_to_9((p & 3) == 0 ? kUlaDefault[i & 15] : uint8_t(i));
        sprite_attr_.fill(0);
        sprite_cache_valid_ = false;
    }
    nr_12_layer2_bank_ = 9;
    dma_.reset();
    port_e3_w(0);
    divmmc_.reset_lines();
    layer2_map_wr_ = layer2_map_rd_ = layer2_map_shadow_ = false;
    layer2_map_segment_ = layer2_offset_ = 0;
    port_ff_ = 0;

    // Loading screens
    const bool has_palette = !(screen & 0x80) && (screen & 0x05);
    if (has_palette || (screen & 0x40 && (screen2 == 1 || screen2 == 2) && !(screen & 0x80))) {
        const uint8_t* pal = take(512);
        if (!pal) goto truncated;
        const int target = (screen & 0x04) ? 0 : 1;  // LoRes uses the ULA palette
        for (int i = 0; i < 256; ++i) {
            const uint16_t v = uint16_t((pal[i * 2] << 1) | (pal[i * 2 + 1] & 1));
            palette_[size_t(target)][size_t(i)] =
                uint16_t(v | ((target == 1 && (pal[i * 2 + 1] & 0x80)) ? 0x8000 : 0));
        }
    }
    if (screen & 0x01) {  // Layer 2 256x192
        const uint8_t* p = take(49152);
        if (!p) goto truncated;
        std::memcpy(bank_ptr(9), p, 49152);
        layer2_en_ = true;
    }
    if (screen & 0x02) {  // ULA
        const uint8_t* p = take(6912);
        if (!p) goto truncated;
        std::memcpy(bank_ptr(5), p, 6912);
    }
    if (screen & 0x04) {  // LoRes
        const uint8_t* p = take(12288);
        if (!p) goto truncated;
        std::memcpy(bank_ptr(5), p, 6144);
        std::memcpy(bank_ptr(5) + 0x2000, p + 6144, 6144);
        nr_15_ |= 0x80;
    }
    if (screen & 0x08) {  // Timex hi-res
        const uint8_t* p = take(12288);
        if (!p) goto truncated;
        std::memcpy(bank_ptr(5), p, 6144);
        std::memcpy(bank_ptr(5) + 0x2000, p + 6144, 6144);
        port_ff_ = uint8_t(0x06 | (h[138] & 0x38));
    }
    if (screen & 0x10) {  // Timex hi-colour
        const uint8_t* p = take(12288);
        if (!p) goto truncated;
        std::memcpy(bank_ptr(5), p, 6144);
        std::memcpy(bank_ptr(5) + 0x2000, p + 6144, 6144);
        port_ff_ = 0x02;
    }
    if ((screen & 0x40) && (screen2 == 1 || screen2 == 2)) {  // Layer 2 320x256 / 640x256
        const uint8_t* p = take(81920);
        if (!p) goto truncated;
        std::memcpy(bank_ptr(9), p, 81920);
        nr_70_ = uint8_t(screen2 == 1 ? 0x10 : 0x20);
        clip_l2_ = {0x00, 0x9f, 0x00, 0xff};
        layer2_en_ = true;
    }
    if (has_copper) {
        const uint8_t* p = take(2048);
        if (!p) goto truncated;
        std::memcpy(copper_ram_.data(), p, 2048);
        copper_pc_ = 0;
        copper_mode_ = 3;
    }

    // Memory banks: 5, 2, 0, 1, 3, 4, 6, 7, 8, ...
    {
        static const int order[6] = {5, 2, 0, 1, 3, 4};
        for (int i = 0; i < 112; ++i) {
            const int bank = i < 6 ? order[i] : i;
            if (!banks[bank]) continue;
            const uint8_t* p = take(0x4000);
            if (!p) goto truncated;
            if (0x40000 + size_t(bank + 1) * 0x4000 <= sram_.size()) std::memcpy(bank_ptr(bank), p, 0x4000);
        }
    }

    // Paging: ROM 3, banks 5/2 and the entry bank at $C000.
    port_7ffd_ = 0x10;
    port_1ffd_ = 0x04;
    port_dffd_ = 0;
    port_eff7_ = 0;
    port_1ffd_special_old_ = false;
    mmu_x2_w(0, 0xff);
    mmu_x2_w(2, 0x0a);
    mmu_x2_w(4, 0x04);
    mmu_x2_w(6, uint8_t(entry_bank * 2));
    bank_update_range(0, 8);

    port_fe_ = uint8_t(border & 7);
    if (file_handle >= 0x4000)
        mem_write(file_handle, 0xff);  // no esxDOS handle to pass on
    else if (file_handle != 0)
        cpu_.c = cpu_.b = 0xff;
    if (file_handle != 0)
        warnings_.push_back("NEX expects an open file handle: run it from the NextZXOS browser instead");

    int_pending_.fill(false);
    int_service_.fill(false);
    pulse_left_ = 0;
    cpu_.halted = false;
    cpu_.sp = sp;
    cpu_.im = 1;
    cpu_.iff1 = cpu_.iff2 = false;
    if (pc) cpu_.set_pc(pc);
    return true;

truncated:
    if (error) *error = "truncated NEX file";
    return false;
}

}  // namespace dsp
