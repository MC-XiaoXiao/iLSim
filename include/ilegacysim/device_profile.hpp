#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ilegacysim {

struct DeviceProfile {
    std::string_view product_type;
    std::string_view board_config;
    // Retail configuration identifier exposed by the platform device tree.
    // This is distinct from product_type (for example, iPhone1,1) and the
    // hardware board configuration (for example, M68AP).
    std::string_view model_number;
    std::string_view soc;
    std::string_view cpu_core;
    std::string_view instruction_set;
    std::uint32_t cpu_hz;
    std::uint32_t bus_hz;
    std::uint64_t ram_bytes;
    std::size_t physical_cpu_count;
    std::uint32_t display_width;
    std::uint32_t display_height;

    static const DeviceProfile& iphone_2g_1_0();
};

}  // namespace ilegacysim
