#include "ilegacysim/audio_toolbox_hle.hpp"

#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "ilegacysim/audio.hpp"
#include "ilegacysim/output.hpp"
#include "ilegacysim/userland_hle.hpp"

namespace ilegacysim {
namespace {

constexpr std::string_view audio_toolbox_image{
    "/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox"};

std::string status_name(AudioPlayStatus status) {
  switch (status) {
  case AudioPlayStatus::Queued:
    return "queued";
  case AudioPlayStatus::UnknownSound:
    return "unknown-sound";
  case AudioPlayStatus::ResourceUnavailable:
    return "resource-unavailable";
  case AudioPlayStatus::UnsupportedResource:
    return "unsupported-resource";
  case AudioPlayStatus::NoSink:
    return "no-sink";
  case AudioPlayStatus::SinkError:
    return "sink-error";
  }
  return "invalid";
}

} // namespace

AudioToolboxHle::AudioToolboxHle(
    UserlandHleRegistry &registry, std::shared_ptr<AudioService> service)
    : service_{std::move(service)} {
  const auto register_play = [&](std::string symbol) {
    registry.register_function(
        std::string{audio_toolbox_image}, std::move(symbol),
        [this](UserlandHleCall &call) { play_system_sound(call); });
  };
  register_play("_AudioServicesPlaySystemSound");
  register_play("_AudioServicesPlayInterfaceSound");
  register_play("_AudioServicesPlayAlertSound");
}

void AudioToolboxHle::set_service(std::shared_ptr<AudioService> service) {
  service_ = std::move(service);
}

void AudioToolboxHle::play_system_sound(UserlandHleCall &call) {
  const auto sound_id = call.argument(0);
  const auto result = service_->play_system_sound(sound_id);
  std::ostringstream message;
  message << "[audio] system-sound pid=" << call.process_id()
          << " id=" << sound_id << " status=" << status_name(result.status);
  if (!result.guest_path.empty())
    message << " path=" << result.guest_path.string();
  if (!result.detail.empty())
    message << " detail=" << std::quoted(result.detail);
  call.output().line(message.str());
  call.set_return(0);
}

} // namespace ilegacysim
