#pragma once

#include <vector>

#include "ilegacysim/display_geometry.hpp"
#include "ilegacysim/ringer_switch_state.hpp"
#include "ilegacysim/system_button_input.hpp"
#include "ilegacysim/touch_input.hpp"

struct SDL_Window;

namespace ilegacysim {

class SdlInput {
public:
  explicit SdlInput(DisplayGeometry geometry) : geometry_{geometry} {}
  [[nodiscard]] bool poll(SDL_Window *window);
  [[nodiscard]] std::vector<TouchInput> take_touch_events();
  [[nodiscard]] std::vector<SystemButtonInput> take_button_events();
  [[nodiscard]] std::vector<RingerSwitchInput>
  take_ringer_switch_events();

private:
  DisplayGeometry geometry_;
  std::vector<TouchInput> touch_events_;
  std::vector<SystemButtonInput> button_events_;
  std::vector<RingerSwitchInput> ringer_switch_events_;
  bool mouse_active_{};
  bool running_{true};
};

} // namespace ilegacysim
