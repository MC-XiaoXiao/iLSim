#include "ilegacysim/layerkit_hle.hpp"

#include "ilegacysim/address_space.hpp"
#include "ilegacysim/device_profile.hpp"
#include "ilegacysim/graphics_services_input.hpp"
#include "ilegacysim/kernel_shared_state.hpp"
#include "ilegacysim/output.hpp"
#include "ilegacysim/userland_hle.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ilegacysim {
namespace {

constexpr const char *layerkit_image = "LayerKit.framework/LayerKit";

} // namespace

void LayerKitHle::register_handlers(
    UserlandHleRegistry &registry,
    std::shared_ptr<KernelSharedState> shared_state, Output &output) {
  shared_state_ = std::move(shared_state);
  registry.register_function(layerkit_image, "_LKRenderDecodeInt8",
                             [](UserlandHleCall &call) {
    const auto decoder = call.argument(0);
    const auto current = call.memory().read32(decoder + 12U).value_or(0);
    const auto end = call.memory().read32(decoder + 16U).value_or(0);
    std::uint32_t value{};
    if (current < end) {
      value = call.memory().read8(current).value_or(0);
      static_cast<void>(call.write32(decoder + 12U, current + 1U));
    }
    call.set_return(value);
  });

  registry.register_function(
      layerkit_image, "_LKRenderContextSetLayerId",
      [this](UserlandHleCall &call) {
        if (compatibility_.set_layer_id(call.argument(0), call.argument(1))) {
          graphics_services_input::reset_application_scene_context(
              *shared_state_, call.process_id(), call.argument(0));
        }
        call.resume_original_persistently();
      });

  registry.register_function(
      layerkit_image, "_LKRenderContextCommit",
      [this, &output](UserlandHleCall &call) {
        const auto context = call.argument(0);
        const auto object = call.argument(2);
        const auto render_flags = call.memory().read32(object).value_or(0);
        const auto children_address =
            call.memory().read32(object + 0x20U).value_or(0);
        std::vector<std::uint32_t> children;
        if (children_address > 1U) {
          const auto count =
              call.memory().read32(children_address + 4U).value_or(0);
          if (count <= 4096U) {
            children.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
              const auto child =
                  call.memory().read32(children_address + 8U + index * 4U);
              if (!child) {
                children.clear();
                break;
              }
              children.push_back(*child);
            }
          }
        }

        const auto current_layer_id =
            call.memory().read32(context + 0x3cU).value_or(0);
        const bool root_handle_cached =
            call.memory().read32(context + 0x40U).value_or(0) != 0U;
        const auto &device = DeviceProfile::iphone_2g_1_0();
        const auto position_x =
            call.memory().read32(object + 0xcU).value_or(0U);
        const auto position_y =
            call.memory().read32(object + 0x10U).value_or(0U);
        const auto width = call.memory().read32(object + 0x14U).value_or(0U);
        const auto height = call.memory().read32(object + 0x18U).value_or(0U);
        if (const auto placement = compatibility_.application_window_placement(
                context, current_layer_id, call.argument(1), render_flags,
                position_x, position_y, width, height, device.display_width,
                device.display_height)) {
          static_cast<void>(call.write32(object + 0x10U, placement->position_y));
          graphics_services_input::record_application_scene_transform(
              *shared_state_, call.process_id(), context,
              KernelSharedState::ApplicationTouchTransform{
                  placement->presentation_offset_x,
                  placement->presentation_offset_y,
                  placement->screen_origin_y});
        }
        if (const auto replacement = compatibility_.observe_commit(
                context, current_layer_id, call.argument(1), render_flags,
                call.argument(3), children, root_handle_cached)) {
          if (call.write32(context + 0x3cU, *replacement)) {
            graphics_services_input::activate_resolved_application(
                *shared_state_, call.process_id());
            output.write("[layerkit] reconnected detached root context=" +
                         std::to_string(context) +
                         " layer=" + std::to_string(*replacement) + "\n");
          }
        }
        call.resume_original_persistently();
      });
}

void LayerKitHle::set_shared_state(
    std::shared_ptr<KernelSharedState> shared_state) {
  shared_state_ = std::move(shared_state);
}

void LayerKitHle::reset() { compatibility_ = {}; }

void LayerKitHle::inherit_state(const LayerKitHle &parent) {
  compatibility_ = parent.compatibility_;
}

} // namespace ilegacysim
