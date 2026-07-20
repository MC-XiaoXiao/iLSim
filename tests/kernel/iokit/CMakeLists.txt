add_subdirectory(display)

add_library(ilegacysim_kernel_iokit_power_tests STATIC
    power_tests.cpp)
add_library(iLegacySim::kernel_iokit_power_tests ALIAS
    ilegacysim_kernel_iokit_power_tests)
ilegacysim_configure_module(ilegacysim_kernel_iokit_power_tests)
target_link_libraries(ilegacysim_kernel_iokit_power_tests PRIVATE
    iLegacySim::core
    ilegacysim_test_support)
