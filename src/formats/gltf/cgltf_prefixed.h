#pragma once

// cgltf with its external symbols renamed (sh_ prefix), so that linking sh_gltf next to another copy of cgltf
// does not collide. Include this header instead of cgltf.h.
// The list is the global symbol table of the compiled implementation (cgltf 1.15, nm); regenerate it when the
// vendored cgltf is updated.

#define cgltf_accessor_index sh_cgltf_accessor_index
#define cgltf_accessor_read_float sh_cgltf_accessor_read_float
#define cgltf_accessor_read_index sh_cgltf_accessor_read_index
#define cgltf_accessor_read_uint sh_cgltf_accessor_read_uint
#define cgltf_accessor_unpack_floats sh_cgltf_accessor_unpack_floats
#define cgltf_accessor_unpack_indices sh_cgltf_accessor_unpack_indices
#define cgltf_animation_channel_index sh_cgltf_animation_channel_index
#define cgltf_animation_index sh_cgltf_animation_index
#define cgltf_animation_sampler_index sh_cgltf_animation_sampler_index
#define cgltf_buffer_index sh_cgltf_buffer_index
#define cgltf_buffer_view_data sh_cgltf_buffer_view_data
#define cgltf_buffer_view_index sh_cgltf_buffer_view_index
#define cgltf_calc_size sh_cgltf_calc_size
#define cgltf_camera_index sh_cgltf_camera_index
#define cgltf_component_size sh_cgltf_component_size
#define cgltf_copy_extras_json sh_cgltf_copy_extras_json
#define cgltf_decode_string sh_cgltf_decode_string
#define cgltf_decode_uri sh_cgltf_decode_uri
#define cgltf_find_accessor sh_cgltf_find_accessor
#define cgltf_free sh_cgltf_free
#define cgltf_image_index sh_cgltf_image_index
#define cgltf_light_index sh_cgltf_light_index
#define cgltf_load_buffer_base64 sh_cgltf_load_buffer_base64
#define cgltf_load_buffers sh_cgltf_load_buffers
#define cgltf_material_index sh_cgltf_material_index
#define cgltf_mesh_index sh_cgltf_mesh_index
#define cgltf_node_index sh_cgltf_node_index
#define cgltf_node_transform_local sh_cgltf_node_transform_local
#define cgltf_node_transform_world sh_cgltf_node_transform_world
#define cgltf_num_components sh_cgltf_num_components
#define cgltf_parse sh_cgltf_parse
#define cgltf_parse_file sh_cgltf_parse_file
#define cgltf_sampler_index sh_cgltf_sampler_index
#define cgltf_scene_index sh_cgltf_scene_index
#define cgltf_skin_index sh_cgltf_skin_index
#define cgltf_texture_index sh_cgltf_texture_index
#define cgltf_validate sh_cgltf_validate

#include "cgltf.h"
