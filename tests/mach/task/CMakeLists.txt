add_library(ilegacysim_kernel_mach_task_tests STATIC
    enumeration_tests.cpp)
add_library(iLegacySim::kernel_mach_task_tests ALIAS
    ilegacysim_kernel_mach_task_tests)
ilegacysim_configure_module(ilegacysim_kernel_mach_task_tests)
target_link_libraries(ilegacysim_kernel_mach_task_tests PRIVATE
    iLegacySim::core
    ilegacysim_test_support)
