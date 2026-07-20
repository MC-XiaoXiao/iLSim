add_library(ilegacysim_media STATIC
    fig_movie_hle.cpp)
add_library(iLegacySim::media ALIAS ilegacysim_media)
ilegacysim_configure_module(ilegacysim_media)
target_link_libraries(ilegacysim_media PUBLIC iLegacySim::foundation)
