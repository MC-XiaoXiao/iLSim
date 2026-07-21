add_library(ilegacysim_app_audio STATIC sdl_audio_sink.cpp)
add_library(iLegacySim::app_audio ALIAS ilegacysim_app_audio)
ilegacysim_configure_module(ilegacysim_app_audio)
target_include_directories(ilegacysim_app_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(ilegacysim_app_audio PUBLIC iLegacySim::media)
if(TARGET PkgConfig::SDL2)
    target_link_libraries(ilegacysim_app_audio PUBLIC PkgConfig::SDL2)
    target_compile_definitions(ilegacysim_app_audio PUBLIC
        ILEGACYSIM_HAS_SDL2=1)
endif()
