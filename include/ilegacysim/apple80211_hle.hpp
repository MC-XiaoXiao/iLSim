#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "ilegacysim/wifi_state.hpp"

namespace ilegacysim {

class UserlandHleCall;
class UserlandHleRegistry;

namespace apple80211_abi {

inline constexpr std::uint32_t handle_size = 36;
inline constexpr std::uint32_t socket_descriptor_offset = 0;
inline constexpr std::uint32_t interface_name_offset = 4;
inline constexpr std::uint32_t interface_name_capacity = 16;
inline constexpr std::uint32_t success = 0;
inline constexpr std::uint32_t invalid_argument = 0xffff'ffffU;
inline constexpr std::uint32_t allocation_failure = 0xffff'fffeU;

}  // namespace apple80211_abi

class Apple80211Hle {
public:
    using StateChangedHandler =
        std::function<void(const WifiSnapshot&, const WifiSnapshot&)>;

    Apple80211Hle(UserlandHleRegistry& registry,
                  std::shared_ptr<WifiState> state,
                  StateChangedHandler state_changed = {});

    void reset();
    void inherit_state(const Apple80211Hle& parent,
                       std::uint32_t parent_process_id,
                       std::uint32_t child_process_id);
    void set_wifi_state(std::shared_ptr<WifiState> state);

private:
    [[nodiscard]] bool valid_handle(const UserlandHleCall& call,
                                    std::uint32_t handle) const;
    void notify_change(const WifiSnapshot& before);

    std::shared_ptr<WifiState> state_;
    StateChangedHandler state_changed_;
    std::map<std::uint32_t, std::set<std::uint32_t>> process_handles_;
    std::uint32_t next_virtual_descriptor_{100};
};

}  // namespace ilegacysim
