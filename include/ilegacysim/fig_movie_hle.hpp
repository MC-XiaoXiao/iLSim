#pragma once

namespace ilegacysim {

class UserlandHleRegistry;

// Provides the inactive media-control state needed during SpringBoard startup
// without running the firmware movie server.
void register_fig_movie_hle(UserlandHleRegistry& registry);

}  // namespace ilegacysim
