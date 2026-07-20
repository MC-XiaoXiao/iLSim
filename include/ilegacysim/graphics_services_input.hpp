#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "ilegacysim/kernel_shared_state.hpp"
#include "ilegacysim/system_button_input.hpp"
#include "ilegacysim/touch_input.hpp"

namespace ilegacysim::graphics_services_input {

inline constexpr std::string_view system_event_service{"PurpleSystemEventPort"};
inline constexpr std::uint32_t application_will_resign_active_event_type =
    2002;

enum class EnqueueResult {
  Queued,
  Deferred,
};

struct ServiceResolution {
  std::uint32_t object{};
  std::size_t flushed_events{};
  bool application_event_port{};
  std::string service_name;
};

// Extracts the leading GSEventRecord type from a message with id 123. This is
// shared by input injection and Mach tracing so application lifecycle events
// can be diagnosed without duplicating the private wire offsets.
[[nodiscard]] std::optional<std::uint32_t>
event_type(std::span<const std::byte> message);

// These two functions observe launchd's ordinary bootstrap MIG traffic. The
// caller must hold KernelSharedState::mach_mutex.
void record_bootstrap_lookup_locked(KernelSharedState &state,
                                    std::uint32_t reply_object,
                                    std::string_view service_name);
[[nodiscard]] ServiceResolution record_bootstrap_reply_locked(
    KernelSharedState &state, std::uint32_t reply_object,
    std::span<const KernelSharedState::MachMessage::PortTransfer> transfers);

// Thread-safe host entry point. Input arriving before SpringBoard has resolved
// its event service is retained and flushed as soon as launchd replies.
[[nodiscard]] EnqueueResult enqueue_touch(KernelSharedState &state,
                                          const TouchInput &input);

// A complete Home Down/Up pair wakes a sleeping lock screen before touch.
[[nodiscard]] EnqueueResult
enqueue_system_button(KernelSharedState &state, const SystemButtonInput &input);

// A resolved application port becomes foreground only when LayerKit attaches
// that application's client context; background application services must not
// steal SpringBoard touches.
void activate_resolved_application(KernelSharedState &state,
                                   std::uint32_t process_id);

// Records the scene translation introduced while SpringBoard places the most
// recently resolved remote application context. Resolve its receive owner so
// lifecycle route changes cannot leak geometry between applications.
void record_pending_application_scene_translation(
    KernelSharedState &state, std::int32_t screen_to_client_y);

// HOME/lock temporarily return touch ownership to SpringBoard without losing
// the application port needed after the unlock lifecycle completes.
void suspend_active_application(KernelSharedState &state);

// Observes ordinary SpringBoard-to-application GSEvents while the caller holds
// KernelSharedState::mach_mutex. Foreground lifecycle events resume the saved
// application route after unlock; resign-active events suspend it.
void record_application_lifecycle_event_locked(KernelSharedState &state,
                                               std::uint32_t sender_pid,
                                               std::uint32_t destination,
                                               std::uint32_t event_type,
                                               std::span<const std::uint32_t>
                                                   exit_snapshot_pixels = {});

} // namespace ilegacysim::graphics_services_input
