#pragma once

namespace dsp {

// Interrupt line states, same meaning as CLEAR/ASSERT/HOLD/PULSE_LINE in cpu_misc.pas.
enum class IrqLine { Clear, Assert, Hold, Pulse };

}  // namespace dsp
