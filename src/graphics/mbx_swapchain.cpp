#include "ilegacysim/mbx2d_hle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ilegacysim/display.hpp"
#include "ilegacysim/userland_hle.hpp"

namespace ilegacysim {

void Mbx2dHle::initialize_destination(
    UserlandHleCall& call, RenderState& state) {
    const auto destination = resolve(state.destination);
    if (!destination || destination->framebuffer ||
        destination->width != iphone_2g_display_width ||
        destination->height != iphone_2g_display_height) {
        return;
    }
    const auto surface = state.destination->surface;
    if (initialized_destinations_.contains(surface)) return;

    // CoreGraphics can rasterize static LayerKit content into a newly-created
    // CoreSurface before MBX2D binds it as the render destination.  Keep that
    // CPU-rendered backing store; replacing it with the previous scanout would
    // erase layers such as the lock-screen bottom bar before the first GPU
    // dirty update arrives.
    const auto existing = read_region(*destination, 0, 0, destination->width,
                                      destination->height, call);
    if (existing && std::any_of(existing->begin(), existing->end(),
                                [](std::uint32_t pixel) {
                                    return pixel != 0;
                                })) {
        initialized_destinations_.insert(surface);
        return;
    }

    auto initial = display_ && display_->presented_frames() != 0
                       ? display_->snapshot().pixels
                       : std::vector<std::uint32_t>{};
    const auto pixel_count = static_cast<std::size_t>(destination->width) *
                             destination->height;
    if (initial.size() != pixel_count) {
        initial.assign(pixel_count, 0xff000000U);
    }
    if (write_region(*destination, 0, 0, destination->width,
                     destination->height, initial, call)) {
        initialized_destinations_.insert(surface);
    }
}

void Mbx2dHle::prepare_destination_for_frame(
    UserlandHleCall& call, RenderState& state) {
    initialize_destination(call, state);
    const auto destination = resolve(state.destination);
    if (!destination || destination->framebuffer ||
        destination->width != iphone_2g_display_width ||
        destination->height != iphone_2g_display_height) {
        return;
    }
    const auto surface = state.destination->surface;
    const auto sequence = display_ ? display_->presented_frames() : 0;
    const auto prepared = destination_frame_sequences_.find(surface);
    if (prepared != destination_frame_sequences_.end() &&
        prepared->second == sequence) {
        return;
    }
    // LayerKit's valid textured quad submission begins a complete scene pass.
    // Small 2D dirty updates retain swap-chain contents and must not clear it.
    constexpr auto desktop_top = mbx2d_abi::springboard_status_bar_height;
    constexpr auto desktop_bottom = mbx2d_abi::springboard_dock_origin_y;
    constexpr auto desktop_height = desktop_bottom - desktop_top;
    const std::vector<std::uint32_t> blank(
        static_cast<std::size_t>(destination->width) * desktop_height,
        0xff000000U);
    if (write_region(*destination, 0, desktop_top, destination->width,
                     desktop_height, blank, call)) {
        destination_frame_sequences_[surface] = sequence;
    }
}

}  // namespace ilegacysim
