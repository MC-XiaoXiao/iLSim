ilegacysim_add_compatibility_suite(
    ilegacysim_control_tests control
    control_tests.cpp)
target_link_libraries(ilegacysim_control_tests PRIVATE
    iLegacySim::app_control)
