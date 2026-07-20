add_library(ilegacysim_kernel_mach_transport STATIC
    descriptor.cpp)
add_library(iLegacySim::kernel_mach_transport ALIAS
    ilegacysim_kernel_mach_transport)
ilegacysim_configure_module(ilegacysim_kernel_mach_transport)
