add_library(ilegacysim_mach STATIC
    mach_namespace.cpp
    kernel_mach_ipc.cpp
    xnu_scheduler.cpp)
add_library(iLegacySim::mach ALIAS ilegacysim_mach)
ilegacysim_configure_module(ilegacysim_mach)
target_link_libraries(ilegacysim_mach PUBLIC
    iLegacySim::foundation iLegacySim::xnu_adapter)
