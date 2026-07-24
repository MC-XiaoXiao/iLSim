add_library(ilegacysim_telephony STATIC
    core_telephony_hle.cpp)
add_library(iLegacySim::telephony ALIAS ilegacysim_telephony)
ilegacysim_configure_module(ilegacysim_telephony)
target_link_libraries(ilegacysim_telephony PUBLIC
    iLegacySim::foundation
    iLegacySim::network)
