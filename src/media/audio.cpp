#include "ilegacysim/audio.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <span>

#include "ilegacysim/audio_toolbox_hle.hpp"

namespace ilegacysim {
namespace {

constexpr std::array<std::byte, 4> caf_signature{
    std::byte{'c'}, std::byte{'a'}, std::byte{'f'}, std::byte{'f'}};
constexpr std::array<std::byte, 4> description_chunk{
    std::byte{'d'}, std::byte{'e'}, std::byte{'s'}, std::byte{'c'}};
constexpr std::array<std::byte, 4> data_chunk{
    std::byte{'d'}, std::byte{'a'}, std::byte{'t'}, std::byte{'a'}};
constexpr std::array<std::byte, 4> linear_pcm_format{
    std::byte{'l'}, std::byte{'p'}, std::byte{'c'}, std::byte{'m'}};
constexpr std::size_t caf_header_size = 8;
constexpr std::size_t caf_chunk_header_size = 12;
constexpr std::size_t caf_description_size = 32;
constexpr std::size_t caf_data_edit_count_size = 4;
constexpr std::uint32_t signed_little_endian_pcm_flags = 2;
constexpr std::uint32_t supported_bits_per_channel = 16;

std::uint32_t read_big_endian_u32(std::span<const std::byte> bytes,
                                  std::size_t offset) {
  return (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         std::to_integer<std::uint32_t>(bytes[offset + 3U]);
}

std::uint64_t read_big_endian_u64(std::span<const std::byte> bytes,
                                  std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value = (value << 8U) |
            std::to_integer<std::uint64_t>(bytes[offset + index]);
  }
  return value;
}

double read_big_endian_f64(std::span<const std::byte> bytes,
                           std::size_t offset) {
  return std::bit_cast<double>(read_big_endian_u64(bytes, offset));
}

bool equal_tag(std::span<const std::byte> bytes, std::size_t offset,
               const std::array<std::byte, 4> &tag) {
  return offset <= bytes.size() && tag.size() <= bytes.size() - offset &&
         std::equal(tag.begin(), tag.end(), bytes.begin() +
                                                static_cast<std::ptrdiff_t>(offset));
}

std::optional<std::vector<std::byte>> read_file(
    const std::filesystem::path &path) {
  std::ifstream stream{path, std::ios::binary | std::ios::ate};
  if (!stream)
    return std::nullopt;
  const auto end = stream.tellg();
  if (end < 0)
    return std::nullopt;
  const auto size = static_cast<std::uint64_t>(end);
  if (size > std::numeric_limits<std::size_t>::max())
    return std::nullopt;
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  if (!bytes.empty() &&
      !stream.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()))) {
    return std::nullopt;
  }
  return bytes;
}

std::optional<AudioBuffer> decode_linear_pcm_caf(
    std::span<const std::byte> bytes) {
  if (bytes.size() < caf_header_size || !equal_tag(bytes, 0, caf_signature))
    return std::nullopt;

  std::optional<std::uint32_t> sample_rate;
  std::optional<std::uint32_t> channel_count;
  std::optional<std::uint32_t> bytes_per_packet;
  std::span<const std::byte> sample_bytes;
  std::size_t cursor = caf_header_size;
  while (cursor <= bytes.size() &&
         caf_chunk_header_size <= bytes.size() - cursor) {
    const auto chunk_size_u64 = read_big_endian_u64(bytes, cursor + 4U);
    cursor += caf_chunk_header_size;
    if (chunk_size_u64 > bytes.size() - cursor)
      return std::nullopt;
    const auto chunk_size = static_cast<std::size_t>(chunk_size_u64);
    const auto payload = bytes.subspan(cursor, chunk_size);

    if (equal_tag(bytes, cursor - caf_chunk_header_size, description_chunk)) {
      if (payload.size() < caf_description_size ||
          !equal_tag(payload, 8U, linear_pcm_format) ||
          read_big_endian_u32(payload, 12U) !=
              signed_little_endian_pcm_flags ||
          read_big_endian_u32(payload, 28U) != supported_bits_per_channel) {
        return std::nullopt;
      }
      const auto rate = read_big_endian_f64(payload, 0U);
      const auto channels = read_big_endian_u32(payload, 24U);
      const auto packet_bytes = read_big_endian_u32(payload, 16U);
      if (!std::isfinite(rate) || rate < 1.0 ||
          rate > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
          channels == 0 || channels > std::numeric_limits<std::uint16_t>::max() ||
          packet_bytes != channels * sizeof(std::int16_t)) {
        return std::nullopt;
      }
      sample_rate = static_cast<std::uint32_t>(std::lround(rate));
      channel_count = channels;
      bytes_per_packet = packet_bytes;
    } else if (equal_tag(bytes, cursor - caf_chunk_header_size, data_chunk)) {
      if (payload.size() < caf_data_edit_count_size)
        return std::nullopt;
      sample_bytes = payload.subspan(caf_data_edit_count_size);
    }
    cursor += chunk_size;
  }

  if (!sample_rate || !channel_count || !bytes_per_packet ||
      sample_bytes.empty() || sample_bytes.size() % sizeof(std::int16_t) != 0 ||
      sample_bytes.size() % *bytes_per_packet != 0) {
    return std::nullopt;
  }
  AudioBuffer result{*sample_rate, static_cast<std::uint16_t>(*channel_count), {}};
  result.samples.reserve(sample_bytes.size() / sizeof(std::int16_t));
  for (std::size_t index = 0; index < sample_bytes.size();
       index += sizeof(std::int16_t)) {
    const auto value = static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(sample_bytes[index]) |
        (std::to_integer<std::uint16_t>(sample_bytes[index + 1U]) << 8U));
    result.samples.push_back(std::bit_cast<std::int16_t>(value));
  }
  return result;
}

AudioBuffer apply_volume(AudioBuffer buffer, float volume) {
  for (auto &sample : buffer.samples) {
    const auto scaled = std::lround(static_cast<float>(sample) * volume);
    sample = static_cast<std::int16_t>(std::clamp<long>(
        scaled, std::numeric_limits<std::int16_t>::min(),
        std::numeric_limits<std::int16_t>::max()));
  }
  return buffer;
}

} // namespace

AudioSubsystem::AudioSubsystem(std::filesystem::path rootfs)
    : rootfs_{std::move(rootfs)},
      system_sounds_{{audio_toolbox::lock_system_sound_id,
                      "/System/Library/Audio/UISounds/lock.caf"},
                     {audio_toolbox::unlock_system_sound_id,
                      "/System/Library/Audio/UISounds/unlock.caf"}} {}

void AudioSubsystem::set_sink(std::shared_ptr<AudioSink> sink) {
  std::lock_guard lock{mutex_};
  sink_ = std::move(sink);
}

AudioPlayResult AudioSubsystem::play_system_sound(std::uint32_t sound_id) {
  std::shared_ptr<AudioSink> sink;
  std::optional<AudioBuffer> buffer;
  AudioPlayResult result;
  float current_volume = 1.0F;
  {
    std::lock_guard lock{mutex_};
    buffer = load_sound_locked(sound_id, result);
    sink = sink_;
    current_volume = volume_;
  }
  if (!buffer)
    return result;
  if (!sink) {
    result.status = AudioPlayStatus::NoSink;
    result.detail = "no host audio sink";
    return result;
  }
  if (!sink->play(apply_volume(std::move(*buffer),
                               muted() ? 0.0F : current_volume))) {
    result.status = AudioPlayStatus::SinkError;
    result.detail = sink->last_error();
    return result;
  }
  result.status = AudioPlayStatus::Queued;
  return result;
}

float AudioSubsystem::volume() const {
  std::lock_guard lock{mutex_};
  return volume_;
}

float AudioSubsystem::change_volume(float delta) {
  std::lock_guard lock{mutex_};
  volume_ = std::clamp(volume_ + delta, 0.0F, 1.0F);
  return volume_;
}

float AudioSubsystem::set_volume(float value) {
  std::lock_guard lock{mutex_};
  volume_ = std::clamp(value, 0.0F, 1.0F);
  return volume_;
}

bool AudioSubsystem::muted() const {
  std::lock_guard lock{mutex_};
  return muted_;
}

bool AudioSubsystem::toggle_muted() {
  std::lock_guard lock{mutex_};
  muted_ = !muted_;
  return muted_;
}

std::optional<AudioBuffer>
AudioSubsystem::load_sound_locked(std::uint32_t sound_id,
                                  AudioPlayResult &result) {
  const auto sound = system_sounds_.find(sound_id);
  if (sound == system_sounds_.end()) {
    result.status = AudioPlayStatus::UnknownSound;
    result.detail = "unregistered system sound ID";
    return std::nullopt;
  }
  result.guest_path = sound->second;
  if (const auto cached = decoded_sounds_.find(sound_id);
      cached != decoded_sounds_.end()) {
    return cached->second;
  }
  auto relative = sound->second.relative_path();
  const auto bytes = read_file(rootfs_ / relative);
  if (!bytes) {
    result.status = AudioPlayStatus::ResourceUnavailable;
    result.detail = "system sound resource is unavailable";
    return std::nullopt;
  }
  auto decoded = decode_linear_pcm_caf(*bytes);
  if (!decoded) {
    result.status = AudioPlayStatus::UnsupportedResource;
    result.detail = "unsupported CAF encoding";
    return std::nullopt;
  }
  auto [entry, inserted] =
      decoded_sounds_.emplace(sound_id, std::move(*decoded));
  static_cast<void>(inserted);
  return entry->second;
}

} // namespace ilegacysim
