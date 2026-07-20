#pragma once

#include <atomic>
#include <cstdint>

namespace ilegacysim {

// Process-shared monotonic time used by guest timer waits, plus the Unix-time
// offset used by calendar APIs. Both are deterministic by default; realtime
// boot sessions synchronize the offset and monotonic progression to the host.
class VirtualClock {
public:
    static constexpr std::uint64_t default_initial_time = 1'000'000;
    static constexpr std::uint64_t default_wall_time_epoch_seconds =
        1'180'000'000;
    static constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000;

    explicit VirtualClock(std::uint64_t initial_time = default_initial_time)
        : now_{initial_time} {}

    [[nodiscard]] std::uint64_t now() const;
    [[nodiscard]] std::uint64_t wall_time() const;
    void synchronize_wall_time(std::uint64_t unix_time_nanoseconds);
    std::uint64_t tick(std::uint64_t increment);
    void advance_to(std::uint64_t deadline);

private:
    std::atomic_uint64_t now_;
    std::atomic_uint64_t wall_time_offset_{
        default_wall_time_epoch_seconds * nanoseconds_per_second};
};

}  // namespace ilegacysim
