#include "drivers/msx2.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {
const std::vector<RomEntry> kBiosRom = {{"MSX2.ROM|nms8250_basic-bios2.rom|msx2.rom|msx2_bios.rom|cbios_main_msx2.rom|cbios_main_msx2_eu.rom", 0x8000, 0x0000, 0x6cdaf3a5}};
const std::vector<RomEntry> kSubRom = {{"MSX2EXT.ROM|nms8250_msx2sub.rom|msx2ext.rom|msx2_ext.rom|cbios_sub.rom", 0x4000, 0x0000, 0x66237ecf}};

bool read_plain_or_zip_file(const std::string& path, std::vector<uint8_t>& data, size_t max_size, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        bool zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4;
        if (!zip) {
            probe.clear(); probe.seekg(0, std::ios::end); std::streamoff size = probe.tellg(); probe.seekg(0, std::ios::beg);
            if (size <= 0) { if (error) *error = "cannot read " + path; return false; }
            data.resize(size_t(size)); probe.read(reinterpret_cast<char*>(data.data()), size);
            if (data.size() > max_size) data.resize(max_size);
            return bool(probe);
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    data.reserve(max_size);
    if (!loader.load_first_file(data, error)) return false;
    if (data.size() > max_size) data.resize(max_size);
    return true;
}

bool ends_with_ci(const std::string& s, const char* ext) {
    size_t n = std::strlen(ext); return s.size() >= n && s.compare(s.size() - n, n, ext) == 0;
}
} // namespace

Msx2::Msx2() : z80_(kMainClock), vdp_([this](bool a) { on_vdp_interrupt(a); }), ay8910_(kMainClock / 2, 0.8f) {
    z80_.set_memory_handlers([this](uint16_t a) { return read_byte(a); }, [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return read_port(p); }, [this](uint16_t p, uint8_t v) { write_port(p, v); });
    z80_.set_cycle_handler([this](int c) { on_main_cycles(c); });
    ay8910_.set_port_handlers([this] { return ay_port_a_read(); }, [this] { return ay_port_b_read(); }, nullptr, [this](uint8_t v) { ay_port_b_write(v); });
    ppi_.set_port_handlers([this] { return port_a_read(); }, [this] { return port_b_read(); }, nullptr, [this](uint8_t v) { port_a_write(v); }, nullptr, [this](uint8_t v) { port_c_write(v); });
    fdc_.set_disk(&disk_); diskrom_.fill(0xff);
}

bool Msx2::init(const std::string& rom_path, std::string* error) {
    RomLoader loader; if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> bios(0x8000, 0), sub(0x4000, 0);
    if (!loader.load(kBiosRom, bios, error) || !loader.load(kSubRom, sub, error)) return false;
    std::copy(bios.begin(), bios.end(), bios_.begin()); std::copy(sub.begin(), sub.end(), subrom_.begin());
    diskrom_.fill(0xff); disk_rom_loaded_ = false;
    const char* disk_names[] = {"nms8250_disk.rom", "DISK.ROM", "disk.rom", "DISKROM.ROM", "cbios_disk.rom"};
    for (const char* n : disk_names) { std::vector<uint8_t> d; if (loader.try_read(n, d) && d.size() == 0x4000) { std::copy(d.begin(), d.end(), diskrom_.begin()); disk_rom_loaded_ = true; break; } }
    if (!disk_rom_loaded_) warnings_.emplace_back("MSX2 disk ROM not found; floppy support disabled");
    logorom_.fill(0xff); logo_rom_loaded_ = false;
    const char* logo_names[] = {"cbios_logo_msx2.rom", "cbios_logo.rom"};
    for (const char* n : logo_names) { std::vector<uint8_t> d; if (loader.try_read(n, d) && d.size() == 0x4000) { std::copy(d.begin(), d.end(), logorom_.begin()); logo_rom_loaded_ = true; break; } }
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end()); reset(); return true;
}

void Msx2::reset() {
    z80_.reset(); ay8910_.reset(); ppi_.reset(); vdp_.reset(); rtc_.reset(); fdc_.reset(); cartridge_.reset();
    keypad_.fill(0xff); joystick_ = {0x3f, 0x3f}; joy_select_ = 0; port_a_ = 0; port_c_ = 0x7f; last_irq_ = false;
    audio_accumulator_ = 0; audio_.clear(); mapper_ = {3,2,1,0}; subslot_[0]=subslot_[1]=subslot_[2]=subslot_[3]=0; cart_bank0_=0; cart_bank1_=1; fdc_control_=0;
    for (auto& bank : ram_) bank.fill(0);
}

int Msx2::sub_slot(int prim, int page) const { return slot_expanded(prim) ? ((subslot_[size_t(prim)] >> (page * 2)) & 3) : 0; }

uint8_t Msx2::read_slot(int prim, int sub, int page, uint16_t address) {
    uint16_t off = address & 0x3fff;
    if (prim == 0) { if (page <= 1) return bios_[size_t(page)*0x4000 + off]; if (page == 2 && logo_rom_loaded_) return logorom_[off]; return 0xff; }
    if (prim == 1) return cartridge_.read(address);
    if (prim == 3) {
        if (sub == 0) return page == 0 ? subrom_[off] : 0xff;
        if (sub == 1) return ram_[size_t(mapper_[size_t(page)] % kMapperSegments)][off];
        if (sub == 2) return page == 1 ? diskrom_[off] : 0xff;
    }
    return 0xff;
}

void Msx2::write_slot(int prim, int sub, int page, uint16_t address, uint8_t value) {
    if (prim == 1) { cartridge_.write(address, value); return; }
    if (prim == 3 && sub == 1) ram_[size_t(mapper_[size_t(page)] % kMapperSegments)][address & 0x3fff] = value;
}

bool Msx2::slot_is_disk(int page) const { return primary_slot(page) == 3 && sub_slot(3, page) == 2; }
uint8_t Msx2::fdc_read(uint16_t a) { a &= 0x3fff; if (a>=0x3ff8&&a<=0x3ffb) return fdc_.read_reg(a-0x3ff8); if(a==0x3ffc)return uint8_t(0xfe|(fdc_control_&1)); if(a==0x3ffd)return fdc_control_; if(a==0x3fff){uint8_t v=0x3f;if(!fdc_.intrq())v|=0x40;if(!fdc_.drq())v|=0x80;return v;} if(a>=0x3fb8&&a<=0x3fbb)return fdc_.read_reg(a-0x3fb8); if(a==0x3fbc){uint8_t v=0x3f;if(!fdc_.intrq())v|=0x40;if(!fdc_.drq())v|=0x80;return v;} return 0xff; }
void Msx2::fdc_write(uint16_t a,uint8_t v){a&=0x3fff;if(a>=0x3ff8&&a<=0x3ffb){fdc_.write_reg(a-0x3ff8,v);return;}if(a==0x3ffc){fdc_.set_side(v&1);fdc_control_=uint8_t((fdc_control_&0xfe)|(v&1));return;}if(a==0x3ffd){fdc_control_=v;fdc_.set_drive(v&3);fdc_.set_motor((v&0x80)!=0);return;}if(a>=0x3fb8&&a<=0x3fbb){fdc_.write_reg(a-0x3fb8,v);return;}if(a==0x3fbc){fdc_control_=v;fdc_.set_drive(v&3);fdc_.set_side((v>>2)&1);fdc_.set_motor((v&8)!=0);}}
bool Msx2::fdc_mapped(uint16_t address) const { uint16_t a=address&0x3fff; int p=address>>14; if(!slot_is_disk(p))return false; return (a>=0x3ff8&&a<=0x3fff)||(a>=0x3fb8&&a<=0x3fbc); }

uint8_t Msx2::read_byte(uint16_t address){if(address==0xffff&&primary_slot(3)==3)return uint8_t(~subslot_[3]);if(fdc_mapped(address))return fdc_read(address);int p=address>>14;int prim=primary_slot(p);return read_slot(prim,sub_slot(prim,p),p,address);}
void Msx2::write_byte(uint16_t address,uint8_t value){if(address==0xffff&&primary_slot(3)==3){subslot_[3]=value;return;}if(fdc_mapped(address)){fdc_write(address,value);return;}int p=address>>14;int prim=primary_slot(p);write_slot(prim,sub_slot(prim,p),p,address,value);}

uint8_t Msx2::read_port(uint16_t port){port&=0xff;switch(port){case 0x98:return vdp_.vram_read();case 0x99:return vdp_.status_read();case 0xa2:return ay8910_.read();case 0xa8:case 0xa9:case 0xaa:case 0xab:return ppi_.read(port&3);case 0xb5:return rtc_.read();case 0xfc:case 0xfd:case 0xfe:case 0xff:return mapper_[port&3];default:return 0xff;}}
void Msx2::write_port(uint16_t port,uint8_t value){port&=0xff;switch(port){case 0x98:vdp_.vram_write(value);break;case 0x99:vdp_.register_write(value);break;case 0x9a:vdp_.palette_write(value);break;case 0x9b:vdp_.indirect_write(value);break;case 0xa0:ay8910_.control(value);break;case 0xa1:ay8910_.write(value);break;case 0xa8:case 0xa9:case 0xaa:case 0xab:ppi_.write(port&3,value);break;case 0xb4:rtc_.set_address(value);break;case 0xb5:rtc_.write(value);break;case 0xfc:case 0xfd:case 0xfe:case 0xff:mapper_[port&3]=value;break;default:break;}}
void Msx2::on_vdp_interrupt(bool a){if(a&&!last_irq_)z80_.set_irq(IrqLine::Hold);else if(!a&&last_irq_)z80_.set_irq(IrqLine::Clear);last_irq_=a;}
uint8_t Msx2::ay_port_a_read(){return uint8_t(joystick_[joy_select_]|uint8_t(tape_.level()<<1));}
void Msx2::ay_port_b_write(uint8_t v){joy_select_=(v&0x40)>>6;port_b_ay_=v;}
uint8_t Msx2::port_b_read(){return teclado_<keypad_.size()?keypad_[teclado_]:0xff;}
void Msx2::port_a_write(uint8_t v){port_a_=v;}
void Msx2::port_c_write(uint8_t v){teclado_=v&0x0f;if(((port_c_^v)&0x10)&&tape_.is_loaded()){bool on=(v&0x10)==0;if(on&&!tape_.is_playing())tape_.play(false);if(!on&&tape_.is_playing())tape_.pause();}port_c_=v;}
void Msx2::on_main_cycles(int cycles){if(tape_.is_playing())tape_.advance(int(double(cycles)*3500000.0/double(kMainClock)));audio_accumulator_+=uint64_t(cycles)*uint64_t(AY8910::kSampleRate);while(audio_accumulator_>=kMainClock){audio_accumulator_-=kMainClock;int32_t s=ay8910_.update();if(port_c_&0x80)s+=3000;if(tape_.is_playing())s+=int32_t(tape_.level())*96;audio_.push_back(int16_t(std::clamp(s,-32768,32767)));}}
void Msx2::run_frame(){for(int line=0;line<kScanlines;++line){z80_.run(kCyclesPerLine);vdp_.refresh_line(line,kScanlines);}}

void Msx2::set_inputs(const MachineInputs& inputs){keypad_.fill(0xff);bool r=inputs.key(Key::RightShift);if(inputs.key(Key::Num0))keypad_[0]&=0xfe;if(inputs.key(Key::Num1)&&!r)keypad_[0]&=0xfd;if(inputs.key(Key::Num2)&&!r)keypad_[0]&=0xfb;if(inputs.key(Key::Num3)&&!r)keypad_[0]&=0xf7;if(inputs.key(Key::Num4)&&!r)keypad_[0]&=0xef;if(inputs.key(Key::Num5)&&!r)keypad_[0]&=0xdf;if(inputs.key(Key::Num6))keypad_[0]&=0xbf;if(inputs.key(Key::Num7))keypad_[0]&=0x7f;if(inputs.key(Key::Num8))keypad_[1]&=0xfe;if(inputs.key(Key::Num9))keypad_[1]&=0xfd;if(inputs.key(Key::Semicolon))keypad_[1]&=0xfb;if(inputs.key(Key::Comma))keypad_[1]&=0xef;if(inputs.key(Key::Period))keypad_[1]&=0xdf;if(inputs.key(Key::Slash))keypad_[1]&=0xbf;if(inputs.key(Key::Minus))keypad_[1]&=0x7f;if(inputs.key(Key::Quote))keypad_[2]&=0xfe;if(inputs.key(Key::A))keypad_[2]&=0xbf;if(inputs.key(Key::B))keypad_[2]&=0x7f;if(inputs.key(Key::C))keypad_[3]&=0xfe;if(inputs.key(Key::D))keypad_[3]&=0xfd;if(inputs.key(Key::E))keypad_[3]&=0xfb;if(inputs.key(Key::F))keypad_[3]&=0xf7;if(inputs.key(Key::G))keypad_[3]&=0xef;if(inputs.key(Key::H))keypad_[3]&=0xdf;if(inputs.key(Key::I))keypad_[3]&=0xbf;if(inputs.key(Key::J))keypad_[3]&=0x7f;if(inputs.key(Key::K))keypad_[4]&=0xfe;if(inputs.key(Key::L))keypad_[4]&=0xfd;if(inputs.key(Key::M))keypad_[4]&=0xfb;if(inputs.key(Key::N))keypad_[4]&=0xf7;if(inputs.key(Key::O))keypad_[4]&=0xef;if(inputs.key(Key::P))keypad_[4]&=0xdf;if(inputs.key(Key::Q))keypad_[4]&=0xbf;if(inputs.key(Key::R))keypad_[4]&=0x7f;if(inputs.key(Key::S))keypad_[5]&=0xfe;if(inputs.key(Key::T))keypad_[5]&=0xfd;if(inputs.key(Key::U))keypad_[5]&=0xfb;if(inputs.key(Key::V))keypad_[5]&=0xf7;if(inputs.key(Key::W))keypad_[5]&=0xef;if(inputs.key(Key::X))keypad_[5]&=0xdf;if(inputs.key(Key::Y))keypad_[5]&=0xbf;if(inputs.key(Key::Z))keypad_[5]&=0x7f;if(inputs.key(Key::LeftShift))keypad_[6]&=0xfe;if(inputs.key(Key::LeftCtrl))keypad_[6]&=0xfd;if(inputs.key(Key::CapsLock))keypad_[6]&=0xf7;if(inputs.key(Key::F1))keypad_[6]&=0xdf;if(inputs.key(Key::F2)||(inputs.key(Key::Num2)&&r))keypad_[6]&=0xbf;if(inputs.key(Key::F3)||(inputs.key(Key::Num3)&&r))keypad_[6]&=0x7f;if(inputs.key(Key::F4)||(inputs.key(Key::Num4)&&r))keypad_[7]&=0xfe;if(inputs.key(Key::F5)||(inputs.key(Key::Num5)&&r))keypad_[7]&=0xfd;if(inputs.key(Key::Escape))keypad_[7]&=0xfb;if(inputs.key(Key::Tab))keypad_[7]&=0xf7;if(inputs.key(Key::Backspace))keypad_[7]&=0xdf;if(inputs.key(Key::Enter))keypad_[7]&=0x7f;if(inputs.key(Key::Space))keypad_[8]&=0xfe;if(inputs.key(Key::Left))keypad_[8]&=0xef;if(inputs.key(Key::Up))keypad_[8]&=0xdf;if(inputs.key(Key::Down))keypad_[8]&=0xbf;if(inputs.key(Key::Right))keypad_[8]&=0x7f;joystick_={0x3f,0x3f};const InputState* ps[2]={&inputs.player1,&inputs.player2};for(int p=0;p<2;++p){const auto& q=*ps[p];uint8_t j=0x3f;if(q.up)j&=0xfe;if(q.down)j&=0xfd;if(q.left)j&=0xfb;if(q.right)j&=0xf7;if(q.button1)j&=0xef;if(q.button2)j&=0xdf;joystick_[size_t(p)]=j;}}
void Msx2::set_dip_switch(int,uint8_t){}
void Msx2::drain_audio(std::vector<int16_t>& out){out.insert(out.end(),audio_.begin(),audio_.end());audio_.clear();}
bool Msx2::load_media(const std::string& path,std::string* error){std::string l=path;for(char& c:l)c=char(std::tolower(static_cast<unsigned char>(c)));if(ends_with_ci(l,".dsk")||ends_with_ci(l,".edsk")){if(!disk_.load_file(path,error))return false;fdc_.set_disk(&disk_);return true;}if(ends_with_ci(l,".tzx")||ends_with_ci(l,".tsx")||ends_with_ci(l,".cas")||ends_with_ci(l,".wav"))return load_tape(path,error);return load_cartridge(path,error);}
bool Msx2::load_tape(const std::string& path,std::string* error){if(!tape_.load_file(path,error))return false;tape_.stop();return true;}
void Msx2::tape_toggle_play(){if(!tape_.is_loaded())return;if(tape_.is_playing())tape_.pause();else tape_.play(false);}
bool Msx2::load_cartridge(const std::string& path,std::string* error){std::vector<uint8_t> data;if(!read_plain_or_zip_file(path,data,kMaxCartridge,error))return false;if(data.empty()){if(error)*error="empty cartridge";return false;}if(!cartridge_.load(std::move(data),path)){if(error)*error="cannot initialize cartridge mapper";return false;}warnings_.emplace_back(std::string("MSX2 cartridge mapper: ")+cartridge_.name());return true;}

} // namespace dsp
