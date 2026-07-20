#include "ilegacysim/darwin_network_abi.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ilegacysim::darwin::network {
namespace {

constexpr std::size_t interface_message_header_size = 112;
constexpr std::size_t interface_data_offset = 16;
constexpr std::size_t interface_data_mtu_offset = interface_data_offset + 8;
constexpr std::size_t interface_data_baudrate_offset = interface_data_offset + 16;
constexpr std::size_t interface_address_message_header_size = 20;
constexpr std::size_t sockaddr_data_offset = 8;
constexpr std::size_t minimum_sockaddr_dl_size = 20;
constexpr std::uint32_t ethernet_baudrate = 10'000'000;

void put16(
    std::span<std::byte> bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void put32(
    std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(value >> (index * 8U));
    }
}

std::size_t rounded_sockaddr_size(std::size_t size) {
    constexpr std::size_t alignment = sizeof(std::uint32_t);
    return std::max(alignment, (size + alignment - 1U) & ~(alignment - 1U));
}

template<std::size_t Size>
void append_sockaddr(
    std::vector<std::byte>& record, const std::array<std::byte, Size>& address) {
    const auto declared_size = std::to_integer<std::uint8_t>(address[0]);
    const auto copied_size = declared_size == 0
                                 ? Size
                                 : std::min<std::size_t>(declared_size, Size);
    const auto padded_size = rounded_sockaddr_size(copied_size);
    const auto original_size = record.size();
    record.resize(original_size + padded_size);
    std::copy_n(address.begin(), copied_size, record.begin() + original_size);
}

std::vector<std::byte> make_interface_record(const InterfaceSnapshot& interface) {
    const auto name_length = std::min(
        interface.name.size(), interface_name_size - 1U);
    const auto link_length = std::min<std::size_t>(
        interface.link_address_length, interface.link_address.size());
    const auto sockaddr_length = std::max(
        minimum_sockaddr_dl_size,
        sockaddr_data_offset + name_length + link_length);
    const auto padded_sockaddr_length = rounded_sockaddr_size(sockaddr_length);
    const auto record_size = interface_message_header_size + padded_sockaddr_length;
    if (record_size > std::numeric_limits<std::uint16_t>::max()) {
        throw std::length_error{"Darwin interface record exceeds ifm_msglen"};
    }

    std::vector<std::byte> record(record_size);
    put16(record, 0, static_cast<std::uint16_t>(record.size()));
    record[2] = static_cast<std::byte>(route_message_version);
    record[3] = static_cast<std::byte>(route_message_interface_info);
    put32(record, 4, route_address_interface);
    put32(record, 8, interface.flags);
    put16(record, 12, interface.index);

    record[interface_data_offset] = static_cast<std::byte>(interface.type);
    record[interface_data_offset + 3] =
        static_cast<std::byte>(link_length);
    record[interface_data_offset + 4] = static_cast<std::byte>(
        interface.type == interface_type_ethernet ? 14U : 0U);
    put32(record, interface_data_mtu_offset, interface.mtu);
    put32(record, interface_data_baudrate_offset,
          interface.type == interface_type_ethernet ? ethernet_baudrate : 0U);

    const auto sockaddr_offset = interface_message_header_size;
    record[sockaddr_offset] = static_cast<std::byte>(sockaddr_length);
    record[sockaddr_offset + 1] = static_cast<std::byte>(address_family_link);
    put16(record, sockaddr_offset + 2, interface.index);
    record[sockaddr_offset + 4] = static_cast<std::byte>(interface.type);
    record[sockaddr_offset + 5] = static_cast<std::byte>(name_length);
    record[sockaddr_offset + 6] = static_cast<std::byte>(link_length);
    std::transform(
        interface.name.begin(),
        interface.name.begin() + static_cast<std::ptrdiff_t>(name_length),
        record.begin() + static_cast<std::ptrdiff_t>(sockaddr_offset + sockaddr_data_offset),
        [](char value) { return static_cast<std::byte>(value); });
    std::copy_n(
        interface.link_address.begin(), link_length,
        record.begin() + static_cast<std::ptrdiff_t>(
                             sockaddr_offset + sockaddr_data_offset + name_length));
    return record;
}

template<std::size_t Size>
std::vector<std::byte> make_address_record(
    const InterfaceSnapshot& interface,
    const std::array<std::byte, Size>& address,
    const std::optional<std::array<std::byte, Size>>& netmask,
    const std::optional<std::array<std::byte, Size>>& broadcast) {
    std::uint32_t address_bits = route_address_interface_address;
    if (netmask) address_bits |= route_address_netmask;
    if (broadcast) address_bits |= route_address_broadcast;

    std::vector<std::byte> record(interface_address_message_header_size);
    record[2] = static_cast<std::byte>(route_message_version);
    record[3] = static_cast<std::byte>(route_message_new_address);
    put32(record, 4, address_bits);
    put16(record, 12, interface.index);
    if (netmask) append_sockaddr(record, *netmask);
    append_sockaddr(record, address);
    if (broadcast) append_sockaddr(record, *broadcast);
    if (record.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::length_error{"Darwin address record exceeds ifam_msglen"};
    }
    put16(record, 0, static_cast<std::uint16_t>(record.size()));
    return record;
}

void append_record(
    std::vector<std::byte>& output, std::vector<std::byte> record) {
    output.insert(output.end(), record.begin(), record.end());
}

}  // namespace

std::vector<std::byte> make_route_interface_list(
    std::span<const InterfaceSnapshot> interfaces,
    std::uint32_t address_family, std::uint32_t interface_index) {
    std::vector<std::byte> result;
    for (const auto& interface : interfaces) {
        if (interface_index != 0 && interface.index != interface_index) continue;
        append_record(result, make_interface_record(interface));
        if ((address_family == address_family_unspecified ||
             address_family == address_family_inet) &&
            interface.ipv4_address) {
            auto destination = interface.ipv4_broadcast;
            if (!destination &&
                (interface.flags & interface_flag_loopback) != 0) {
                // XNU 792's in_ifinit redirects a loopback in_ifaddr's
                // ifa_dstaddr to ifa_addr. sysctl_iflist exports that pointer
                // as RTAX_BRD, which Darwin getifaddrs exposes through the
                // ifa_broadaddr/ifa_dstaddr union.
                destination = interface.ipv4_address;
            }
            append_record(result, make_address_record(
                                      interface, *interface.ipv4_address,
                                      interface.ipv4_netmask,
                                      destination));
        }
        if ((address_family == address_family_unspecified ||
             address_family == address_family_inet6) &&
            interface.ipv6_address) {
            append_record(result, make_address_record(
                                      interface, *interface.ipv6_address,
                                      interface.ipv6_netmask,
                                      std::optional<std::array<std::byte, 28>>{}));
        }
    }
    return result;
}

std::vector<std::byte> make_network_event_data(
    const InterfaceSnapshot& interface) {
    std::vector<std::byte> result(sizeof(std::uint32_t) * 2U + interface_name_size);
    put32(result, 0, interface.family);
    put32(result, 4, interface.unit);
    const auto name_length = std::min(
        interface.name.size(), interface_name_size - 1U);
    std::transform(
        interface.name.begin(),
        interface.name.begin() + static_cast<std::ptrdiff_t>(name_length),
        result.begin() + 8,
        [](char value) { return static_cast<std::byte>(value); });
    return result;
}

std::vector<std::byte> make_kernel_event(
    std::uint32_t identifier, std::uint32_t vendor,
    std::uint32_t event_class, std::uint32_t event_subclass,
    std::uint32_t event_code, std::span<const std::byte> event_data) {
    if (event_data.size() >
        std::numeric_limits<std::uint32_t>::max() - kernel_event_header_size) {
        throw std::length_error{"Darwin kernel event exceeds total_size"};
    }
    std::vector<std::byte> result(kernel_event_header_size + event_data.size());
    put32(result, 0, static_cast<std::uint32_t>(result.size()));
    put32(result, 4, vendor);
    put32(result, 8, event_class);
    put32(result, 12, event_subclass);
    put32(result, 16, identifier);
    put32(result, 20, event_code);
    std::copy(event_data.begin(), event_data.end(),
              result.begin() + static_cast<std::ptrdiff_t>(kernel_event_header_size));
    return result;
}

}  // namespace ilegacysim::darwin::network
