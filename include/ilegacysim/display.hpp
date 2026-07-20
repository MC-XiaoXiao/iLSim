#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace ilegacysim {

inline constexpr std::uint32_t iphone_2g_display_width = 320;
inline constexpr std::uint32_t iphone_2g_display_height = 480;

struct DisplayFrame {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t sequence{};
  // Host-endian 0xAARRGGBB pixels. Backends perform any required upload
  // format conversion without exposing it to the guest graphics HLE.
  std::vector<std::uint32_t> pixels;
};

class DisplayState {
public:
  using Presenter = std::function<void(const DisplayFrame &)>;

  DisplayState();

  void set_presenter(Presenter presenter);
  void clear(std::uint32_t argb);
  void replace_pixels(std::vector<std::uint32_t> pixels);
  // The framebuffer keeps its last scanout contents while the LCD is off.
  // Presenters and snapshots expose a black panel until power is restored.
  void set_powered_on(bool powered_on);
  void present();

  [[nodiscard]] DisplayFrame snapshot() const;
  [[nodiscard]] std::uint64_t presented_frames() const;
  [[nodiscard]] bool powered_on() const;

private:
  mutable std::mutex mutex_;
  std::vector<std::uint32_t> pixels_;
  Presenter presenter_;
  std::uint64_t sequence_{};
  bool powered_on_{true};
};

} // namespace ilegacysim
