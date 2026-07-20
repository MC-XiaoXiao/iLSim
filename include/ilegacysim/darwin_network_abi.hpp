#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ilegacysim::darwin::network {

// XNU 792 / Darwin 8 networking constants. These are kept here rather than
// scattered through the syscall dispatcher so both builders and ABI tests use
// the same firmware-facing definitions.
inline constexpr std::uint32_t address_family_unspecified = 0;
inline constexpr std::uint32_t address_family_inet = 2;
inline constexpr std::uint32_t address_family_link = 18;
inline constexpr std::uint32_t address_family_inet6 = 30;

inline constexpr std::uint32_t protocol_ip = 0;
inline constexpr std::uint32_t protocol_udp = 17;
inline constexpr std::uint32_t protocol_ipv6 = 41;
inline constexpr std::uint32_t ip_type_of_service = 3;
inline constexpr std::uint32_t ip_time_to_live = 4;
inline constexpr std::uint32_t ip_receive_destination_address = 7;
inline constexpr std::uint32_t ip_multicast_interface = 9;
inline constexpr std::uint32_t ip_multicast_ttl = 10;
inline constexpr std::uint32_t ip_multicast_loop = 11;
inline constexpr std::uint32_t ip_add_membership = 12;
inline constexpr std::uint32_t ip_drop_membership = 13;
inline constexpr std::uint32_t ip_receive_interface = 20;
inline constexpr std::uint32_t ip_receive_ttl = 24;
inline constexpr std::uint32_t ipv6_unicast_hops = 4;
inline constexpr std::uint32_t ipv6_multicast_interface = 9;
inline constexpr std::uint32_t ipv6_multicast_hops = 10;
inline constexpr std::uint32_t ipv6_multicast_loop = 11;
inline constexpr std::uint32_t ipv6_join_group = 12;
inline constexpr std::uint32_t ipv6_leave_group = 13;
inline constexpr std::uint32_t ipv6_packet_info = 19;
inline constexpr std::uint32_t ipv6_hop_limit = 20;
inline constexpr std::uint32_t ipv6_only = 27;
inline constexpr std::uint32_t ipv4_membership_size = 8;
inline constexpr std::uint32_t ipv6_membership_size = 20;
inline constexpr std::uint32_t arm32_control_header_size = 12;
inline constexpr std::uint32_t arm32_control_level_offset = 4;
inline constexpr std::uint32_t arm32_control_type_offset = 8;
inline constexpr std::uint32_t arm32_sockaddr_dl_size = 20;
inline constexpr std::uint32_t arm32_sockaddr_in_size = 16;
inline constexpr std::uint32_t arm32_sockaddr_in6_size = 28;
inline constexpr std::uint32_t sockaddr_length_offset = 0;
inline constexpr std::uint32_t sockaddr_family_offset = 1;
inline constexpr std::uint32_t sockaddr_port_offset = 2;
inline constexpr std::uint32_t sockaddr_ipv4_address_offset = 4;
inline constexpr std::uint32_t sockaddr_ipv6_address_offset = 8;
inline constexpr std::uint32_t sockaddr_ipv6_scope_offset = 24;

inline constexpr std::uint8_t route_message_version = 5;
inline constexpr std::uint8_t route_message_new_address = 0x0c;
inline constexpr std::uint8_t route_message_interface_info = 0x0e;
inline constexpr std::uint32_t route_address_netmask = 0x04;
inline constexpr std::uint32_t route_address_interface = 0x10;
inline constexpr std::uint32_t route_address_interface_address = 0x20;
inline constexpr std::uint32_t route_address_broadcast = 0x80;

inline constexpr std::uint16_t interface_flag_up = 0x0001;
inline constexpr std::uint16_t interface_flag_broadcast = 0x0002;
inline constexpr std::uint16_t interface_flag_loopback = 0x0008;
inline constexpr std::uint16_t interface_flag_point_to_point = 0x0010;
inline constexpr std::uint16_t interface_flag_running = 0x0040;
inline constexpr std::uint16_t interface_flag_output_active = 0x0400;
inline constexpr std::uint16_t interface_flag_simplex = 0x0800;
inline constexpr std::uint16_t interface_flag_all_multicast = 0x0200;
inline constexpr std::uint16_t interface_flag_multicast = 0x8000;
inline constexpr std::uint16_t interface_flags_kernel_managed =
    interface_flag_broadcast | interface_flag_point_to_point |
    interface_flag_running | interface_flag_output_active |
    interface_flag_simplex | interface_flag_all_multicast |
    interface_flag_multicast;

inline constexpr std::uint8_t interface_type_ethernet = 6;
inline constexpr std::uint8_t interface_type_loopback = 24;
inline constexpr std::uint32_t interface_family_loopback = 1;
inline constexpr std::uint32_t interface_family_ethernet = 2;
inline constexpr std::uint32_t default_loopback_mtu = 16'384;
inline constexpr std::uint32_t default_ethernet_mtu = 1'500;

// Darwin 8 encodes the ioctl parameter size in bits 16..28. Matching the
// direction/group/number separately keeps these identities valid for the
// ARM32 structures used by the firmware while documenting their wire layout.
inline constexpr std::uint32_t ioctl_identity_mask = 0xe000ffffU;
inline constexpr std::uint32_t ioctl_get_interface_mtu = 0xc0006933U;
inline constexpr std::uint32_t ioctl_set_interface_mtu = 0x80006934U;
inline constexpr std::uint32_t ioctl_set_interface_media = 0xc0006937U;
inline constexpr std::uint32_t ioctl_get_interface_media = 0xc0006938U;
inline constexpr std::uint32_t ioctl_get_ipv6_address_flags = 0xc0006949U;
inline constexpr std::uint32_t interface_request_value_offset = 16;
inline constexpr std::uint32_t interface_media_current_offset = 16;
inline constexpr std::uint32_t interface_media_mask_offset = 20;
inline constexpr std::uint32_t interface_media_status_offset = 24;
inline constexpr std::uint32_t interface_media_active_offset = 28;
inline constexpr std::uint32_t interface_media_count_offset = 32;
inline constexpr std::uint32_t interface_media_list_offset = 36;
inline constexpr std::uint32_t media_type_mask = 0x000000e0U;
inline constexpr std::uint32_t media_type_ethernet = 0x00000020U;
inline constexpr std::uint32_t media_subtype_auto = 0;
inline constexpr std::uint32_t media_subtype_100_tx = 6;
inline constexpr std::uint32_t media_option_full_duplex = 0x00100000U;
inline constexpr std::uint32_t media_status_valid = 0x00000001U;
inline constexpr std::uint32_t media_status_active = 0x00000002U;

constexpr std::uint32_t ioctl_identity(std::uint32_t command) {
    return command & ioctl_identity_mask;
}

inline constexpr std::uint32_t kernel_event_any = 0;
inline constexpr std::uint32_t kernel_event_vendor_apple = 1;
inline constexpr std::uint32_t kernel_event_network_class = 1;
inline constexpr std::uint32_t kernel_event_inet_subclass = 1;
inline constexpr std::uint32_t kernel_event_data_link_subclass = 2;
inline constexpr std::uint32_t kernel_event_inet6_subclass = 6;
inline constexpr std::uint32_t kernel_event_inet_new_address = 1;
inline constexpr std::uint32_t kernel_event_inet_address_changed = 2;
inline constexpr std::uint32_t kernel_event_inet_address_deleted = 3;
inline constexpr std::uint32_t kernel_event_inet6_new_user_address = 1;
inline constexpr std::uint32_t kernel_event_inet6_address_deleted = 3;
inline constexpr std::uint32_t kernel_event_interface_flags_changed = 1;
inline constexpr std::uint32_t kernel_event_link_off = 12;
inline constexpr std::uint32_t kernel_event_link_on = 13;
inline constexpr std::size_t kernel_event_header_size = 24;
inline constexpr std::size_t interface_name_size = 16;
inline constexpr std::size_t maximum_retained_kernel_events = 256;

struct InterfaceSnapshot {
    std::string name;
    std::uint16_t index{};
    std::uint16_t flags{};
    std::uint32_t family{};
    std::uint32_t unit{};
    std::uint32_t mtu{};
    std::uint8_t type{};
    std::array<std::byte, 6> link_address{};
    std::uint8_t link_address_length{};
    std::optional<std::array<std::byte, 16>> ipv4_address;
    std::optional<std::array<std::byte, 16>> ipv4_netmask;
    std::optional<std::array<std::byte, 16>> ipv4_broadcast;
    std::optional<std::array<std::byte, 28>> ipv6_address;
    std::optional<std::array<std::byte, 28>> ipv6_netmask;
};

[[nodiscard]] std::vector<std::byte> make_route_interface_list(
    std::span<const InterfaceSnapshot> interfaces,
    std::uint32_t address_family = address_family_unspecified,
    std::uint32_t interface_index = 0);

[[nodiscard]] std::vector<std::byte> make_network_event_data(
    const InterfaceSnapshot& interface);

[[nodiscard]] std::vector<std::byte> make_kernel_event(
    std::uint32_t identifier, std::uint32_t vendor,
    std::uint32_t event_class, std::uint32_t event_subclass,
    std::uint32_t event_code, std::span<const std::byte> event_data);

}  // namespace ilegacysim::darwin::network
