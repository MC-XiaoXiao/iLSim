if(TARGET ilegacysim_guest_dns_probe)
    add_test(NAME inspect_guest_dns_probe
        COMMAND ilegacysim inspect
                --rootfs "${PROJECT_BINARY_DIR}"
                --binary /guest/dns_async_probe)
    set_tests_properties(inspect_guest_dns_probe PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        PASS_REGULAR_EXPRESSION
            "cpu: ARMv6;file_type: executable;dyld: /usr/lib/dyld")
endif()

set(_launchd "${ILEGACYSIM_ROOTFS}/sbin/launchd")
if(EXISTS "${_launchd}")
    add_test(NAME inspect_real_launchd
        COMMAND ilegacysim inspect --rootfs "${ILEGACYSIM_ROOTFS}"
                                 --binary /sbin/launchd)
    add_test(NAME boot_real_launchd
        COMMAND ilegacysim boot --rootfs "${ILEGACYSIM_ROOTFS}"
                                --ticks 110000000)
    set_tests_properties(boot_real_launchd PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        PASS_REGULAR_EXPRESSION
            "processes=(2[6-9]|[3-9][0-9]+) threads=(3[4-9]|[4-9][0-9]+) runnable=[0-9]+"
        TIMEOUT 60)
    set_tests_properties(inspect_real_launchd PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()
