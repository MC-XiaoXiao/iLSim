#pragma once

#include <atomic>
#include <cstdint>

namespace ilegacysim {

// Process-shared deterministic monotonic time used by guest timer waits.  It
// is independent from host wall time so boot traces and tests are repeatable.
class VirtualClock {
public:
    static constexpr std::uint64_t default_initial_time = 1'000'000;

    explicit VirtualClock(std::uint64_t initial_time = default_initial_time)
        : now_{initial_time} {}

    [[nodiscard]] std::uint64_t now() const;
    std::uint64_t tick(std::uint64_t increment);
    void advance_to(std::uint64_t deadline);

private:
    std::atomic_uint64_t now_;
};

}  // namespace ilegacysim
