add_library(ilegacysim_kernel STATIC
    kernel.cpp
    clock.cpp
    iokit.cpp
    network.cpp)
add_library(iLegacySim::kernel ALIAS ilegacysim_kernel)
ilegacysim_configure_module(ilegacysim_kernel)
target_link_libraries(ilegacysim_kernel PUBLIC
    iLegacySim::foundation
    iLegacySim::mach
    iLegacySim::filesystem
    iLegacySim::network
    iLegacySim::storage
    iLegacySim::telephony
    iLegacySim::media
    iLegacySim::bluetooth
    iLegacySim::graphics
    iLegacySim::xnu_adapter)

add_subdirectory(bsd)
add_subdirectory(iokit)
add_subdirectory(mach)

target_link_libraries(ilegacysim_kernel PRIVATE
    iLegacySim::kernel_iokit_baseband
    iLegacySim::kernel_iokit_power
    iLegacySim::kernel_iokit_display)
