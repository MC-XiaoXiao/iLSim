add_library(ilegacysim_kernel_mach_vm STATIC
    allocate.cpp
    copy.cpp
    map.cpp
    memory_entry.cpp
    purgable.cpp
    read.cpp)
add_library(iLegacySim::kernel_mach_vm ALIAS
    ilegacysim_kernel_mach_vm)
ilegacysim_configure_module(ilegacysim_kernel_mach_vm)
target_link_libraries(ilegacysim_kernel_mach_vm PRIVATE
    iLegacySim::foundation)
