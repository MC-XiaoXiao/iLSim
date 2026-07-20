add_library(ilegacysim_kernel_bsd_signal STATIC
    signal.cpp)
add_library(iLegacySim::kernel_bsd_signal ALIAS
    ilegacysim_kernel_bsd_signal)
ilegacysim_configure_module(ilegacysim_kernel_bsd_signal)
target_link_libraries(ilegacysim_kernel_bsd_signal PRIVATE
    iLegacySim::foundation)
