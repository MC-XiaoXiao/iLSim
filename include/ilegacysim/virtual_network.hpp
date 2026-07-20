#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ilegacysim::virtual_network {

using Ipv4Address = std::array<std::byte, 4>;

inline constexpr Ipv4Address client_address{
    std::byte{10}, std::byte{0}, std::byte{2}, std::byte{15}};
inline constexpr Ipv4Address netmask{
    std::byte{255}, std::byte{255}, std::byte{255}, std::byte{0}};
inline constexpr Ipv4Address broadcast_address{
    std::byte{10}, std::byte{0}, std::byte{2}, std::byte{255}};
inline constexpr Ipv4Address gateway_address{
    std::byte{10}, std::byte{0}, std::byte{2}, std::byte{2}};
inline constexpr Ipv4Address dns_proxy_address{
    std::byte{10}, std::byte{0}, std::byte{2}, std::byte{3}};
inline constexpr std::uint16_t dns_port = 53;

}  // namespace ilegacysim::virtual_network
