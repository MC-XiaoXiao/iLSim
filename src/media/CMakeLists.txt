add_library(ilegacysim_media STATIC
    audio.cpp
    audio_toolbox_hle.cpp
    celestial_audio_hle.cpp
    fig_movie_hle.cpp)
add_library(iLegacySim::media ALIAS ilegacysim_media)
ilegacysim_configure_module(ilegacysim_media)
target_link_libraries(ilegacysim_media PUBLIC iLegacySim::foundation)
