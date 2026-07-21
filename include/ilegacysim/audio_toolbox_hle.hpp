#pragma once

#include <cstdint>
#include <memory>

namespace ilegacysim {

class AudioSubsystem;
class UserlandHleRegistry;

namespace audio_toolbox {

inline constexpr std::uint32_t lock_system_sound_id = 1100;
inline constexpr std::uint32_t unlock_system_sound_id = 1101;

} // namespace audio_toolbox

class AudioToolboxHle {
public:
  AudioToolboxHle(UserlandHleRegistry &registry,
                  std::shared_ptr<AudioSubsystem> subsystem);

  void set_subsystem(std::shared_ptr<AudioSubsystem> subsystem);

private:
  void play_system_sound(class UserlandHleCall &call);

  std::shared_ptr<AudioSubsystem> subsystem_;
};

} // namespace ilegacysim
