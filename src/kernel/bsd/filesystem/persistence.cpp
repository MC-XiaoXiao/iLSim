#include "ilegacysim/kernel.hpp"

#include "ilegacysim/darwin_abi.hpp"

#include <cerrno>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>

#include "../support.hpp"

namespace ilegacysim {

bool CompatibilityKernel::dispatch_bsd_filesystem_persistence(
    Cpu &cpu, std::uint32_t number) {
  if (number != darwin::syscall::synchronize_file)
    return false;

  auto fd = cpu.registers()[0];
  if (const auto duplicate = duplicated_descriptors_.find(fd);
      duplicate != duplicated_descriptors_.end()) {
    fd = duplicate->second;
  }
  const auto descriptor = file_descriptors_.find(fd);
  if (descriptor == file_descriptors_.end()) {
    bsd_error(cpu, bsd_support::bad_file_descriptor);
    return true;
  }

  const auto host_fd = ::open(descriptor->second.c_str(), O_RDONLY | O_CLOEXEC);
  if (host_fd < 0) {
    bsd_error(cpu, bsd_support::darwin_filesystem_error(
                       std::error_code{errno, std::generic_category()}));
    return true;
  }
  const auto result = ::fsync(host_fd);
  const auto sync_error = errno;
  static_cast<void>(::close(host_fd));
  if (result != 0) {
    bsd_error(cpu, bsd_support::darwin_filesystem_error(
                       std::error_code{sync_error, std::generic_category()}));
    return true;
  }
  output_.write("[vfs] fsync fd=" + std::to_string(fd) + "\n");
  bsd_success(cpu, 0);
  return true;
}

} // namespace ilegacysim
