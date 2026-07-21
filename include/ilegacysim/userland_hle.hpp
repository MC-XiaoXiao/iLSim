#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ilegacysim {

class AddressSpace;
class Cpu;
class Output;

// ARM SVC immediates in this namespace are emitted only into intercepted
// userspace framework entry points. Darwin's syscall gate uses SVC 0x80.
inline constexpr std::uint32_t userland_hle_svc_namespace = 0x00fa0000U;
inline constexpr std::uint32_t userland_hle_svc_namespace_mask = 0x00ff0000U;
inline constexpr std::uint32_t userland_hle_call_mask = 0x0000ffffU;
inline constexpr std::uint32_t userland_hle_thumb_svc = 0x000000faU;

class UserlandHleRegistry;

class UserlandHleCall {
public:
  [[nodiscard]] std::uint32_t argument(std::size_t index) const;
  [[nodiscard]] std::optional<std::string>
  string_argument(std::size_t index, std::size_t maximum_size = 4096) const;
  [[nodiscard]] bool write32(std::uint32_t address, std::uint32_t value);
  [[nodiscard]] std::uint32_t intern_string(std::string_view value);
  [[nodiscard]] std::uint32_t
  allocate_data(std::size_t size,
                std::size_t alignment = alignof(std::max_align_t));
  [[nodiscard]] std::optional<std::uint32_t>
  symbol_address(std::string_view symbol) const;
  [[nodiscard]] bool image_loaded(std::string_view image_suffix) const;
  [[nodiscard]] bool
  image_loaded_beneath(std::string_view directory) const;
  void set_return(std::uint32_t value);
  // Stop intercepting this entry in the current process and execute the
  // original guest implementation from its first instruction.
  void resume_original();
  // Execute the original implementation through a small trampoline while
  // retaining the entry interception for later calls.
  void resume_original_persistently();

  [[nodiscard]] Cpu &cpu() const { return cpu_; }
  [[nodiscard]] AddressSpace &memory() const { return memory_; }
  [[nodiscard]] Output &output() const { return output_; }
  [[nodiscard]] std::uint32_t process_id() const { return process_id_; }
  [[nodiscard]] std::string_view symbol() const { return symbol_; }

private:
  friend class UserlandHleRegistry;
  UserlandHleCall(UserlandHleRegistry &registry, Cpu &cpu, AddressSpace &memory,
                  Output &output, std::uint32_t process_id,
                  std::string_view symbol);

  UserlandHleRegistry &registry_;
  Cpu &cpu_;
  AddressSpace &memory_;
  Output &output_;
  std::uint32_t process_id_{};
  std::string_view symbol_;
  bool resume_original_{};
  bool resume_original_persistently_{};
};

class UserlandHleRegistry {
public:
  using Handler = std::function<void(UserlandHleCall &)>;

  UserlandHleRegistry(AddressSpace &memory, Output &output);

  // Exact registrations take precedence over prefix registrations.
  void register_function(std::string image_suffix, std::string symbol,
                         Handler handler);
  void register_prefix(std::string image_suffix, std::string symbol_prefix,
                       Handler handler);
  // Resolve a stripped Objective-C 1.x instance method by metadata name when
  // the image is mapped. This avoids firmware-version-specific addresses.
  void register_objc_instance_method(std::string image_suffix,
                                     std::string class_name,
                                     std::string selector,
                                     std::string diagnostic_name,
                                     Handler handler);
  // Register a stripped firmware entry point by its preferred Mach-O virtual
  // address. Bit 0 selects a Thumb entry, matching Mach symbol convention.
  void register_address(std::string image_suffix, std::uint32_t virtual_address,
                        std::string diagnostic_name, Handler handler);

  // Called after dyld has copied one file range into guest memory. Returns
  // the number of newly patched ARM entry points.
  [[nodiscard]] std::size_t
  install_mapped_image(Cpu &cpu, std::uint32_t process_id,
                       const std::filesystem::path &image_path,
                       std::uint32_t mapping_address,
                       std::uint32_t mapping_size, std::uint64_t file_offset);

  // Returns true only for a registered HLE SVC. The guest return path is
  // completed with ARM BX lr semantics after the handler returns.
  [[nodiscard]] bool dispatch(Cpu &cpu, std::uint32_t process_id,
                              std::uint32_t svc_immediate);

  [[nodiscard]] std::uint32_t intern_string(std::string_view value);
  [[nodiscard]] std::uint32_t
  allocate_data(std::size_t size,
                std::size_t alignment = alignof(std::max_align_t));
  [[nodiscard]] std::optional<std::uint32_t>
  symbol_address(std::string_view symbol) const;
  [[nodiscard]] bool image_loaded(std::string_view image_suffix) const;
  [[nodiscard]] bool
  image_loaded_beneath(std::string_view directory) const;
  void record_loaded_image(std::string image_path);

  void reset_mappings();
  void inherit_mappings(const UserlandHleRegistry &parent);

private:
  struct Registration {
    std::uint16_t id{};
    std::string image_suffix;
    std::string symbol;
    bool prefix{};
    std::optional<std::uint32_t> virtual_address;
    std::optional<std::pair<std::string, std::string>> objc_instance_method;
    Handler handler;
  };
  struct InstalledCall {
    std::uint16_t id{};
    std::string symbol;
    bool thumb{};
    std::vector<std::byte> original;
  };

  [[nodiscard]] Registration *select_registration(std::string_view image_path,
                                                  std::string_view symbol);
  [[nodiscard]] const Registration *find_registration(std::uint16_t id) const;
  [[nodiscard]] std::uint32_t ensure_string_page();

  AddressSpace &memory_;
  Output &output_;
  std::vector<Registration> registrations_;
  std::map<std::uint32_t, InstalledCall> installed_calls_;
  std::map<std::string, std::uint32_t, std::less<>> installed_symbols_;
  std::set<std::string, std::less<>> loaded_images_;
  std::map<std::string, std::uint32_t, std::less<>> interned_strings_;
  std::uint32_t string_page_{};
  std::uint32_t string_cursor_{};
  std::set<std::uint32_t> data_pages_;
  std::uint32_t data_cursor_{};
  std::map<std::uint32_t, std::uint32_t> persistent_trampolines_;
  std::uint32_t persistent_trampoline_cursor_{0x60000000U};
  // Keep one diagnostic per concrete intercepted symbol. A flat call-count
  // limit hid late framework activity after early startup repeatedly called
  // only a few functions.
  std::set<std::string, std::less<>> traced_symbols_;
};

} // namespace ilegacysim
