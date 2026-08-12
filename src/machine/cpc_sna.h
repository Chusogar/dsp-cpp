#pragma once

#include <cstdint>
#include <string>

namespace dsp {

class AmstradCpc;

// Amstrad CPC .SNA (MV - SNA) v1–v3, from snapshot.pas.
bool cpc_load_sna(AmstradCpc& machine, const uint8_t* data, size_t size, std::string* error);
bool cpc_load_sna_file(AmstradCpc& machine, const std::string& path, std::string* error);
bool cpc_save_sna_file(const AmstradCpc& machine, const std::string& path, std::string* error);

}  // namespace dsp
