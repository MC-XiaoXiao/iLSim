add_subdirectory(control)

add_library(ilegacysim_kernel_bsd_virtual_udp STATIC
    virtual_udp.cpp)
add_library(iLegacySim::kernel_bsd_virtual_udp ALIAS
    ilegacysim_kernel_bsd_virtual_udp)
ilegacysim_configure_module(ilegacysim_kernel_bsd_virtual_udp)
target_link_libraries(ilegacysim_kernel_bsd_virtual_udp PRIVATE
    iLegacySim::foundation)

add_library(ilegacysim_kernel_bsd_virtual_udp_syscalls STATIC
    virtual_udp_syscalls.cpp)
add_library(iLegacySim::kernel_bsd_virtual_udp_syscalls ALIAS
    ilegacysim_kernel_bsd_virtual_udp_syscalls)
ilegacysim_configure_module(ilegacysim_kernel_bsd_virtual_udp_syscalls)
target_link_libraries(ilegacysim_kernel_bsd_virtual_udp_syscalls PRIVATE
    iLegacySim::kernel_bsd_virtual_udp
    iLegacySim::foundation)
