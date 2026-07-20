add_library(ilegacysim_kernel_iokit_display_tests STATIC
    vsync_tests.cpp)
add_library(iLegacySim::kernel_iokit_display_tests ALIAS
    ilegacysim_kernel_iokit_display_tests)
ilegacysim_configure_module(ilegacysim_kernel_iokit_display_tests)
target_link_libraries(ilegacysim_kernel_iokit_display_tests PRIVATE
    iLegacySim::core
    ilegacysim_test_support)
