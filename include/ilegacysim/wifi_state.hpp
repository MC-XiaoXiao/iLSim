#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ilegacysim {

enum class WifiLinkState : std::uint8_t {
    PoweredOff,
    Idle,
    Scanning,
    Associated,
    Configured,
};

enum class WifiSecurity : std::uint8_t {
    Open,
    Wep,
    WpaPersonal,
};

struct VirtualAccessPoint {
    std::string ssid;
    std::array<std::byte, 6> bssid{};
    std::uint16_t channel{};
    std::int32_t rssi{};
    WifiSecurity security{WifiSecurity::Open};
};

struct WifiIpv4Configuration {
    std::array<std::byte, 4> address{};
    std::array<std::byte, 4> netmask{};
    std::array<std::byte, 4> broadcast{};
    std::array<std::byte, 4> gateway{};
    std::vector<std::array<std::byte, 4>> dns_servers;
};

struct WifiSnapshot {
    WifiLinkState link_state{WifiLinkState::PoweredOff};
    bool powered{};
    std::optional<VirtualAccessPoint> associated_access_point;
    std::optional<WifiIpv4Configuration> ipv4;
};

// User-visible 802.11 state. It intentionally models radio/link and IP
// configuration, not Broadcom registers or an IO80211 hardware data path.
// BSD sockets continue through HostNetwork while SystemConfiguration observes
// this state through en0 and XNU kernel events.
class WifiState {
public:
    WifiState();
    explicit WifiState(std::vector<VirtualAccessPoint> access_points);

    [[nodiscard]] WifiSnapshot snapshot() const;
    [[nodiscard]] std::vector<VirtualAccessPoint> scan();
    [[nodiscard]] bool set_power(bool powered);
    [[nodiscard]] bool associate(std::string_view ssid = {});
    [[nodiscard]] bool disassociate();

private:
    [[nodiscard]] static WifiIpv4Configuration default_ipv4_configuration();

    mutable std::mutex mutex_;
    std::vector<VirtualAccessPoint> access_points_;
    WifiLinkState link_state_{WifiLinkState::PoweredOff};
    std::optional<std::size_t> associated_index_;
    std::optional<WifiIpv4Configuration> ipv4_;
};

}  // namespace ilegacysim
