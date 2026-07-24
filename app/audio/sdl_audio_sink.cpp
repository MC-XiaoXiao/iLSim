#include "sdl_audio_sink.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#if defined(ILEGACYSIM_HAS_SDL2)
#include <SDL.h>
#endif

namespace ilegacysim {

struct SdlAudioSink::Impl {
  mutable std::mutex mutex;
  std::string error;
#if defined(ILEGACYSIM_HAS_SDL2)
  SDL_AudioDeviceID device{};
  SDL_AudioSpec format{};
  SDL_AudioStream *streaming_converter{};
  std::uint32_t streaming_sample_rate{};
  std::uint16_t streaming_channel_count{};
  bool owns_audio_subsystem{};
  std::vector<std::int16_t> queued_samples;
  std::size_t next_sample{};
  std::size_t fade_frames_remaining{};
  std::size_t fade_frames_total{};
  float gain{1.0F};

  static void consume(void *userdata, Uint8 *output, int byte_count) {
    if (userdata == nullptr || output == nullptr || byte_count <= 0)
      return;
    std::memset(output, 0, static_cast<std::size_t>(byte_count));
    auto &state = *static_cast<Impl *>(userdata);
    std::lock_guard lock{state.mutex};
    const auto output_samples =
        static_cast<std::size_t>(byte_count) / sizeof(std::int16_t);
    const auto available = state.queued_samples.size() - state.next_sample;
    const auto count = std::min(output_samples, available);
    auto *samples = reinterpret_cast<std::int16_t *>(output);
    const auto channels =
        std::max<std::size_t>(1U, state.format.channels);
    const auto fade_frames =
        std::min(state.fade_frames_remaining, count / channels);
    const auto fade_samples = fade_frames * channels;
    for (std::size_t frame = 0; frame < fade_frames; ++frame) {
      const auto remaining = state.fade_frames_remaining - frame;
      const auto envelope =
          state.fade_frames_total > 1U
              ? static_cast<float>(remaining - 1U) /
                    static_cast<float>(state.fade_frames_total - 1U)
              : 0.0F;
      for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto index = frame * channels + channel;
        const auto scaled = std::lround(
            static_cast<float>(
                state.queued_samples[state.next_sample + index]) *
            state.gain * envelope);
        samples[index] = static_cast<std::int16_t>(std::clamp<long>(
            scaled, std::numeric_limits<std::int16_t>::min(),
            std::numeric_limits<std::int16_t>::max()));
      }
    }
    state.fade_frames_remaining -= fade_frames;
    if (state.fade_frames_remaining == 0)
      state.fade_frames_total = 0;
    for (std::size_t index = fade_samples; index < count; ++index) {
      const auto scaled = std::lround(
          static_cast<float>(state.queued_samples[state.next_sample + index]) *
          state.gain);
      samples[index] = static_cast<std::int16_t>(std::clamp<long>(
          scaled, std::numeric_limits<std::int16_t>::min(),
          std::numeric_limits<std::int16_t>::max()));
    }
    state.next_sample += count;
    if (state.next_sample == state.queued_samples.size()) {
      state.queued_samples.clear();
      state.next_sample = 0;
      state.fade_frames_remaining = 0;
      state.fade_frames_total = 0;
    }
  }
#endif
};

SdlAudioSink::SdlAudioSink() : impl_{std::make_unique<Impl>()} {}

SdlAudioSink::~SdlAudioSink() {
#if defined(ILEGACYSIM_HAS_SDL2)
  SDL_AudioDeviceID device = 0;
  SDL_AudioStream *streaming_converter = nullptr;
  bool owns_audio_subsystem = false;
  {
    std::lock_guard lock{impl_->mutex};
    device = std::exchange(impl_->device, 0);
    streaming_converter =
        std::exchange(impl_->streaming_converter, nullptr);
    owns_audio_subsystem = std::exchange(impl_->owns_audio_subsystem, false);
  }
  if (streaming_converter != nullptr)
    SDL_FreeAudioStream(streaming_converter);
  if (device != 0)
    SDL_CloseAudioDevice(device);
  if (owns_audio_subsystem)
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
}

bool SdlAudioSink::available() {
#if defined(ILEGACYSIM_HAS_SDL2)
  return true;
#else
  return false;
#endif
}

bool SdlAudioSink::play(const AudioBuffer &buffer) {
#if defined(ILEGACYSIM_HAS_SDL2)
  std::unique_lock lock{impl_->mutex};
  if (buffer.sample_rate == 0 || buffer.channel_count == 0 ||
      buffer.samples.empty()) {
    impl_->error = "invalid empty audio buffer";
    return false;
  }
  bool resume = false;
  if (impl_->device == 0) {
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0U) {
      if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        impl_->error = SDL_GetError();
        return false;
      }
      impl_->owns_audio_subsystem = true;
    }
    SDL_AudioSpec requested{};
    requested.freq = 48000;
    requested.format = AUDIO_S16SYS;
    requested.channels = 2;
    requested.samples = 1024;
    requested.callback = &Impl::consume;
    requested.userdata = impl_.get();
    impl_->device = SDL_OpenAudioDevice(nullptr, 0, &requested, &impl_->format,
                                        0);
    if (impl_->device == 0) {
      impl_->error = SDL_GetError();
      return false;
    }
    resume = true;
  }

  SDL_AudioStream *owned_stream = nullptr;
  SDL_AudioStream *stream = nullptr;
  if (buffer.streaming) {
    if (impl_->streaming_converter != nullptr &&
        (impl_->streaming_sample_rate != buffer.sample_rate ||
         impl_->streaming_channel_count != buffer.channel_count)) {
      SDL_FreeAudioStream(impl_->streaming_converter);
      impl_->streaming_converter = nullptr;
    }
    if (impl_->streaming_converter == nullptr) {
      impl_->streaming_converter = SDL_NewAudioStream(
          AUDIO_S16SYS, static_cast<Uint8>(buffer.channel_count),
          static_cast<int>(buffer.sample_rate), impl_->format.format,
          impl_->format.channels, impl_->format.freq);
      impl_->streaming_sample_rate = buffer.sample_rate;
      impl_->streaming_channel_count = buffer.channel_count;
    }
    stream = impl_->streaming_converter;
  } else {
    owned_stream = SDL_NewAudioStream(
        AUDIO_S16SYS, static_cast<Uint8>(buffer.channel_count),
        static_cast<int>(buffer.sample_rate), impl_->format.format,
        impl_->format.channels, impl_->format.freq);
    stream = owned_stream;
  }
  if (stream == nullptr) {
    impl_->error = SDL_GetError();
    return false;
  }
  const auto source_bytes = buffer.samples.size() * sizeof(std::int16_t);
  if (source_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      SDL_AudioStreamPut(stream, buffer.samples.data(),
                         static_cast<int>(source_bytes)) != 0 ||
      (!buffer.streaming && SDL_AudioStreamFlush(stream) != 0)) {
    impl_->error = SDL_GetError();
    if (owned_stream != nullptr)
      SDL_FreeAudioStream(owned_stream);
    return false;
  }
  const auto converted_size = SDL_AudioStreamAvailable(stream);
  if (converted_size < 0) {
    impl_->error = SDL_GetError();
    if (owned_stream != nullptr)
      SDL_FreeAudioStream(owned_stream);
    return false;
  }
  if (converted_size == 0) {
    if (owned_stream != nullptr)
      SDL_FreeAudioStream(owned_stream);
    impl_->error.clear();
    return true;
  }
  if (converted_size % static_cast<int>(sizeof(std::int16_t)) != 0) {
    impl_->error = "audio conversion is not 16-bit aligned";
    if (owned_stream != nullptr)
      SDL_FreeAudioStream(owned_stream);
    return false;
  }
  std::vector<std::int16_t> converted(
      static_cast<std::size_t>(converted_size) / sizeof(std::int16_t));
  const auto received =
      SDL_AudioStreamGet(stream, converted.data(), converted_size);
  if (owned_stream != nullptr)
    SDL_FreeAudioStream(owned_stream);
  if (received != converted_size) {
    impl_->error = SDL_GetError();
    return false;
  }
  if (impl_->next_sample != 0) {
    impl_->queued_samples.erase(
        impl_->queued_samples.begin(),
        impl_->queued_samples.begin() +
            static_cast<std::ptrdiff_t>(impl_->next_sample));
    impl_->next_sample = 0;
  }
  impl_->queued_samples.insert(impl_->queued_samples.end(), converted.begin(),
                               converted.end());
  const auto device = impl_->device;
  impl_->error.clear();
  lock.unlock();
  if (resume)
    SDL_PauseAudioDevice(device, 0);
  return true;
#else
  std::lock_guard lock{impl_->mutex};
  static_cast<void>(buffer);
  impl_->error = "SDL2 audio support was not built";
  return false;
#endif
}

bool SdlAudioSink::has_pending_audio() const {
  std::lock_guard lock{impl_->mutex};
#if defined(ILEGACYSIM_HAS_SDL2)
  return impl_->next_sample < impl_->queued_samples.size();
#else
  return false;
#endif
}

void SdlAudioSink::set_gain(float gain) {
  std::lock_guard lock{impl_->mutex};
#if defined(ILEGACYSIM_HAS_SDL2)
  impl_->gain = std::clamp(gain, 0.0F, 1.0F);
#else
  static_cast<void>(gain);
#endif
}

void SdlAudioSink::stop(AudioStopMode mode) {
  std::lock_guard lock{impl_->mutex};
#if defined(ILEGACYSIM_HAS_SDL2)
  if (impl_->streaming_converter != nullptr)
    SDL_AudioStreamClear(impl_->streaming_converter);
  if (mode == AudioStopMode::FadeOut && impl_->device != 0 &&
      impl_->next_sample < impl_->queued_samples.size()) {
    const auto channels =
        std::max<std::size_t>(1U, impl_->format.channels);
    if (impl_->fade_frames_remaining != 0) {
      impl_->queued_samples.resize(
          impl_->next_sample + impl_->fade_frames_remaining * channels);
      return;
    }
    constexpr std::size_t fade_duration_milliseconds = 40;
    const auto available_frames =
        (impl_->queued_samples.size() - impl_->next_sample) / channels;
    const auto requested_frames =
        static_cast<std::size_t>(impl_->format.freq) *
        fade_duration_milliseconds / 1000U;
    const auto fade_frames = std::min(available_frames, requested_frames);
    if (fade_frames != 0) {
      impl_->queued_samples.resize(impl_->next_sample +
                                   fade_frames * channels);
      impl_->fade_frames_remaining = fade_frames;
      impl_->fade_frames_total = fade_frames;
      return;
    }
  }
  impl_->queued_samples.clear();
  impl_->next_sample = 0;
  impl_->fade_frames_remaining = 0;
  impl_->fade_frames_total = 0;
#else
  static_cast<void>(mode);
#endif
}

std::string SdlAudioSink::last_error() const {
  std::lock_guard lock{impl_->mutex};
  return impl_->error;
}

} // namespace ilegacysim
