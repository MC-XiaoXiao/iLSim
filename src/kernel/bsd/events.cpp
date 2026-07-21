#include "ilegacysim/kernel.hpp"

#include "ilegacysim/baseband_device.hpp"
#include "ilegacysim/darwin_abi.hpp"
#include "ilegacysim/darwin_kqueue_abi.hpp"
#include "ilegacysim/darwin_network_abi.hpp"
#include "ilegacysim/darwin_resource_abi.hpp"
#include "ilegacysim/darwin_route_socket.hpp"
#include "ilegacysim/darwin_sysctl.hpp"
#include "ilegacysim/kernel_network.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support.hpp"

namespace ilegacysim {

void CompatibilityKernel::dispatch_bsd_events(Cpu &cpu, std::uint32_t number) {
  auto &registers = cpu.registers();
  switch (number) {
  case 54: { // ioctl
    const auto fd = registers[0];
    if (const auto block = virtual_block_descriptors_.find(fd);
        block != virtual_block_descriptors_.end()) {
      switch (registers[1]) {
      case 0x40046418U: // DKIOCGETBLOCKSIZE / legacy DKIOCBLKSIZE
        if (!memory_.write32(registers[2], 512)) {
          bsd_error(cpu, bsd_support::bad_address);
        } else {
          bsd_success(cpu, 0);
        }
        return;
      case 0x40086419U: { // DKIOCGETBLOCKCOUNT
        std::error_code size_error;
        const auto size =
            std::filesystem::file_size(file_descriptors_.at(fd), size_error);
        if (size_error || !memory_.write64(registers[2], size / 512U)) {
          bsd_error(cpu, size_error ? 5U : bsd_support::bad_address);
        } else {
          bsd_success(cpu, 0);
        }
        return;
      }
      case 0x40046419U: { // DKIOCGETBLOCKCOUNT32
        std::error_code size_error;
        const auto size =
            std::filesystem::file_size(file_descriptors_.at(fd), size_error);
        if (size_error ||
            !memory_.write32(registers[2],
                             static_cast<std::uint32_t>(size / 512U))) {
          bsd_error(cpu, size_error ? 5U : bsd_support::bad_address);
        } else {
          bsd_success(cpu, 0);
        }
        return;
      }
      case 0x40046417U: // DKIOCISFORMATTED
      case 0x4004641dU: // DKIOCISWRITABLE
        if (!memory_.write32(registers[2], 1)) {
          bsd_error(cpu, bsd_support::bad_address);
        } else {
          bsd_success(cpu, 0);
        }
        return;
      case 0x20006416U: // DKIOCSYNCHRONIZECACHE
        bsd_success(cpu, 0);
        return;
      default: {
        std::ostringstream message;
        message << "[disk] unsupported ioctl 0x" << std::hex << registers[1]
                << " minor=" << std::dec << block->second.first << '\n';
        output_.write(message.str());
        bsd_error(cpu, 25);
        return;
      }
      }
    }
    const auto device = virtual_descriptors_.find(fd);
    if (device == virtual_descriptors_.end()) {
      bsd_error(cpu, 25); // ENOTTY
      return;
    }
    if (registers[1] == darwin::socket::ioctl_non_block) {
      const auto enabled = memory_.read32(registers[2]);
      if (!enabled) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      auto &flags = file_status_flags_[fd];
      if (*enabled != 0)
        flags |= darwin::open_flag::non_block;
      else
        flags &= ~darwin::open_flag::non_block;
      bsd_success(cpu, 0);
      return;
    }
    if (ioctl_kernel_control_socket(cpu))
      return;
    if (device->second == bsd::baseband_device::descriptor_kind) {
      const auto request = registers[1];
      const auto argument = registers[2];
      if (request == darwin::tty::get_attributes) {
        const auto attributes =
            shared_state_->baseband_device_state.attributes();
        const auto control_characters =
            std::as_bytes(std::span{attributes.control_characters});
        if (!memory_.write32(
                argument + darwin::tty::arm32_attributes_offset::input_flags,
                attributes.input_flags) ||
            !memory_.write32(
                argument + darwin::tty::arm32_attributes_offset::output_flags,
                attributes.output_flags) ||
            !memory_.write32(
                argument + darwin::tty::arm32_attributes_offset::control_flags,
                attributes.control_flags) ||
            !memory_.write32(
                argument + darwin::tty::arm32_attributes_offset::local_flags,
                attributes.local_flags) ||
            !memory_.copy_in(
                argument +
                    darwin::tty::arm32_attributes_offset::control_characters,
                control_characters) ||
            !memory_.write32(
                argument + darwin::tty::arm32_attributes_offset::input_speed,
                static_cast<std::uint32_t>(attributes.input_speed)) ||
            !memory_.write32(
                argument + darwin::tty::arm32_attributes_offset::output_speed,
                static_cast<std::uint32_t>(attributes.output_speed))) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        output_.write("[baseband] ioctl pid=" + std::to_string(process_.pid) +
                      " TIOCGETA\n");
        bsd_success(cpu, 0);
        return;
      }
      if (request == darwin::tty::set_attributes ||
          request == darwin::tty::set_attributes_after_drain ||
          request == darwin::tty::set_attributes_after_drain_and_flush) {
        const auto input_flags = memory_.read32(
            argument + darwin::tty::arm32_attributes_offset::input_flags);
        const auto output_flags = memory_.read32(
            argument + darwin::tty::arm32_attributes_offset::output_flags);
        const auto control_flags = memory_.read32(
            argument + darwin::tty::arm32_attributes_offset::control_flags);
        const auto local_flags = memory_.read32(
            argument + darwin::tty::arm32_attributes_offset::local_flags);
        const auto control_characters = memory_.read_bytes(
            argument + darwin::tty::arm32_attributes_offset::control_characters,
            darwin::tty::control_character_count);
        const auto input_speed = memory_.read32(
            argument + darwin::tty::arm32_attributes_offset::input_speed);
        const auto output_speed = memory_.read32(
            argument + darwin::tty::arm32_attributes_offset::output_speed);
        if (!input_flags || !output_flags || !control_flags || !local_flags ||
            !control_characters || !input_speed || !output_speed) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        darwin::tty::Arm32Attributes attributes{
            .input_flags = *input_flags,
            .output_flags = *output_flags,
            .control_flags = *control_flags,
            .local_flags = *local_flags,
            .input_speed = static_cast<std::int32_t>(*input_speed),
            .output_speed = static_cast<std::int32_t>(*output_speed),
        };
        std::transform(control_characters->begin(), control_characters->end(),
                       attributes.control_characters.begin(),
                       [](std::byte value) {
                         return std::to_integer<std::uint8_t>(value);
                       });
        shared_state_->baseband_device_state.set_attributes(attributes);
        std::ostringstream message;
        message << "[baseband] ioctl pid=" << process_.pid
                << " TIOCSETA request=0x" << std::hex << request << std::dec
                << " ispeed=" << attributes.input_speed
                << " ospeed=" << attributes.output_speed << '\n';
        output_.write(message.str());
        bsd_success(cpu, 0);
        return;
      }
      if (request == darwin::tty::set_h5_transport_mode) {
        const auto enabled = memory_.read32(argument);
        if (!enabled) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        shared_state_->baseband_device_state.set_h5_transport_mode(
            *enabled != 0);
        output_.write("[baseband] ioctl pid=" +
                      std::to_string(process_.pid) +
                      " IOAOSH5 enabled=" +
                      std::to_string(*enabled != 0) + "\n");
        bsd_success(cpu, 0);
        return;
      }
      if (shared_state_->baseband_device_state.ioctl(registers[1]) ==
          bsd::baseband_device::IoctlResult::success) {
        std::ostringstream message;
        message << "[baseband] ioctl pid=" << process_.pid << " request=0x"
                << std::hex << registers[1] << std::dec << " exclusive="
                << shared_state_->baseband_device_state.exclusive() << '\n';
        output_.write(message.str());
        bsd_success(cpu, 0);
        return;
      }
      std::ostringstream message;
      message << "[baseband] unsupported ioctl pid=" << process_.pid
              << " request=0x" << std::hex << registers[1];
      constexpr std::uint32_t maximum_traced_ioctl_payload = 64;
      const auto payload_size = darwin::tty::parameter_length(registers[1]);
      if ((registers[1] & darwin::tty::ioctl_input) != 0 && payload_size != 0 &&
          payload_size <= maximum_traced_ioctl_payload) {
        if (const auto payload =
                memory_.read_bytes(registers[2], payload_size)) {
          message << " input=" << bsd_support::format_payload_prefix(*payload);
        }
      }
      message << '\n';
      output_.write(message.str());
      bsd_error(cpu, darwin::error::inappropriate_ioctl);
      return;
    }
    if (registers[1] == darwin::socket::ioctl_pending_bytes) {
      std::uint32_t pending_error = 0;
      const auto pending_count = socket_pending_byte_count(fd, pending_error);
      if (!pending_count) {
        bsd_error(cpu, pending_error);
        return;
      }
      if (!memory_.write32(registers[2], *pending_count)) {
        bsd_error(cpu, bsd_support::bad_address);
      } else {
        bsd_success(cpu, 0);
      }
      return;
    }
    if (device->second == "inet-dgram" || device->second == "inet6-dgram") {
      const auto name_bytes = memory_.read_bytes(registers[2], 16);
      if (!name_bytes) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      std::string name;
      for (const auto byte : *name_bytes) {
        const auto value = static_cast<char>(byte);
        if (value == '\0')
          break;
        name.push_back(value);
      }
      std::unique_lock network_lock{shared_state_->network_mutex};
      const auto interface = shared_state_->network_interfaces.find(name);
      if (interface == shared_state_->network_interfaces.end()) {
        bsd_error(cpu, 6); // ENXIO
        return;
      }
      const auto ioctl_identity = darwin::network::ioctl_identity(registers[1]);
      if (ioctl_identity == darwin::network::ioctl_get_interface_media) {
        const auto ethernet =
            interface->second.type == darwin::network::interface_type_ethernet;
        const auto active = (interface->second.flags &
                             (darwin::network::interface_flag_up |
                              darwin::network::interface_flag_running)) ==
                            (darwin::network::interface_flag_up |
                             darwin::network::interface_flag_running);
        const auto automatic_media = darwin::network::media_type_ethernet |
                                     darwin::network::media_subtype_auto;
        const auto active_media = darwin::network::media_type_ethernet |
                                  darwin::network::media_subtype_100_tx |
                                  darwin::network::media_option_full_duplex;
        const std::array<std::uint32_t, 2> available_media{automatic_media,
                                                           active_media};
        const auto requested_count = memory_.read32(
            registers[2] + darwin::network::interface_media_count_offset);
        const auto list_address = memory_.read32(
            registers[2] + darwin::network::interface_media_list_offset);
        if (!requested_count || !list_address) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        if (ethernet && *list_address != 0) {
          const auto copied =
              std::min<std::size_t>(*requested_count, available_media.size());
          for (std::size_t index = 0; index < copied; ++index) {
            if (!memory_.write32(*list_address +
                                     static_cast<std::uint32_t>(
                                         index * sizeof(std::uint32_t)),
                                 available_media[index])) {
              bsd_error(cpu, bsd_support::bad_address);
              return;
            }
          }
        }
        const auto media_status =
            ethernet ? darwin::network::media_status_valid |
                           (active ? darwin::network::media_status_active : 0U)
                     : 0U;
        if (!memory_.write32(
                registers[2] + darwin::network::interface_media_current_offset,
                ethernet ? active_media : 0U) ||
            !memory_.write32(registers[2] +
                                 darwin::network::interface_media_mask_offset,
                             0) ||
            !memory_.write32(registers[2] +
                                 darwin::network::interface_media_status_offset,
                             media_status) ||
            !memory_.write32(registers[2] +
                                 darwin::network::interface_media_active_offset,
                             ethernet ? active_media : 0U) ||
            !memory_.write32(
                registers[2] + darwin::network::interface_media_count_offset,
                ethernet ? static_cast<std::uint32_t>(available_media.size())
                         : 0U)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        bsd_success(cpu, 0);
        return;
      }
      if (ioctl_identity == darwin::network::ioctl_set_interface_media) {
        const auto media = memory_.read32(
            registers[2] + darwin::network::interface_request_value_offset);
        if (!media) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        if (interface->second.type !=
                darwin::network::interface_type_ethernet ||
            (*media & darwin::network::media_type_mask) !=
                darwin::network::media_type_ethernet) {
          bsd_error(cpu, bsd_support::invalid_argument);
          return;
        }
        bsd_success(cpu, 0);
        return;
      }
      if (ioctl_identity == darwin::network::ioctl_get_interface_mtu) {
        if (!memory_.write32(
                registers[2] + darwin::network::interface_request_value_offset,
                interface->second.mtu)) {
          bsd_error(cpu, bsd_support::bad_address);
        } else {
          bsd_success(cpu, 0);
        }
        return;
      }
      if (ioctl_identity == darwin::network::ioctl_set_interface_mtu) {
        const auto mtu = memory_.read32(
            registers[2] + darwin::network::interface_request_value_offset);
        if (!mtu) {
          bsd_error(cpu, bsd_support::bad_address);
        } else if (*mtu < 68U || *mtu > 65'535U) {
          bsd_error(cpu, bsd_support::invalid_argument);
        } else {
          interface->second.mtu = *mtu;
          bsd_success(cpu, 0);
        }
        return;
      }
      if (ioctl_identity == darwin::network::ioctl_get_ipv6_address_flags) {
        if (!memory_.write32(
                registers[2] + darwin::network::interface_request_value_offset,
                0)) {
          bsd_error(cpu, bsd_support::bad_address);
        } else {
          bsd_success(cpu, 0);
        }
        return;
      }
      if (ioctl_identity == 0x80006919U) { // SIOCDIFADDR[_IN6]
        const auto ipv6 = device->second == "inet6-dgram";
        const auto family = static_cast<std::uint8_t>(
            ipv6 ? darwin::network::address_family_inet6
                 : darwin::network::address_family_inet);
        const auto address_size = ipv6 ? 28U : 16U;
        const auto requested =
            memory_.read_bytes(registers[2] + 16, address_size);
        if (!requested) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        const auto configured =
            ipv6 ? interface->second.has_ipv6 : interface->second.has_ipv4;
        const auto address_matches =
            ipv6 ? std::equal(requested->begin(), requested->end(),
                              interface->second.ipv6_address.begin())
                 : std::equal(requested->begin(), requested->end(),
                              interface->second.ipv4_address.begin());
        if (!configured || !address_matches) {
          bsd_error(cpu, darwin::error::address_not_available);
          return;
        }
        if (ipv6) {
          interface->second.has_ipv6 = false;
          interface->second.ipv6_address = {};
          interface->second.ipv6_netmask = {};
        } else {
          interface->second.has_ipv4 = false;
          interface->second.ipv4_address = {};
          interface->second.ipv4_netmask = {};
          interface->second.ipv4_broadcast = {};
        }
        network_lock.unlock();
        synchronize_interface_routes(name, family);
        post_network_event(
            name,
            ipv6 ? darwin::network::kernel_event_inet6_subclass
                 : darwin::network::kernel_event_inet_subclass,
            ipv6 ? darwin::network::kernel_event_inet6_address_deleted
                 : darwin::network::kernel_event_inet_address_deleted);
        bsd_success(cpu, 0);
        return;
      }
      switch (registers[1]) {
      case 0xc0206911U: // SIOCGIFFLAGS
        if (!memory_.write16(registers[2] + 16, interface->second.flags)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        bsd_success(cpu, 0);
        return;
      case 0x80206910U: { // SIOCSIFFLAGS
        const auto flags = memory_.read16(registers[2] + 16);
        if (!flags) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        const auto kernel_managed =
            darwin::network::interface_flags_kernel_managed;
        interface->second.flags = static_cast<std::uint16_t>(
            (interface->second.flags & kernel_managed) |
            (*flags & static_cast<std::uint16_t>(~kernel_managed)));
        network_lock.unlock();
        post_data_link_event(
            name, darwin::network::kernel_event_interface_flags_changed);
        bsd_success(cpu, 0);
        return;
      }
      case 0x8040691aU: { // SIOCAIFADDR, struct ifaliasreq
        const auto previously_configured = interface->second.has_ipv4;
        const auto address = memory_.read_bytes(registers[2] + 16, 16);
        const auto broadcast = memory_.read_bytes(registers[2] + 32, 16);
        const auto netmask = memory_.read_bytes(registers[2] + 48, 16);
        if (!address || !broadcast || !netmask) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        if (std::to_integer<std::uint8_t>((*address)[0]) != 16 ||
            std::to_integer<std::uint8_t>((*address)[1]) !=
                darwin::network::address_family_inet) {
          bsd_error(cpu, bsd_support::invalid_argument);
          return;
        }
        std::copy(address->begin(), address->end(),
                  interface->second.ipv4_address.begin());
        std::copy(netmask->begin(), netmask->end(),
                  interface->second.ipv4_netmask.begin());
        std::copy(broadcast->begin(), broadcast->end(),
                  interface->second.ipv4_broadcast.begin());
        interface->second.has_ipv4 = true;
        network_lock.unlock();
        synchronize_interface_routes(name,
                                     darwin::network::address_family_inet);
        post_network_event(
            name, darwin::network::kernel_event_inet_subclass,
            previously_configured
                ? darwin::network::kernel_event_inet_address_changed
                : darwin::network::kernel_event_inet_new_address);
        bsd_success(cpu, 0);
        return;
      }
      case 0x8078691aU: { // SIOCAIFADDR_IN6, struct in6_aliasreq
        const auto address = memory_.read_bytes(registers[2] + 16, 28);
        const auto netmask = memory_.read_bytes(registers[2] + 72, 28);
        if (!address || !netmask) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        if (std::to_integer<std::uint8_t>((*address)[0]) != 28 ||
            std::to_integer<std::uint8_t>((*address)[1]) !=
                darwin::network::address_family_inet6) {
          bsd_error(cpu, bsd_support::invalid_argument);
          return;
        }
        std::copy(address->begin(), address->end(),
                  interface->second.ipv6_address.begin());
        std::copy(netmask->begin(), netmask->end(),
                  interface->second.ipv6_netmask.begin());
        interface->second.has_ipv6 = true;
        network_lock.unlock();
        synchronize_interface_routes(name,
                                     darwin::network::address_family_inet6);
        post_network_event(
            name, darwin::network::kernel_event_inet6_subclass,
            darwin::network::kernel_event_inet6_new_user_address);
        bsd_success(cpu, 0);
        return;
      }
      default: {
        std::ostringstream message;
        message << "[network] unsupported ioctl 0x" << std::hex << registers[1]
                << " on " << name << '\n';
        output_.write(message.str());
        bsd_error(cpu, 25);
        return;
      }
      }
    }
    if (device->second != "system-event-socket") {
      bsd_error(cpu, 25);
      return;
    }
    if (registers[1] == 0x800c6502U) { // SIOCSKEVFILT
      std::array<std::uint32_t, 3> filter{};
      for (std::size_t index = 0; index < filter.size(); ++index) {
        const auto value = memory_.read32(
            registers[2] + static_cast<std::uint32_t>(index * 4U));
        if (!value) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        filter[index] = *value;
      }
      system_event_filters_[fd] = filter;
      bsd_success(cpu, 0);
      return;
    }
    if (registers[1] == 0x400c6503U) { // SIOCGKEVFILT
      const auto filter = system_event_filters_[fd];
      for (std::size_t index = 0; index < filter.size(); ++index) {
        if (!memory_.write32(registers[2] +
                                 static_cast<std::uint32_t>(index * 4U),
                             filter[index])) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    if (registers[1] == 0x40046501U) { // SIOCGKEVID
      std::uint32_t last_identifier = 0;
      {
        std::lock_guard socket_lock{shared_state_->socket_mutex};
        last_identifier = shared_state_->next_kernel_event_identifier - 1U;
      }
      if (!memory_.write32(registers[2], last_identifier)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      bsd_success(cpu, 0);
      return;
    }
    bsd_error(cpu, 25); // ENOTTY
    return;
  }
  case 93: { // select
    constexpr std::uint64_t microseconds_per_second = 1'000'000ULL;
    constexpr std::uint64_t nanoseconds_per_microsecond = 1'000ULL;
    const auto descriptor_count = registers[0];
    if (descriptor_count > 1024) {
      bsd_error(cpu, bsd_support::invalid_argument);
      return;
    }
    const auto words = (descriptor_count + 31U) / 32U;
    std::uint32_t ready_count = 0;
    std::vector<std::uint32_t> requested_read_words(words);
    std::vector<std::uint32_t> requested_write_words(words);
    bool watches_host_socket = false;
    for (std::uint32_t word_index = 0; word_index < words; ++word_index) {
      std::uint32_t ready_read_word = 0;
      std::uint32_t ready_write_word = 0;
      if (registers[1] != 0) {
        const auto requested = memory_.read32(registers[1] + word_index * 4U);
        if (!requested) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        requested_read_words[word_index] = *requested;
        for (std::uint32_t bit = 0; bit < 32; ++bit) {
          const auto fd = word_index * 32U + bit;
          if (fd >= descriptor_count || (*requested & (1U << bit)) == 0)
            continue;
          watches_host_socket = watches_host_socket || host_sockets_.contains(fd);
          if (descriptor_readable(fd)) {
            ready_read_word |= 1U << bit;
            ++ready_count;
          }
        }
        if (!memory_.write32(registers[1] + word_index * 4U, ready_read_word)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      if (registers[2] != 0) {
        const auto requested = memory_.read32(registers[2] + word_index * 4U);
        if (!requested) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        requested_write_words[word_index] = *requested;
        for (std::uint32_t bit = 0; bit < 32; ++bit) {
          const auto fd = word_index * 32U + bit;
          if (fd >= descriptor_count || (*requested & (1U << bit)) == 0) {
            continue;
          }
          watches_host_socket = watches_host_socket || host_sockets_.contains(fd);
          if (descriptor_writable(fd)) {
            ready_write_word |= 1U << bit;
            ++ready_count;
          }
        }
        if (!memory_.write32(registers[2] + word_index * 4U,
                             ready_write_word)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      if (registers[3] != 0 &&
          !memory_.write32(registers[3] + word_index * 4U, 0)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
    }
    if (ready_count != 0) {
      bsd_success(cpu, ready_count);
      return;
    }
    std::optional<std::uint64_t> timeout_deadline;
    if (registers[4] != 0) {
      const auto seconds_word = memory_.read32(registers[4]);
      const auto microseconds_word = memory_.read32(registers[4] + 4);
      if (!seconds_word || !microseconds_word) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      const auto seconds = static_cast<std::int32_t>(*seconds_word);
      const auto microseconds = static_cast<std::int32_t>(*microseconds_word);
      if (seconds < 0 || microseconds < 0 ||
          static_cast<std::uint64_t>(microseconds) >=
              microseconds_per_second) {
        bsd_error(cpu, bsd_support::invalid_argument);
        return;
      }
      const auto duration =
          (static_cast<std::uint64_t>(seconds) * microseconds_per_second +
           static_cast<std::uint64_t>(microseconds)) *
          nanoseconds_per_microsecond;
      if (duration == 0) {
        bsd_success(cpu, 0);
        return;
      }
      // Host sockets have a real poll source but still need the guest timer to
      // wake a finite select when no packet arrives. Other virtual descriptors
      // retain their existing provider-specific waiting behavior.
      if (watches_host_socket) {
        const auto now = shared_state_->clock.now();
        timeout_deadline =
            duration > std::numeric_limits<std::uint64_t>::max() - now
                ? std::numeric_limits<std::uint64_t>::max()
                : now + duration;
      }
    }
    process_.waiting_for_events = true;
    pending_selects_[cpu.processor_id()] =
        PendingSelect{descriptor_count,
                      registers[1],
                      registers[2],
                      registers[3],
                      std::move(requested_read_words),
                      std::move(requested_write_words),
                      cpu.processor_id(),
                      timeout_deadline};
    output_.write("[network] select wait pid=" + std::to_string(process_.pid) +
                  " nfds=" + std::to_string(descriptor_count) + "\n");
    bsd_success(cpu, 0);
    cpu.halt(Dynarmic::HaltReason::UserDefined5);
    return;
  }
  case 202: { // __sysctl
    const auto mib0 = memory_.read32(registers[0]);
    const auto mib1 =
        registers[1] >= 2 ? memory_.read32(registers[0] + 4) : std::nullopt;
    const auto old_size = registers[3] != 0 ? memory_.read32(registers[3])
                                            : std::optional<std::uint32_t>{0};
    if (!mib0 || !mib1 || !old_size) {
      bsd_error(cpu, bsd_support::bad_address);
      return;
    }
    if (*mib0 == darwin::sysctl::control_unspecified &&
        *mib1 == darwin::sysctl::operation_name_to_oid && registers[1] == 2) {
      // XNU's sysctl.name2oid consumes an un-terminated name through the new
      // value and returns the integer MIB through the old value.
      constexpr std::uint32_t maximum_name_length = 1024;
      if (registers[4] == 0 || registers[5] == 0 ||
          registers[5] >= maximum_name_length) {
        bsd_error(cpu, registers[5] >= maximum_name_length
                           ? darwin::error::argument_list_too_long
                           : darwin::error::no_entry);
        return;
      }
      const auto name_bytes = memory_.read_bytes(registers[4], registers[5]);
      if (!name_bytes) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      std::string_view name{reinterpret_cast<const char *>(name_bytes->data()),
                            name_bytes->size()};
      if (const auto terminator = name.find('\0');
          terminator != std::string_view::npos) {
        name = name.substr(0, terminator);
      }
      const auto identifier = darwin::sysctl::resolve_name(name);
      if (!identifier) {
        bsd_error(cpu, darwin::error::no_entry);
        return;
      }
      const auto required =
          static_cast<std::uint32_t>(identifier->size * sizeof(std::uint32_t));
      if (registers[3] == 0 || !memory_.write32(registers[3], required)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[2] != 0) {
        if (*old_size < required) {
          bsd_error(cpu, darwin::error::no_memory);
          return;
        }
        for (std::size_t index = 0; index < identifier->size; ++index) {
          if (!memory_.write32(registers[2] +
                                   static_cast<std::uint32_t>(index * 4U),
                               identifier->components[index])) {
            bsd_error(cpu, bsd_support::bad_address);
            return;
          }
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    const auto route_operation =
        registers[1] == 6 ? memory_.read32(registers[0] + 16) : std::nullopt;
    if (*mib0 == 4 && *mib1 == darwin::route::protocol_family &&
        route_operation &&
        (*route_operation == darwin::route::sysctl_dump ||
         *route_operation == darwin::route::sysctl_flags ||
         *route_operation == darwin::route::sysctl_dump2)) {
      // CTL_NET/PF_ROUTE/NET_RT_DUMP, NET_RT_FLAGS, or NET_RT_DUMP2.
      // rt_msghdr and ARM32 rt_msghdr2 are both 92 bytes; offsets 16,
      // 20, and 24 become refcnt/parentflags/reserved for RTM_GET2.
      const auto address_family = memory_.read32(registers[0] + 12);
      const auto flags = memory_.read32(registers[0] + 20);
      if (!address_family || !flags || registers[3] == 0) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[4] != 0) {
        bsd_error(cpu, darwin::error::operation_not_permitted);
        return;
      }
      const auto routes = shared_state_->route_table.snapshot();
      const auto records = darwin::route::make_table_dump(
          routes, *address_family,
          *route_operation == darwin::route::sysctl_flags ? *flags : 0U,
          *route_operation == darwin::route::sysctl_dump2
              ? darwin::route::message_get2
              : darwin::route::message_get);
      const auto required = static_cast<std::uint32_t>(records.size());
      if (!memory_.write32(registers[3], required)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[2] != 0) {
        if (*old_size < required) {
          bsd_error(cpu, darwin::error::no_memory);
          return;
        }
        if (!memory_.copy_in(registers[2], records)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    if (*mib0 == 4 && *mib1 == darwin::route::protocol_family &&
        registers[1] == 6 &&
        memory_.read32(registers[0] + 16).value_or(0) ==
            darwin::route::sysctl_interface_list) {
      // CTL_NET/PF_ROUTE/NET_RT_IFLIST. XNU returns a sequence of
      // variable-length if_msghdr/ifa_msghdr records, not host ifaddrs.
      const auto address_family = memory_.read32(registers[0] + 12);
      const auto interface_index = memory_.read32(registers[0] + 20);
      if (!address_family || !interface_index || registers[3] == 0) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[4] != 0) {
        bsd_error(cpu, darwin::error::operation_not_permitted);
        return;
      }
      std::vector<darwin::network::InterfaceSnapshot> interfaces;
      {
        std::lock_guard network_lock{shared_state_->network_mutex};
        interfaces.reserve(shared_state_->network_interfaces.size());
        for (const auto &[name, interface] :
             shared_state_->network_interfaces) {
          interfaces.push_back(
              kernel_network::make_interface_snapshot(name, interface));
        }
      }
      std::sort(interfaces.begin(), interfaces.end(),
                [](const auto &left, const auto &right) {
                  return left.index < right.index;
                });
      const auto records = darwin::network::make_route_interface_list(
          interfaces, *address_family, *interface_index);
      const auto required = static_cast<std::uint32_t>(records.size());
      if (!memory_.write32(registers[3], required)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[2] != 0) {
        if (*old_size < required) {
          bsd_error(cpu, 12); // ENOMEM
          return;
        }
        if (!memory_.copy_in(registers[2], records)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    if (*mib0 == darwin::sysctl::control_kernel &&
        *mib1 == darwin::sysctl::kernel_process_arguments &&
        registers[1] == 3) { // KERN_PROCARGS / pid
      const auto requested_pid = memory_.read32(registers[0] + 8U);
      if (!requested_pid || registers[3] == 0) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[4] != 0) {
        bsd_error(cpu, darwin::error::operation_not_permitted);
        return;
      }
      const auto process = shared_state_->processes.find(*requested_pid);
      if (process == shared_state_->processes.end()) {
        bsd_error(cpu, bsd_support::invalid_argument);
        return;
      }
      const auto &record = process->second;
      const auto path = record.executable_path.empty()
                            ? std::string{"/"} + record.command
                            : record.executable_path;
      const auto bytes = darwin::sysctl::encode_process_arguments(
          path, record.arguments, record.environment);
      const auto required = static_cast<std::uint32_t>(bytes.size());
      if (registers[2] == 0) {
        if (!memory_.write32(registers[3], required)) {
          bsd_error(cpu, bsd_support::bad_address);
        } else {
          bsd_success(cpu, 0);
        }
        return;
      }
      const auto copied = std::min(*old_size, required);
      if (copied == 0 ||
          !memory_.copy_in(registers[2],
                           std::span<const std::byte>{bytes}.first(copied)) ||
          !memory_.write32(registers[3], copied)) {
        bsd_error(cpu, copied == 0 ? bsd_support::invalid_argument
                                   : bsd_support::bad_address);
        return;
      }
      bsd_success(cpu, 0);
      return;
    }
    if (*mib0 == 1 && *mib1 == 14 && registers[1] == 4 &&
        memory_.read32(registers[0] + 8).value_or(0xffffffffU) == 1U) {
      // CTL_KERN/KERN_PROC/KERN_PROC_PID. XNU 792's 32-bit
      // kinfo_proc is 492 bytes (extern_proc=196, eproc=296).
      constexpr std::uint32_t kinfo_proc_size = 492;
      const auto requested_pid = memory_.read32(registers[0] + 12);
      if (!requested_pid || registers[3] == 0) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      const auto process = shared_state_->processes.find(*requested_pid);
      if (registers[2] == 0) {
        const auto required = process == shared_state_->processes.end()
                                  ? 5U * kinfo_proc_size
                                  : 6U * kinfo_proc_size;
        if (!memory_.write32(registers[3], required)) {
          bsd_error(cpu, bsd_support::bad_address);
        } else {
          bsd_success(cpu, 0);
        }
        return;
      }
      if (process == shared_state_->processes.end()) {
        if (!memory_.write32(registers[3], 0))
          bsd_error(cpu, bsd_support::bad_address);
        else
          bsd_success(cpu, 0);
        return;
      }
      if (*old_size < kinfo_proc_size) {
        if (!memory_.write32(registers[3], 0))
          bsd_error(cpu, bsd_support::bad_address);
        else
          bsd_error(cpu, 12); // ENOMEM
        return;
      }
      std::vector<std::byte> bytes(kinfo_proc_size);
      const auto put16 = [&](std::size_t offset, std::uint16_t value) {
        bytes[offset] = static_cast<std::byte>(value);
        bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
      };
      const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
          bytes[offset + byte] = static_cast<std::byte>(value >> (byte * 8U));
        }
      };
      const auto &record = process->second;
      bytes[20] = static_cast<std::byte>(record.exited ? 5U : 2U); // SZOMB/SRUN
      put32(24, *requested_pid);
      bytes[162] = static_cast<std::byte>(process_.nice_value);
      for (std::size_t index = 0;
           index < std::min<std::size_t>(16, record.command.size()); ++index) {
        bytes[163 + index] = static_cast<std::byte>(record.command[index]);
      }
      put16(188, static_cast<std::uint16_t>(record.exit_status));
      put32(280, record.uid); // e_pcred.p_ruid
      put32(284, record.uid); // e_pcred.p_svuid
      put32(288, record.gid); // e_pcred.p_rgid
      put32(292, record.gid); // e_pcred.p_svgid
      put32(304, record.effective_uid); // e_ucred.cr_uid
      put16(308, 1);          // e_ucred.cr_ngroups
      put32(312, record.effective_gid); // e_ucred.cr_groups[0]
      put32(416, record.parent_pid);
      put32(420, record.process_group);
      if (!memory_.copy_in(registers[2], bytes) ||
          !memory_.write32(registers[3], kinfo_proc_size)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      bsd_success(cpu, 0);
      return;
    }
    if (*mib0 == 1 && *mib1 == 65) { // KERN_OSVERSION
      constexpr std::string_view build_version{"1A543a"};
      const auto required =
          static_cast<std::uint32_t>(build_version.size() + 1);
      if (!memory_.write32(registers[3], required)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[2] != 0) {
        if (*old_size < required) {
          bsd_error(cpu, 12); // ENOMEM
          return;
        }
        std::array<std::byte, 7> bytes{};
        for (std::size_t index = 0; index < build_version.size(); ++index) {
          bytes[index] = static_cast<std::byte>(build_version[index]);
        }
        if (!memory_.copy_in(registers[2], bytes)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    if (*mib0 == 1 && *mib1 == 61 && registers[1] == 3) { // KERN_TFP
      const auto selector = memory_.read32(registers[0] + 8);
      if (!selector || (*selector != 2 && *selector != 3) ||
          registers[4] == 0 || registers[5] != 4) {
        bsd_error(cpu, bsd_support::invalid_argument);
        return;
      }
      const auto group = memory_.read32(registers[4]);
      if (!group) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      shared_state_->task_for_pid_groups[*selector - 2] = *group;
      bsd_success(cpu, 0);
      return;
    }
    if (*mib0 == 1 && *mib1 == 5) { // KERN_MAXVNODES (read/write)
      const auto previous = shared_state_->desired_vnodes;
      if (registers[4] != 0) {
        if (registers[5] != 4) {
          bsd_error(cpu, bsd_support::invalid_argument);
          return;
        }
        const auto requested = memory_.read32(registers[4]);
        if (!requested) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        shared_state_->desired_vnodes = *requested;
      }
      if (registers[3] != 0) {
        if (!memory_.write32(registers[3], 4) ||
            (registers[2] != 0 &&
             (*old_size < 4 || !memory_.write32(registers[2], previous)))) {
          bsd_error(cpu, *old_size < 4 ? 12 : bsd_support::bad_address);
          return;
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    if (*mib0 == 1 && *mib1 == 10) { // KERN_HOSTNAME (read/write)
      const auto previous = shared_state_->hostname;
      if (registers[4] != 0) {
        if (registers[5] == 0 || registers[5] > 256) {
          bsd_error(cpu, bsd_support::invalid_argument);
          return;
        }
        const auto bytes = memory_.read_bytes(registers[4], registers[5]);
        if (!bytes) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        shared_state_->hostname.assign(
            reinterpret_cast<const char *>(bytes->data()), bytes->size());
        if (!shared_state_->hostname.empty() &&
            shared_state_->hostname.back() == '\0') {
          shared_state_->hostname.pop_back();
        }
      }
      if (registers[3] != 0) {
        const auto required = static_cast<std::uint32_t>(previous.size() + 1);
        if (!memory_.write32(registers[3], required)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
        if (registers[2] != 0) {
          if (*old_size < required) {
            bsd_error(cpu, 12);
            return;
          }
          std::vector<std::byte> bytes(required);
          std::transform(
              previous.begin(), previous.end(), bytes.begin(),
              [](char value) { return static_cast<std::byte>(value); });
          if (!memory_.copy_in(registers[2], bytes)) {
            bsd_error(cpu, bsd_support::bad_address);
            return;
          }
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    if (*mib0 == 6 && *mib1 == 24) { // HW_MEMSIZE
      if (registers[3] == 0 || !memory_.write32(registers[3], 8)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[2] != 0) {
        if (*old_size < 8) {
          bsd_error(cpu, 12);
          return;
        }
        if (!memory_.write64(registers[2], 0x08000000ULL)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    if (const auto hardware_string =
            *mib0 == darwin::sysctl::control_hardware
                ? darwin::sysctl::hardware_string(*mib1)
                : std::nullopt) {
      const auto required =
          static_cast<std::uint32_t>(hardware_string->size() + 1);
      if (registers[4] != 0) {
        bsd_error(cpu, darwin::error::operation_not_permitted);
        return;
      }
      if (registers[3] == 0 || !memory_.write32(registers[3], required)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[2] != 0) {
        if (*old_size < required) {
          bsd_error(cpu, darwin::error::no_memory);
          return;
        }
        std::vector<std::byte> bytes(required);
        std::transform(
            hardware_string->begin(), hardware_string->end(), bytes.begin(),
            [](char value) { return static_cast<std::byte>(value); });
        if (!memory_.copy_in(registers[2], bytes)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    if ((*mib0 == 1 && (*mib1 == 6 || *mib1 == 7 || *mib1 == 8 || *mib1 == 35 ||
                        *mib1 == 40 || *mib1 == 54 || *mib1 == 66)) ||
        (*mib0 == 6 &&
         (*mib1 == 3 || *mib1 == 4 || *mib1 == 5 || *mib1 == 6 || *mib1 == 7 ||
          *mib1 == 11 || *mib1 == 13 || *mib1 == 25))) {
      // Common read-only Darwin 8 capacity/boot values plus HW_NCPU.
      if (registers[4] != 0) {
        bsd_error(cpu, 1); // EPERM: read-only MIB
        return;
      }
      if (!memory_.write32(registers[3], 4)) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (registers[2] != 0) {
        if (*old_size < 4) {
          bsd_error(cpu, 12); // ENOMEM
          return;
        }
        std::uint32_t value = 1;
        if (*mib0 == 1) {
          switch (*mib1) {
          case 5:
            value = 65'536;
            break; // KERN_MAXVNODES
          case 6:
            value = 2'048;
            break; // KERN_MAXPROC
          case 7:
            value = 12'288;
            break; // KERN_MAXFILES
          case 8:
            value = 262'144;
            break; // KERN_ARGMAX
          case 35:
            value = 0x30000000U;
            break; // KERN_USRSTACK32
          case 40: // KERN_NETBOOT
          case 66:
            value = 0;
            break; // KERN_SAFEBOOT
          default:
            value = 1;
            break;
          }
        } else {
          switch (*mib1) {
          case 4:
            value = 1234;
            break; // HW_BYTEORDER
          case 5:
            value = 0x08000000U;
            break; // HW_PHYSMEM
          case 6:
            value = 0x07000000U;
            break; // HW_USERMEM
          case 7:
            value = 4096;
            break; // HW_PAGESIZE
          case 11:
            value = 1;
            break; // HW_FLOATINGPT
          case 13:
            value = 0;
            break; // HW_VECTORUNIT
          default:
            value = 1;
            break; // CPU counts
          }
        }
        if (!memory_.write32(registers[2], value)) {
          bsd_error(cpu, bsd_support::bad_address);
          return;
        }
      }
      bsd_success(cpu, 0);
      return;
    }
    std::ostringstream message;
    message << "[sysctl] unsupported mib";
    for (std::uint32_t index = 0; index < registers[1] && index < 8; ++index) {
      message
          << ' '
          << memory_.read32(registers[0] + index * 4U).value_or(0xffffffffU);
    }
    message << '\n';
    output_.write(message.str());
    trace_unknown(cpu, "BSD syscall", number);
    bsd_error(cpu, bsd_support::not_implemented);
    return;
  }
  case 362: { // kqueue
    const auto fd = allocate_file_descriptor();
    if (!fd) {
      bsd_error(cpu, 24); // EMFILE
      return;
    }
    virtual_descriptors_.emplace(*fd, "kqueue");
    kqueues_.emplace(*fd, std::vector<KeventRegistration>{});
    bsd_success(cpu, *fd);
    return;
  }
  case 363: { // kevent
    const auto fd = registers[0];
    const auto queue = kqueues_.find(fd);
    if (queue == kqueues_.end() || registers[2] > 4096 || registers[4] > 4096) {
      bsd_error(cpu, queue == kqueues_.end() ? bsd_support::bad_file_descriptor
                                             : bsd_support::invalid_argument);
      return;
    }
    for (std::uint32_t index = 0; index < registers[2]; ++index) {
      const auto address =
          registers[1] + index * darwin::kqueue::arm32_event::size;
      const auto ident = memory_.read32(
          address + darwin::kqueue::arm32_event::identifier_offset);
      const auto filter =
          memory_.read16(address + darwin::kqueue::arm32_event::filter_offset);
      const auto flags =
          memory_.read16(address + darwin::kqueue::arm32_event::flags_offset);
      const auto filter_flags = memory_.read32(
          address + darwin::kqueue::arm32_event::filter_flags_offset);
      const auto data =
          memory_.read32(address + darwin::kqueue::arm32_event::data_offset);
      const auto user_data = memory_.read32(
          address + darwin::kqueue::arm32_event::user_data_offset);
      if (!ident || !filter || !flags || !filter_flags || !data || !user_data) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      const auto signed_filter = static_cast<std::int16_t>(*filter);
      auto found = std::find_if(queue->second.begin(), queue->second.end(),
                                [&](const auto &registration) {
                                  return registration.ident == *ident &&
                                         registration.filter == signed_filter;
                                });
      if ((*flags & darwin::kqueue::event_delete) != 0) {
        if (found != queue->second.end())
          queue->second.erase(found);
        continue;
      }
      if ((*flags & darwin::kqueue::event_add) != 0) {
        KeventRegistration registration{
            *ident,
            signed_filter,
            *flags,
            *filter_flags,
            static_cast<std::int32_t>(*data),
            *user_data,
        };
        if (found == queue->second.end()) {
          queue->second.push_back(registration);
        } else {
          *found = registration;
        }
      }
    }
    std::optional<std::uint64_t> timeout_deadline;
    bool poll_only = false;
    if (registers[5] != 0) {
      const auto seconds_word = memory_.read32(
          registers[5] + darwin::kqueue::arm32_timespec::seconds_offset);
      const auto nanoseconds_word = memory_.read32(
          registers[5] + darwin::kqueue::arm32_timespec::nanoseconds_offset);
      if (!seconds_word || !nanoseconds_word) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      const auto seconds = static_cast<std::int32_t>(*seconds_word);
      const auto nanoseconds = static_cast<std::int32_t>(*nanoseconds_word);
      if (seconds < 0 || nanoseconds < 0 ||
          static_cast<std::uint64_t>(nanoseconds) >=
              darwin::kqueue::nanoseconds_per_second) {
        bsd_error(cpu, bsd_support::invalid_argument);
        return;
      }
      const auto duration = static_cast<std::uint64_t>(seconds) *
                                darwin::kqueue::nanoseconds_per_second +
                            static_cast<std::uint64_t>(nanoseconds);
      poll_only = duration == 0;
      const auto now = shared_state_->clock.now();
      timeout_deadline =
          duration > std::numeric_limits<std::uint64_t>::max() - now
              ? std::numeric_limits<std::uint64_t>::max()
              : now + duration;
    }
    if (registers[4] != 0) {
      const auto ready = collect_ready_kevents(fd, registers[3], registers[4]);
      if (!ready) {
        bsd_error(cpu, bsd_support::bad_address);
        return;
      }
      if (*ready != 0) {
        bsd_success(cpu, *ready);
        return;
      }
    }
    if (registers[4] == 0) {
      bsd_success(cpu, 0);
      return;
    }
    if (poll_only) {
      bsd_success(cpu, 0);
      return;
    }
    process_.waiting_for_events = true;
    pending_kevents_[cpu.processor_id()] = PendingKevent{
        fd, registers[3], registers[4], cpu.processor_id(), timeout_deadline};
    output_.write("[network] kevent wait pid=" + std::to_string(process_.pid) +
                  " fd=" + std::to_string(fd) + " registrations=" +
                  std::to_string(queue->second.size()) + "\n");
    bsd_success(cpu, 0);
    cpu.halt(Dynarmic::HaltReason::UserDefined5);
    return;
  }
  default:
    trace_unknown(cpu, "BSD syscall", number);
    return;
  }
}

} // namespace ilegacysim
