#pragma once

#include <memory>

namespace ilegacysim {

class AudioSubsystem;
class UserlandHleCall;
class UserlandHleRegistry;

class CelestialAudioHle {
public:
  CelestialAudioHle(UserlandHleRegistry &registry,
                    std::shared_ptr<AudioSubsystem> subsystem);

  void set_subsystem(std::shared_ptr<AudioSubsystem> subsystem);

private:
  void volume_operation(UserlandHleCall &call);

  std::shared_ptr<AudioSubsystem> subsystem_;
};

} // namespace ilegacysim
