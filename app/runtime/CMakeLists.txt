add_library(ilegacysim_app_runtime STATIC
    realtime_pacer.cpp)
add_library(iLegacySim::app_runtime ALIAS ilegacysim_app_runtime)
ilegacysim_configure_module(ilegacysim_app_runtime)
target_link_libraries(ilegacysim_app_runtime PUBLIC
    iLegacySim::foundation)
