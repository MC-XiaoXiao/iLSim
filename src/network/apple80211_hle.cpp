#include "ilegacysim/apple80211_hle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "ilegacysim/address_space.hpp"
#include "ilegacysim/cpu.hpp"
#include "ilegacysim/userland_hle.hpp"

namespace ilegacysim {
namespace {

constexpr std::string_view aeropuerto_image{
    "/System/Library/SystemConfiguration/Aeropuerto.bundle/Aeropuerto"};
constexpr std::string_view springboard_image{
    "/System/Library/CoreServices/SpringBoard.app/SpringBoard"};
constexpr std::string_view interface_name{"en0"};

// The 1.0 SpringBoard status controller is stripped, so these are its stable
// preferred Mach-O addresses.  The native Aeropuerto bundle discovers Wi-Fi
// devices through an IO80211 service which the simulator intentionally does
// not expose.  Keep the stock status-controller implementation, but feed it
// the association already represented by WifiState.
constexpr std::uint32_t springboard_set_airport_strength{0x00029e24U};
constexpr std::uint32_t springboard_set_shows_airport{0x00029e88U};
constexpr std::uint32_t springboard_shows_airport{0x00029ef4U};
constexpr std::uint32_t springboard_airport_strength{0x00029f1cU};
constexpr std::uint32_t springboard_reflow_status_bar{0x00027fecU};

constexpr std::uint32_t status_bar_controller_flags_offset{0x2dU};
constexpr std::uint32_t status_bar_controller_strength_offset{0x44U};
constexpr std::uint32_t status_bar_contents_airport_view_offset{0x40U};
constexpr std::uint32_t airport_view_strength_offset{0x24U};
constexpr std::uint32_t airport_view_flags_offset{0x28U};

bool wifi_is_configured(const WifiSnapshot& snapshot) {
    return snapshot.powered && snapshot.associated_access_point.has_value() &&
           snapshot.ipv4.has_value();
}

std::uint32_t wifi_signal_bars(const WifiSnapshot& snapshot) {
    if (!wifi_is_configured(snapshot)) return 0;
    const auto rssi = snapshot.associated_access_point->rssi;
    if (rssi >= -55) return 3;
    if (rssi >= -70) return 2;
    return 1;
}

void synchronize_status_bar_view(UserlandHleCall& call,
                                 const WifiSnapshot& snapshot) {
    const auto configured = wifi_is_configured(snapshot);
    const auto bars = wifi_signal_bars(snapshot);
    const auto contents = call.argument(0);
    const auto controller = call.memory().read32(contents + 0x1cU).value_or(0);
    const auto airport_view = call.memory()
                                  .read32(contents +
                                          status_bar_contents_airport_view_offset)
                                  .value_or(0);
    if (controller != 0) {
        if (const auto flags = call.memory().read8(
                controller + status_bar_controller_flags_offset)) {
            const auto value = configured
                                   ? static_cast<std::uint8_t>(*flags | 0x02U)
                                   : static_cast<std::uint8_t>(*flags & ~0x02U);
            static_cast<void>(call.memory().write8(
                controller + status_bar_controller_flags_offset, value));
        }
        static_cast<void>(call.memory().write32(
            controller + status_bar_controller_strength_offset, bars));
    }
    if (airport_view != 0) {
        if (const auto flags = call.memory().read8(
                airport_view + airport_view_flags_offset)) {
            const auto value = configured
                                   ? static_cast<std::uint8_t>(*flags | 0x01U)
                                   : static_cast<std::uint8_t>(*flags & ~0x01U);
            static_cast<void>(call.memory().write8(
                airport_view + airport_view_flags_offset, value));
        }
        static_cast<void>(call.memory().write32(
            airport_view + airport_view_strength_offset, bars));
    }
}

}  // namespace

Apple80211Hle::Apple80211Hle(
    UserlandHleRegistry& registry, std::shared_ptr<WifiState> state,
    StateChangedHandler state_changed)
    : state_{state ? std::move(state) : std::make_shared<WifiState>()},
      state_changed_{std::move(state_changed)} {
    const auto add = [&](std::string symbol, UserlandHleRegistry::Handler handler) {
        registry.register_function(
            std::string{aeropuerto_image}, std::move(symbol),
            std::move(handler));
    };

    add("_Apple80211Open", [this](UserlandHleCall& call) {
        const auto output = call.argument(0);
        if (output == 0) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        const auto handle = call.allocate_data(
            apple80211_abi::handle_size, alignof(std::uint32_t));
        if (handle == 0) {
            static_cast<void>(call.write32(output, 0));
            call.set_return(apple80211_abi::allocation_failure);
            return;
        }
        std::array<std::byte, apple80211_abi::handle_size> bytes{};
        const auto descriptor = next_virtual_descriptor_++;
        for (std::size_t index = 0; index < sizeof(descriptor); ++index) {
            bytes[index] = static_cast<std::byte>(
                (descriptor >> (index * 8U)) & 0xffU);
        }
        for (std::size_t index = 0; index < interface_name.size(); ++index) {
            bytes[apple80211_abi::interface_name_offset + index] =
                static_cast<std::byte>(interface_name[index]);
        }
        if (!call.memory().copy_in(handle, bytes) ||
            !call.write32(output, handle)) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        process_handles_[call.process_id()].insert(handle);
        call.set_return(apple80211_abi::success);
    });

    add("_Apple80211Close", [this](UserlandHleCall& call) {
        const auto handle = call.argument(0);
        auto process = process_handles_.find(call.process_id());
        if (process == process_handles_.end() ||
            process->second.erase(handle) == 0) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        call.set_return(apple80211_abi::success);
    });

    add("_Apple80211BindToInterface", [this](UserlandHleCall& call) {
        const auto handle = call.argument(0);
        if (!valid_handle(call, handle) || call.argument(1) == 0) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        // The selected target firmware exposes a single 802.11 interface. The
        // CFString is validated by the caller; avoiding CF object fabrication
        // keeps this HLE at the Aeropuerto ABI boundary.
        std::array<std::byte, apple80211_abi::interface_name_capacity> bytes{};
        for (std::size_t index = 0; index < interface_name.size(); ++index) {
            bytes[index] = static_cast<std::byte>(interface_name[index]);
        }
        if (!call.memory().copy_in(
                handle + apple80211_abi::interface_name_offset, bytes)) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        call.set_return(apple80211_abi::success);
    });

    add("_Apple80211GetPower", [this](UserlandHleCall& call) {
        if (!valid_handle(call, call.argument(0)) || call.argument(1) == 0 ||
            !call.memory().write8(
                call.argument(1), state_->snapshot().powered ? 1U : 0U)) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        call.set_return(apple80211_abi::success);
    });

    add("_Apple80211SetPower", [this](UserlandHleCall& call) {
        if (!valid_handle(call, call.argument(0))) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        const auto before = state_->snapshot();
        const auto powered = call.argument(1) != 0;
        static_cast<void>(state_->set_power(powered));
        if (powered) {
            // Network support is the compatibility goal, not RF simulation:
            // presenting the radio as enabled also completes association and
            // the deterministic virtual DHCP lease.
            static_cast<void>(state_->associate());
        }
        notify_change(before);
        call.set_return(apple80211_abi::success);
    });

    add("_Apple80211Associate", [this](UserlandHleCall& call) {
        if (!valid_handle(call, call.argument(0)) || call.argument(1) == 0) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        const auto before = state_->snapshot();
        if (!state_->associate()) {
            call.set_return(apple80211_abi::invalid_argument);
            return;
        }
        notify_change(before);
        call.set_return(apple80211_abi::success);
    });

    registry.register_address(
        std::string{springboard_image}, springboard_set_shows_airport,
        "-[SBStatusBarController setShowsAirPort:]",
        [this](UserlandHleCall& call) {
            if (wifi_is_configured(state_->snapshot())) {
                call.cpu().registers()[2] = 1;
            }
            call.resume_original_persistently();
        });
    registry.register_address(
        std::string{springboard_image}, springboard_set_airport_strength,
        "-[SBStatusBarController setAirPortStrength:]",
        [this](UserlandHleCall& call) {
            const auto bars = wifi_signal_bars(state_->snapshot());
            if (bars != 0) call.cpu().registers()[2] = bars;
            call.resume_original_persistently();
        });
    registry.register_address(
        std::string{springboard_image}, springboard_shows_airport,
        "-[SBStatusBarController showsAirPort]",
        [this](UserlandHleCall& call) {
            call.set_return(wifi_is_configured(state_->snapshot()) ? 1U : 0U);
        });
    registry.register_address(
        std::string{springboard_image}, springboard_airport_strength,
        "-[SBStatusBarController airPortStrength]",
        [this](UserlandHleCall& call) {
            call.set_return(wifi_signal_bars(state_->snapshot()));
        });
    registry.register_address(
        std::string{springboard_image}, springboard_reflow_status_bar,
        "-[SBStatusBarContentsView reflowContentViews]",
        [this](UserlandHleCall& call) {
            synchronize_status_bar_view(call, state_->snapshot());
            call.resume_original_persistently();
        });
}

void Apple80211Hle::reset() {
    process_handles_.clear();
    next_virtual_descriptor_ = 100;
}

void Apple80211Hle::inherit_state(
    const Apple80211Hle& parent, std::uint32_t parent_process_id,
    std::uint32_t child_process_id) {
    process_handles_.clear();
    if (const auto handles = parent.process_handles_.find(parent_process_id);
        handles != parent.process_handles_.end()) {
        process_handles_.emplace(child_process_id, handles->second);
    }
    next_virtual_descriptor_ = parent.next_virtual_descriptor_;
}

void Apple80211Hle::set_wifi_state(std::shared_ptr<WifiState> state) {
    state_ = state ? std::move(state) : std::make_shared<WifiState>();
}

bool Apple80211Hle::valid_handle(
    const UserlandHleCall& call, std::uint32_t handle) const {
    const auto process = process_handles_.find(call.process_id());
    return handle != 0 && process != process_handles_.end() &&
           process->second.contains(handle);
}

void Apple80211Hle::notify_change(const WifiSnapshot& before) {
    const auto after = state_->snapshot();
    if (state_changed_) state_changed_(before, after);
}

}  // namespace ilegacysim
