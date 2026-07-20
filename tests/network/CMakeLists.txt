add_executable(ilegacysim_resolver_contract_tests resolver_contract_tests.cpp)
target_link_libraries(ilegacysim_resolver_contract_tests PRIVATE
    iLegacySim::network
    iLegacySim::foundation
    ilegacysim_test_support)
add_test(NAME resolver_contract COMMAND ilegacysim_resolver_contract_tests)
set_tests_properties(resolver_contract PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

ilegacysim_add_compatibility_suite(
    ilegacysim_network_tests network
    main.cpp
    abi_route_tests.cpp
    host_socket_tests.cpp
    wifi_tests.cpp
    virtual_udp_tests.cpp
    event_tests.cpp
    kernel_control_tests.cpp
    unix_socket_tests.cpp)
