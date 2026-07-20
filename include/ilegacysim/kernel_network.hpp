#pragma once

#include <string_view>

#include "ilegacysim/darwin_network_abi.hpp"
#include "ilegacysim/kernel_shared_state.hpp"

namespace ilegacysim::kernel_network {

[[nodiscard]] darwin::network::InterfaceSnapshot make_interface_snapshot(
    std::string_view name,
    const KernelSharedState::NetworkInterface& interface);

}  // namespace ilegacysim::kernel_network
