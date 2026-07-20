#include "ilegacysim/file_page_cache.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <tuple>

namespace ilegacysim {
namespace {

[[nodiscard]] std::string stable_path(const std::filesystem::path &path) {
  std::error_code error;
  const auto canonical = std::filesystem::canonical(path, error);
  return (error ? path.lexically_normal() : canonical).string();
}

} // namespace

bool FilePageCache::Key::operator<(const Key &other) const {
  return std::tie(path, file_size, modified, file_offset, byte_count) <
         std::tie(other.path, other.file_size, other.modified,
                  other.file_offset, other.byte_count);
}

std::optional<std::vector<std::shared_ptr<GuestPageBacking>>>
FilePageCache::load_pages(const std::filesystem::path &path,
                          std::uint64_t file_offset, std::uint32_t size) {
  if (size == 0 || file_offset % guest_memory_page_size != 0) {
    return std::nullopt;
  }

  std::error_code error;
  const auto file_size = std::filesystem::file_size(path, error);
  if (error || file_offset > file_size || size > file_size - file_offset) {
    return std::nullopt;
  }
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error) return std::nullopt;
  const auto normalized_path = stable_path(path);

  {
    const std::scoped_lock lock{mutex_};
    const auto identity = identities_.find(normalized_path);
    if (identity == identities_.end()) {
      identities_.emplace(normalized_path, Identity{file_size, modified});
    } else if (identity->second.file_size != file_size ||
               identity->second.modified != modified) {
      std::erase_if(pages_, [&](const auto &entry) {
        return entry.first.path == normalized_path;
      });
      identity->second = Identity{file_size, modified};
    }
  }

  std::vector<std::shared_ptr<GuestPageBacking>> result;
  result.reserve(static_cast<std::size_t>(
      (static_cast<std::uint64_t>(size) + guest_memory_page_size - 1U) /
      guest_memory_page_size));
  std::ifstream stream;
  for (std::uint64_t offset = file_offset;
       offset < file_offset + size; offset += guest_memory_page_size) {
    const auto byte_count = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(guest_memory_page_size,
                                file_offset + size - offset));
    const Key key{normalized_path, file_size, modified, offset, byte_count};
    {
      const std::scoped_lock lock{mutex_};
      if (const auto cached = pages_.find(key); cached != pages_.end()) {
        result.push_back(cached->second);
        continue;
      }
    }

    if (!stream.is_open()) {
      stream.open(path, std::ios::binary);
      if (!stream) return std::nullopt;
    }
    if (offset > static_cast<std::uint64_t>(
                     std::numeric_limits<std::streamoff>::max())) {
      return std::nullopt;
    }
    auto page = std::make_shared<GuestPageBacking>();
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(reinterpret_cast<char *>(page->bytes.data()),
                static_cast<std::streamsize>(byte_count));
    if (!stream ||
        static_cast<std::size_t>(stream.gcount()) != byte_count) {
      return std::nullopt;
    }

    {
      const std::scoped_lock lock{mutex_};
      const auto [entry, inserted] = pages_.try_emplace(key, page);
      result.push_back(inserted ? std::move(page) : entry->second);
    }
  }

  const auto final_size = std::filesystem::file_size(path, error);
  if (error || final_size != file_size) return std::nullopt;
  const auto final_modified = std::filesystem::last_write_time(path, error);
  if (error || final_modified != modified) {
    return std::nullopt;
  }
  return result;
}

std::size_t FilePageCache::page_count() const {
  const std::scoped_lock lock{mutex_};
  return pages_.size();
}

} // namespace ilegacysim
