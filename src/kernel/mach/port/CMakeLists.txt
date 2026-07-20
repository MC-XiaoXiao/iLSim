add_library(ilegacysim_kernel_mach_port STATIC
    enumeration.cpp
    limits.cpp
    membership.cpp)
add_library(iLegacySim::kernel_mach_port ALIAS
    ilegacysim_kernel_mach_port)
ilegacysim_configure_module(ilegacysim_kernel_mach_port)
target_link_libraries(ilegacysim_kernel_mach_port PRIVATE
    iLegacySim::foundation)
