# Build a real ARMv6/iPhoneOS executable that is loaded by the extracted dyld
# and calls the firmware libSystem resolver APIs. Modern ld64.lld no longer
# supports ARM32, so this optional target uses the cctools-port linker produced
# by tools/bootstrap_armv6_ld.sh (or a caller-supplied compatible ld64).
set(ILEGACYSIM_ROOTFS "${PROJECT_SOURCE_DIR}/build/rootfs" CACHE PATH
    "Extracted iPhoneOS root filesystem used by guest integration probes")
set(ILEGACYSIM_ARMV6_LD
    "${PROJECT_SOURCE_DIR}/build/toolchain/bin/arm-apple-darwin9-ld"
    CACHE FILEPATH "cctools ld64 with ARMv6 output support")
find_program(ILEGACYSIM_GUEST_CLANG NAMES clang)
set(ILEGACYSIM_GUEST_DNS_PROBE
    "${PROJECT_BINARY_DIR}/guest/dns_async_probe")
if(ILEGACYSIM_GUEST_CLANG AND EXISTS "${ILEGACYSIM_ARMV6_LD}" AND
   EXISTS "${ILEGACYSIM_ROOTFS}/usr/lib/libSystem.B.dylib")
    add_custom_command(
        OUTPUT "${ILEGACYSIM_GUEST_DNS_PROBE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${PROJECT_BINARY_DIR}/guest"
        COMMAND "${ILEGACYSIM_GUEST_CLANG}"
                -target armv6-apple-ios1.0 -march=armv6 -marm -Oz
                -ffreestanding -fno-stack-protector -fno-builtin
                -c "${CMAKE_CURRENT_SOURCE_DIR}/dns_async_probe.c"
                -o "${PROJECT_BINARY_DIR}/guest/dns_async_probe.o"
        COMMAND "${ILEGACYSIM_GUEST_CLANG}"
                -target armv6-apple-ios1.0 -march=armv6 -marm -Oz
                -ffreestanding -fno-stack-protector -fno-builtin
                -c "${CMAKE_CURRENT_SOURCE_DIR}/system_configuration_probe.c"
                -o "${PROJECT_BINARY_DIR}/guest/system_configuration_probe.o"
        COMMAND "${ILEGACYSIM_GUEST_CLANG}"
                -target armv6-apple-ios1.0 -march=armv6 -marm -Oz
                -ffreestanding -fno-stack-protector -fno-builtin
                -c "${CMAKE_CURRENT_SOURCE_DIR}/runtime_initializers.c"
                -o "${PROJECT_BINARY_DIR}/guest/runtime_initializers.o"
        COMMAND "${ILEGACYSIM_GUEST_CLANG}"
                -target armv6-apple-ios1.0 -march=armv6 -marm
                -c "${CMAKE_CURRENT_SOURCE_DIR}/armv6_crt.S"
                -o "${PROJECT_BINARY_DIR}/guest/armv6_crt.o"
        COMMAND "${ILEGACYSIM_ARMV6_LD}"
                -arch armv6 -execute -e _start
                -syslibroot "${ILEGACYSIM_ROOTFS}"
                -iphoneos_version_min 1.0 -no_pie -segalign 0x1000
                -bind_at_load -no_uuid -no_function_starts
                -no_data_in_code_info -no_encryption
                -o "${ILEGACYSIM_GUEST_DNS_PROBE}"
                "${PROJECT_BINARY_DIR}/guest/armv6_crt.o"
                "${PROJECT_BINARY_DIR}/guest/dns_async_probe.o"
                "${PROJECT_BINARY_DIR}/guest/system_configuration_probe.o"
                "${PROJECT_BINARY_DIR}/guest/runtime_initializers.o"
                "${ILEGACYSIM_ROOTFS}/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration"
                "${ILEGACYSIM_ROOTFS}/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
                "${ILEGACYSIM_ROOTFS}/usr/lib/libSystem.B.dylib"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/armv6_crt.S"
                "${CMAKE_CURRENT_SOURCE_DIR}/dns_async_probe.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/system_configuration_probe.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/system_configuration_probe.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime_initializers.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime_initializers.h"
                "${ILEGACYSIM_ROOTFS}/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration"
                "${ILEGACYSIM_ROOTFS}/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
                "${ILEGACYSIM_ROOTFS}/usr/lib/libSystem.B.dylib"
        VERBATIM
        COMMENT "Building ARMv6 firmware DNS/SystemConfiguration callback probe"
    )
    add_custom_target(ilegacysim_guest_dns_probe ALL
        DEPENDS "${ILEGACYSIM_GUEST_DNS_PROBE}")
    add_custom_target(ilegacysim_install_guest_dns_probe
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${ILEGACYSIM_ROOTFS}/usr/libexec"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ILEGACYSIM_GUEST_DNS_PROBE}"
                "${ILEGACYSIM_ROOTFS}/usr/libexec/ilegacysim-dns-probe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/com.ilegacysim.dns-probe.plist"
                "${ILEGACYSIM_ROOTFS}/System/Library/LaunchDaemons/com.ilegacysim.dns-probe.plist"
        DEPENDS ilegacysim_guest_dns_probe
                "${CMAKE_CURRENT_SOURCE_DIR}/com.ilegacysim.dns-probe.plist"
        COMMENT "Installing opt-in DNS/Libinfo probe into the extracted rootfs"
        VERBATIM)
else()
    message(STATUS
        "ARMv6 guest DNS probe disabled: clang, cctools ld64, or firmware "
        "libSystem is unavailable")
endif()
