#pragma once

#include <cstddef>
#include <cstdint>

namespace ilegacysim::gles_abi {

inline constexpr std::uint32_t no_error = 0;
inline constexpr std::uint32_t invalid_enum = 0x0500U;
inline constexpr std::uint32_t invalid_value = 0x0501U;
inline constexpr std::uint32_t invalid_operation = 0x0502U;
inline constexpr std::uint32_t out_of_memory = 0x0505U;

inline constexpr std::uint32_t texture_2d = 0x0de1U;
inline constexpr std::uint32_t texture_rectangle_apple = 0x84f5U;
inline constexpr std::uint32_t texture_binding_2d = 0x8069U;
inline constexpr std::uint32_t texture_binding_rectangle_apple = 0x84f6U;
inline constexpr std::uint32_t texture0 = 0x84c0U;
inline constexpr std::uint32_t active_texture = 0x84e0U;
inline constexpr std::uint32_t client_active_texture = 0x84e1U;
inline constexpr std::uint32_t maximum_texture_units = 0x84e2U;
inline constexpr std::size_t texture_unit_count = 2;
inline constexpr std::uint32_t array_buffer = 0x8892U;
inline constexpr std::uint32_t element_array_buffer = 0x8893U;
inline constexpr std::uint32_t buffer_size = 0x8764U;
inline constexpr std::uint32_t buffer_usage = 0x8765U;

inline constexpr std::uint32_t pack_alignment = 0x0d05U;
inline constexpr std::uint32_t unpack_alignment = 0x0cf5U;

inline constexpr std::uint32_t alpha = 0x1906U;
inline constexpr std::uint32_t rgb = 0x1907U;
inline constexpr std::uint32_t rgba = 0x1908U;
inline constexpr std::uint32_t luminance = 0x1909U;
inline constexpr std::uint32_t luminance_alpha = 0x190aU;
inline constexpr std::uint32_t bgra_apple = 0x80e1U;

inline constexpr std::uint32_t unsigned_byte = 0x1401U;
inline constexpr std::uint32_t byte = 0x1400U;
inline constexpr std::uint32_t short_type = 0x1402U;
inline constexpr std::uint32_t unsigned_short = 0x1403U;
inline constexpr std::uint32_t float_type = 0x1406U;
inline constexpr std::uint32_t fixed = 0x140cU;
inline constexpr std::uint32_t unsigned_short_4_4_4_4 = 0x8033U;
inline constexpr std::uint32_t unsigned_short_5_5_5_1 = 0x8034U;
inline constexpr std::uint32_t unsigned_short_5_6_5 = 0x8363U;

inline constexpr std::uint32_t static_draw = 0x88e4U;
inline constexpr std::uint32_t dynamic_draw = 0x88e8U;

inline constexpr std::uint32_t triangles = 0x0004U;
inline constexpr std::uint32_t triangle_strip = 0x0005U;
inline constexpr std::uint32_t triangle_fan = 0x0006U;
inline constexpr std::uint32_t depth_buffer_bit = 0x00000100U;
inline constexpr std::uint32_t stencil_buffer_bit = 0x00000400U;
inline constexpr std::uint32_t color_buffer_bit = 0x00004000U;

inline constexpr std::uint32_t vertex_array = 0x8074U;
inline constexpr std::uint32_t color_array = 0x8076U;
inline constexpr std::uint32_t texture_coord_array = 0x8078U;
inline constexpr std::uint32_t blend = 0x0be2U;
inline constexpr std::uint32_t scissor_test = 0x0c11U;

inline constexpr std::uint32_t current_color = 0x0b00U;
inline constexpr std::uint32_t matrix_mode_query = 0x0ba0U;
inline constexpr std::uint32_t viewport_query = 0x0ba2U;
inline constexpr std::uint32_t modelview_matrix_query = 0x0ba6U;
inline constexpr std::uint32_t projection_matrix_query = 0x0ba7U;
inline constexpr std::uint32_t texture_matrix_query = 0x0ba8U;
inline constexpr std::uint32_t scissor_box = 0x0c10U;
inline constexpr std::uint32_t color_write_mask = 0x0c23U;
inline constexpr std::uint32_t front_face_query = 0x0b46U;
inline constexpr std::uint32_t depth_write_mask = 0x0b72U;
inline constexpr std::uint32_t stencil_write_mask = 0x0b98U;
inline constexpr std::uint32_t maximum_texture_size = 0x0d33U;

inline constexpr std::uint32_t modelview = 0x1700U;
inline constexpr std::uint32_t projection = 0x1701U;
inline constexpr std::uint32_t texture_matrix = 0x1702U;
inline constexpr std::size_t maximum_matrix_stack_depth = 32;

inline constexpr std::uint32_t zero = 0;
inline constexpr std::uint32_t one = 1;
inline constexpr std::uint32_t source_alpha = 0x0302U;
inline constexpr std::uint32_t one_minus_source_alpha = 0x0303U;

inline constexpr std::uint32_t clockwise = 0x0900U;
inline constexpr std::uint32_t counter_clockwise = 0x0901U;
inline constexpr std::uint32_t texture_environment = 0x2300U;
inline constexpr std::uint32_t texture_environment_mode = 0x2200U;
inline constexpr std::uint32_t texture_environment_color = 0x2201U;
inline constexpr std::uint32_t modulate = 0x2100U;
inline constexpr std::uint32_t replace = 0x1e01U;

inline constexpr std::uint32_t default_pixel_alignment = 4;
inline constexpr std::uint32_t maximum_texture_dimension = 4096;
inline constexpr std::uint64_t maximum_resource_bytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t maximum_draw_vertices = 1'000'000U;

}  // namespace ilegacysim::gles_abi
