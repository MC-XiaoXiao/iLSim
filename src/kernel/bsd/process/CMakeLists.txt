add_library(ilegacysim_kernel_bsd_process STATIC
    credentials.cpp
    spawn.cpp)
add_library(iLegacySim::kernel_bsd_process ALIAS
    ilegacysim_kernel_bsd_process)
ilegacysim_configure_module(ilegacysim_kernel_bsd_process)
target_link_libraries(ilegacysim_kernel_bsd_process PRIVATE
    iLegacySim::foundation)
