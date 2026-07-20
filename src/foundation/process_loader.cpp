#include "ilegacysim/process_loader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ilegacysim {
namespace {

constexpr std::uint32_t stack_base = 0x2ff00000U;
constexpr std::uint32_t stack_size = 0x00100000U;
constexpr std::uint32_t stack_top = stack_base + stack_size;

std::array<std::byte, 4> little_endian_word(std::uint32_t value) {
    return {
        static_cast<std::byte>(value & 0xffU),
        static_cast<std::byte>((value >> 8U) & 0xffU),
        static_cast<std::byte>((value >> 16U) & 0xffU),
        static_cast<std::byte>((value >> 24U) & 0xffU),
    };
}

}  // namespace

ProcessLoader::ProcessLoader(std::filesystem::path rootfs, AddressSpace& memory)
    : rootfs_{std::move(rootfs)}, memory_{memory} {}

std::filesystem::path ProcessLoader::host_path(const std::string& guest_path) const {
    std::filesystem::path relative{guest_path};
    if (relative.is_absolute()) {
        relative = relative.relative_path();
    }
    if (relative.empty()) {
        throw std::runtime_error{"guest path is empty"};
    }
    for (const auto& component : relative) {
        if (component == "..") {
            throw std::runtime_error{"guest path escapes the root filesystem"};
        }
    }
    return rootfs_ / relative;
}

LoadedProcess ProcessLoader::load(
    std::string guest_executable,
    std::vector<std::string> arguments,
    std::vector<std::string> environment) {
    auto executable = MachOImage::parse(host_path(guest_executable));
    if (!executable.dynamic_linker() || !executable.entry_point()) {
        throw std::runtime_error{"executable lacks LC_LOAD_DYLINKER or LC_UNIXTHREAD"};
    }
    auto dynamic_linker = MachOImage::parse(host_path(*executable.dynamic_linker()));
    if (!dynamic_linker.entry_point()) {
        throw std::runtime_error{"dynamic linker lacks LC_UNIXTHREAD"};
    }
    executable.map_into(memory_);
    dynamic_linker.map_into(memory_);
    if (!memory_.map(stack_base, stack_size,
                     MemoryPermission::Read | MemoryPermission::Write)) {
        throw std::runtime_error{"failed to map initial user stack"};
    }

    if (arguments.empty()) {
        arguments.push_back(guest_executable);
    }
    if (environment.empty()) {
        environment = {
            "PATH=/usr/bin:/bin:/usr/sbin:/sbin",
            "HOME=/var/root",
            "SHELL=/bin/sh",
        };
    }

    std::uint32_t string_cursor = stack_top;
    auto push_string = [&](const std::string& value) {
        const auto bytes = static_cast<std::uint32_t>(value.size() + 1);
        if (bytes > string_cursor - stack_base) {
            throw std::runtime_error{"initial stack strings exceed stack mapping"};
        }
        string_cursor -= bytes;
        const auto* data = reinterpret_cast<const std::byte*>(value.c_str());
        if (!memory_.copy_in(string_cursor, std::span<const std::byte>{data, bytes})) {
            throw std::runtime_error{"failed to write initial stack string"};
        }
        return string_cursor;
    };

    std::vector<std::uint32_t> argument_pointers;
    for (auto it = arguments.rbegin(); it != arguments.rend(); ++it) {
        argument_pointers.push_back(push_string(*it));
    }
    std::reverse(argument_pointers.begin(), argument_pointers.end());

    std::vector<std::uint32_t> environment_pointers;
    for (auto it = environment.rbegin(); it != environment.rend(); ++it) {
        environment_pointers.push_back(push_string(*it));
    }
    std::reverse(environment_pointers.begin(), environment_pointers.end());
    const auto executable_path = push_string("executable_path=" + guest_executable);

    const auto text_segment = std::find_if(
        executable.segments().begin(), executable.segments().end(),
        [](const MachSegment& segment) { return segment.file_offset == 0 && segment.file_size != 0; });
    if (text_segment == executable.segments().end()) {
        throw std::runtime_error{"cannot locate the main Mach-O header mapping"};
    }
    const auto main_header = text_segment->vm_address;

    std::vector<std::uint32_t> words;
    words.reserve(5 + argument_pointers.size() + environment_pointers.size());
    words.push_back(main_header);  // dyld's ARM start shim consumes this first.
    words.push_back(static_cast<std::uint32_t>(argument_pointers.size()));
    words.insert(words.end(), argument_pointers.begin(), argument_pointers.end());
    words.push_back(0);
    words.insert(words.end(), environment_pointers.begin(), environment_pointers.end());
    words.push_back(0);
    words.push_back(executable_path);
    words.push_back(0);

    const auto word_bytes = static_cast<std::uint32_t>(words.size() * sizeof(std::uint32_t));
    if (word_bytes > string_cursor - stack_base) {
        throw std::runtime_error{"initial stack vectors exceed stack mapping"};
    }
    auto stack_pointer = (string_cursor - word_bytes) & ~7U;
    if (stack_pointer < stack_base) {
        throw std::runtime_error{"initial stack vectors exceed stack mapping"};
    }
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto encoded = little_endian_word(words[index]);
        if (!memory_.copy_in(stack_pointer + static_cast<std::uint32_t>(index * 4U), encoded)) {
            throw std::runtime_error{"failed to write initial stack vector"};
        }
    }

    const auto dyld_entry = *dynamic_linker.entry_point();
    return LoadedProcess{
        std::move(executable),
        std::move(dynamic_linker),
        dyld_entry,
        stack_pointer,
        main_header,
    };
}

}  // namespace ilegacysim
