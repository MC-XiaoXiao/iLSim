add_library(ilegacysim_kernel_mach_thread STATIC
    lifecycle.cpp
    self.cpp
    state.cpp)
add_library(iLegacySim::kernel_mach_thread ALIAS
    ilegacysim_kernel_mach_thread)
ilegacysim_configure_module(ilegacysim_kernel_mach_thread)
target_link_libraries(ilegacysim_kernel_mach_thread PRIVATE
    iLegacySim::foundation
    iLegacySim::mach)
