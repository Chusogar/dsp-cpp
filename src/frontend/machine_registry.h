#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"

namespace dsp {

// The machine for a --game name (or alias), not initialised; nullptr when the
// name is unknown or its driver is not built in (see DSP_DRIVERS).
std::unique_ptr<Machine> create_machine(const std::string& game);

// One driver as listed by --listarcades / --listconsoles / --listcomputers,
// the web launcher and the microcontroller menus: its --game name, other
// names for the same machine, title and type.
struct DriverInfo {
    std::string name;
    std::vector<std::string> aliases;
    std::string title;
    MachineType type;
};

// Every driver built in, in registry order. Each one is instantiated once
// (not initialised: no ROMs are read) to ask its type and title.
const std::vector<DriverInfo>& list_drivers();

}  // namespace dsp
