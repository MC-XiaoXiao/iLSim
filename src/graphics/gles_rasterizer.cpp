#include "ilegacysim/gles_rasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ilegacysim/display.hpp"
#include "ilegacysim/gles_abi.hpp"
#include "ilegacysim/gles_resources.hpp"

namespace ilegacysim {
namespace {

struct ScreenVertex {
    float x{};
    float y{};
    std::array<float, 4> color{};
    std::array<std::array<float, 2>, gles_abi::texture_unit_count> texture{};
};

float edge(const ScreenVertex& a, const ScreenVertex& b, float x, float y) {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

std::uint32_t modulate(
    std::uint32_t pixel, const std::array<float, 4>& color) {
    const auto multiply = [&](std::uint32_t shift, std::size_t component) {
        return static_cast<std::uint32_t>(std::lround(
            static_cast<float>((pixel >> shift) & 0xffU) *
            std::clamp(color[component], 0.0F, 1.0F)));
    };
    return (multiply(24U, 3) << 24U) | (multiply(16U, 0) << 16U) |
           (multiply(8U, 1) << 8U) | multiply(0U, 2);
}

std::uint32_t modulate_pixels(std::uint32_t left, std::uint32_t right) {
    const auto channel = [&](std::uint32_t shift) {
        return ((((left >> shift) & 0xffU) *
                 ((right >> shift) & 0xffU)) +
                127U) /
               255U;
    };
    return (channel(24U) << 24U) | (channel(16U) << 16U) |
           (channel(8U) << 8U) | channel(0U);
}

std::uint32_t source_over(std::uint32_t source, std::uint32_t destination) {
    const auto source_alpha = (source >> 24U) & 0xffU;
    const auto inverse = 255U - source_alpha;
    const auto blend_channel = [&](std::uint32_t shift) {
        const auto source_channel = (source >> shift) & 0xffU;
        const auto destination_channel = (destination >> shift) & 0xffU;
        return (source_channel * source_alpha +
                destination_channel * inverse + 127U) / 255U;
    };
    const auto destination_alpha = (destination >> 24U) & 0xffU;
    const auto alpha = std::min(
        255U, source_alpha + (destination_alpha * inverse + 127U) / 255U);
    return (alpha << 24U) | (blend_channel(16U) << 16U) |
           (blend_channel(8U) << 8U) | blend_channel(0U);
}

std::uint32_t premultiplied_source_over(
    std::uint32_t source, std::uint32_t destination) {
    const auto source_alpha = (source >> 24U) & 0xffU;
    const auto inverse = 255U - source_alpha;
    const auto blend_channel = [&](std::uint32_t shift) {
        return std::min(
            255U, ((source >> shift) & 0xffU) +
                      ((((destination >> shift) & 0xffU) * inverse + 127U) /
                       255U));
    };
    const auto destination_alpha = (destination >> 24U) & 0xffU;
    const auto alpha = std::min(
        255U, source_alpha + (destination_alpha * inverse + 127U) / 255U);
    return (alpha << 24U) | (blend_channel(16U) << 16U) |
           (blend_channel(8U) << 8U) | blend_channel(0U);
}

std::uint32_t apply_color_mask(
    std::uint32_t source, std::uint32_t destination,
    const std::array<bool, 4>& mask) {
    constexpr std::array<std::uint32_t, 4> channel_masks{
        0x00ff0000U, 0x0000ff00U, 0x000000ffU, 0xff000000U};
    auto result = destination;
    for (std::size_t component = 0; component < mask.size(); ++component) {
        if (!mask[component]) continue;
        result = (result & ~channel_masks[component]) |
                 (source & channel_masks[component]);
    }
    return result;
}

std::uint32_t sample_texture(
    const GlesRasterState& state, const GlesRasterTextureUnit& unit,
    float s, float t) {
    if (!unit.enabled || state.resources == nullptr || unit.texture == 0) {
        return 0xffffffffU;
    }
    const auto* texture = state.resources->texture(unit.texture);
    if (texture == nullptr) return 0xffffffffU;
    const auto level = texture->levels.find(0);
    if (level == texture->levels.end() || level->second.width == 0 ||
        level->second.height == 0 || level->second.argb.empty()) {
        return 0xffffffffU;
    }
    const auto coordinate = [](float value, std::uint32_t dimension,
                               bool rectangle) {
        if (rectangle) {
            return static_cast<std::uint32_t>(std::clamp(
                std::floor(value), 0.0F,
                static_cast<float>(dimension - 1U)));
        }
        const auto wrapped = value - std::floor(value);
        return std::min(
            dimension - 1U,
            static_cast<std::uint32_t>(
                wrapped * static_cast<float>(dimension)));
    };
    const auto x = coordinate(s, level->second.width, unit.rectangle);
    const auto y = coordinate(t, level->second.height, unit.rectangle);
    return level->second.argb[static_cast<std::size_t>(y) *
                                  level->second.width + x];
}

void draw_triangle(
    DisplayFrame& frame, const std::array<ScreenVertex, 3>& triangle,
    const GlesRasterState& state) {
    const auto area = edge(
        triangle[0], triangle[1], triangle[2].x, triangle[2].y);
    if (std::abs(area) < 1.0e-6F) return;
    const auto minimum_x = std::max(
        0, static_cast<int>(std::floor(std::min({
               triangle[0].x, triangle[1].x, triangle[2].x}))));
    const auto maximum_x = std::min(
        static_cast<int>(frame.width) - 1,
        static_cast<int>(std::ceil(std::max({
            triangle[0].x, triangle[1].x, triangle[2].x}))));
    const auto minimum_y = std::max(
        0, static_cast<int>(std::floor(std::min({
               triangle[0].y, triangle[1].y, triangle[2].y}))));
    const auto maximum_y = std::min(
        static_cast<int>(frame.height) - 1,
        static_cast<int>(std::ceil(std::max({
            triangle[0].y, triangle[1].y, triangle[2].y}))));
    for (int y = minimum_y; y <= maximum_y; ++y) {
        for (int x = minimum_x; x <= maximum_x; ++x) {
            const auto sample_x = static_cast<float>(x) + 0.5F;
            const auto sample_y = static_cast<float>(y) + 0.5F;
            if (state.scissor_enabled) {
                const auto guest_y = static_cast<float>(frame.height) - sample_y;
                const auto scissor_right =
                    static_cast<float>(state.scissor_box[0]) +
                    static_cast<float>(state.scissor_box[2]);
                const auto scissor_top =
                    static_cast<float>(state.scissor_box[1]) +
                    static_cast<float>(state.scissor_box[3]);
                if (sample_x < static_cast<float>(state.scissor_box[0]) ||
                    sample_x >= scissor_right ||
                    guest_y < static_cast<float>(state.scissor_box[1]) ||
                    guest_y >= scissor_top) {
                    continue;
                }
            }
            const auto w0 = edge(
                triangle[1], triangle[2], sample_x, sample_y) / area;
            const auto w1 = edge(
                triangle[2], triangle[0], sample_x, sample_y) / area;
            const auto w2 = 1.0F - w0 - w1;
            if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F) continue;
            std::array<float, 4> color{};
            for (std::size_t component = 0; component < color.size(); ++component) {
                color[component] = triangle[0].color[component] * w0 +
                                   triangle[1].color[component] * w1 +
                                   triangle[2].color[component] * w2;
            }
            auto pixel = modulate(0xffffffffU, color);
            for (std::size_t unit_index = 0;
                 unit_index < state.texture_units.size(); ++unit_index) {
                const auto& unit = state.texture_units[unit_index];
                if (!unit.enabled) continue;
                const auto texture_s =
                    triangle[0].texture[unit_index][0] * w0 +
                    triangle[1].texture[unit_index][0] * w1 +
                    triangle[2].texture[unit_index][0] * w2;
                const auto texture_t =
                    triangle[0].texture[unit_index][1] * w0 +
                    triangle[1].texture[unit_index][1] * w1 +
                    triangle[2].texture[unit_index][1] * w2;
                const auto sampled = sample_texture(
                    state, unit, texture_s, texture_t);
                pixel = unit.replace ? sampled
                                     : modulate_pixels(pixel, sampled);
            }
            const auto offset = static_cast<std::size_t>(y) * frame.width +
                                static_cast<std::size_t>(x);
            if (state.blend_enabled &&
                state.blend_source == gles_abi::source_alpha &&
                state.blend_destination == gles_abi::one_minus_source_alpha) {
                pixel = source_over(pixel, frame.pixels[offset]);
            } else if (state.blend_enabled &&
                       state.blend_source == gles_abi::one &&
                       state.blend_destination ==
                           gles_abi::one_minus_source_alpha) {
                pixel = premultiplied_source_over(
                    pixel, frame.pixels[offset]);
            }
            frame.pixels[offset] = apply_color_mask(
                pixel, frame.pixels[offset], state.color_mask);
        }
    }
}

}  // namespace

bool GlesSoftwareRasterizer::draw(
    DisplayState& display, std::span<const GlesRasterVertex> vertices,
    std::uint32_t mode, const GlesRasterState& state) {
    if (state.viewport_width == 0 || state.viewport_height == 0 ||
        vertices.size() < 3 ||
        (mode != gles_abi::triangles && mode != gles_abi::triangle_strip &&
         mode != gles_abi::triangle_fan)) {
        return false;
    }
    auto frame = display.snapshot();
    std::vector<ScreenVertex> screen;
    screen.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        if (vertex.position[3] == 0.0F) return false;
        const auto inverse_w = 1.0F / vertex.position[3];
        const auto ndc_x = vertex.position[0] * inverse_w;
        const auto ndc_y = vertex.position[1] * inverse_w;
        const auto window_x = static_cast<float>(state.viewport_x) +
                              (ndc_x * 0.5F + 0.5F) *
                                  static_cast<float>(state.viewport_width);
        const auto window_y = static_cast<float>(state.viewport_y) +
                              (ndc_y * 0.5F + 0.5F) *
                                  static_cast<float>(state.viewport_height);
        screen.push_back(ScreenVertex{
            window_x, static_cast<float>(frame.height) - window_y,
            vertex.color, vertex.texture});
    }
    const auto emit = [&](std::size_t a, std::size_t b, std::size_t c) {
        draw_triangle(frame, {screen[a], screen[b], screen[c]}, state);
    };
    if (mode == gles_abi::triangles) {
        for (std::size_t index = 0; index + 2 < screen.size(); index += 3) {
            emit(index, index + 1U, index + 2U);
        }
    } else if (mode == gles_abi::triangle_strip) {
        for (std::size_t index = 0; index + 2 < screen.size(); ++index) {
            if ((index & 1U) == 0) emit(index, index + 1U, index + 2U);
            else emit(index + 1U, index, index + 2U);
        }
    } else {
        for (std::size_t index = 1; index + 1 < screen.size(); ++index) {
            emit(0, index, index + 1U);
        }
    }
    display.replace_pixels(std::move(frame.pixels));
    return true;
}

}  // namespace ilegacysim
