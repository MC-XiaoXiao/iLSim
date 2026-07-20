add_library(ilegacysim_kernel_bsd_debug STATIC
    tracing.cpp)
add_library(iLegacySim::kernel_bsd_debug ALIAS
    ilegacysim_kernel_bsd_debug)
ilegacysim_configure_module(ilegacysim_kernel_bsd_debug)
target_link_libraries(ilegacysim_kernel_bsd_debug PRIVATE
    iLegacySim::foundation)
