#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
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
  virtual void set_gain(float gain) = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual std::string last_error() const = 0;
};

class AudioDecoder {
public:
  virtual ~AudioDecoder() = default;
  [[nodiscard]] virtual std::optional<AudioBuffer>
  decode(const std::filesystem::path &path) = 0;
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
  float applied_gain{1.0F};
};

struct AudioClientObject {
  std::uint32_t client{};
  std::uint32_t object{};

  bool operator==(const AudioClientObject &) const = default;
  auto operator<=>(const AudioClientObject &) const = default;
};

class AudioService {
public:
  explicit AudioService(std::filesystem::path rootfs);

  void set_sink(std::shared_ptr<AudioSink> sink);
  void set_decoder(std::shared_ptr<AudioDecoder> decoder);
  [[nodiscard]] AudioPlayResult play_system_sound(std::uint32_t sound_id);
  [[nodiscard]] AudioPlayResult
  play_audio_file(const std::filesystem::path &guest_path,
                  bool replace_current = false,
                  float device_volume = 1.0F);
  [[nodiscard]] AudioPlayResult queue_pcm(AudioBuffer buffer,
                                          float device_volume = 1.0F);
  void stop_playback();

  // The guest FigMovie service is the source-of-truth for source selection.
  // A create request's path is correlated with its reply through the Mach
  // reply-port object; subsequent properties address the returned movie ID.
  void observe_service_source_create_request(
      std::uint32_t reply_object, std::filesystem::path guest_path);
  [[nodiscard]] std::optional<std::filesystem::path>
  observe_service_source_create_reply(std::uint32_t reply_object,
                                      std::uint32_t source);
  [[nodiscard]] bool observe_service_source_property(
      std::uint32_t source, std::string_view property, float value);
  [[nodiscard]] bool service_source_playing() const;

  void observe_category_volume(std::string category, float value);
  [[nodiscard]] float category_volume(std::string_view category) const;

  void initialize_player(AudioClientObject player,
                         AudioClientObject queue);
  void set_player_queue(AudioClientObject player, AudioClientObject queue);
  [[nodiscard]] AudioClientObject player_queue(AudioClientObject player) const;
  [[nodiscard]] AudioPlayResult set_player_rate(AudioClientObject player,
                                                float rate);
  [[nodiscard]] float player_rate(AudioClientObject player) const;
  void stop_player(AudioClientObject player);
  void set_player_repeat_mode(AudioClientObject player, std::uint32_t mode);
  [[nodiscard]] std::uint32_t
  player_repeat_mode(AudioClientObject player) const;

  void initialize_queue(AudioClientObject queue);
  void clear_queue(AudioClientObject queue);
  [[nodiscard]] bool append_queue_item(AudioClientObject queue,
                                       AudioClientObject item);
  void set_item_path(AudioClientObject item, std::filesystem::path path);
  [[nodiscard]] std::optional<std::filesystem::path>
  item_path(AudioClientObject item) const;

private:
  [[nodiscard]] std::optional<AudioBuffer>
  load_sound_locked(std::uint32_t sound_id, AudioPlayResult &result);
  [[nodiscard]] std::optional<AudioBuffer>
  load_file_locked(const std::filesystem::path &guest_path,
                   AudioPlayResult &result);
  void load_category_aliases();
  void load_system_volume_state();
  [[nodiscard]] std::string
  canonical_category_locked(std::string_view category) const;
  [[nodiscard]] AudioPlayResult
  play_audio_file_with_gain(const std::filesystem::path &guest_path,
                            bool replace_current, float gain);

  std::filesystem::path rootfs_;
  mutable std::mutex mutex_;
  std::shared_ptr<AudioSink> sink_;
  std::shared_ptr<AudioDecoder> decoder_;
  std::map<std::uint32_t, std::filesystem::path> system_sounds_;
  std::map<std::uint32_t, AudioBuffer> decoded_sounds_;
  std::map<std::filesystem::path, AudioBuffer> decoded_files_;
  struct ServiceSource {
    std::filesystem::path path;
    std::optional<float> user_volume;
    float rate{};
  };
  std::map<std::uint32_t, std::deque<std::filesystem::path>>
      pending_service_source_creates_;
  std::map<std::uint32_t, ServiceSource> service_sources_;
  std::optional<std::uint32_t> playing_service_source_id_;
  std::optional<std::filesystem::path> playing_service_source_;
  struct PlayerState {
    AudioClientObject queue;
    float rate{};
    std::uint32_t repeat_mode{};
  };
  std::map<AudioClientObject, PlayerState> players_;
  std::map<AudioClientObject, std::vector<AudioClientObject>> queues_;
  std::map<AudioClientObject, std::filesystem::path> items_;
  std::map<std::string, std::string, std::less<>> category_aliases_;
  std::map<std::string, float, std::less<>> category_volumes_{
      {"Ringtone", 0.5F}};
};

} // namespace ilegacysim
