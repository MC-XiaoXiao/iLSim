add_library(ilegacysim_kernel_iokit_baseband STATIC
    service.cpp)
add_library(iLegacySim::kernel_iokit_baseband ALIAS
    ilegacysim_kernel_iokit_baseband)
ilegacysim_configure_module(ilegacysim_kernel_iokit_baseband)
target_link_libraries(ilegacysim_kernel_iokit_baseband PRIVATE
    iLegacySim::foundation)
