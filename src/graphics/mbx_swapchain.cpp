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
    if (!destination || destination->framebuffer || !display_ ||
        destination->width != display_->width() ||
        destination->height != display_->height()) {
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
        destination_frame_sequences_[surface] =
            display_ ? display_->presented_frames() : 0;
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
        destination_frame_sequences_[surface] =
            display_ ? display_->presented_frames() : 0;
    }
}

void Mbx2dHle::prepare_destination_for_frame(
    UserlandHleCall& call, RenderState& state) {
    initialize_destination(call, state);
    const auto destination = resolve(state.destination);
    if (!destination || destination->framebuffer || !display_ ||
        destination->width != display_->width() ||
        destination->height != display_->height()) {
        return;
    }
    const auto surface = state.destination->surface;
    const auto sequence = display_ ? display_->presented_frames() : 0;
    const auto prepared = destination_frame_sequences_.find(surface);
    if (prepared != destination_frame_sequences_.end() &&
        prepared->second == sequence) {
        return;
    }
    const auto pixel_count = static_cast<std::size_t>(destination->width) *
                             destination->height;
    const std::vector<std::uint32_t> clear(pixel_count, 0xff000000U);
    // A complete scene pass constructs its destination from submitted layers.
    // Clear the entire reused swap backing once, independent of any UI bands;
    // retaining the previous scanout here would leave trails behind moving
    // source-over layers and would make opacity depend on screen coordinates.
    if (write_region(*destination, 0, 0, destination->width,
                     destination->height, clear, call)) {
        destination_frame_sequences_[surface] = sequence;
    }
}

}  // namespace ilegacysim
