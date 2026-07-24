add_library(ilegacysim_device_state STATIC
    lockdown_profile.cpp
    network_preferences.cpp)
add_library(iLegacySim::device_state ALIAS ilegacysim_device_state)
ilegacysim_configure_module(ilegacysim_device_state)
if(TARGET PkgConfig::PLIST)
    target_link_libraries(ilegacysim_device_state PRIVATE PkgConfig::PLIST)
    target_compile_definitions(ilegacysim_device_state PRIVATE
        ILEGACYSIM_HAS_LIBPLIST=1)
endif()
