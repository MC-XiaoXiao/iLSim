add_library(ilegacysim_bluetooth STATIC
    bluetooth_manager_hle.cpp)
add_library(iLegacySim::bluetooth ALIAS ilegacysim_bluetooth)
ilegacysim_configure_module(ilegacysim_bluetooth)
target_link_libraries(ilegacysim_bluetooth PUBLIC iLegacySim::foundation)
