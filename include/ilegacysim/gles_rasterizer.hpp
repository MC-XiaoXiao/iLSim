#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "ilegacysim/gles_abi.hpp"

namespace ilegacysim {

class DisplayState;
class GlesResourceStore;

struct GlesRasterVertex {
    std::array<float, 4> position{0.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<std::array<float, 2>, gles_abi::texture_unit_count> texture{};
};

struct GlesRasterTextureUnit {
    std::uint32_t texture{};
    bool enabled{};
    bool rectangle{};
    bool replace{};
};

struct GlesRasterState {
    std::int32_t viewport_x{};
    std::int32_t viewport_y{};
    std::uint32_t viewport_width{};
    std::uint32_t viewport_height{};
    const GlesResourceStore* resources{};
    std::array<GlesRasterTextureUnit, gles_abi::texture_unit_count>
        texture_units{};
    bool blend_enabled{};
    bool scissor_enabled{};
    std::array<std::int32_t, 4> scissor_box{};
    std::array<bool, 4> color_mask{true, true, true, true};
    std::uint32_t blend_source{};
    std::uint32_t blend_destination{};
};

class GlesSoftwareRasterizer {
public:
    static bool draw(
        DisplayState& display, std::span<const GlesRasterVertex> vertices,
        std::uint32_t mode, const GlesRasterState& state);
};

}  // namespace ilegacysim
