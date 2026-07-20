add_subdirectory(baseband)
add_subdirectory(display)

add_library(ilegacysim_kernel_iokit_power STATIC
    power.cpp)
add_library(iLegacySim::kernel_iokit_power ALIAS
    ilegacysim_kernel_iokit_power)
ilegacysim_configure_module(ilegacysim_kernel_iokit_power)
target_link_libraries(ilegacysim_kernel_iokit_power PRIVATE
    iLegacySim::foundation)
