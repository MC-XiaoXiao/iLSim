#pragma once

#include <cstdint>

namespace ilegacysim {

enum class TouchPhase : std::uint8_t {
  Down,
  Move,
  Up,
  Cancel,
};

struct TouchInput {
  TouchPhase phase{TouchPhase::Down};
  float x{};
  float y{};
};

} // namespace ilegacysim
