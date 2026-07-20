add_library(ilegacysim_xnu_adapter STATIC
    xnu_mig_adapter.cpp
    ${ILEGACYSIM_MIG_GENERATED_HEADERS})
add_library(iLegacySim::xnu_adapter ALIAS ilegacysim_xnu_adapter)
add_dependencies(ilegacysim_xnu_adapter ilegacysim_mig_metadata)
target_include_directories(ilegacysim_xnu_adapter PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>)
target_compile_features(ilegacysim_xnu_adapter PUBLIC cxx_std_20)
target_compile_options(ilegacysim_xnu_adapter PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)
