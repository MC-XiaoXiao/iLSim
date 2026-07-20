add_library(ilegacysim_storage STATIC
    app_support_hle.cpp)
add_library(iLegacySim::storage ALIAS ilegacysim_storage)
ilegacysim_configure_module(ilegacysim_storage)
target_link_libraries(ilegacysim_storage PUBLIC iLegacySim::foundation)
