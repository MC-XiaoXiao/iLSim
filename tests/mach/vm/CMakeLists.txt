add_library(ilegacysim_kernel_mach_vm_tests STATIC
    allocate_tests.cpp
    copy_tests.cpp
    main.cpp
    memory_entry_tests.cpp
    purgable_tests.cpp
    read_tests.cpp)
add_library(iLegacySim::kernel_mach_vm_tests ALIAS
    ilegacysim_kernel_mach_vm_tests)
ilegacysim_configure_module(ilegacysim_kernel_mach_vm_tests)
target_link_libraries(ilegacysim_kernel_mach_vm_tests PRIVATE
    iLegacySim::core
    ilegacysim_test_support)
