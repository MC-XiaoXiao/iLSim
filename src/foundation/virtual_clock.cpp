#include "ilegacysim/virtual_clock.hpp"

namespace ilegacysim {

std::uint64_t VirtualClock::now() const {
    return now_.load(std::memory_order_relaxed);
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
