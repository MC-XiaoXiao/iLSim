#pragma once

namespace ilegacysim {

class UserlandHleRegistry;

// Supplies the high-level telephony state needed by SpringBoard without
// emulating a baseband device or the CommCenter transport protocol.
void register_core_telephony_hle(UserlandHleRegistry& registry);

}  // namespace ilegacysim
