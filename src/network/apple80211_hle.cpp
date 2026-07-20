#include "ilegacysim/apple80211_hle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "ilegacysim/address_space.hpp"
#include "ilegacysim/userland_hle.hpp"

namespace ilegacysim {
namespace {

constexpr std::string_view aeropuerto_image{
    "/System/Library/SystemConfiguration/Aeropuerto.bundle/Aeropuerto"};
constexpr std::string_view interface_name{"en0"};

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
        // The iPhone 2G exposes a single 802.11 interface. The CFString is
        // validated by the caller; avoiding CF object fabrication keeps this
        // HLE at the Aeropuerto ABI boundary.
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
