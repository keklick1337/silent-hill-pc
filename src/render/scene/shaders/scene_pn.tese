#version 450
// Model upscaling, tessellation path (RENDER.md section 7.11): a point of the PN surface (scene_pn.glsl), projected as
// scene.vert projects. Colour, fog, UV and the UV box are the corners' own, interpolated as the plain triangle
// interpolates them: only the shape changes, never the light the original gave the model.
#extension GL_GOOGLE_include_directive : require

layout(triangles, equal_spacing, ccw) in;

#define SH_SCENE_LIB 1
#include "scene_lib.glsl"
#include "scene_pn.glsl"

layout(location = 0) in vec2 c_uv[];
layout(location = 1) in vec4 c_color[];
layout(location = 2) in float c_fog[];
layout(location = 3) in vec4 c_box[];
layout(location = 4) in vec2 c_setback[];
layout(location = 5) in vec3 c_pos[];
layout(location = 6) in vec4 c_normal[];
layout(location = 7) in vec3 c_edge[];

layout(std140, set = 1, binding = 2) uniform ScenePn {
    vec4 u_pnLevel;  // y > 0: a map surface (scene_pn.tesc)
    uvec4 u_pnDraw;
};

layout(location = 0) centroid out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec3 v_viewpos;
layout(location = 3) out float v_fog;
layout(location = 4) flat out vec4 v_box;
layout(location = 5) out float v_depth;
layout(location = 6) flat out vec2 v_setback;

void main() {
    vec3 t = gl_TessCoord;
    PnPatch b = u_pnLevel.y > 0.0 ? PnMakeEdges(c_pos[0], c_pos[1], c_pos[2], c_normal[0].xyz, c_edge[0],
                                                c_normal[1].xyz, c_edge[1], c_normal[2].xyz, c_edge[2])
                                  : PnMake(c_pos[0], c_pos[1], c_pos[2], c_normal[0], c_normal[1], c_normal[2]);
    vec3 v = PnEval(b, t);
    v_viewpos = v;
    v_uv = t.x * c_uv[0] + t.y * c_uv[1] + t.z * c_uv[2];
    v_color = t.x * c_color[0] + t.y * c_color[1] + t.z * c_color[2];
    v_fog = t.x * c_fog[0] + t.y * c_fog[1] + t.z * c_fog[2];
    v_box = c_box[0];
    v_setback = c_setback[0];
    gl_Position = SceneProject(v, v_depth);
}
