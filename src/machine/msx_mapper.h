#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

enum class MsxMapperType {
    Plain,
    Ascii8,
    Ascii16,
    Konami,
    KonamiScc,
    Generic8,
    Generic16,
    RType,
    CrossBlaim,
    HarryFox,
    GameMaster2,
    HolyQuran,
    SuperLodeRunner,
    SuperPierrot,
    Zemina8,
    Zemina16,
    SuperGameWorld126,
    Koei8,
    Halnote,
    EseramAscii8,
    EseramAscii16,
    MegaRamDdx,
    Manbow2,
};

// Cartridge mapper shared by MSX1/MSX2.  Detection follows the same basic
// strategy used by openMSX: special signatures first, then scoring of Z80
// LD (nn),A instructions at known mapper-register addresses.
class MsxCartridgeMapper {
public:
    bool load(std::vector<uint8_t> data, const std::string& filename) {
        data_ = std::move(data);
        type_ = detect(filename);
        reset();
        return !data_.empty();
    }

    void reset() {
        bank_.fill(0);
        bank_[1] = 1;
        bank_[2] = 2;
        bank_[3] = 3;
        sram_.assign(sram_size(), 0xff);
        write_enable_ = false;
        hal_mode_ = 0;
    }

    MsxMapperType type() const { return type_; }
    bool empty() const { return data_.empty(); }
    size_t size() const { return data_.size(); }

    uint8_t read(uint16_t address) const {
        if (data_.empty()) return 0xff;
        switch (type_) {
            case MsxMapperType::Plain:
                return plain(address);
            case MsxMapperType::Ascii8:
            case MsxMapperType::EseramAscii8:
            case MsxMapperType::Koei8:
                return bank8(address, 0x4000, 0x6000, 0x8000, 0xa000);
            case MsxMapperType::Ascii16:
            case MsxMapperType::EseramAscii16:
            case MsxMapperType::RType:
            case MsxMapperType::HarryFox:
            case MsxMapperType::SuperPierrot:
            case MsxMapperType::Zemina16:
            case MsxMapperType::SuperGameWorld126:
                return bank16(address);
            case MsxMapperType::Konami:
            case MsxMapperType::KonamiScc:
            case MsxMapperType::Generic8:
            case MsxMapperType::Zemina8:
            case MsxMapperType::HolyQuran:
            case MsxMapperType::GameMaster2:
            case MsxMapperType::Halnote:
            case MsxMapperType::MegaRamDdx:
            case MsxMapperType::Manbow2:
                return bank8_special(address);
            case MsxMapperType::Generic16:
                return generic16(address);
            case MsxMapperType::CrossBlaim:
                return cross_blaim(address);
            case MsxMapperType::SuperLodeRunner:
                return super_lode_runner(address);
        }
        return 0xff;
    }

    void write(uint16_t address, uint8_t value) {
        if (data_.empty()) return;
        switch (type_) {
            case MsxMapperType::Ascii8:
            case MsxMapperType::EseramAscii8:
            case MsxMapperType::Koei8:
                ascii8_write(address, value);
                break;
            case MsxMapperType::Ascii16:
            case MsxMapperType::EseramAscii16:
                ascii16_write(address, value);
                break;
            case MsxMapperType::Konami:
            case MsxMapperType::KonamiScc:
                konami_write(address, value);
                break;
            case MsxMapperType::Generic8:
                generic8_write(address, value);
                break;
            case MsxMapperType::Generic16:
                generic16_write(address, value);
                break;
            case MsxMapperType::RType:
                if (address >= 0x6000 && address < 0x6800) bank_[0] = value;
                else if (address >= 0x7000 && address < 0x7800) bank_[1] = value;
                break;
            case MsxMapperType::HarryFox:
                if (address >= 0x6000 && address < 0x7000) bank_[0] = value;
                else if (address >= 0x7000 && address < 0x8000) bank_[1] = value;
                break;
            case MsxMapperType::SuperPierrot:
                if ((address & 0x1000) == 0 && address >= 0x4000) bank_[0] = value;
                else if ((address & 0x1000) != 0 && address >= 0x5000 && address < 0xc000) bank_[1] = value;
                break;
            case MsxMapperType::Zemina8:
                if (address >= 0x4000 && address < 0x6000) bank_[0] = value;
                else if (address >= 0x6000 && address < 0x8000) bank_[1] = value;
                else if (address >= 0x8000 && address < 0xa000) bank_[2] = value;
                else if (address >= 0xa000 && address < 0xc000) bank_[3] = value;
                break;
            case MsxMapperType::Zemina16:
            case MsxMapperType::SuperGameWorld126:
                if (address >= 0x4000 && address < 0x8000) bank_[0] = value;
                else if (address >= 0x8000 && address < 0xc000) bank_[1] = value;
                break;
            case MsxMapperType::HolyQuran:
                if (address >= 0x5000 && address < 0x5400) bank_[0] = value;
                else if (address >= 0x5400 && address < 0x5800) bank_[1] = value;
                else if (address >= 0x5800 && address < 0x5c00) bank_[2] = value;
                else if (address >= 0x5c00 && address < 0x6000) bank_[3] = value;
                break;
            case MsxMapperType::GameMaster2:
                if (address >= 0x6000 && address < 0x6800) bank_[1] = value & 0x0f;
                else if (address >= 0x8000 && address < 0x8800) bank_[2] = value & 0x0f;
                else if (address >= 0xa000 && address < 0xa800) bank_[3] = value & 0x0f;
                else if (address >= 0x7000 && address < 0x7800) write_enable_ = (value & 0x10) != 0;
                break;
            case MsxMapperType::CrossBlaim:
                if (address >= 0x4045 && address < 0x4046) bank_[1] = value;
                break;
            case MsxMapperType::SuperLodeRunner:
                if (address < 0x4000) bank_[0] = value;
                break;
            case MsxMapperType::Halnote:
                if (address == 0x4fff) { bank_[0] = value & 0x7f; write_enable_ = (value & 0x80) != 0; }
                else if (address == 0x6fff) bank_[1] = value & 0x7f;
                else if (address == 0x8fff) bank_[2] = value & 0x7f;
                else if (address == 0xafff) bank_[3] = value & 0x7f;
                break;
            case MsxMapperType::MegaRamDdx:
                if (address == 0x6000) { bank_[0] = value; write_enable_ = true; }
                else if (address == 0x7000) bank_[1] = value;
                break;
            case MsxMapperType::Manbow2:
                if (address >= 0x5000 && address < 0x5800) bank_[0] = value;
                else if (address >= 0x5800 && address < 0x6000) bank_[1] = value;
                else if (address >= 0x6000 && address < 0x6800) bank_[2] = value;
                else if (address >= 0x6800 && address < 0x7000) bank_[3] = value;
                break;
            case MsxMapperType::Plain:
                break;
        }
    }

    const char* name() const {
        switch (type_) {
            case MsxMapperType::Plain: return "Plain";
            case MsxMapperType::Ascii8: return "ASCII8";
            case MsxMapperType::Ascii16: return "ASCII16";
            case MsxMapperType::Konami: return "Konami";
            case MsxMapperType::KonamiScc: return "Konami SCC";
            case MsxMapperType::Generic8: return "Generic8";
            case MsxMapperType::Generic16: return "Generic16";
            case MsxMapperType::RType: return "R-Type";
            case MsxMapperType::CrossBlaim: return "Cross Blaim";
            case MsxMapperType::HarryFox: return "Harry Fox";
            case MsxMapperType::GameMaster2: return "Game Master 2";
            case MsxMapperType::HolyQuran: return "Holy Quran";
            case MsxMapperType::SuperLodeRunner: return "Super Lode Runner";
            case MsxMapperType::SuperPierrot: return "Super Pierrot";
            case MsxMapperType::Zemina8: return "Zemina8";
            case MsxMapperType::Zemina16: return "Zemina16";
            case MsxMapperType::SuperGameWorld126: return "Super Game World 126";
            case MsxMapperType::Koei8: return "Koei8";
            case MsxMapperType::Halnote: return "Halnote";
            case MsxMapperType::EseramAscii8: return "ESE-RAM ASCII8";
            case MsxMapperType::EseramAscii16: return "ESE-RAM ASCII16";
            case MsxMapperType::MegaRamDdx: return "MegaRAM DDX";
            case MsxMapperType::Manbow2: return "Manbow2";
        }
        return "Unknown";
    }

private:
    static uint16_t le16(const std::vector<uint8_t>& d, size_t p) {
        return p + 1 < d.size() ? uint16_t(d[p] | (uint16_t(d[p + 1]) << 8)) : 0;
    }

    MsxMapperType detect(const std::string& filename) const {
        std::string f = filename;
        for (char& c : f) c = char(std::tolower(static_cast<unsigned char>(c)));
        const auto has = [&](const char* s) { return f.find(s) != std::string::npos; };
        if (has("asc-08") || has("ascii8")) return MsxMapperType::Ascii8;
        if (has("asc-16") || has("ascii16")) return MsxMapperType::Ascii16;
        if (has("konscc") || has("konami-scc")) return MsxMapperType::KonamiScc;
        if (has("konami")) return MsxMapperType::Konami;
        if (has("generic8")) return MsxMapperType::Generic8;
        if (has("generic16")) return MsxMapperType::Generic16;
        if (has("rtype")) return MsxMapperType::RType;
        if (has("cross")) return MsxMapperType::CrossBlaim;
        if (has("harry")) return MsxMapperType::HarryFox;
        if (has("gamemaster")) return MsxMapperType::GameMaster2;
        if (has("quran")) return MsxMapperType::HolyQuran;
        if (has("loderunner")) return MsxMapperType::SuperLodeRunner;
        if (has("pierrot")) return MsxMapperType::SuperPierrot;
        if (has("zemina8")) return MsxMapperType::Zemina8;
        if (has("zemina16") || has("zemina126")) return MsxMapperType::Zemina16;
        if (has("halnote")) return MsxMapperType::Halnote;
        if (has("eseram8")) return MsxMapperType::EseramAscii8;
        if (has("eseram16")) return MsxMapperType::EseramAscii16;
        if (has("megaram") || has("ddx")) return MsxMapperType::MegaRamDdx;
        if (has("manbow")) return MsxMapperType::Manbow2;

        if (data_.size() <= 0x8000) return MsxMapperType::Plain;
        if (data_.size() == 0x60000 && data_.size() >= 0x28000 &&
            std::equal(data_.begin() + 0x28000, data_.begin() + 0x28007, "MANBOW2"))
            return MsxMapperType::Manbow2;

        unsigned ascii8 = 0, ascii16 = 0, konami = 0, scc = 0, generic16 = 0;
        for (size_t i = 0; i + 2 < data_.size(); ++i) {
            if (data_[i] != 0x32) continue;
            uint16_t a = le16(data_, i + 1);
            switch (a) {
                case 0x5000: case 0xb000: ++scc; break;
                case 0x4000: case 0x8000: case 0xa000: ++konami; ++generic16; break;
                case 0x6800: case 0x7800: ++ascii8; break;
                case 0x6000: ++ascii8; ++ascii16; ++konami; break;
                case 0x7000: ++ascii8; ++ascii16; ++scc; break;
                case 0x77ff: ++ascii16; break;
                default: break;
            }
        }
        if (scc > ascii8 && scc >= ascii16 && scc >= konami) return MsxMapperType::KonamiScc;
        if (ascii8 > ascii16 && ascii8 >= konami) return MsxMapperType::Ascii8;
        if (ascii16 > konami && ascii16 >= ascii8) return MsxMapperType::Ascii16;
        if (konami) return MsxMapperType::Konami;
        if (generic16) return MsxMapperType::Generic16;
        return data_.size() > 0x10000 ? MsxMapperType::Generic8 : MsxMapperType::Plain;
    }

    size_t bank8_size() const { return data_.size() / 0x2000; }
    size_t bank16_size() const { return data_.size() / 0x4000; }
    uint8_t rom8(size_t bank, uint16_t off) const {
        size_t count = std::max<size_t>(1, bank8_size());
        size_t p = (bank % count) * 0x2000 + (off & 0x1fff);
        return p < data_.size() ? data_[p] : 0xff;
    }
    uint8_t rom16(size_t bank, uint16_t off) const {
        size_t count = std::max<size_t>(1, bank16_size());
        size_t p = (bank % count) * 0x4000 + (off & 0x3fff);
        return p < data_.size() ? data_[p] : 0xff;
    }
    uint8_t plain(uint16_t a) const {
        if (data_.size() <= 0x4000) return a < data_.size() ? data_[a] : 0xff;
        size_t p = size_t(a - 0x4000);
        if (a < 0x4000) p = a;
        return p < data_.size() ? data_[p] : 0xff;
    }
    uint8_t bank8(uint16_t a, int a0, int a1, int a2, int a3) const {
        if (a < 0x4000 || a >= 0xc000) return 0xff;
        int slot = (a < 0x6000) ? 0 : (a < 0x8000) ? 1 : (a < 0xa000) ? 2 : 3;
        static_cast<void>(a0); static_cast<void>(a1); static_cast<void>(a2); static_cast<void>(a3);
        return rom8(bank_[slot], a);
    }
    uint8_t bank8_special(uint16_t a) const {
        if (type_ == MsxMapperType::HolyQuran) {
            int s = a < 0x6000 ? (a < 0x5400 ? 0 : 1) : (a < 0x8000 ? 1 : (a < 0xa000 ? 2 : 3));
            return rom8(bank_[s], a);
        }
        if (a < 0x4000 || a >= 0xc000) return 0xff;
        int s = (a - 0x4000) >> 13;
        return rom8(bank_[s], a);
    }
    uint8_t bank16(uint16_t a) const {
        if (a >= 0x4000 && a < 0x8000) return rom16(bank_[0], a);
        if (a >= 0x8000 && a < 0xc000) return rom16(bank_[1], a);
        return 0xff;
    }
    uint8_t generic16(uint16_t a) const {
        if (a >= 0x4000 && a < 0x8000) return rom16(bank_[0], a);
        if (a >= 0x8000 && a < 0xc000) return rom16(bank_[1], a);
        return 0xff;
    }
    uint8_t cross_blaim(uint16_t a) const {
        if (a >= 0x4000 && a < 0x8000) return rom16(0, a);
        if (a >= 0x8000 && a < 0xc000) return rom16(bank_[1], a);
        return 0xff;
    }
    uint8_t super_lode_runner(uint16_t a) const {
        if (a >= 0x8000 && a < 0xc000) return rom16(bank_[0], a);
        return 0xff;
    }
    size_t sram_size() const {
        if (type_ == MsxMapperType::GameMaster2) return 0x2000;
        if (type_ == MsxMapperType::Halnote) return 0x4000;
        if (type_ == MsxMapperType::EseramAscii8 || type_ == MsxMapperType::EseramAscii16) return 0x8000;
        return 0;
    }
    void ascii8_write(uint16_t a, uint8_t v) {
        if (type_ == MsxMapperType::EseramAscii8 && (a == 0x7ffe || a == 0x7fff)) { write_enable_ = (v & 0x10) != 0; return; }
        if (a >= 0x6000 && a < 0x6800) bank_[0] = v;
        else if (a >= 0x6800 && a < 0x7000) bank_[1] = v;
        else if (a >= 0x7000 && a < 0x7800) bank_[2] = v;
        else if (a >= 0x7800 && a < 0x8000) bank_[3] = v;
    }
    void ascii16_write(uint16_t a, uint8_t v) {
        if (type_ == MsxMapperType::EseramAscii16 && (a == 0x7ffe || a == 0x7fff)) { write_enable_ = (v & 0x10) != 0; return; }
        if (a >= 0x6000 && a < 0x6800) bank_[0] = v;
        else if (a >= 0x7000 && a < 0x7800) bank_[1] = v;
    }
    void konami_write(uint16_t a, uint8_t v) {
        if (a >= 0x5000 && a < 0x5800) bank_[0] = v;
        else if (a >= 0x7000 && a < 0x7800) bank_[1] = v;
        else if (a >= 0x9000 && a < 0x9800) bank_[2] = v;
        else if (a >= 0xb000 && a < 0xb800) bank_[3] = v;
    }

    std::vector<uint8_t> data_;
    std::vector<uint8_t> sram_;
    std::array<uint8_t, 4> bank_{};
    MsxMapperType type_ = MsxMapperType::Plain;
    bool write_enable_ = false;
    uint8_t hal_mode_ = 0;
};

} // namespace dsp
