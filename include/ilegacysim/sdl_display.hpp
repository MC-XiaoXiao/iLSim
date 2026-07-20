#pragma once

#include <memory>
#include <vector>

#include "ilegacysim/system_button_input.hpp"
#include "ilegacysim/touch_input.hpp"

namespace ilegacysim {

struct DisplayFrame;

class SdlDisplay {
public:
  SdlDisplay();
  ~SdlDisplay();
  SdlDisplay(const SdlDisplay &) = delete;
  SdlDisplay &operator=(const SdlDisplay &) = delete;

  [[nodiscard]] static bool available();
  void present(const DisplayFrame &frame);
  // Returns false after the user closes the window.
  [[nodiscard]] bool poll_events();
  [[nodiscard]] std::vector<TouchInput> take_touch_events();
  [[nodiscard]] std::vector<SystemButtonInput> take_button_events();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ilegacysim
