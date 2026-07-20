#include "ilegacysim/kernel_iokit.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ilegacysim/address_space.hpp"
#include "ilegacysim/device_mig_ids.hpp"
#include "ilegacysim/iokit_abi.hpp"
#include "ilegacysim/kernel_iokit_baseband.hpp"
#include "ilegacysim/kernel_iokit_display.hpp"
#include "ilegacysim/kernel_shared_state.hpp"
#include "ilegacysim/mig_wire_abi.hpp"
#include "ilegacysim/output.hpp"

#include "iokit/power.hpp"

namespace ilegacysim {
namespace {

constexpr std::uint32_t mach_rcv_invalid_data = 0x10004008U;
constexpr std::uint32_t mach_reply_bits = 0x00000012U;
constexpr std::uint32_t mach_ndr_native = 0x00000000U;
constexpr std::uint32_t mach_ndr_little_endian = 0x00000001U;
constexpr std::uint32_t mig_reply_id_delta = 100;
constexpr std::string_view apple_h1clcd_class{"AppleH1CLCD"};
constexpr std::string_view mobile_framebuffer_class{"IOMobileFramebuffer"};
constexpr std::size_t maximum_matching_trace_bytes = 256;

namespace device_mig = xnu792::mig::device;

constexpr const auto &matching_services_matching =
    device_mig::io_service_get_matching_services_arguments[1];
constexpr const auto &notification_type =
    device_mig::io_service_add_notification_arguments[1];
constexpr const auto &notification_matching =
    device_mig::io_service_add_notification_arguments[2];
constexpr const auto &notification_wake_port =
    device_mig::io_service_add_notification_arguments[3];
constexpr const auto &notification_reference =
    device_mig::io_service_add_notification_arguments[4];
constexpr const auto &set_properties_data =
    device_mig::io_registry_entry_set_properties_arguments[1];
constexpr const auto &service_open_owning_task =
    device_mig::io_service_open_arguments[1];
constexpr const auto &service_open_connect_type =
    device_mig::io_service_open_arguments[2];

static_assert(
    device_mig::id(device_mig::Routine::io_service_get_matching_services) ==
    static_cast<std::uint32_t>(iokit_abi::Message::ServiceGetMatchingServices));
static_assert(
    device_mig::id(device_mig::Routine::io_service_add_notification) ==
    static_cast<std::uint32_t>(iokit_abi::Message::ServiceAddNotification));
static_assert(
    device_mig::id(device_mig::Routine::io_registry_entry_set_properties) ==
    static_cast<std::uint32_t>(iokit_abi::Message::RegistryEntrySetProperties));
static_assert(device_mig::id(device_mig::Routine::io_service_open) ==
              static_cast<std::uint32_t>(iokit_abi::Message::ServiceOpen));

struct ConnectMethodRequest {
  std::uint32_t selector{};
  std::array<std::uint64_t, iokit_abi::connect_method::maximum_scalar_count>
      scalar_input{};
  std::uint32_t scalar_input_count{};
  std::vector<std::byte> inband_input;
  std::uint32_t scalar_output_capacity{};
  std::uint32_t inband_output_capacity{};
};

struct ConnectMethodResult {
  std::uint32_t return_code{iokit_abi::unsupported};
  std::vector<std::uint64_t> scalar_output;
  std::vector<std::byte> inband_output;
};

template <std::size_t Size>
std::uint32_t write_reply(AddressSpace &memory, std::uint32_t address,
                          const std::array<std::uint32_t, Size> &reply);

constexpr std::uint32_t align_mig_field(std::uint32_t size) {
  constexpr std::uint32_t alignment = sizeof(std::uint32_t);
  return (size + alignment - 1U) & ~(alignment - 1U);
}

std::optional<ConnectMethodRequest>
read_connect_method_request(AddressSpace &memory, std::uint32_t address,
                            std::uint32_t send_size,
                            std::uint32_t receive_size) {
  using namespace iokit_abi::connect_method;
  if (send_size < minimum_request_size || receive_size < minimum_reply_size) {
    return std::nullopt;
  }

  ConnectMethodRequest request;
  request.selector = memory.read32(address + selector_offset).value_or(0);
  request.scalar_input_count =
      memory.read32(address + scalar_input_count_offset)
          .value_or(maximum_scalar_count + 1U);
  if (request.scalar_input_count > maximum_scalar_count) {
    return std::nullopt;
  }
  const auto scalar_bytes = request.scalar_input_count * scalar_size;
  const auto inband_count_offset = scalar_input_offset + scalar_bytes;
  if (inband_count_offset + inband_count_size > send_size) {
    return std::nullopt;
  }
  const auto inband_count = memory.read32(address + inband_count_offset);
  if (!inband_count || *inband_count > maximum_inband_size) {
    return std::nullopt;
  }
  const auto trailing_offset =
      inband_count_offset + inband_count_size + align_mig_field(*inband_count);
  const auto required_size = trailing_offset + trailing_request_size;
  if (send_size < required_size)
    return std::nullopt;

  for (std::uint32_t index = 0; index < request.scalar_input_count; ++index) {
    const auto scalar_address =
        address + scalar_input_offset + index * scalar_size;
    const auto low = memory.read32(scalar_address);
    const auto high = memory.read32(scalar_address + sizeof(std::uint32_t));
    if (!low || !high)
      return std::nullopt;
    request.scalar_input[index] = static_cast<std::uint64_t>(*low) |
                                  (static_cast<std::uint64_t>(*high) << 32U);
  }
  const auto inband_address = address + inband_count_offset + inband_count_size;
  if (*inband_count != 0) {
    const auto bytes = memory.read_bytes(inband_address, *inband_count);
    if (!bytes)
      return std::nullopt;
    request.inband_input = std::move(*bytes);
  }
  // ool_input occupies the first two trailing words. The two CountInOut
  // capacities immediately follow it in the firmware-generated request.
  request.scalar_output_capacity =
      memory.read32(address + trailing_offset + 2U * sizeof(std::uint32_t))
          .value_or(0);
  request.inband_output_capacity =
      memory.read32(address + trailing_offset + 3U * sizeof(std::uint32_t))
          .value_or(0);
  if (request.scalar_output_capacity > maximum_scalar_count ||
      request.inband_output_capacity > maximum_inband_size) {
    return std::nullopt;
  }
  return request;
}

std::uint32_t write_connect_method_reply(AddressSpace &memory,
                                         std::uint32_t address,
                                         std::uint32_t local_port,
                                         std::uint32_t message_id,
                                         std::uint32_t receive_size,
                                         const ConnectMethodResult &result) {
  using namespace iokit_abi::connect_method;
  const auto scalar_count = static_cast<std::uint32_t>(
      std::min<std::size_t>(result.scalar_output.size(), maximum_scalar_count));
  const auto inband_count = static_cast<std::uint32_t>(
      std::min<std::size_t>(result.inband_output.size(), maximum_inband_size));
  const auto reply_size =
      inband_output_offset(scalar_count) + align_mig_field(inband_count);
  if (reply_size > receive_size)
    return mach_rcv_invalid_data;
  const std::vector<std::byte> cleared_reply(reply_size);
  if (!memory.copy_in(address, cleared_reply))
    return mach_rcv_invalid_data;
  const auto write = [&](std::uint32_t offset, std::uint32_t value) {
    return memory.write32(address + offset, value);
  };
  if (!write(darwin::mig_wire::header_bits_offset, mach_reply_bits) ||
      !write(darwin::mig_wire::header_size_offset, reply_size) ||
      !write(darwin::mig_wire::header_remote_port_offset, local_port) ||
      !write(darwin::mig_wire::header_identifier_offset,
             message_id + mig_reply_id_delta) ||
      !write(darwin::mig_wire::message_header_size, mach_ndr_native) ||
      !write(darwin::mig_wire::message_header_size +
                 darwin::mig_wire::word_size,
             mach_ndr_little_endian) ||
      !write(return_code_offset, result.return_code) ||
      !write(scalar_output_count_offset, scalar_count)) {
    return mach_rcv_invalid_data;
  }
  for (std::uint32_t index = 0; index < scalar_count; ++index) {
    const auto scalar_offset = scalar_output_offset + index * scalar_size;
    if (!write(scalar_offset,
               static_cast<std::uint32_t>(result.scalar_output[index])) ||
        !write(
            scalar_offset + sizeof(std::uint32_t),
            static_cast<std::uint32_t>(result.scalar_output[index] >> 32U))) {
      return mach_rcv_invalid_data;
    }
  }
  if (!write(inband_output_count_offset(scalar_count), inband_count)) {
    return mach_rcv_invalid_data;
  }
  if (inband_count != 0 &&
      !memory.copy_in(address + inband_output_offset(scalar_count),
                      std::span<const std::byte>{result.inband_output.data(),
                                                 inband_count})) {
    return mach_rcv_invalid_data;
  }
  return 0;
}

bool contains_text(std::span<const std::byte> bytes, std::string_view text) {
  if (text.empty() || bytes.size() < text.size())
    return false;
  return std::search(bytes.begin(), bytes.end(), text.begin(), text.end(),
                     [](std::byte byte, char character) {
                       return std::to_integer<unsigned char>(byte) ==
                              static_cast<unsigned char>(character);
                     }) != bytes.end();
}

bool trace_repeated_call(std::uint64_t count) {
  constexpr std::uint64_t initial_trace_count = 16;
  return count <= initial_trace_count ||
         (count != 0 && (count & (count - 1U)) == 0);
}

std::string format_call_site(AddressSpace &memory,
                             const IOKitMachCallSite &call_site) {
  if (call_site.program_counter == 0 && call_site.link_register == 0 &&
      call_site.frame_pointer == 0) {
    return {};
  }
  std::ostringstream trace;
  trace << std::hex << " pc=0x" << call_site.program_counter << " lr=0x"
        << call_site.link_register << " frames=";
  constexpr std::size_t maximum_frames = 8;
  auto frame = call_site.frame_pointer;
  for (std::size_t depth = 0; depth < maximum_frames && frame != 0; ++depth) {
    const auto previous = memory.read32(frame);
    const auto return_address = memory.read32(frame + sizeof(std::uint32_t));
    if (!previous || !return_address)
      break;
    if (depth != 0)
      trace << ',';
    trace << "0x" << *return_address;
    if (*previous == frame)
      break;
    frame = *previous;
  }
  return trace.str();
}

std::string matching_hex(std::span<const std::byte> matching) {
  constexpr std::string_view digits{"0123456789abcdef"};
  const auto size = std::min(matching.size(), maximum_matching_trace_bytes);
  std::string result;
  result.reserve(size * 2U);
  for (std::size_t index = 0; index < size; ++index) {
    const auto value = std::to_integer<unsigned>(matching[index]);
    result.push_back(digits[value >> 4U]);
    result.push_back(digits[value & 0xfU]);
  }
  return result;
}

std::uint32_t
ensure_mobile_framebuffer_service_locked(KernelSharedState &shared_state) {
  if (shared_state.mobile_framebuffer_service != 0) {
    return shared_state.mobile_framebuffer_service;
  }
  const auto port = shared_state.allocate_mach_object();
  shared_state.mobile_framebuffer_service = port;
  static_cast<void>(shared_state.mach_port_objects.create(port));
  shared_state.mach_queues.try_emplace(port);
  shared_state.iokit_services.emplace(
      port,
      KernelSharedState::IOKitService{std::string{apple_h1clcd_class},
                                      {std::string{mobile_framebuffer_class}}});
  return port;
}

std::uint32_t resolve_task_name_locked(const KernelSharedState &shared_state,
                                       std::uint32_t task, std::uint32_t name) {
  if (const auto object = shared_state.mach_namespaces.resolve(task, name)) {
    return *object;
  }
  // Standalone ABI tests may invoke this module without constructing a
  // CompatibilityKernel. A live guest task always has an ipc_space and an
  // unknown name must not accidentally address a same-valued kernel object.
  return shared_state.mach_namespaces.contains_task(task) ? 0U : name;
}

std::uint32_t copyout_send_locked(KernelSharedState &shared_state,
                                  std::uint32_t task, std::uint32_t object) {
  if (object == xnu792::ipc::null_name)
    return xnu792::ipc::null_name;
  return shared_state.mach_namespaces
      .copyout(task, object, xnu792::ipc::type_mask(xnu792::ipc::Right::Send))
      .value_or(xnu792::ipc::null_name);
}

void populate_matching_services_locked(KernelSharedState &shared_state,
                                       std::span<const std::byte> matching,
                                       std::deque<std::uint32_t> &services) {
  if (kernel_iokit::baseband::matches_service(matching)) {
    services.push_back(
        kernel_iokit::baseband::ensure_service_locked(shared_state));
  }
  // S5L8900/iPhone1,1 uses AppleH1CLCD. AppleMX31IPU belongs to a different
  // first-generation platform and intentionally remains unmatched.
  if (contains_text(matching, apple_h1clcd_class) ||
      contains_text(matching, mobile_framebuffer_class)) {
    services.push_back(ensure_mobile_framebuffer_service_locked(shared_state));
  }
}

template <std::size_t Size>
std::uint32_t write_reply(AddressSpace &memory, std::uint32_t address,
                          const std::array<std::uint32_t, Size> &reply) {
  for (std::size_t index = 0; index < reply.size(); ++index) {
    if (!memory.write32(address + static_cast<std::uint32_t>(index * 4U),
                        reply[index])) {
      return mach_rcv_invalid_data;
    }
  }
  return 0;
}

std::uint32_t write_port_reply(AddressSpace &memory, std::uint32_t address,
                               std::uint32_t local_port,
                               std::uint32_t message_id, std::uint32_t port) {
  const std::array<std::uint32_t, 10> reply{
      0x80000012U,      40, local_port, 0, 0,
      message_id + 100, 1,  port,       0, 0x00110000U,
  };
  return write_reply(memory, address, reply);
}

} // namespace

std::optional<std::uint32_t>
handle_iokit_mach_request(AddressSpace &memory, Output &output,
                          KernelSharedState &shared_state,
                          ProcessContext &process, std::uint32_t message_id,
                          std::uint32_t message_address,
                          std::uint32_t send_size, std::uint32_t receive_size,
                          std::uint32_t remote_port, std::uint32_t local_port,
                          IOKitMachCallSite call_site) {
  std::uint32_t remote_object = 0;
  {
    std::lock_guard mach_lock{shared_state.mach_mutex};
    remote_object =
        resolve_task_name_locked(shared_state, process.pid, remote_port);
  }
  if (remote_object == xnu792::ipc::null_name) {
    if (message_id == static_cast<std::uint32_t>(
                          iokit_abi::Message::ConnectSetNotificationPort) ||
        message_id ==
            static_cast<std::uint32_t>(iokit_abi::Message::ConnectMethod)) {
      output.write(
          "[iokit] invalid-connection pid=" + std::to_string(process.pid) +
          " connection-name=" + std::to_string(remote_port) +
          " id=" + std::to_string(message_id) + "\n");
    }
    return std::nullopt;
  }
  if (const auto power_result = kernel_iokit::handle_power_mach_request(
          memory, output, shared_state, process, message_id, message_address,
          receive_size, remote_object, local_port)) {
    return power_result;
  }
  if (const auto display_result =
          kernel_iokit::display::handle_notification_port_request(
              memory, output, shared_state, process, message_id,
              message_address, send_size, receive_size, remote_object,
              local_port)) {
    return display_result;
  }
  if (message_id == static_cast<std::uint32_t>(
                        iokit_abi::Message::ServiceGetMatchingServices)) {
    // io_service_get_matching_services uses the Darwin 8 c_string wire
    // form generated from device.defs. Until a modeled service matches,
    // return a valid empty iterator so callers take their no-hardware path.
    // receive_size is the mach_msg receive capacity, not the request's
    // send size. Validate only the minimum port reply here; AddressSpace
    // reads below validate the independently sized request buffer.
    if (receive_size < 40U)
      return mach_rcv_invalid_data;
    const auto matching_count =
        memory
            .read32(message_address +
                    matching_services_matching.request_count_offset)
            .value_or(0);
    const auto matching =
        matching_count != 0 && matching_count <= 512U
            ? memory.read_bytes(message_address +
                                    matching_services_matching.request_offset,
                                matching_count)
            : std::nullopt;
    if (!matching)
      return mach_rcv_invalid_data;

    std::uint32_t iterator_object = 0;
    std::uint32_t iterator_name = 0;
    std::size_t matching_service_count = 0;
    {
      std::lock_guard mach_lock{shared_state.mach_mutex};
      iterator_object = shared_state.allocate_mach_object();
      static_cast<void>(shared_state.mach_port_objects.create(iterator_object));
      auto [iterator, inserted] =
          shared_state.iokit_iterators.try_emplace(iterator_object);
      static_cast<void>(inserted);
      populate_matching_services_locked(shared_state, *matching,
                                        iterator->second);
      matching_service_count = iterator->second.size();
      iterator_name =
          copyout_send_locked(shared_state, process.pid, iterator_object);
    }
    output.write("[iokit] matching pid=" + std::to_string(process.pid) +
                 " bytes=" + std::to_string(matching->size()) +
                 " iterator-name=" + std::to_string(iterator_name) +
                 " iterator-object=" + std::to_string(iterator_object) +
                 " matches=" + std::to_string(matching_service_count) +
                 (matching_service_count == 0
                      ? " matching-hex=" + matching_hex(*matching)
                      : "") +
                 "\n");
    return write_port_reply(memory, message_address, local_port, message_id,
                            iterator_name);
  }

  if (message_id ==
      static_cast<std::uint32_t>(iokit_abi::Message::ServiceAddNotification)) {
    // io_service_add_notification. iPhoneOS 1.0 uses routine 0xb21 for
    // the inline matching form. The returned iterator contains services
    // which already matched when notification registration completed.
    if (receive_size < 40U)
      return mach_rcv_invalid_data;
    const auto descriptor_count =
        memory
            .read32(message_address +
                    darwin::mig_wire::complex_descriptor_count_offset)
            .value_or(0);
    const auto notification_port =
        memory.read32(message_address + notification_wake_port.request_offset)
            .value_or(0);
    const auto type_count =
        memory.read32(message_address + notification_type.request_count_offset)
            .value_or(0);
    std::array<std::uint32_t,
               device_mig::io_service_add_notification_arguments.size()>
        element_counts{};
    element_counts[1] = type_count;
    const auto type_layout = xnu792::mig::compute_wire_layout(
        device_mig::io_service_add_notification_arguments,
        xnu792::mig::WireLayoutSide::Request, element_counts);
    if (!type_layout)
      return mach_rcv_invalid_data;
    const auto matching_count_offset = (*type_layout)[2].count_offset;
    const auto matching_count =
        memory.read32(message_address + matching_count_offset).value_or(0);
    element_counts[2] = matching_count;
    const auto matching_layout = xnu792::mig::compute_wire_layout(
        device_mig::io_service_add_notification_arguments,
        xnu792::mig::WireLayoutSide::Request, element_counts);
    if (!matching_layout)
      return mach_rcv_invalid_data;
    const auto reference_count =
        memory.read32(message_address + (*matching_layout)[4].count_offset)
            .value_or(0);
    element_counts[4] = reference_count;
    const auto complete_layout = xnu792::mig::compute_wire_layout(
        device_mig::io_service_add_notification_arguments,
        xnu792::mig::WireLayoutSide::Request, element_counts);
    if (!complete_layout)
      return mach_rcv_invalid_data;
    const auto type_bytes = memory.read_bytes(
        message_address + (*complete_layout)[1].offset, type_count);
    const auto matching_bytes = memory.read_bytes(
        message_address + (*complete_layout)[2].offset, matching_count);
    const auto reference_bytes = memory.read_bytes(
        message_address + (*complete_layout)[4].offset,
        reference_count * notification_reference.element_size);
    if (descriptor_count != 1 || !type_bytes || !matching_bytes ||
        !reference_bytes) {
      return mach_rcv_invalid_data;
    }

    std::uint32_t iterator_object = 0;
    std::uint32_t iterator_name = 0;
    {
      std::lock_guard mach_lock{shared_state.mach_mutex};
      const auto notification_object = resolve_task_name_locked(
          shared_state, process.pid, notification_port);
      if (notification_object == xnu792::ipc::null_name) {
        return mach_rcv_invalid_data;
      }
      iterator_object = shared_state.allocate_mach_object();
      static_cast<void>(shared_state.mach_port_objects.create(iterator_object));
      auto [iterator, inserted] =
          shared_state.iokit_iterators.try_emplace(iterator_object);
      static_cast<void>(inserted);
      populate_matching_services_locked(shared_state, *matching_bytes,
                                        iterator->second);
      std::string type;
      type.reserve(type_bytes->size());
      for (const auto byte : *type_bytes) {
        type.push_back(static_cast<char>(byte));
      }
      if (!type.empty() && type.back() == '\0')
        type.pop_back();
      shared_state.iokit_notifications.push_back(
          KernelSharedState::IOKitNotification{process.pid, notification_object,
                                               std::move(type),
                                               std::move(*matching_bytes)});
      iterator_name =
          copyout_send_locked(shared_state, process.pid, iterator_object);
    }
    output.write("[iokit] notification pid=" + std::to_string(process.pid) +
                 " port=" + std::to_string(notification_port) +
                 " iterator-name=" + std::to_string(iterator_name) +
                 " iterator-object=" + std::to_string(iterator_object) + "\n");
    return write_port_reply(memory, message_address, local_port, message_id,
                            iterator_name);
  }

  if (message_id ==
      static_cast<std::uint32_t>(iokit_abi::Message::IteratorNext)) {
    if (receive_size < 40)
      return mach_rcv_invalid_data;
    std::uint32_t object_port = 0;
    std::uint32_t object_name = 0;
    {
      std::lock_guard mach_lock{shared_state.mach_mutex};
      if (auto iterator = shared_state.iokit_iterators.find(remote_object);
          iterator != shared_state.iokit_iterators.end() &&
          !iterator->second.empty()) {
        object_port = iterator->second.front();
        iterator->second.pop_front();
        object_name =
            copyout_send_locked(shared_state, process.pid, object_port);
      }
    }
    return write_port_reply(memory, message_address, local_port, message_id,
                            object_name);
  }

  if (message_id ==
      static_cast<std::uint32_t>(iokit_abi::Message::RegistryEntryFromPath)) {
    if (receive_size < 40)
      return mach_rcv_invalid_data;
    std::uint32_t options_object = 0;
    std::uint32_t options_name = 0;
    {
      std::lock_guard mach_lock{shared_state.mach_mutex};
      options_object = resolve_task_name_locked(
          shared_state, process.pid, process.io_registry_options_port);
      options_name =
          copyout_send_locked(shared_state, process.pid, options_object);
    }
    return write_port_reply(memory, message_address, local_port, message_id,
                            options_name);
  }

  if (message_id == static_cast<std::uint32_t>(
                        iokit_abi::Message::RegistryEntryGetProperty)) {
    if (receive_size < 36)
      return mach_rcv_invalid_data;
    const std::array<std::uint32_t, 9> reply{
        18,          36,          local_port,           0, 0, message_id + 100,
        0x00000000U, 0x00000001U, iokit_abi::not_found,
    };
    return write_reply(memory, message_address, reply);
  }

  if (message_id == static_cast<std::uint32_t>(
                        iokit_abi::Message::RegistryEntrySetProperties)) {
    if (receive_size < 40U)
      return mach_rcv_invalid_data;
    const auto descriptor_count =
        memory
            .read32(message_address +
                    darwin::mig_wire::complex_descriptor_count_offset)
            .value_or(0);
    const auto data_address =
        memory.read32(message_address + set_properties_data.request_offset)
            .value_or(0);
    const auto data_size =
        memory
            .read32(message_address + set_properties_data.request_offset +
                    darwin::mig_wire::word_size)
            .value_or(0);
    const auto data = descriptor_count == 1 && data_size <= 64U * 1024U
                          ? memory.read_bytes(data_address, data_size)
                          : std::nullopt;
    const auto result = data ? iokit_abi::success : iokit_abi::bad_argument;
    if (data)
      shared_state.nvram_serialized = *data;
    const std::array<std::uint32_t, 10> reply{
        18,          40,          local_port, 0,      0, message_id + 100,
        0x00000000U, 0x00000001U, 0,          result,
    };
    return write_reply(memory, message_address, reply);
  }

  if (message_id ==
      static_cast<std::uint32_t>(iokit_abi::Message::ServiceOpen)) {
    if (receive_size < 40U)
      return mach_rcv_invalid_data;
    // The task port is one complex descriptor and connect_type follows
    // its NDR record, exactly as emitted by the target firmware stub.
    const auto descriptor_count =
        memory
            .read32(message_address +
                    darwin::mig_wire::complex_descriptor_count_offset)
            .value_or(0);
    if (descriptor_count != 1)
      return mach_rcv_invalid_data;
    const auto owning_task_name =
        memory.read32(message_address + service_open_owning_task.request_offset)
            .value_or(0);
    if (owning_task_name == xnu792::ipc::null_name) {
      return mach_rcv_invalid_data;
    }
    const auto connect_type =
        memory
            .read32(message_address + service_open_connect_type.request_offset)
            .value_or(0);
    std::uint32_t connection_object = 0;
    std::uint32_t connection_name = 0;
    {
      std::lock_guard mach_lock{shared_state.mach_mutex};
      const auto owning_task =
          resolve_task_name_locked(shared_state, process.pid, owning_task_name);
      const auto owning_pid = shared_state.task_port_pids.find(owning_task);
      if (owning_pid == shared_state.task_port_pids.end() ||
          owning_pid->second != process.pid) {
        return mach_rcv_invalid_data;
      }
      if (!shared_state.iokit_services.contains(remote_object)) {
        return write_port_reply(memory, message_address, local_port, message_id,
                                0);
      }
      connection_object = shared_state.allocate_mach_object();
      static_cast<void>(
          shared_state.mach_port_objects.create(connection_object));
      shared_state.mach_queues.try_emplace(connection_object);
      shared_state.iokit_connections.emplace(
          connection_object, KernelSharedState::IOKitConnection{
                                 remote_object, process.pid, connect_type});
      connection_name =
          copyout_send_locked(shared_state, process.pid, connection_object);
    }
    output.write("[iokit] open-display pid=" + std::to_string(process.pid) +
                 " service-object=" + std::to_string(remote_object) +
                 " connection-name=" + std::to_string(connection_name) +
                 " connection-object=" + std::to_string(connection_object) +
                 "\n");
    return write_port_reply(memory, message_address, local_port, message_id,
                            connection_name);
  }

  if (message_id ==
      static_cast<std::uint32_t>(iokit_abi::Message::ServiceOpenExtended)) {
    using namespace iokit_abi::service_open_extended;
    if (receive_size < reply_size)
      return mach_rcv_invalid_data;
    const auto descriptor_count =
        memory
            .read32(message_address +
                    darwin::mig_wire::complex_descriptor_count_offset)
            .value_or(0);
    if (descriptor_count != request_descriptor_count) {
      return mach_rcv_invalid_data;
    }
    std::uint32_t connection_object = 0;
    std::uint32_t connection_name = 0;
    bool display_service = false;
    {
      std::lock_guard mach_lock{shared_state.mach_mutex};
      display_service =
          remote_object == shared_state.mobile_framebuffer_service &&
          shared_state.iokit_services.contains(remote_object);
      if (display_service) {
        connection_object = shared_state.allocate_mach_object();
        static_cast<void>(
            shared_state.mach_port_objects.create(connection_object));
        shared_state.mach_queues.try_emplace(connection_object);
        shared_state.iokit_connections.emplace(
            connection_object, KernelSharedState::IOKitConnection{
                                   remote_object, process.pid,
                                   iokit_abi::apple_h1clcd_service_type});
        connection_name =
            copyout_send_locked(shared_state, process.pid, connection_object);
      }
    }
    const auto result =
        display_service ? iokit_abi::success : iokit_abi::unsupported;
    const std::array<std::uint32_t, reply_word_count> reply{
        0x80000012U,      reply_size,  local_port,      0, 0,
        message_id + 100, 1,           connection_name, 0, 0x00110000U,
        0x00000000U,      0x00000001U, result,
    };
    output.write("[iokit] open-extended pid=" + std::to_string(process.pid) +
                 " service-object=" + std::to_string(remote_object) +
                 " connection-name=" + std::to_string(connection_name) +
                 " connection-object=" + std::to_string(connection_object) +
                 " result=" + (display_service ? "success" : "unsupported") +
                 "\n");
    return write_reply(memory, message_address, reply);
  }

  if (message_id ==
      static_cast<std::uint32_t>(iokit_abi::Message::ConnectMethod)) {
    const auto request = read_connect_method_request(memory, message_address,
                                                     send_size, receive_size);
    if (!request)
      return mach_rcv_invalid_data;
    const auto display_result = kernel_iokit::display::dispatch_connect_method(
        shared_state, process, remote_object, request->selector,
        std::span<const std::uint64_t>{request->scalar_input.data(),
                                       request->scalar_input_count},
        request->inband_input, request->scalar_output_capacity);
    const ConnectMethodResult result =
        display_result
            ? ConnectMethodResult{display_result->return_code,
                                  std::move(display_result->scalar_output),
                                  {}}
            : ConnectMethodResult{};
    std::uint64_t method_call_count = 0;
    bool vsync_enabled = false;
    if (request->selector == static_cast<std::uint32_t>(
                                 iokit_abi::AppleH1ClcdSelector::
                                     SetVSyncNotifications)) {
      std::lock_guard mach_lock{shared_state.mach_mutex};
      if (const auto registration =
              shared_state.iokit_display_vsync.find(remote_object);
          registration != shared_state.iokit_display_vsync.end()) {
        method_call_count = registration->second.method_call_count;
        vsync_enabled = registration->second.enabled;
      }
    }
    const auto log_method =
        method_call_count == 0 || trace_repeated_call(method_call_count);
    if (log_method) {
      output.write("[iokit] method pid=" + std::to_string(process.pid) +
                 " connection-name=" + std::to_string(remote_port) +
                 " connection-object=" + std::to_string(remote_object) +
                 " selector=" + std::to_string(request->selector) +
                 " scalar-in=" + std::to_string(request->scalar_input_count) +
                 (request->scalar_input_count >= 1U
                      ? " scalar0=" + std::to_string(request->scalar_input[0])
                      : "") +
                 (request->scalar_input_count >= 2U
                      ? " scalar1=" + std::to_string(request->scalar_input[1])
                      : "") +
                   " scalar-out=" +
                   std::to_string(result.scalar_output.size()) +
                   " result=" + std::to_string(result.return_code) +
                   (method_call_count == 0
                        ? std::string{}
                        : " call=" + std::to_string(method_call_count) +
                              " enabled=" + std::to_string(vsync_enabled)) +
                   (method_call_count == 0
                        ? std::string{}
                        : format_call_site(memory, call_site)) +
                   "\n");
    }
    return write_connect_method_reply(memory, message_address, local_port,
                                      message_id, receive_size, result);
  }

  if (message_id ==
      static_cast<std::uint32_t>(iokit_abi::Message::ServiceClose)) {
    if (receive_size < 24)
      return mach_rcv_invalid_data;
    {
      std::lock_guard mach_lock{shared_state.mach_mutex};
      kernel_iokit::display::close_connection_locked(shared_state,
                                                     remote_object);
      shared_state.iokit_connections.erase(remote_object);
      static_cast<void>(shared_state.mach_port_objects.erase(remote_object));
      shared_state.mach_queues.erase(remote_object);
      static_cast<void>(
          shared_state.mach_namespaces.deallocate(process.pid, remote_port));
    }
    const std::array<std::uint32_t, 9> reply{
        18,          36,          local_port,         0, 0, message_id + 100,
        0x00000000U, 0x00000001U, iokit_abi::success,
    };
    return write_reply(memory, message_address, reply);
  }

  return std::nullopt;
}

} // namespace ilegacysim
