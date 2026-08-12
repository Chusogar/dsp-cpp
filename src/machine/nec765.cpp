#include "machine/nec765.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

constexpr uint8_t kBytesInCmd[32] = {
    1, 1, 9, 3, 2, 9, 9, 2,
    1, 9, 2, 1, 9, 6, 1, 3,
    1, 9, 1, 1, 1, 1, 1, 1,
    1, 9, 1, 1, 1, 1, 9, 1
};

uint16_t read_u16(const uint8_t* p) {
    return uint16_t(p[0] | (p[1] << 8));
}

bool magic_is(const uint8_t* p, const char* text, size_t length) {
    return std::memcmp(p, text, length) == 0;
}

uint32_t sector_nominal_size(uint8_t n) {
    if (n > 7) return 0x4000;
    return 1u << (uint32_t(n) + 7u);
}

}  // namespace

Nec765Fdc::Nec765Fdc() {
    reset();
}

bool Nec765Fdc::disk_inserted(int drive) const {
    return drive >= 0 && drive < kDriveCount && disks_[size_t(drive)].open;
}

void Nec765Fdc::eject_disk(int drive) {
    if (drive < 0 || drive >= kDriveCount) return;
    disks_[size_t(drive)] = Disk{};
}

bool Nec765Fdc::load_disk(int drive, const std::string& path, std::string* error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        if (error) *error = "cannot open " + path;
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());

    if (data.empty()) {
        if (error) *error = path + " is empty";
        return false;
    }

    return load_disk_from_memory(drive, data, error);
}

bool Nec765Fdc::load_disk_from_memory(int drive,
                                      const std::vector<uint8_t>& data,
                                      std::string* error) {
    if (drive < 0 || drive >= kDriveCount) {
        if (error) *error = "invalid drive number";
        return false;
    }

    Disk fresh;
    if (!parse_dsk(fresh, data, error)) return false;

    disks_[size_t(drive)] = std::move(fresh);
    return true;
}

bool Nec765Fdc::parse_dsk(Disk& disk,
                          const std::vector<uint8_t>& raw,
                          std::string* error) {
    if (raw.size() < 0x100) {
        if (error) *error = "file too small to be a .dsk image";
        return false;
    }

    const uint8_t* base = raw.data();
    const uint32_t file_length = uint32_t(raw.size());

    bool standard = false;
    if (magic_is(base, "MV - CPC", 8)) {
        standard = true;
    } else if (magic_is(base, "EXTENDED", 8)) {
        standard = false;
    } else {
        if (error) *error = "not a .dsk/.edsk image (bad signature)";
        return false;
    }

    const uint8_t header_tracks = base[0x30];
    const uint8_t header_sides = base[0x31];
    const uint16_t header_track_size = read_u16(base + 0x32);

    if (header_tracks == 0 || header_sides == 0 || header_sides > 2) {
        if (error) *error = "unsupported disk geometry";
        return false;
    }

    disk.tracks_count = header_tracks;
    disk.heads_count = header_sides;

    std::array<std::array<uint8_t, 132>, 2> track_size_table{};
    size_t map_index = 0;
    for (uint8_t t = 0; t < header_tracks; t++) {
        for (uint8_t s = 0; s < header_sides; s++) {
            if (0x34 + map_index < raw.size()) {
                track_size_table[s][t] = base[0x34 + map_index];
            }
            map_index++;
        }
    }

    bool has_multi = false;
    uint32_t offset = 0x100;

    for (uint8_t logical_track = 0; logical_track < header_tracks; logical_track++) {
        for (uint8_t logical_side = 0; logical_side < header_sides; logical_side++) {
            uint32_t full_track_size = 0;

            if (standard) {
                full_track_size = header_track_size;
            } else {
                const uint8_t entry = track_size_table[logical_side][logical_track];
                if (entry == 0) continue;
                full_track_size = uint32_t(entry) * 0x100u;
            }

            if (full_track_size < 0x100) continue;
            if (offset + 0x100 > file_length) break;

            const uint8_t* th = base + offset;
            if (!magic_is(th, "Track-Info", 10)) break;

            const uint8_t track_num = th[16];
            const uint8_t side_num = th[17];

            if (side_num >= 2 || track_num >= disk.tracks[0].size()) {
                if (error) *error = "track header out of supported geometry";
                return false;
            }

            TrackInfo& track = disk.tracks[side_num][track_num];

            track.track_number = track_num;
            track.side_number = side_num;
            track.data_rate = th[18];
            track.recording_mode = th[19];
            track.sector_size = th[20];
            track.number_sector =
                uint8_t(std::min<size_t>(th[21], track.sectors.size()));
            track.gap3 = th[22];
            track.filler = th[23];

            uint32_t position = 0;
            uint32_t sector_ptr = offset + 24;

            for (uint8_t i = 0; i < track.number_sector; i++) {
                if (sector_ptr + 8 > file_length) break;

                const uint8_t* sd = base + sector_ptr;
                sector_ptr += 8;

                SectorInfo& sector = track.sectors[i];

                sector.track = sd[0];
                sector.head = sd[1];
                sector.sector = sd[2];
                sector.sector_size = sd[3];
                sector.status1 = sd[4];
                sector.status2 = sd[5];

                const uint32_t nominal = sector_nominal_size(sd[3]);

                if (standard) {
                    sector.data_length = uint16_t(std::min<uint32_t>(nominal, 0xffffu));
                } else {
                    sector.data_length = read_u16(sd + 6);
                    if (sector.data_length == 0) {
                        sector.data_length = uint16_t(std::min<uint32_t>(nominal, 0xffffu));
                    }
                }

                sector.position = position;
                position += sector.data_length;

                if (nominal != 0 && sector.data_length > nominal) {
                    uint32_t multiplicity = sector.data_length / nominal;
                    if (multiplicity > 1) {
                        if (multiplicity > 4) multiplicity = 4;
                        sector.multi = true;
                        disk.multi_counter = 0;
                        disk.multi_max = uint8_t(multiplicity);
                        has_multi = true;
                    }
                }
            }

            offset += 0x100;

            const uint32_t data_size =
                full_track_size >= 0x100 ? full_track_size - 0x100u : 0;

            const uint32_t available = offset < file_length ? file_length - offset : 0;
            const uint32_t to_copy = std::min<uint32_t>(data_size, available);

            track.track_length = to_copy;
            track.data.assign(base + offset, base + offset + to_copy);

            offset += data_size;
        }
    }

    apply_protection_patches(disk, has_multi);

    disk.open = true;
    disk.write_protected = false;
    disk.track_actual = 0;
    disk.side_actual = 0;
    disk.sector_actual = 0;
    disk.sector_read_track = 0;

    if (disk.multi_max == 0) disk.multi_max = 1;
    if (disk.multi_counter >= disk.multi_max) disk.multi_counter = 0;

    return true;
}

void Nec765Fdc::apply_protection_patches(Disk& disk, bool) {
    TrackInfo& track0 = disk.tracks[0][0];

    if (track0.data.empty()) return;
    if (track0.sectors[0].data_length == 0 ||
        track0.sectors[0].data_length > track0.data.size()) {
        return;
    }

    const uint32_t crc = crc32_of(track0.data.data(), track0.sectors[0].data_length);

    switch (crc) {
        case 0x8c817e25:
        case 0x4b616c83:
            if (disk.tracks[0].size() > 40) {
                disk.tracks[0][40].sectors[6].sector_size = 2;
            }
            break;

        case 0x57a3276f:
            if (disk.tracks[0].size() > 39) {
                disk.tracks[0][39].sectors[10].sector_size = 0;
            }
            break;

        case 0xf05fe06e:
            if (disk.tracks[0].size() > 39) {
                disk.tracks[0][39].sectors[0].sector_size = 2;
            }
            break;

        default:
            break;
    }
}

void Nec765Fdc::reset() {
    floppy_motor_ = false;
    current_drive_ = 0;

    exec_cmd_phase_ = false;
    result_phase_ = false;
    status_register_ = 0x80;

    st0_ = 0;
    st1_ = 0;
    st2_ = 0;
    st3_ = 0;

    status_counter_ = 0;
    read_status_counter_ = 0;

    command_pointer_ = 0;
    result_pointer_ = 0;
    result_counter_ = 0;

    data_pointer_ = 0;
    data_length_ = 0;
    counter_ = 0;

    seek_track_flag_ = false;

    command_.fill(0);
    result_.fill(0);

    for (Disk& disk : disks_) {
        disk.track_actual = 0;
        disk.side_actual = 0;
        disk.sector_actual = 0;
        disk.sector_read_track = 0;

        if (disk.multi_max == 0) disk.multi_max = 1;
        if (disk.multi_counter >= disk.multi_max) disk.multi_counter = 0;
    }
}

void Nec765Fdc::write_motor(uint8_t value) {
    floppy_motor_ = value != 0;
}

void Nec765Fdc::get_result7() {
    Disk& disk = disks_[current_drive_];
    const TrackInfo& track = disk.tracks[disk.side_actual][disk.track_actual];
    const SectorInfo& sector = track.sectors[disk.sector_actual];

    result_[0] = st0_;
    result_[1] = st1_;
    result_[2] = st2_;
    result_[3] = sector.track;
    result_[4] = sector.head;
    result_[5] = sector.sector;
    result_[6] = sector.sector_size;

    result_pointer_ = 0;
    result_counter_ = 7;

    exec_cmd_phase_ = false;
    result_phase_ = true;
    status_register_ = 0xd0;

    st0_ = 0;
    st1_ = 0;
    st2_ = 0;
}

bool Nec765Fdc::find_sector() {
    Disk& disk = disks_[current_drive_];

    if (!disk.open || disk.side_actual >= 2 ||
        disk.track_actual >= disk.tracks[0].size()) {
        st1_ |= 0x04;
        st2_ |= 0x10;
        return false;
    }

    TrackInfo& track = disk.tracks[disk.side_actual][disk.track_actual];

    if (track.number_sector == 0) {
        st1_ |= 0x04;
        st2_ |= 0x10;
        return false;
    }

    if (disk.sector_actual >= track.number_sector) {
        disk.sector_actual = 0;
    }

    const uint8_t wanted_c = command_[2];
    const uint8_t wanted_h = command_[3];
    const uint8_t wanted_r = command_[4];
    const uint8_t wanted_n = command_[5];

    for (uint8_t tries = 0; tries < track.number_sector; tries++) {
        SectorInfo& sector = track.sectors[disk.sector_actual];

        if (sector.track == wanted_c &&
            sector.head == wanted_h &&
            sector.sector == wanted_r &&
            sector.sector_size == wanted_n) {
            if (command_[4] == command_[6]) {
                st1_ |= 0x80;
            }

            st1_ |= uint8_t(sector.status1 & 0x20);
            st2_ |= uint8_t(sector.status2 & 0x60);
            return true;
        }

        disk.sector_actual++;
        if (disk.sector_actual >= track.number_sector) {
            disk.sector_actual = 0;
        }
    }

    st1_ |= 0x04;
    st2_ |= 0x10;
    return false;
}

bool Nec765Fdc::should_skip_sector() const {
    const Disk& disk = disks_[current_drive_];
    const TrackInfo& track = disk.tracks[disk.side_actual][disk.track_actual];
    const SectorInfo& sector = track.sectors[disk.sector_actual];

    if ((command_[0] & 0x20) != 0) {
        if ((command_[0] & 0x1f) == 0x06) {
            if ((sector.status2 & 0x40) != 0) return true;
        } else if ((command_[0] & 0x1f) == 0x0c) {
            if ((sector.status2 & 0x40) == 0) return true;
        }
    }

    return false;
}

void Nec765Fdc::start_read_sector() {
    Disk& disk = disks_[current_drive_];

    if (!find_sector()) {
        st0_ |= 0x40;
        st1_ |= 0x04;
        get_result7();
        return;
    }

    if (should_skip_sector()) {
        if (command_[4] == command_[6]) {
            st1_ &= 0x7f;
            get_result7();
            return;
        }

        command_[4]++;
        start_read_sector();
        return;
    }

    TrackInfo& track = disk.tracks[disk.side_actual][disk.track_actual];
    SectorInfo& sector = track.sectors[disk.sector_actual];

    uint32_t nominal_length = 0;

    if (command_[5] == 0) {
        nominal_length = command_[8];
        if (nominal_length > 0x80) nominal_length = 0x80;
    } else {
        nominal_length = sector_nominal_size(command_[5]);
    }

    uint32_t real_length = sector.data_length;
    if (real_length == 0) real_length = nominal_length;

    if (sector.multi) {
        const uint32_t slice_length = sector_nominal_size(sector.sector_size);

        if (disk.multi_max == 0) disk.multi_max = 1;

        disk.multi_counter++;
        if (disk.multi_counter >= disk.multi_max) {
            disk.multi_counter = 0;
        }

        data_pointer_ = sector.position + uint32_t(disk.multi_counter) * slice_length;
        data_length_ = std::min<uint32_t>(nominal_length, slice_length);
    } else {
        data_pointer_ = sector.position;
        data_length_ = std::min<uint32_t>(nominal_length, real_length);
    }

    if (data_pointer_ >= track.data.size()) {
        data_length_ = 0;
    } else if (data_pointer_ + data_length_ > track.data.size()) {
        data_length_ = uint32_t(track.data.size()) - data_pointer_;
    }

    counter_ = 0;
    exec_cmd_phase_ = true;
    result_phase_ = false;
    status_register_ = 0xf0;
}

void Nec765Fdc::start_read_track() {
    Disk& disk = disks_[current_drive_];

    if (!disk.open || disk.side_actual >= 2 ||
        disk.track_actual >= disk.tracks[0].size()) {
        st0_ = 0x40;
        st1_ = 0x04;
        st2_ = 0;
        get_result7();
        return;
    }

    TrackInfo& track = disk.tracks[disk.side_actual][disk.track_actual];

    if (track.number_sector == 0 || track.data.empty()) {
        st0_ = 0x40;
        st1_ = 0x04;
        st2_ = 0;
        get_result7();
        return;
    }

    if (disk.sector_read_track >= track.number_sector) {
        disk.sector_read_track = 0;
    }

    SectorInfo& sector = track.sectors[disk.sector_read_track];

    data_pointer_ = sector.position;
    data_length_ = sector.data_length;

    if (data_length_ == 0) {
        data_length_ = sector_nominal_size(sector.sector_size);
    }

    if (data_pointer_ >= track.data.size()) {
        data_length_ = 0;
    } else if (data_pointer_ + data_length_ > track.data.size()) {
        data_length_ = uint32_t(track.data.size()) - data_pointer_;
    }

    counter_ = 0;
    exec_cmd_phase_ = true;
    result_phase_ = false;
    status_register_ = 0xf0;
}

void Nec765Fdc::select_drive() {
    current_drive_ = uint8_t(command_[1] & 1);

    Disk& disk = disks_[current_drive_];
    disk.side_actual = uint8_t((command_[1] & 4) >> 2);

    if (disk.heads_count != 0 && disk.side_actual >= disk.heads_count) {
        disk.side_actual = 0;
    }

    st0_ = uint8_t(st0_ & 0xf8);
    st3_ = uint8_t(st3_ & 0xf8);

    st0_ = uint8_t(st0_ | (command_[1] & 1) | (command_[1] & 4));
    st3_ = uint8_t(st3_ | (command_[1] & 1) | (command_[1] & 4));
}

bool Nec765Fdc::seek_track(uint8_t track) {
    Disk& disk = disks_[current_drive_];

    if (!disk.open || disk.tracks_count == 0) {
        disk.track_actual = 0;
        disk.sector_actual = 0;
        disk.sector_read_track = 0;
        return false;
    }

    const bool ok = track < disk.tracks_count;

    disk.track_actual = ok ? track : uint8_t(disk.tracks_count - 1);
    disk.sector_actual = 0;
    disk.sector_read_track = 0;

    return ok;
}

void Nec765Fdc::exec_write_command() {
    switch (command_[0] & 0x1f) {
        case 2: {
            st0_ = 0;
            st1_ = 0;
            st2_ = 0;

            select_drive();

            if (!disks_[current_drive_].open) {
                st0_ |= 0x48;
                get_result7();
            } else {
                disks_[current_drive_].sector_read_track = 0;
                start_read_track();
            }
            break;
        }

        case 3: {
            exec_cmd_phase_ = false;
            result_phase_ = false;
            status_register_ = 0x80;
            break;
        }

        case 4: {
            current_drive_ = uint8_t(command_[1] & 1);
            Disk& drive = disks_[current_drive_];

            st3_ = uint8_t((command_[1] & 1) | (command_[1] & 4));

            if (drive.open) st3_ |= 0x20;
            if (drive.write_protected) st3_ |= 0x40;
            if (drive.track_actual == 0) st3_ |= 0x10;
            if (drive.heads_count > 1) st3_ |= 0x08;

            result_[0] = st3_;
            result_pointer_ = 0;
            result_counter_ = 1;

            exec_cmd_phase_ = false;
            result_phase_ = true;
            status_register_ = 0xd0;
            break;
        }

        case 5:
        case 9: {
            st0_ = 0;
            st1_ = 0;
            st2_ = 0;

            select_drive();

            if (!disks_[current_drive_].open) {
                st0_ |= 0x48;
                get_result7();
                break;
            }

            st0_ |= 0x40;
            st1_ |= 0x02;
            get_result7();
            break;
        }

        case 6:
        case 12: {
            st0_ = 0;
            st1_ = 0;
            st2_ = 0;

            select_drive();

            if (!disks_[current_drive_].open) {
                st0_ |= 0x48;
                get_result7();
            } else {
                start_read_sector();
            }
            break;
        }

        case 7: {
            st0_ = 0x20;
            st1_ = 0;
            st2_ = 0;

            select_drive();

            if (!disks_[current_drive_].open) {
                st0_ |= 0x48;
            } else {
                seek_track(0);
            }

            seek_track_flag_ = true;
            exec_cmd_phase_ = false;
            result_phase_ = false;
            status_register_ = 0x80;
            break;
        }

        case 8: {
            result_pointer_ = 0;

            if (seek_track_flag_) {
                st0_ = uint8_t((st0_ & 0xf8) | 0x20 | (current_drive_ & 1));
                result_[0] = st0_;
                result_[1] = disks_[current_drive_].track_actual;
                result_counter_ = 2;
                seek_track_flag_ = false;
            } else {
                result_[0] = 0x80;
                result_counter_ = 1;
            }

            exec_cmd_phase_ = false;
            result_phase_ = true;
            status_register_ = 0xd0;
            break;
        }

        case 10: {
            st0_ = 0;
            st1_ = 0;
            st2_ = 0;

            select_drive();

            Disk& drive = disks_[current_drive_];

            if (!drive.open) {
                st0_ = 0x48;
                get_result7();
                break;
            }

            TrackInfo& track = drive.tracks[drive.side_actual][drive.track_actual];

            if (track.number_sector == 0) {
                st0_ = 0x40;
                st1_ = 0x01;

                result_[0] = st0_;
                result_[1] = st1_;
                result_[2] = st2_;
                result_[3] = drive.track_actual;
                result_[4] = drive.side_actual;
                result_[5] = 0;
                result_[6] = 0;

                result_pointer_ = 0;
                result_counter_ = 7;

                exec_cmd_phase_ = false;
                result_phase_ = true;
                status_register_ = 0xd0;
                break;
            }

            if (drive.sector_actual >= track.number_sector) {
                drive.sector_actual = 0;
            }

            get_result7();

            drive.sector_actual++;
            if (drive.sector_actual >= track.number_sector) {
                drive.sector_actual = 0;
            }

            break;
        }

        case 15: {
            select_drive();

            st0_ = 0x20;
            st1_ = 0;
            st2_ = 0;

            if (!disks_[current_drive_].open) {
                st0_ |= 0x48;
            } else {
                seek_track(command_[2]);
            }

            seek_track_flag_ = true;
            exec_cmd_phase_ = false;
            result_phase_ = false;
            status_register_ = 0x80;
            break;
        }

        default: {
            result_pointer_ = 0;
            result_counter_ = 1;
            result_[0] = 0x80;

            exec_cmd_phase_ = false;
            result_phase_ = true;
            seek_track_flag_ = false;
            status_register_ = 0xd0;
            break;
        }
    }
}

bool Nec765Fdc::read_data_should_stop() {
    Disk& disk = disks_[current_drive_];
    TrackInfo& track = disk.tracks[disk.side_actual][disk.track_actual];
    SectorInfo& sector = track.sectors[disk.sector_actual];

    bool stop = false;

    if ((command_[0] & 0x20) == 0) {
        if ((command_[0] & 0x1f) == 0x06) {
            if ((sector.status2 & 0x40) != 0) {
                st2_ |= 0x40;
                stop = true;
            }
        } else if ((command_[0] & 0x1f) == 0x0c) {
            if ((sector.status2 & 0x40) == 0) {
                st2_ |= 0x40;
                stop = true;
            }
        }
    }

    if ((sector.status1 & 0x20) != 0) {
        st1_ |= 0x20;
        stop = true;
    }

    return stop;
}

uint8_t Nec765Fdc::exec_read_command() {
    Disk& disk = disks_[current_drive_];

    switch (command_[0] & 0x1f) {
        case 2: {
            TrackInfo& track = disk.tracks[disk.side_actual][disk.track_actual];

            if (track.data.empty() || data_length_ == 0) {
                st0_ = 0x40;
                st1_ = 0x20;
                st2_ = 0x01;
                get_result7();
                return 0;
            }

            const uint8_t value =
                data_pointer_ < track.data.size() ? track.data[data_pointer_] : 0xff;

            data_pointer_++;
            counter_++;

            if (counter_ >= data_length_) {
                if (disk.sector_read_track >= uint8_t(command_[6] - 1) ||
                    disk.sector_read_track + 1 >= track.number_sector) {
                    st1_ |= 0x80;
                    get_result7();
                } else {
                    disk.sector_read_track++;
                    start_read_track();
                }
            }

            return value;
        }

        case 6:
        case 12: {
            TrackInfo& track = disk.tracks[disk.side_actual][disk.track_actual];

            if (track.data.empty() || data_length_ == 0) {
                st0_ = 0x40;
                st1_ = 0x20;
                st2_ = 0x01;
                get_result7();
                return 0;
            }

            SectorInfo& sector = track.sectors[disk.sector_actual];

            const uint8_t value =
                data_pointer_ < track.data.size() ? track.data[data_pointer_] : 0xff;

            data_pointer_++;
            counter_++;

            if (counter_ >= data_length_) {
                if (command_[4] == command_[6] || read_data_should_stop()) {
                    st0_ |= 0x40;

                    if (sector.sector_size > 5) {
                        st1_ |= 0x20;
                    }

                    get_result7();
                } else {
                    command_[4]++;

                    disk.sector_actual++;
                    if (disk.sector_actual >= track.number_sector) {
                        disk.sector_actual = 0;
                    }

                    start_read_sector();
                }
            }

            return value;
        }

        default:
            return 0xff;
    }
}

uint8_t Nec765Fdc::get_result() {
    const uint8_t value = result_[result_pointer_];

    result_pointer_++;

    if (result_pointer_ >= result_counter_) {
        status_register_ = 0x80;
        result_phase_ = false;
        result_pointer_ = 0;
        result_counter_ = 0;
        result_.fill(0);
        command_.fill(0);
    }

    return value;
}

void Nec765Fdc::write_data(uint8_t value) {
    status_counter_ = 0;

    if (result_phase_) {
        return;
    }

    if (!exec_cmd_phase_) {
        if (command_pointer_ == 0) {
            command_[0] = value;
            command_pointer_ = 1;
            status_register_ = 0x90;
        } else {
            command_[command_pointer_] = value;
            command_pointer_++;
        }

        const uint8_t expected = kBytesInCmd[command_[0] & 0x1f];

        if (command_pointer_ >= expected) {
            command_pointer_ = 0;
            status_register_ = 0x80;
            exec_write_command();
        }
    } else {
        exec_write_command();
    }
}

uint8_t Nec765Fdc::read_data() {
    status_counter_ = 0;

    if (exec_cmd_phase_) {
        return exec_read_command();
    }

    if (result_phase_) {
        return get_result();
    }

    return 0xff;
}

uint8_t Nec765Fdc::read_status() {
    if (result_phase_) {
        status_register_ = 0xd0;
    } else if (exec_cmd_phase_) {
        status_register_ = 0xf0;
    } else {
        status_register_ = 0x80;
    }

    return status_register_;
}

}  // namespace dsp