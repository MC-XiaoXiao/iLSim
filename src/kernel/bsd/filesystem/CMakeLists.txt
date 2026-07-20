add_library(ilegacysim_kernel_bsd_filesystem_ownership STATIC
    ownership.cpp)
add_library(iLegacySim::kernel_bsd_filesystem_ownership ALIAS
    ilegacysim_kernel_bsd_filesystem_ownership)
ilegacysim_configure_module(ilegacysim_kernel_bsd_filesystem_ownership)
target_link_libraries(ilegacysim_kernel_bsd_filesystem_ownership PRIVATE
    iLegacySim::filesystem
    iLegacySim::foundation)

add_library(ilegacysim_kernel_bsd_filesystem_locking STATIC
    locking.cpp)
add_library(iLegacySim::kernel_bsd_filesystem_locking ALIAS
    ilegacysim_kernel_bsd_filesystem_locking)
ilegacysim_configure_module(ilegacysim_kernel_bsd_filesystem_locking)
target_link_libraries(ilegacysim_kernel_bsd_filesystem_locking PRIVATE
    iLegacySim::filesystem
    iLegacySim::foundation)

add_library(ilegacysim_kernel_bsd_filesystem_persistence STATIC
    persistence.cpp)
add_library(iLegacySim::kernel_bsd_filesystem_persistence ALIAS
    ilegacysim_kernel_bsd_filesystem_persistence)
ilegacysim_configure_module(ilegacysim_kernel_bsd_filesystem_persistence)
target_link_libraries(ilegacysim_kernel_bsd_filesystem_persistence PRIVATE
    iLegacySim::filesystem
    iLegacySim::foundation)

add_library(ilegacysim_kernel_bsd_filesystem_timestamps STATIC
    timestamps.cpp)
add_library(iLegacySim::kernel_bsd_filesystem_timestamps ALIAS
    ilegacysim_kernel_bsd_filesystem_timestamps)
ilegacysim_configure_module(ilegacysim_kernel_bsd_filesystem_timestamps)
target_link_libraries(ilegacysim_kernel_bsd_filesystem_timestamps PRIVATE
    iLegacySim::filesystem
    iLegacySim::foundation)
