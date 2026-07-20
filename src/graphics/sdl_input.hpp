#pragma once

#include <vector>

#include "ilegacysim/system_button_input.hpp"
#include "ilegacysim/touch_input.hpp"

struct SDL_Window;

namespace ilegacysim {

class SdlInput {
public:
  [[nodiscard]] bool poll(SDL_Window *window);
  [[nodiscard]] std::vector<TouchInput> take_touch_events();
  [[nodiscard]] std::vector<SystemButtonInput> take_button_events();

private:
  std::vector<TouchInput> touch_events_;
  std::vector<SystemButtonInput> button_events_;
  bool mouse_active_{};
  bool running_{true};
};

} // namespace ilegacysim
