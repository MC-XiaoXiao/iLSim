add_library(ilegacysim_media STATIC
    audio.cpp
    audio_toolbox_hle.cpp
    celestial_volume_protocol.cpp
    core_audio_hle.cpp
    media_library_service.cpp)
add_library(iLegacySim::media ALIAS ilegacysim_media)
ilegacysim_configure_module(ilegacysim_media)
target_link_libraries(ilegacysim_media PUBLIC iLegacySim::foundation)
if(TARGET PkgConfig::PLIST)
    target_link_libraries(ilegacysim_media PRIVATE PkgConfig::PLIST)
    target_compile_definitions(ilegacysim_media PRIVATE
        ILEGACYSIM_HAS_LIBPLIST=1)
endif()
