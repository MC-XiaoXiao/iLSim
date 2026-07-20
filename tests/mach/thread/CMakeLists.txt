add_library(ilegacysim_kernel_mach_thread_tests STATIC
    state_tests.cpp)
add_library(iLegacySim::kernel_mach_thread_tests ALIAS
    ilegacysim_kernel_mach_thread_tests)
ilegacysim_configure_module(ilegacysim_kernel_mach_thread_tests)
target_link_libraries(ilegacysim_kernel_mach_thread_tests PRIVATE
    iLegacySim::core
    ilegacysim_test_support)
