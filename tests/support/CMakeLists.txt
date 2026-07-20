add_library(ilegacysim_test_support STATIC test_support.cpp)
target_include_directories(ilegacysim_test_support PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ilegacysim_test_support PUBLIC cxx_std_20)
target_compile_options(ilegacysim_test_support PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)
