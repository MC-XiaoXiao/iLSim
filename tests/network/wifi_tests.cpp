#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/xattr.h>

#include "ilegacysim/address_space.hpp"
#include "ilegacysim/apple80211_hle.hpp"
#include "ilegacysim/clock_mig_ids.hpp"
#include "ilegacysim/clock_reply_mig_ids.hpp"
#include "ilegacysim/core_surface_abi.hpp"
#include "ilegacysim/core_surface_hle.hpp"
#include "ilegacysim/cpu.hpp"
#include "ilegacysim/darwin_abi.hpp"
#include "ilegacysim/darwin_kqueue_abi.hpp"
#include "ilegacysim/darwin_network_abi.hpp"
#include "ilegacysim/darwin_resource_abi.hpp"
#include "ilegacysim/darwin_route_socket.hpp"
#include "ilegacysim/device_mig_ids.hpp"
#include "ilegacysim/display.hpp"
#include "ilegacysim/dnssd_ipc_abi.hpp"
#include "ilegacysim/gdb_rsp.hpp"
#include "ilegacysim/gles_abi.hpp"
#include "ilegacysim/hfs_metadata.hpp"
#include "ilegacysim/host_network.hpp"
#include "ilegacysim/iokit_abi.hpp"
#include "ilegacysim/kernel.hpp"
#include "ilegacysim/kernel_iokit.hpp"
#include "ilegacysim/kernel_mach_ipc.hpp"
#include "ilegacysim/mach_clock_abi.hpp"
#include "ilegacysim/mach_namespace.hpp"
#include "ilegacysim/mach_port_mig_ids.hpp"
#include "ilegacysim/mach_port_object.hpp"
#include "ilegacysim/mach_scheduler_abi.hpp"
#include "ilegacysim/mach_thread_policy_abi.hpp"
#include "ilegacysim/macho.hpp"
#include "ilegacysim/mbx2d_abi.hpp"
#include "ilegacysim/mbx2d_hle.hpp"
#include "ilegacysim/mig_wire_abi.hpp"
#include "ilegacysim/mobile_framebuffer_hle.hpp"
#include "ilegacysim/opengles_hle.hpp"
#include "ilegacysim/surface_store.hpp"
#include "ilegacysim/system_configuration_mig_ids.hpp"
#include "ilegacysim/userland_hle.hpp"
#include "ilegacysim/virtual_network.hpp"
#include "ilegacysim/wifi_state.hpp"
#include "ilegacysim/xnu_mig_adapter.hpp"
#include "ilegacysim/xnu_scheduler.hpp"

#include "test_support.hpp"

#include "suite.hpp"

namespace ilegacysim::test::network_suite {
namespace {

using namespace ::ilegacysim;
using ::ilegacysim::test::require;

void wifi_state_test() {
  WifiState wifi;
  require(!wifi.snapshot().powered && wifi.scan().empty(),
          "powered-off Wi-Fi exposed scan results");
  require(wifi.set_power(true), "virtual Wi-Fi did not power on");
  const auto access_points = wifi.scan();
  require(access_points.size() == 1 &&
              access_points.front().ssid == "iLegacySim",
          "virtual Wi-Fi scan did not expose the compatibility network");
  require(wifi.associate("iLegacySim"), "virtual Wi-Fi association failed");
  const auto configured = wifi.snapshot();
  require(configured.link_state == WifiLinkState::Configured &&
              configured.associated_access_point.has_value() &&
              configured.ipv4.has_value() &&
              configured.ipv4->address == virtual_network::client_address &&
              configured.ipv4->dns_servers.size() == 1,
          "virtual association did not complete DHCP/DNS state");
  require(wifi.set_power(false) && !wifi.snapshot().ipv4,
          "power-off did not remove the virtual lease");

  AddressSpace memory;
  std::ostringstream stream;
  Output output{stream};
  CompatibilityKernel kernel{memory, output};
  kernel.set_host_network_policy(HostNetworkPolicy::Loopback);
  const auto connected = kernel.network_interface_snapshot("en0");
  const auto connected_routes = kernel.route_snapshot();
  require(kernel.wifi_snapshot().link_state == WifiLinkState::Configured &&
              connected && connected->ipv4_address.has_value() &&
              (connected->flags & darwin::network::interface_flag_up) != 0 &&
              (connected->flags & darwin::network::interface_flag_running) !=
                  0 &&
              connected_routes.size() == 1 &&
              connected_routes.front().interface_name == "en0" &&
              connected_routes.front().origin ==
                  darwin::route::Entry::Origin::Interface,
          "enabled host networking did not configure virtual en0");
  kernel.set_host_network_policy(HostNetworkPolicy::Isolated);
  const auto isolated = kernel.network_interface_snapshot("en0");
  require(isolated && isolated->ipv4_address.has_value() &&
              (isolated->flags & darwin::network::interface_flag_up) != 0 &&
              !kernel.route_snapshot().empty(),
          "host isolation incorrectly disconnected the guest-only LAN");
}

void configd_network_ioctl_test() {
  using namespace darwin::network;
  AddressSpace memory;
  constexpr std::uint32_t request = 0x3f000;
  constexpr std::uint32_t media_list = request + 0x100;
  require(memory.map(request, AddressSpace::page_size,
                     MemoryPermission::Read | MemoryPermission::Write),
          "configd network ioctl page failed to map");
  Dynarmic::ExclusiveMonitor monitor{1};
  Cpu cpu{0, memory, monitor};
  std::ostringstream stream;
  Output output{stream};
  CompatibilityKernel kernel{memory, output};

  const auto create_control_socket = [&](std::uint32_t family) {
    cpu.registers()[0] = family;
    cpu.registers()[1] = 2; // SOCK_DGRAM
    cpu.registers()[2] = 0;
    cpu.registers()[12] = 97;
    kernel.dispatch(cpu, 0x80);
    require((cpu.cpsr() & (1U << 29U)) == 0,
            "configd control socket creation failed");
    return cpu.registers()[0];
  };
  const auto issue_ioctl = [&](std::uint32_t fd, std::uint32_t command) {
    cpu.registers()[0] = fd;
    cpu.registers()[1] = command;
    cpu.registers()[2] = request;
    cpu.registers()[12] = 54;
    kernel.dispatch(cpu, 0x80);
  };
  const auto write_interface_name = [&] {
    std::array<std::byte, interface_name_size> name{};
    name[0] = std::byte{'e'};
    name[1] = std::byte{'n'};
    name[2] = std::byte{'0'};
    require(memory.copy_in(request, name),
            "configd interface name write failed");
  };

  const auto ipv4_fd = create_control_socket(address_family_inet);
  const auto ipv6_fd = create_control_socket(address_family_inet6);
  kernel.set_host_network_policy(HostNetworkPolicy::Loopback);
  write_interface_name();
  constexpr std::uint32_t arm32_ifmediareq_size = 40;
  const auto get_media_command =
      ioctl_get_interface_media | (arm32_ifmediareq_size << 16U);
  issue_ioctl(ipv4_fd, get_media_command);
  const auto active_media =
      media_type_ethernet | media_subtype_100_tx | media_option_full_duplex;
  require(cpu.registers()[0] == 0 &&
              memory.read32(request + interface_media_current_offset) ==
                  std::optional<std::uint32_t>{active_media} &&
              memory.read32(request + interface_media_status_offset) ==
                  std::optional<std::uint32_t>{media_status_valid |
                                               media_status_active} &&
              memory.read32(request + interface_media_count_offset) ==
                  std::optional<std::uint32_t>{2},
          "SIOCGIFMEDIA did not expose active virtual en0 media");

  require(memory.write32(request + interface_media_count_offset, 2) &&
              memory.write32(request + interface_media_list_offset, media_list),
          "SIOCGIFMEDIA list request setup failed");
  issue_ioctl(ipv4_fd, get_media_command);
  require(cpu.registers()[0] == 0 &&
              memory.read32(media_list) ==
                  std::optional<std::uint32_t>{media_type_ethernet |
                                               media_subtype_auto} &&
              memory.read32(media_list + 4) ==
                  std::optional<std::uint32_t>{active_media},
          "SIOCGIFMEDIA did not copy out available media words");

  constexpr std::uint32_t arm32_ifreq_size = 32;
  const auto get_mtu_command =
      ioctl_get_interface_mtu | (arm32_ifreq_size << 16U);
  write_interface_name();
  issue_ioctl(ipv4_fd, get_mtu_command);
  require(cpu.registers()[0] == 0 &&
              memory.read32(request + interface_request_value_offset) ==
                  std::optional<std::uint32_t>{default_ethernet_mtu},
          "SIOCGIFMTU did not return virtual en0 MTU");
  constexpr std::uint32_t configured_mtu = 1'400;
  require(
      memory.write32(request + interface_request_value_offset, configured_mtu),
      "SIOCSIFMTU input write failed");
  const auto set_mtu_command =
      ioctl_set_interface_mtu | (arm32_ifreq_size << 16U);
  issue_ioctl(ipv4_fd, set_mtu_command);
  require(cpu.registers()[0] == 0, "SIOCSIFMTU rejected a valid virtual MTU");
  issue_ioctl(ipv4_fd, get_mtu_command);
  require(cpu.registers()[0] == 0 &&
              memory.read32(request + interface_request_value_offset) ==
                  std::optional<std::uint32_t>{configured_mtu},
          "SIOCSIFMTU did not persist the virtual MTU");

  require(
      memory.write32(request + interface_request_value_offset, active_media),
      "SIOCSIFMEDIA input write failed");
  issue_ioctl(ipv4_fd, ioctl_set_interface_media | (arm32_ifreq_size << 16U));
  require(cpu.registers()[0] == 0,
          "SIOCSIFMEDIA rejected virtual Ethernet media");

  write_interface_name();
  constexpr std::uint32_t representative_in6_ifreq_size = 256;
  issue_ioctl(ipv6_fd, ioctl_get_ipv6_address_flags |
                           (representative_in6_ifreq_size << 16U));
  require(cpu.registers()[0] == 0 &&
              memory.read32(request + interface_request_value_offset) ==
                  std::optional<std::uint32_t>{0},
          "SIOCGIFAFLAG_IN6 did not return stable address flags");
}

void apple80211_firmware_hle_test() {
  const std::array candidates{
      std::filesystem::path{"build/rootfs/System/Library/SystemConfiguration/"
                            "Aeropuerto.bundle/Aeropuerto"},
      std::filesystem::path{"rootfs/System/Library/SystemConfiguration/"
                            "Aeropuerto.bundle/Aeropuerto"},
  };
  const auto path = std::find_if(
      candidates.begin(), candidates.end(),
      [](const auto &candidate) { return std::filesystem::exists(candidate); });
  if (path == candidates.end())
    return;

  AddressSpace memory;
  const auto image = MachOImage::parse(*path);
  image.map_into(memory);
  Dynarmic::ExclusiveMonitor monitor{1};
  Cpu cpu{0, memory, monitor};
  std::ostringstream stream;
  Output output{stream};
  UserlandHleRegistry registry{memory, output};
  auto wifi = std::make_shared<WifiState>();
  std::size_t state_changes = 0;
  Apple80211Hle apple80211{
      registry, wifi,
      [&](const WifiSnapshot &, const WifiSnapshot &) { ++state_changes; }};
  cpu.set_svc_handler([&](Cpu &source, std::uint32_t immediate) {
    require(registry.dispatch(source, 31, immediate),
            "Aeropuerto firmware issued an unregistered HLE SVC");
  });

  std::size_t installed = 0;
  for (const auto &segment : image.segments()) {
    if (segment.file_size == 0)
      continue;
    installed +=
        registry.install_mapped_image(cpu, 31, *path, segment.vm_address,
                                      segment.file_size, segment.file_offset);
  }
  require(installed == 6,
          "Aeropuerto compatibility exports were not all intercepted");

  const auto output_pointer = registry.allocate_data(sizeof(std::uint32_t));
  const auto power_output = registry.allocate_data(sizeof(std::uint32_t));
  const auto fake_cf_dictionary = registry.allocate_data(sizeof(std::uint32_t));
  require(output_pointer != 0 && power_output != 0 && fake_cf_dictionary != 0,
          "Aeropuerto HLE guest allocations failed");

  const auto invoke = [&](std::string_view symbol,
                          std::array<std::uint32_t, 3> arguments) {
    const auto *entry = image.find_symbol(symbol);
    require(entry != nullptr, "Aeropuerto firmware export is missing");
    cpu.clear_halt();
    cpu.registers()[0] = arguments[0];
    cpu.registers()[1] = arguments[1];
    cpu.registers()[2] = arguments[2];
    cpu.registers()[14] = 0x1000;
    cpu.registers()[15] = entry->value;
    cpu.set_cpsr(0x10);
    const auto result = cpu.step();
    require(result.exception.empty() && !result.fault,
            "Aeropuerto firmware HLE entry faulted");
    return cpu.registers()[0];
  };

  require(invoke("_Apple80211Open", {output_pointer, 0, 0}) == 0,
          "Apple80211Open failed");
  const auto handle = memory.read32(output_pointer).value_or(0);
  require(
      handle != 0 &&
          memory.read32(handle) == std::optional<std::uint32_t>{100} &&
          memory.read_c_string(handle + apple80211_abi::interface_name_offset,
                               apple80211_abi::interface_name_capacity) ==
              std::optional<std::string>{"en0"},
      "Apple80211Open returned a malformed firmware handle");
  require(invoke("_Apple80211BindToInterface",
                 {handle, fake_cf_dictionary, 0}) == 0 &&
              invoke("_Apple80211GetPower", {handle, power_output, 0}) == 0 &&
              memory.read8(power_output) == std::optional<std::uint8_t>{0},
          "Apple80211 initial interface/power state is incorrect");
  require(invoke("_Apple80211SetPower", {handle, 1, 0}) == 0 &&
              wifi->snapshot().link_state == WifiLinkState::Configured &&
              invoke("_Apple80211GetPower", {handle, power_output, 0}) == 0 &&
              memory.read8(power_output) == std::optional<std::uint8_t>{1},
          "Apple80211 power-on did not expose a connected en0");
  require(invoke("_Apple80211Associate", {handle, fake_cf_dictionary, 0}) ==
                  0 &&
              state_changes == 2,
          "Apple80211 association did not reach the shared network state");
  require(invoke("_Apple80211Close", {handle, 0, 0}) == 0 &&
              invoke("_Apple80211GetPower", {handle, power_output, 0}) ==
                  apple80211_abi::invalid_argument,
          "Apple80211Close did not invalidate the process-local handle");
}

} // namespace

void run_wifi_tests() {
  wifi_state_test();
  configd_network_ioctl_test();
  apple80211_firmware_hle_test();
}

} // namespace ilegacysim::test::network_suite
