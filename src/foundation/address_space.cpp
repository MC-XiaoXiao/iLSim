#include "ilegacysim/address_space.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <type_traits>

namespace ilegacysim {
namespace {

constexpr std::uint32_t page_base(std::uint32_t address) {
  return address & ~(AddressSpace::page_size - 1U);
}

bool range_overflows(std::uint32_t address, std::size_t size) {
  if (size == 0) {
    return false;
  }
  return size - 1 > std::numeric_limits<std::uint32_t>::max() - address;
}

std::uint64_t page_range_end(std::uint32_t address, std::size_t size) {
  return static_cast<std::uint64_t>(
             page_base(address + static_cast<std::uint32_t>(size - 1U))) +
         AddressSpace::page_size;
}

} // namespace

AddressSpace::AddressSpace()
    : file_page_cache_{std::make_shared<FilePageCache>()} {}

bool AddressSpace::map(std::uint32_t address, std::uint32_t size,
                       MemoryPermission permissions) {
  if (size == 0 || range_overflows(address, size)) {
    return size == 0;
  }
  const auto first = page_base(address);
  const auto end = page_range_end(address, size);
  std::unique_lock lock{mutex_};
  vm_map_.map_or(first, end, permissions);
  return true;
}

bool AddressSpace::unmap(std::uint32_t address, std::uint32_t size) {
  if (size == 0 || range_overflows(address, size)) {
    return size == 0;
  }
  const auto first = page_base(address);
  const auto end = page_range_end(address, size);
  std::unique_lock lock{mutex_};
  vm_map_.unmap(first, end);
  auto page = pages_.lower_bound(first);
  while (page != pages_.end() && page->first < end) {
    page = pages_.erase(page);
  }
  return true;
}

void AddressSpace::clear() {
  std::unique_lock lock{mutex_};
  vm_map_.clear();
  pages_.clear();
}

bool AddressSpace::protect(std::uint32_t address, std::uint32_t size,
                           MemoryPermission permissions) {
  if (size == 0 || range_overflows(address, size)) {
    return size == 0;
  }
  const auto first = page_base(address);
  const auto end = page_range_end(address, size);
  std::unique_lock lock{mutex_};
  return vm_map_.protect(first, end, permissions);
}

bool AddressSpace::copy_in(std::uint32_t address,
                           std::span<const std::byte> data) {
  if (range_overflows(address, data.size())) {
    return false;
  }
  std::unique_lock lock{mutex_};
  if (!range_accessible_locked(address, data.size(), MemoryPermission::None)) {
    return false;
  }
  std::size_t copied = 0;
  while (copied < data.size()) {
    const auto current = address + static_cast<std::uint32_t>(copied);
    auto &page = ensure_page_locked(current);
    const auto offset = current & (page_size - 1U);
    const auto chunk = std::min<std::size_t>(
        page_size - offset, data.size() - copied);
    auto &backing = writable_backing_locked(page);
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(copied), chunk,
                backing.bytes.begin() + offset);
    copied += chunk;
  }
  mark_written_locked(address, data.size());
  return true;
}

bool AddressSpace::map_file(std::uint32_t address, std::uint32_t size,
                            MemoryPermission permissions,
                            const std::filesystem::path &path,
                            std::uint64_t file_offset) {
  if (size == 0 || range_overflows(address, size) ||
      address % page_size != 0 || file_offset % page_size != 0) {
    return false;
  }
  const auto backings = file_page_cache_->load_pages(path, file_offset, size);
  if (!backings) return false;

  std::unique_lock lock{mutex_};
  const auto end = page_range_end(address, size);
  if (vm_map_.overlaps(address, end)) return false;
  for (std::size_t index = 0; index < backings->size(); ++index) {
    const auto base = address +
                      static_cast<std::uint32_t>(index * page_size);
    pages_.emplace(base, Page{(*backings)[index], 0, true, false});
  }
  vm_map_.map_or(address, end, permissions);
  return true;
}

std::optional<std::vector<std::shared_ptr<GuestPageBacking>>>
AddressSpace::share_pages(std::uint32_t address, std::uint32_t size) {
  if (size == 0 || address % page_size != 0 || size % page_size != 0 ||
      range_overflows(address, size)) {
    return std::nullopt;
  }

  std::unique_lock lock{mutex_};
  const auto end = page_range_end(address, size);
  if (!vm_map_.accessible(address, end, MemoryPermission::None))
    return std::nullopt;

  std::vector<std::shared_ptr<GuestPageBacking>> result;
  result.reserve(size / page_size);
  for (std::uint64_t base = address; base < end; base += page_size) {
    auto &page = pages_[static_cast<std::uint32_t>(base)];
    if (!page.backing) {
      page.backing = std::make_shared<GuestPageBacking>();
    } else if (page.file_cached ||
               (!page.shared_writable && !page.backing.unique())) {
      page.backing = std::make_shared<GuestPageBacking>(*page.backing);
    }
    page.file_cached = false;
    page.shared_writable = true;
    result.push_back(page.backing);
  }
  return result;
}

bool AddressSpace::map_page_backings(
    std::uint32_t address, std::uint32_t size,
    MemoryPermission permissions,
    std::span<const std::shared_ptr<GuestPageBacking>> backings,
    PageMappingMode mode) {
  if (size == 0 || address % page_size != 0 || size % page_size != 0 ||
      range_overflows(address, size) ||
      backings.size() != size / page_size ||
      std::any_of(backings.begin(), backings.end(),
                  [](const auto &backing) { return !backing; })) {
    return false;
  }

  std::unique_lock lock{mutex_};
  const auto end = page_range_end(address, size);
  if (vm_map_.overlaps(address, end)) return false;
  const auto shared_writable = mode == PageMappingMode::Shared;
  for (std::size_t index = 0; index < backings.size(); ++index) {
    const auto base =
        address + static_cast<std::uint32_t>(index * page_size);
    pages_.emplace(base, Page{backings[index], 0, false, shared_writable});
  }
  vm_map_.map_or(address, end, permissions);
  return true;
}

std::optional<std::vector<std::byte>>
AddressSpace::read_bytes(std::uint32_t address, std::size_t size) const {
  std::shared_lock lock{mutex_};
  if (!range_accessible_locked(address, size, MemoryPermission::Read)) {
    return std::nullopt;
  }
  std::vector<std::byte> result(size);
  std::size_t copied = 0;
  while (copied < size) {
    const auto current = address + static_cast<std::uint32_t>(copied);
    const auto *page = find_page_locked(current);
    const auto offset = current & (page_size - 1U);
    const auto chunk =
        std::min<std::size_t>(page_size - offset, size - copied);
    if (page != nullptr && page->backing) {
      std::copy_n(page->backing->bytes.begin() + offset, chunk,
                  result.begin() + static_cast<std::ptrdiff_t>(copied));
    } else {
      std::fill_n(result.begin() + static_cast<std::ptrdiff_t>(copied), chunk,
                  std::byte{});
    }
    copied += chunk;
  }
  return result;
}

std::optional<std::string>
AddressSpace::read_c_string(std::uint32_t address,
                            std::size_t maximum_size) const {
  std::shared_lock lock{mutex_};
  std::string result;
  result.reserve(std::min<std::size_t>(maximum_size, 256));
  for (std::size_t i = 0; i < maximum_size; ++i) {
    if (range_overflows(address, i + 1)) {
      return std::nullopt;
    }
    const auto current = address + static_cast<std::uint32_t>(i);
    if (!range_accessible_locked(current, 1, MemoryPermission::Read)) {
      return std::nullopt;
    }
    const auto *page = find_page_locked(current);
    const auto value = std::to_integer<char>(
        read_byte_locked(page, current & (page_size - 1U)));
    if (value == '\0') {
      return result;
    }
    result.push_back(value);
  }
  return std::nullopt;
}

const AddressSpace::Page *
AddressSpace::find_page_locked(std::uint32_t address) const {
  const auto it = pages_.find(page_base(address));
  return it == pages_.end() ? nullptr : &it->second;
}

AddressSpace::Page *AddressSpace::find_page_locked(std::uint32_t address) {
  const auto it = pages_.find(page_base(address));
  return it == pages_.end() ? nullptr : &it->second;
}

AddressSpace::Page &AddressSpace::ensure_page_locked(std::uint32_t address) {
  return pages_[page_base(address)];
}

std::byte AddressSpace::read_byte_locked(const Page *page,
                                         std::uint32_t offset) {
  return page != nullptr && page->backing ? page->backing->bytes[offset]
                                           : std::byte{};
}

GuestPageBacking &
AddressSpace::writable_backing_locked(Page &page) {
  if (!page.backing) {
    page.backing = std::make_shared<GuestPageBacking>();
  } else if (page.file_cached ||
             (!page.shared_writable && !page.backing.unique())) {
    page.backing = std::make_shared<GuestPageBacking>(*page.backing);
  }
  page.file_cached = false;
  return *page.backing;
}

void AddressSpace::mark_written_locked(std::uint32_t address,
                                       std::size_t size) {
  if (size == 0) return;
  ++write_generation_;
  const auto first = page_base(address);
  const auto last = page_base(
      address + static_cast<std::uint32_t>(size - 1U));
  for (std::uint64_t base = first; base <= last; base += page_size) {
    pages_.at(static_cast<std::uint32_t>(base)).write_generation =
        write_generation_;
  }
}

bool AddressSpace::range_accessible_locked(std::uint32_t address,
                                           std::size_t size,
                                           MemoryPermission access) const {
  if (range_overflows(address, size)) {
    return false;
  }
  if (size == 0) {
    return true;
  }
  const auto first = page_base(address);
  return vm_map_.accessible(first, page_range_end(address, size), access);
}

template <typename T>
std::optional<T> AddressSpace::read_integer(std::uint32_t address,
                                            MemoryPermission access) const {
  static_assert(std::is_unsigned_v<T>);
  std::shared_lock lock{mutex_};
  if (!range_accessible_locked(address, sizeof(T), access)) {
    return std::nullopt;
  }
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    const auto current = address + static_cast<std::uint32_t>(i);
    const auto *page = find_page_locked(current);
    const auto byte = std::to_integer<T>(
        read_byte_locked(page, current & (page_size - 1U)));
    value |= static_cast<T>(byte << (i * 8U));
  }
  return value;
}

template <typename T>
bool AddressSpace::write_integer(std::uint32_t address, T value) {
  static_assert(std::is_unsigned_v<T>);
  std::unique_lock lock{mutex_};
  if (!range_accessible_locked(address, sizeof(T), MemoryPermission::Write)) {
    return false;
  }
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    const auto current = address + static_cast<std::uint32_t>(i);
    auto &page = ensure_page_locked(current);
    writable_backing_locked(page).bytes[current & (page_size - 1U)] =
        static_cast<std::byte>((value >> (i * 8U)) & static_cast<T>(0xffU));
  }
  mark_written_locked(address, sizeof(T));
  return true;
}

template <typename T>
bool AddressSpace::compare_exchange_integer(std::uint32_t address, T expected,
                                            T value) {
  static_assert(std::is_unsigned_v<T>);
  std::unique_lock lock{mutex_};
  if (!range_accessible_locked(address, sizeof(T),
                               MemoryPermission::Read |
                                   MemoryPermission::Write)) {
    return false;
  }
  T current_value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    const auto current = address + static_cast<std::uint32_t>(i);
    const auto *page = find_page_locked(current);
    current_value |= static_cast<T>(
        std::to_integer<T>(
            read_byte_locked(page, current & (page_size - 1U)))
        << (i * 8U));
  }
  if (current_value != expected) {
    return false;
  }
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    const auto current = address + static_cast<std::uint32_t>(i);
    auto &page = ensure_page_locked(current);
    writable_backing_locked(page).bytes[current & (page_size - 1U)] =
        static_cast<std::byte>((value >> (i * 8U)) & static_cast<T>(0xffU));
  }
  mark_written_locked(address, sizeof(T));
  return true;
}

template <typename T>
std::optional<T> AddressSpace::exchange_integer(std::uint32_t address,
                                                T value) {
  static_assert(std::is_unsigned_v<T>);
  std::unique_lock lock{mutex_};
  if (!range_accessible_locked(address, sizeof(T),
                               MemoryPermission::Read |
                                   MemoryPermission::Write)) {
    return std::nullopt;
  }
  T previous = 0;
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    const auto current = address + static_cast<std::uint32_t>(index);
    const auto *page = find_page_locked(current);
    previous |= static_cast<T>(
        std::to_integer<T>(
            read_byte_locked(page, current & (page_size - 1U)))
        << (index * 8U));
  }
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    const auto current = address + static_cast<std::uint32_t>(index);
    auto &page = ensure_page_locked(current);
    writable_backing_locked(page).bytes[current & (page_size - 1U)] =
        static_cast<std::byte>((value >> (index * 8U)) &
                               static_cast<T>(0xffU));
  }
  mark_written_locked(address, sizeof(T));
  return previous;
}

std::optional<std::uint8_t> AddressSpace::read8(std::uint32_t address,
                                                MemoryPermission access) const {
  return read_integer<std::uint8_t>(address, access);
}
std::optional<std::uint16_t>
AddressSpace::read16(std::uint32_t address, MemoryPermission access) const {
  return read_integer<std::uint16_t>(address, access);
}
std::optional<std::uint32_t>
AddressSpace::read32(std::uint32_t address, MemoryPermission access) const {
  return read_integer<std::uint32_t>(address, access);
}
std::optional<std::uint64_t>
AddressSpace::read64(std::uint32_t address, MemoryPermission access) const {
  return read_integer<std::uint64_t>(address, access);
}

bool AddressSpace::write8(std::uint32_t address, std::uint8_t value) {
  return write_integer(address, value);
}
bool AddressSpace::write16(std::uint32_t address, std::uint16_t value) {
  return write_integer(address, value);
}
bool AddressSpace::write32(std::uint32_t address, std::uint32_t value) {
  return write_integer(address, value);
}
bool AddressSpace::write64(std::uint32_t address, std::uint64_t value) {
  return write_integer(address, value);
}

bool AddressSpace::accessible(std::uint32_t address, std::size_t size,
                              MemoryPermission access) const {
  std::shared_lock lock{mutex_};
  return range_accessible_locked(address, size, access);
}

bool AddressSpace::compare_exchange8(std::uint32_t address,
                                     std::uint8_t expected,
                                     std::uint8_t value) {
  return compare_exchange_integer(address, expected, value);
}
bool AddressSpace::compare_exchange16(std::uint32_t address,
                                      std::uint16_t expected,
                                      std::uint16_t value) {
  return compare_exchange_integer(address, expected, value);
}
bool AddressSpace::compare_exchange32(std::uint32_t address,
                                      std::uint32_t expected,
                                      std::uint32_t value) {
  return compare_exchange_integer(address, expected, value);
}
bool AddressSpace::compare_exchange64(std::uint32_t address,
                                      std::uint64_t expected,
                                      std::uint64_t value) {
  return compare_exchange_integer(address, expected, value);
}

std::optional<std::uint8_t> AddressSpace::exchange8(std::uint32_t address,
                                                    std::uint8_t value) {
  return exchange_integer(address, value);
}

std::optional<std::uint32_t> AddressSpace::exchange32(std::uint32_t address,
                                                      std::uint32_t value) {
  return exchange_integer(address, value);
}

bool AddressSpace::mapped(std::uint32_t address, std::size_t size) const {
  std::shared_lock lock{mutex_};
  return range_accessible_locked(address, size, MemoryPermission::None);
}

std::optional<std::uint64_t> AddressSpace::range_write_generation(
    std::uint32_t address, std::size_t size) const {
  if (size == 0 || range_overflows(address, size)) return std::nullopt;
  const auto first = page_base(address);
  const auto last = page_base(
      address + static_cast<std::uint32_t>(size - 1U));
  std::shared_lock lock{mutex_};
  if (!vm_map_.accessible(first, page_range_end(address, size),
                          MemoryPermission::None)) {
    return std::nullopt;
  }
  std::uint64_t generation = 0;
  for (std::uint64_t base = first; base <= last; base += page_size) {
    const auto page = pages_.find(static_cast<std::uint32_t>(base));
    if (page != pages_.end()) {
      generation = std::max(generation, page->second.write_generation);
    }
  }
  return generation;
}

std::size_t AddressSpace::mapped_page_count() const {
  std::shared_lock lock{mutex_};
  return vm_map_.page_count(page_size);
}

std::size_t AddressSpace::resident_page_count() const {
  std::shared_lock lock{mutex_};
  return static_cast<std::size_t>(
      std::count_if(pages_.begin(), pages_.end(), [](const auto &entry) {
        return static_cast<bool>(entry.second.backing);
      }));
}

std::size_t AddressSpace::shared_page_count() const {
  std::shared_lock lock{mutex_};
  return static_cast<std::size_t>(
      std::count_if(pages_.begin(), pages_.end(), [](const auto &entry) {
        return entry.second.backing && !entry.second.backing.unique();
      }));
}

std::size_t AddressSpace::cached_file_mapping_count() const {
  std::shared_lock lock{mutex_};
  return static_cast<std::size_t>(
      std::count_if(pages_.begin(), pages_.end(), [](const auto &entry) {
        return entry.second.file_cached;
      }));
}

std::size_t AddressSpace::cached_file_page_count() const {
  return file_page_cache_->page_count();
}

std::size_t AddressSpace::mapping_region_count() const {
  std::shared_lock lock{mutex_};
  return vm_map_.region_count();
}

std::unique_ptr<AddressSpace> AddressSpace::clone() const {
  auto result = std::make_unique<AddressSpace>();
  std::shared_lock source_lock{mutex_};
  std::unique_lock destination_lock{result->mutex_};
  result->vm_map_ = vm_map_;
  result->pages_ = pages_;
  result->write_generation_ = write_generation_;
  result->file_page_cache_ = file_page_cache_;
  return result;
}

} // namespace ilegacysim
