#include "ilegacysim/darwin_sysctl.hpp"

namespace ilegacysim::darwin::sysctl {

std::optional<std::string_view> hardware_string(std::uint32_t selector) {
  switch (selector) {
  case hardware_machine:
    return iphone_2g_machine;
  case hardware_model:
    return iphone_2g_model;
  default:
    return std::nullopt;
  }
}

} // namespace ilegacysim::darwin::sysctl
