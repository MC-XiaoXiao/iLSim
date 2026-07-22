#include "ilegacysim/presentation_tracker.hpp"

#include <utility>

namespace ilegacysim {

std::uint64_t
PresentationTracker::record(std::uint32_t submitting_process_id,
                            std::vector<PresentationLayer> layers) {
  std::lock_guard lock{mutex_};
  const auto sequence = next_sequence_++;
  if (next_sequence_ == 0U)
    next_sequence_ = 1U;
  latest_ = PresentationFrame{sequence, submitting_process_id,
                              std::move(layers)};
  return sequence;
}

std::optional<PresentationFrame> PresentationTracker::latest() const {
  std::lock_guard lock{mutex_};
  return latest_;
}

} // namespace ilegacysim
