# Darwin's original MIG executable is tied to the Darwin toolchain. This
# host-side generator consumes authoritative .defs files and emits the ARM32
# wire metadata used by the compatibility adapter.
add_executable(ilegacysim_mig_id_gen mig_id_gen.cpp)
target_compile_features(ilegacysim_mig_id_gen PRIVATE cxx_std_20)
target_compile_options(ilegacysim_mig_id_gen PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)

set(ILEGACYSIM_XNU792_DIR
    "${PROJECT_SOURCE_DIR}/sources/xnu-792.24.17")
set(ILEGACYSIM_CONFIGD137_DIR
    "${PROJECT_SOURCE_DIR}/sources/configd-137.3")
set(ILEGACYSIM_LAUNCHD257_DIR
    "${PROJECT_SOURCE_DIR}/sources/launchd-257/launchd/src")
set(ILEGACYSIM_MIG_GENERATED_DIR
    "${PROJECT_BINARY_DIR}/generated/ilegacysim")
set(ILEGACYSIM_MIG_GENERATED_HEADERS)

function(ilegacysim_generate_mig_ids name definition)
    set(output "${ILEGACYSIM_MIG_GENERATED_DIR}/${name}_mig_ids.hpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND $<TARGET_FILE:ilegacysim_mig_id_gen>
                --input "${definition}"
                --output "${output}"
                --namespace "${name}"
                --include-dir "${ILEGACYSIM_XNU792_DIR}/osfmk"
                --include-dir "${ILEGACYSIM_LAUNCHD257_DIR}"
                --type-def "${PROJECT_SOURCE_DIR}/sources/xnu/osfmk/mach/machine/machine_types.defs"
                # launchd-257's private header typedefs this as char*, while
                # job_mig_spawn receives the generated vm_offset_t/count pair.
                --type-alias "_internal_string_t=pointer_t"
        DEPENDS ilegacysim_mig_id_gen "${definition}"
                "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/std_types.defs"
                "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/mach_types.defs"
                "${PROJECT_SOURCE_DIR}/sources/xnu/osfmk/mach/machine/machine_types.defs"
                "${ILEGACYSIM_LAUNCHD257_DIR}/launchd_mig_types.defs"
        VERBATIM
        COMMENT "Generating ${name} MIG metadata from source definitions")
    list(APPEND ILEGACYSIM_MIG_GENERATED_HEADERS "${output}")
    set(ILEGACYSIM_MIG_GENERATED_HEADERS
        "${ILEGACYSIM_MIG_GENERATED_HEADERS}" PARENT_SCOPE)
endfunction()

ilegacysim_generate_mig_ids(
    mach_port "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/mach_port.defs")
ilegacysim_generate_mig_ids(
    task "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/task.defs")
ilegacysim_generate_mig_ids(
    thread_act "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/thread_act.defs")
ilegacysim_generate_mig_ids(
    device "${ILEGACYSIM_XNU792_DIR}/osfmk/device/device.defs")
ilegacysim_generate_mig_ids(
    mach_host "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/mach_host.defs")
ilegacysim_generate_mig_ids(
    host_priv "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/host_priv.defs")
ilegacysim_generate_mig_ids(
    processor_set "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/processor_set.defs")
ilegacysim_generate_mig_ids(
    vm_map "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/vm_map.defs")
ilegacysim_generate_mig_ids(
    clock "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/clock.defs")
ilegacysim_generate_mig_ids(
    clock_reply "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/clock_reply.defs")
ilegacysim_generate_mig_ids(
    semaphore "${ILEGACYSIM_XNU792_DIR}/osfmk/mach/semaphore.defs")
ilegacysim_generate_mig_ids(
    bootstrap "${ILEGACYSIM_LAUNCHD257_DIR}/protocol_jobmgr.defs")
ilegacysim_generate_mig_ids(
    system_configuration
    "${ILEGACYSIM_CONFIGD137_DIR}/SystemConfiguration.fproj/config.defs")

add_custom_target(ilegacysim_mig_metadata
    DEPENDS ${ILEGACYSIM_MIG_GENERATED_HEADERS})

# Publish generated headers to sibling subsystem directories configured later.
set(ILEGACYSIM_MIG_GENERATED_HEADERS
    "${ILEGACYSIM_MIG_GENERATED_HEADERS}" PARENT_SCOPE)
