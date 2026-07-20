add_library(ilegacysim_foundation_memory STATIC
    file_page_cache.cpp
    vm_map.cpp)
add_library(iLegacySim::foundation_memory ALIAS ilegacysim_foundation_memory)
ilegacysim_configure_module(ilegacysim_foundation_memory)
