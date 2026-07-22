add_library(ilegacysim_graphics STATIC
    core_surface_hle.cpp
    gles_resources.cpp
    gles_rasterizer.cpp
    gles_math.cpp
    frame_file_presenter.cpp
    layerkit_compatibility.cpp
    layerkit_hle.cpp
    mbx_connect_hle.cpp
    mbx2d_hle.cpp
    mbx_client_surface.cpp
    mbx_swapchain.cpp
    mbx3d_quad_hle.cpp
    mobile_framebuffer_hle.cpp
    opengles_hle.cpp
    presentation_tracker.cpp
    sdl_display.cpp
    sdl_input.cpp
    surface_store.cpp)
target_sources(ilegacysim_graphics PRIVATE touch_replay.cpp)
find_package(PNG REQUIRED)
add_library(iLegacySim::graphics ALIAS ilegacysim_graphics)
ilegacysim_configure_module(ilegacysim_graphics)
target_link_libraries(ilegacysim_graphics
    PUBLIC iLegacySim::foundation
    PRIVATE PNG::PNG)
if(TARGET PkgConfig::SDL2)
    target_link_libraries(ilegacysim_graphics PUBLIC PkgConfig::SDL2)
    target_compile_definitions(ilegacysim_graphics PUBLIC
        ILEGACYSIM_HAS_SDL2=1)
endif()
