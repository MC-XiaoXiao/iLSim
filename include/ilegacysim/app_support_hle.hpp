#pragma once

namespace ilegacysim {

class UserlandHleRegistry;

// Narrow compatibility boundary for legacy AppSupport database entry points.
void register_app_support_hle(UserlandHleRegistry& registry);

}  // namespace ilegacysim
