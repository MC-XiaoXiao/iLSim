add_subdirectory(device)
add_subdirectory(debug)
add_subdirectory(sysctl)
add_subdirectory(signal)
add_subdirectory(filesystem)
add_subdirectory(network)
add_subdirectory(process)
add_subdirectory(vm)

target_sources(ilegacysim_kernel PRIVATE
    dispatch.cpp
    aio.cpp
    process.cpp
    filesystem.cpp
    descriptor_memory.cpp
    bpf.cpp
    socket.cpp
    events.cpp
    support.cpp)
target_link_libraries(ilegacysim_kernel PRIVATE
    iLegacySim::kernel_bsd_baseband_device
    iLegacySim::kernel_bsd_control
    iLegacySim::kernel_bsd_debug
    iLegacySim::kernel_bsd_filesystem_persistence
    iLegacySim::kernel_bsd_filesystem_timestamps
    iLegacySim::kernel_bsd_filesystem_locking
    iLegacySim::kernel_bsd_filesystem_ownership
    iLegacySim::kernel_bsd_virtual_udp_syscalls
    iLegacySim::kernel_bsd_virtual_udp
    iLegacySim::kernel_bsd_process
    iLegacySim::kernel_bsd_vm_shared_region
    iLegacySim::kernel_bsd_signal
    iLegacySim::kernel_bsd_sysctl)
