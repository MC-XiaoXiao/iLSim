add_library(ilegacysim_kernel_mach_processor STATIC
    processor_set.cpp)
add_library(iLegacySim::kernel_mach_processor ALIAS
    ilegacysim_kernel_mach_processor)
ilegacysim_configure_module(ilegacysim_kernel_mach_processor)
target_link_libraries(ilegacysim_kernel_mach_processor PRIVATE
    iLegacySim::foundation
    iLegacySim::mach)
