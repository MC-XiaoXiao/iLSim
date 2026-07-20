add_subdirectory(control)
add_subdirectory(runtime)

add_executable(ilegacysim main.cpp)
target_link_libraries(ilegacysim PRIVATE
    iLegacySim::app_control
    iLegacySim::app_runtime
    iLegacySim::core
    iLegacySim::device_state
    iLegacySim::kernel_bsd_baseband_device)
set_target_properties(ilegacysim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
