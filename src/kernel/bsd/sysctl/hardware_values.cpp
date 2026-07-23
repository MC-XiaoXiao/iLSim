#include "ilegacysim/darwin_sysctl.hpp"

namespace ilegacysim::darwin::sysctl {

std::optional<std::string_view>
hardware_string(std::uint32_t selector, std::string_view machine,
                std::string_view model) {
  switch (selector) {
  case hardware_machine:
    return machine;
  case hardware_model:
    return model;
  default:
    return std::nullopt;
  }
}

} // namespace ilegacysim::darwin::sysctl
