#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ilegacysim::celestial_volume_protocol {

struct CategoryVolume {
  std::string category;
  float value{};
};

// Decode the mediaserverd response that reports the canonical category
// affected by an AVSystemController volume operation. Callers remain agnostic
// to SpringBoard and Preferences; both clients use the same service protocol.
[[nodiscard]] std::optional<CategoryVolume>
decode_reply(std::uint32_t identifier, std::span<const std::byte> bytes);

} // namespace ilegacysim::celestial_volume_protocol
