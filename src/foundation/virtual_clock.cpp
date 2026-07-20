#include "ilegacysim/virtual_clock.hpp"

#include <limits>

namespace ilegacysim {

std::uint64_t VirtualClock::now() const {
    return now_.load(std::memory_order_relaxed);
}

std::uint64_t VirtualClock::wall_time() const {
    const auto monotonic_time = now();
    const auto offset = wall_time_offset_.load(std::memory_order_relaxed);
    if (monotonic_time > std::numeric_limits<std::uint64_t>::max() - offset) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return offset + monotonic_time;
}

void VirtualClock::synchronize_wall_time(
    std::uint64_t unix_time_nanoseconds) {
    const auto monotonic_time = now();
    wall_time_offset_.store(
        unix_time_nanoseconds > monotonic_time
            ? unix_time_nanoseconds - monotonic_time
            : 0,
        std::memory_order_relaxed);
}

std::uint64_t VirtualClock::tick(std::uint64_t increment) {
    return now_.fetch_add(increment, std::memory_order_relaxed) + increment;
}

void VirtualClock::advance_to(std::uint64_t deadline) {
    auto current = now();
    while (current < deadline &&
           !now_.compare_exchange_weak(current, deadline,
                                       std::memory_order_relaxed)) {
    }
}

}  // namespace ilegacysim
