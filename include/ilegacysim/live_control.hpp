#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "ilegacysim/touch_input.hpp"

namespace ilegacysim {

enum class LiveControlCommandKind {
  Touch,
  Gesture,
  Wake,
  Lock,
  VolumeUp,
  VolumeDown,
  Snapshot,
  SnapshotSequence,
  Status,
  Help,
  Quit,
  Error,
};

struct LiveTouchEvent {
  std::chrono::milliseconds delay{};
  TouchInput input;
};

struct LiveControlCommand {
  LiveControlCommandKind kind{LiveControlCommandKind::Error};
  TouchInput touch;
  std::vector<LiveTouchEvent> gesture;
  bool wake_display{};
  std::filesystem::path path;
  std::chrono::milliseconds snapshot_interval{};
  std::size_t snapshot_count{};
  std::string message;
};

// Non-blocking line-oriented control channel used by headless interactive
// sessions. The descriptor remains owned by the caller.
class LiveControl {
public:
  explicit LiveControl(int descriptor);

  [[nodiscard]] std::vector<LiveControlCommand> poll();
  [[nodiscard]] bool closed() const { return closed_; }

private:
  [[nodiscard]] std::vector<LiveControlCommand> parse_line(std::string line);

  int descriptor_{};
  std::string buffered_input_;
  bool closed_{};
};

} // namespace ilegacysim
