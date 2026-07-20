add_subdirectory(transport)
add_subdirectory(port)
add_subdirectory(processor)
add_subdirectory(task)
add_subdirectory(thread)
add_subdirectory(vm)
add_subdirectory(input)

target_sources(ilegacysim_kernel PRIVATE
    support.cpp
    ipc.cpp
    semaphore.cpp
    message.cpp
    host_message.cpp
    port_message.cpp
    task_vm_message.cpp
    rights_message.cpp
    notification_message.cpp
    traps.cpp)
target_link_libraries(ilegacysim_kernel PRIVATE
    iLegacySim::kernel_mach_transport
    iLegacySim::kernel_mach_port
    iLegacySim::kernel_mach_processor
    iLegacySim::kernel_mach_task
    iLegacySim::kernel_mach_thread
    iLegacySim::kernel_mach_vm
    iLegacySim::kernel_mach_input)
