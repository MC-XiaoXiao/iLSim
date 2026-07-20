add_library(ilegacysim_kernel_mach_task STATIC
    identity.cpp
    enumeration.cpp)
add_library(iLegacySim::kernel_mach_task ALIAS
    ilegacysim_kernel_mach_task)
ilegacysim_configure_module(ilegacysim_kernel_mach_task)
target_link_libraries(ilegacysim_kernel_mach_task PRIVATE
    iLegacySim::foundation
    iLegacySim::mach)
