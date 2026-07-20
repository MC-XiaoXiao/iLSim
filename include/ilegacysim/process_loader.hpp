#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ilegacysim/address_space.hpp"
#include "ilegacysim/macho.hpp"

namespace ilegacysim {

struct LoadedProcess {
    MachOImage executable;
    MachOImage dynamic_linker;
    std::uint32_t entry_point{};
    std::uint32_t stack_pointer{};
    std::uint32_t main_header{};
};

class ProcessLoader {
public:
    ProcessLoader(std::filesystem::path rootfs, AddressSpace& memory);

    LoadedProcess load(
        std::string guest_executable,
        std::vector<std::string> arguments = {},
        std::vector<std::string> environment = {});

private:
    [[nodiscard]] std::filesystem::path host_path(const std::string& guest_path) const;

    std::filesystem::path rootfs_;
    AddressSpace& memory_;
};

}  // namespace ilegacysim

