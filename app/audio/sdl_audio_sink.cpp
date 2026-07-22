#include "sdl_audio_sink.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
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
  bool owns_audio_subsystem{};
#endif
};

SdlAudioSink::SdlAudioSink() : impl_{std::make_unique<Impl>()} {}

SdlAudioSink::~SdlAudioSink() {
#if defined(ILEGACYSIM_HAS_SDL2)
  std::lock_guard lock{impl_->mutex};
  if (impl_->device != 0)
    SDL_CloseAudioDevice(impl_->device);
  if (impl_->owns_audio_subsystem)
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
  std::lock_guard lock{impl_->mutex};
#if defined(ILEGACYSIM_HAS_SDL2)
  if (buffer.sample_rate == 0 || buffer.channel_count == 0 ||
      buffer.samples.empty()) {
    impl_->error = "invalid empty audio buffer";
    return false;
  }
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
    impl_->device = SDL_OpenAudioDevice(nullptr, 0, &requested, &impl_->format,
                                        SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (impl_->device == 0) {
      impl_->error = SDL_GetError();
      return false;
    }
    SDL_PauseAudioDevice(impl_->device, 0);
  }

  auto *stream = SDL_NewAudioStream(
      AUDIO_S16SYS, static_cast<Uint8>(buffer.channel_count),
      static_cast<int>(buffer.sample_rate), impl_->format.format,
      impl_->format.channels, impl_->format.freq);
  if (stream == nullptr) {
    impl_->error = SDL_GetError();
    return false;
  }
  const auto source_bytes = buffer.samples.size() * sizeof(std::int16_t);
  if (source_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      SDL_AudioStreamPut(stream, buffer.samples.data(),
                         static_cast<int>(source_bytes)) != 0 ||
      SDL_AudioStreamFlush(stream) != 0) {
    impl_->error = SDL_GetError();
    SDL_FreeAudioStream(stream);
    return false;
  }
  const auto converted_size = SDL_AudioStreamAvailable(stream);
  if (converted_size <= 0) {
    impl_->error = converted_size < 0 ? SDL_GetError() : "audio conversion is empty";
    SDL_FreeAudioStream(stream);
    return false;
  }
  std::vector<std::byte> converted(static_cast<std::size_t>(converted_size));
  const auto received =
      SDL_AudioStreamGet(stream, converted.data(), converted_size);
  SDL_FreeAudioStream(stream);
  if (received != converted_size ||
      SDL_QueueAudio(impl_->device, converted.data(),
                     static_cast<Uint32>(converted.size())) != 0) {
    impl_->error = SDL_GetError();
    return false;
  }
  impl_->error.clear();
  return true;
#else
  static_cast<void>(buffer);
  impl_->error = "SDL2 audio support was not built";
  return false;
#endif
}

void SdlAudioSink::stop() {
  std::lock_guard lock{impl_->mutex};
#if defined(ILEGACYSIM_HAS_SDL2)
  if (impl_->device != 0)
    SDL_ClearQueuedAudio(impl_->device);
#endif
}

std::string SdlAudioSink::last_error() const {
  std::lock_guard lock{impl_->mutex};
  return impl_->error;
}

} // namespace ilegacysim
