#pragma once

#include <chrono>
#include <deque>
#include <span>
#include <vector>

#include "ilegacysim/live_control.hpp"
#include "ilegacysim/touch_input.hpp"

namespace ilegacysim {

// Replays a complete live-control gesture against host steady time. Keeping
// this separate from command parsing makes multi-point gestures independent of
// terminal read chunking and guest scheduling speed.
class LiveTouchScheduler {
public:
  void schedule(std::span<const LiveTouchEvent> gesture);
  [[nodiscard]] std::vector<TouchInput> poll();
  [[nodiscard]] bool empty() const { return events_.empty(); }

private:
  struct Event {
    std::chrono::steady_clock::time_point deadline;
    TouchInput input;
  };

  std::deque<Event> events_;
};

} // namespace ilegacysim
