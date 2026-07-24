#pragma once

#include <memory>
#include <vector>

#include "ilegacysim/display_geometry.hpp"
#include "ilegacysim/ringer_switch_state.hpp"
#include "ilegacysim/system_button_input.hpp"
#include "ilegacysim/touch_input.hpp"

namespace ilegacysim {

struct DisplayFrame;

class SdlDisplay {
public:
  SdlDisplay(DisplayGeometry frame_geometry,
             DisplayGeometry input_geometry);
  ~SdlDisplay();
  SdlDisplay(const SdlDisplay &) = delete;
  SdlDisplay &operator=(const SdlDisplay &) = delete;

  [[nodiscard]] static bool available();
  void present(const DisplayFrame &frame);
  // Returns false after the user closes the window.
  [[nodiscard]] bool poll_events();
  [[nodiscard]] std::vector<TouchInput> take_touch_events();
  [[nodiscard]] std::vector<SystemButtonInput> take_button_events();
  [[nodiscard]] std::vector<RingerSwitchInput>
  take_ringer_switch_events();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ilegacysim
