add_library(ilegacysim_kernel_bsd_sysctl STATIC
    hardware_values.cpp
    name_resolver.cpp
    process_arguments.cpp)
add_library(iLegacySim::kernel_bsd_sysctl ALIAS
    ilegacysim_kernel_bsd_sysctl)
ilegacysim_configure_module(ilegacysim_kernel_bsd_sysctl)
