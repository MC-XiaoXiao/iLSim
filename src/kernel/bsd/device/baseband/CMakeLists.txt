add_library(ilegacysim_kernel_bsd_baseband_device STATIC
    device.cpp
    replay.cpp)
add_library(iLegacySim::kernel_bsd_baseband_device ALIAS
    ilegacysim_kernel_bsd_baseband_device)
ilegacysim_configure_module(ilegacysim_kernel_bsd_baseband_device)
target_link_libraries(ilegacysim_kernel_bsd_baseband_device PRIVATE
    iLegacySim::foundation)
