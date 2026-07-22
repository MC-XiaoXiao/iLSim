#include "ilegacysim/celestial_volume_protocol.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

namespace ilegacysim::celestial_volume_protocol {
namespace {

constexpr std::uint32_t category_volume_reply_identifier = 1138;
constexpr std::size_t return_code_offset = 32;
constexpr std::size_t volume_offset = 36;
constexpr std::size_t category_present_offset = 40;
constexpr std::size_t category_length_offset = 44;
constexpr std::size_t category_offset = 48;
constexpr std::uint32_t maximum_category_length = 128;

std::uint32_t read_word(std::span<const std::byte> bytes,
                        std::size_t offset) {
  std::uint32_t value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

} // namespace

std::optional<CategoryVolume>
decode_reply(std::uint32_t identifier, std::span<const std::byte> bytes) {
  if (identifier != category_volume_reply_identifier ||
      bytes.size() < category_offset ||
      read_word(bytes, return_code_offset) != 0 ||
      read_word(bytes, category_present_offset) == 0) {
    return std::nullopt;
  }

  const auto length = read_word(bytes, category_length_offset);
  if (length == 0 || length > maximum_category_length ||
      length > bytes.size() - category_offset) {
    return std::nullopt;
  }
  const auto value = std::bit_cast<float>(read_word(bytes, volume_offset));
  if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
    return std::nullopt;

  std::string category;
  category.reserve(length);
  for (std::size_t index = 0; index < length; ++index) {
    const auto character =
        std::to_integer<unsigned char>(bytes[category_offset + index]);
    if (character < 0x20U || character > 0x7eU)
      return std::nullopt;
    category.push_back(static_cast<char>(character));
  }
  return CategoryVolume{std::move(category), value};
}

} // namespace ilegacysim::celestial_volume_protocol
