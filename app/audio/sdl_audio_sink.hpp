#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "ilegacysim/audio.hpp"

namespace ilegacysim {

class SdlAudioSink final : public AudioSink {
public:
  SdlAudioSink();
  ~SdlAudioSink() override;

  SdlAudioSink(const SdlAudioSink &) = delete;
  SdlAudioSink &operator=(const SdlAudioSink &) = delete;

  [[nodiscard]] static bool available();
  [[nodiscard]] bool play(const AudioBuffer &buffer) override;
  void set_gain(float gain) override;
  void stop() override;
  [[nodiscard]] std::string last_error() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ilegacysim
