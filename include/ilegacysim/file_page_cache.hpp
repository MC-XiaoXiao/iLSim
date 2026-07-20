#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ilegacysim {

inline constexpr std::uint32_t guest_memory_page_size = 4096;
struct GuestPageBacking {
  std::array<std::byte, guest_memory_page_size> bytes{};
};

// Process-family cache for immutable firmware file pages. AddressSpace keeps a
// strong reference to the cache across fork, while a private write detaches the
// corresponding GuestPageBacking through its normal copy-on-write path.
class FilePageCache {
public:
  [[nodiscard]] std::optional<std::vector<std::shared_ptr<GuestPageBacking>>>
  load_pages(const std::filesystem::path &path, std::uint64_t file_offset,
             std::uint32_t size);

  [[nodiscard]] std::size_t page_count() const;

private:
  struct Identity {
    std::uintmax_t file_size{};
    std::filesystem::file_time_type modified;
  };

  struct Key {
    std::string path;
    std::uintmax_t file_size{};
    std::filesystem::file_time_type modified;
    std::uint64_t file_offset{};
    std::uint32_t byte_count{};

    [[nodiscard]] bool operator<(const Key &other) const;
  };

  mutable std::mutex mutex_;
  std::map<std::string, Identity> identities_;
  std::map<Key, std::shared_ptr<GuestPageBacking>> pages_;
};

} // namespace ilegacysim
