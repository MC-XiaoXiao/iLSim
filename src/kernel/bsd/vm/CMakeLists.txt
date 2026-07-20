add_library(ilegacysim_kernel_bsd_vm_shared_region STATIC
    shared_region.cpp)
add_library(iLegacySim::kernel_bsd_vm_shared_region ALIAS
    ilegacysim_kernel_bsd_vm_shared_region)
ilegacysim_configure_module(ilegacysim_kernel_bsd_vm_shared_region)
target_link_libraries(ilegacysim_kernel_bsd_vm_shared_region PRIVATE
    iLegacySim::foundation)
