add_library(ilegacysim_kernel_mach_input STATIC
    graphics_services.cpp)
add_library(iLegacySim::kernel_mach_input ALIAS ilegacysim_kernel_mach_input)
ilegacysim_configure_module(ilegacysim_kernel_mach_input)
target_link_libraries(ilegacysim_kernel_mach_input PUBLIC
    iLegacySim::foundation
    iLegacySim::mach)
