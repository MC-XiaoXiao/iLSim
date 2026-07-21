#include "ilegacysim/celestial_audio_hle.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "ilegacysim/address_space.hpp"
#include "ilegacysim/audio.hpp"
#include "ilegacysim/cpu.hpp"
#include "ilegacysim/output.hpp"
#include "ilegacysim/userland_hle.hpp"

namespace ilegacysim {
namespace {

constexpr std::string_view celestial_image{
    "/System/Library/Frameworks/Celestial.framework/Celestial"};
constexpr std::string_view volume_operation_symbol{
    "_FMPlayback_VolumeOperationForCurrentCategory"};
constexpr std::string_view springboard_image{
    "/System/Library/CoreServices/SpringBoard.app/SpringBoard"};
constexpr std::string_view volume_control_show_hud{
    "-[VolumeControl showHUD]"};
constexpr std::uint32_t media_server_unavailable = 5;

enum class VolumeOperation : std::uint32_t {
  Change = 1,
  Set = 2,
  Get = 3,
  ChangeForRoute = 4,
  SetForRoute = 5,
  GetForRoute = 6,
  NotifyChanged = 7,
  ToggleMuted = 8,
  GetMuted = 9,
  ToggleMutedForRoute = 10,
  GetMutedForRoute = 11,
};

bool is_change_operation(VolumeOperation operation) {
  return operation == VolumeOperation::Change ||
         operation == VolumeOperation::ChangeForRoute;
}

bool is_set_operation(VolumeOperation operation) {
  return operation == VolumeOperation::Set ||
         operation == VolumeOperation::SetForRoute ||
         operation == VolumeOperation::NotifyChanged;
}

bool is_toggle_muted_operation(VolumeOperation operation) {
  return operation == VolumeOperation::ToggleMuted ||
         operation == VolumeOperation::ToggleMutedForRoute;
}

} // namespace

CelestialAudioHle::CelestialAudioHle(
    UserlandHleRegistry &registry, std::shared_ptr<AudioSubsystem> subsystem)
    : subsystem_{std::move(subsystem)} {
  registry.register_function(
      std::string{celestial_image}, std::string{volume_operation_symbol},
      [this](UserlandHleCall &call) { volume_operation(call); });
  registry.register_objc_instance_method(
      std::string{celestial_image}, "AVSystemController", "init",
      "-[AVSystemController init]", [](UserlandHleCall &call) {
        if (!call.image_loaded(springboard_image)) {
          call.resume_original_persistently();
          return;
        }
        // The original initializer tears the controller down when the media
        // server registration is unavailable. The compatibility audio model
        // implements the controller operations below, so retain the normal
        // Objective-C instance for system UI clients.
        call.set_return(call.argument(0));
      });
  registry.register_objc_instance_method(
      std::string{celestial_image}, "AVSystemController", "attributeForKey:",
      "-[AVSystemController attributeForKey:]", [](UserlandHleCall &call) {
        if (!call.image_loaded(springboard_image)) {
          call.resume_original_persistently();
          return;
        }
        // A retained compatibility controller has no media-server private
        // state. Unknown attributes are represented by Objective-C nil.
        call.set_return(0);
      });
  registry.register_objc_instance_method(
      std::string{springboard_image}, "VolumeControl", "_changeVolumeBy:",
      "-[VolumeControl _changeVolumeBy:]", [this](UserlandHleCall &call) {
        const auto delta = std::bit_cast<float>(call.argument(2));
        const auto current = std::isfinite(delta)
                                 ? subsystem_->change_volume(delta)
                                 : subsystem_->volume();
        std::ostringstream message;
        message << "[audio] volume-button delta=" << delta
                << " volume=" << current;
        call.output().line(message.str());

        // Finish through SpringBoard's real HUD method. The semantic method
        // registration keeps this independent of image addresses and lets
        // firmware own all drawing, animation, and category presentation.
        static_cast<void>(
            call.tail_call_registered(volume_control_show_hud));
      });
  registry.register_objc_instance_method(
      std::string{springboard_image}, "VolumeControl", "showHUD",
      std::string{volume_control_show_hud}, [this](UserlandHleCall &call) {
        const auto controller = call.argument(0);
        call.resume_original_persistently(
            [this, controller](UserlandHleCall &continuation) {
              constexpr std::uint32_t volume_view_offset = 8;
              constexpr std::uint32_t volume_mode_offset = 16;
              const auto view = continuation.memory()
                                    .read32(controller + volume_view_offset)
                                    .value_or(0);
              if (view == 0)
                return;
              auto &registers = continuation.cpu().registers();
              registers[0] = view;
              registers[2] =
                  std::bit_cast<std::uint32_t>(subsystem_->volume());
              registers[3] = continuation.memory()
                                 .read32(controller + volume_mode_offset)
                                 .value_or(0);
              static_cast<void>(continuation.tail_call_registered(
                  "-[VolumeControlView setVolume:mode:]"));
            });
      });
  registry.register_objc_instance_method(
      std::string{springboard_image}, "VolumeControlView", "setVolume:mode:",
      "-[VolumeControlView setVolume:mode:]", [](UserlandHleCall &call) {
        call.resume_original_persistently();
      });
}

void CelestialAudioHle::set_subsystem(
    std::shared_ptr<AudioSubsystem> subsystem) {
  subsystem_ = std::move(subsystem);
}

void CelestialAudioHle::volume_operation(UserlandHleCall &call) {
  const auto operation = static_cast<VolumeOperation>(call.argument(0));
  const auto requested = std::bit_cast<float>(call.argument(1));
  auto current_volume = subsystem_->volume();
  if (std::isfinite(requested)) {
    if (is_change_operation(operation)) {
      current_volume = subsystem_->change_volume(requested);
    } else if (is_set_operation(operation)) {
      current_volume = subsystem_->set_volume(requested);
    }
  }
  if (is_toggle_muted_operation(operation))
    static_cast<void>(subsystem_->toggle_muted());

  const auto volume_output = call.argument(5);
  const auto muted_output = call.argument(6);
  const auto category_output = call.argument(7);
  const auto fallback_category = call.argument(2);
  const auto outputs_written =
      call.write32(volume_output, std::bit_cast<std::uint32_t>(current_volume)) &&
      (muted_output == 0 || call.memory().write8(
                                  muted_output, subsystem_->muted() ? 1U : 0U)) &&
      call.write32(category_output, fallback_category);

  std::ostringstream message;
  message << "[audio] volume-operation pid=" << call.process_id()
          << " operation=" << static_cast<std::uint32_t>(operation)
          << " requested=" << requested << " volume=" << current_volume
          << " muted=" << (subsystem_->muted() ? 1 : 0)
          << " category=0x" << std::hex << fallback_category << std::dec
          << " status=" << (outputs_written ? "ok" : "invalid-output");
  call.output().line(message.str());
  call.set_return(outputs_written ? 0U : media_server_unavailable);
}

} // namespace ilegacysim
