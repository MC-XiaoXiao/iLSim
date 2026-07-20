add_library(ilegacysim_kernel_bsd_control STATIC
    control.cpp
    syscalls.cpp)
add_library(iLegacySim::kernel_bsd_control ALIAS
    ilegacysim_kernel_bsd_control)
ilegacysim_configure_module(ilegacysim_kernel_bsd_control)
target_link_libraries(ilegacysim_kernel_bsd_control PRIVATE
    iLegacySim::foundation)
