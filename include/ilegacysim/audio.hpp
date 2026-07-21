#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ilegacysim {

struct AudioBuffer {
  std::uint32_t sample_rate{};
  std::uint16_t channel_count{};
  std::vector<std::int16_t> samples;
};

class AudioSink {
public:
  virtual ~AudioSink() = default;
  [[nodiscard]] virtual bool play(const AudioBuffer &buffer) = 0;
  [[nodiscard]] virtual std::string last_error() const = 0;
};

enum class AudioPlayStatus {
  Queued,
  UnknownSound,
  ResourceUnavailable,
  UnsupportedResource,
  NoSink,
  SinkError,
};

struct AudioPlayResult {
  AudioPlayStatus status{AudioPlayStatus::UnknownSound};
  std::filesystem::path guest_path;
  std::string detail;
};

class AudioSubsystem {
public:
  explicit AudioSubsystem(std::filesystem::path rootfs);

  void set_sink(std::shared_ptr<AudioSink> sink);
  [[nodiscard]] AudioPlayResult play_system_sound(std::uint32_t sound_id);

  [[nodiscard]] float volume() const;
  [[nodiscard]] float change_volume(float delta);
  [[nodiscard]] float set_volume(float value);
  [[nodiscard]] bool muted() const;
  [[nodiscard]] bool toggle_muted();

private:
  [[nodiscard]] std::optional<AudioBuffer>
  load_sound_locked(std::uint32_t sound_id, AudioPlayResult &result);

  std::filesystem::path rootfs_;
  mutable std::mutex mutex_;
  std::shared_ptr<AudioSink> sink_;
  std::map<std::uint32_t, std::filesystem::path> system_sounds_;
  std::map<std::uint32_t, AudioBuffer> decoded_sounds_;
  float volume_{0.5F};
  bool muted_{};
};

} // namespace ilegacysim
