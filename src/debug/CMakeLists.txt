add_library(ilegacysim_debug STATIC gdb_rsp.cpp)
add_library(iLegacySim::debug ALIAS ilegacysim_debug)
ilegacysim_configure_module(ilegacysim_debug)
target_link_libraries(ilegacysim_debug PUBLIC iLegacySim::foundation)
