add_library(ilegacysim_app_control STATIC
    live_control.cpp
    live_touch_scheduler.cpp)
add_library(iLegacySim::app_control ALIAS ilegacysim_app_control)
ilegacysim_configure_module(ilegacysim_app_control)
target_link_libraries(ilegacysim_app_control PUBLIC
    iLegacySim::foundation)
