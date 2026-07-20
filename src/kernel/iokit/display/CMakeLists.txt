add_library(ilegacysim_kernel_iokit_display STATIC
    vsync.cpp)
add_library(iLegacySim::kernel_iokit_display ALIAS
    ilegacysim_kernel_iokit_display)
ilegacysim_configure_module(ilegacysim_kernel_iokit_display)
target_link_libraries(ilegacysim_kernel_iokit_display PRIVATE
    iLegacySim::foundation)
