#version 450
// Model upscaling, tessellation path (RENDER.md section 7.11): one patch per triangle of a posed mesh, a world object
// or a curved map surface. The corners pass on as scene.vert lit them. A model's edges and inside take the draw's one
// level; a map surface cuts each edge by its own length on screen, a function of the edge's two ends only, so two
// patches still cut a shared edge alike, and an edge it marks straight stays one piece, like its flat neighbour's.

#extension GL_GOOGLE_include_directive : require

layout(vertices = 3) out;

#define SH_SCENE_LIB 1
#include "scene_lib.glsl"
#include "scene_pn.glsl"

layout(std140, set = 1, binding = 2) uniform ScenePn {
    // x: subdivision level (1 = the plain triangle), for a map surface the most; y > 0 a map surface: the nearest
    // depth an edge is measured at, z window pixels per view unit at depth 1, w 1 / the pixels of a cut piece
    vec4 u_pnLevel;
    uvec4 u_pnDraw;  // the mesh path's draw (scene_pn_data.glsl)
};

layout(location = 0) centroid in vec2 v_uv[];
layout(location = 1) in vec4 v_color[];
layout(location = 2) in vec3 v_viewpos[];
layout(location = 3) in float v_fog[];
layout(location = 4) flat in vec4 v_box[];
layout(location = 5) in float v_depth[];
layout(location = 6) flat in vec2 v_setback[];
layout(location = 7) in vec3 v_pnPos[];
layout(location = 8) in vec4 v_pnNormal[];
layout(location = 9) in vec3 v_pnEdge[];

layout(location = 0) out vec2 c_uv[];
layout(location = 1) out vec4 c_color[];
layout(location = 2) out float c_fog[];
layout(location = 3) out vec4 c_box[];
layout(location = 4) out vec2 c_setback[];
layout(location = 5) out vec3 c_pos[];
layout(location = 6) out vec4 c_normal[];
layout(location = 7) out vec3 c_edge[];

// The pieces edge a-b is cut into: the same for either order of its ends; one where it lies behind the eye.
float EdgeLevel(vec3 a, vec3 b, float most) {
    if (max(a.z, b.z) < u_pnLevel.y) return 1.0;
    float z = max(min(a.z, b.z), u_pnLevel.y);
    return clamp(ceil(length(a - b) * u_pnLevel.z / z * u_pnLevel.w), 1.0, most);
}

// The edge from corner i to the next is marked straight (scene_pn.glsl).
bool Straight(int i) { return (int(v_pnNormal[i].w + 0.5) & 2) != 0; }

void main() {
    c_uv[gl_InvocationID] = v_uv[gl_InvocationID];
    c_color[gl_InvocationID] = v_color[gl_InvocationID];
    c_fog[gl_InvocationID] = v_fog[gl_InvocationID];
    c_box[gl_InvocationID] = v_box[gl_InvocationID];
    c_setback[gl_InvocationID] = v_setback[gl_InvocationID];
    c_pos[gl_InvocationID] = v_pnPos[gl_InvocationID];
    c_normal[gl_InvocationID] = v_pnNormal[gl_InvocationID];
    c_edge[gl_InvocationID] = v_pnEdge[gl_InvocationID];
    if (gl_InvocationID == 0) {
        float level = clamp(u_pnLevel.x, 1.0, 16.0);
        if (u_pnLevel.y > 0.0) {
            // A patch wholly outside the view (or the shadow map's) is dropped: level 0.
            PnPatch b = PnMakeEdges(v_pnPos[0], v_pnPos[1], v_pnPos[2], v_pnNormal[0].xyz, v_pnEdge[0],
                                    v_pnNormal[1].xyz, v_pnEdge[1], v_pnNormal[2].xyz, v_pnEdge[2]);
            if (PnPatchCulled(b)) {
                gl_TessLevelOuter[0] = 0.0;
                gl_TessLevelOuter[1] = 0.0;
                gl_TessLevelOuter[2] = 0.0;
                gl_TessLevelInner[0] = 0.0;
                return;
            }
            float l01 = EdgeLevel(v_pnPos[0], v_pnPos[1], level);
            float l12 = EdgeLevel(v_pnPos[1], v_pnPos[2], level);
            float l20 = EdgeLevel(v_pnPos[2], v_pnPos[0], level);
            // Outer level i is the edge facing corner i.
            gl_TessLevelOuter[0] = Straight(1) ? 1.0 : l12;
            gl_TessLevelOuter[1] = Straight(2) ? 1.0 : l20;
            gl_TessLevelOuter[2] = Straight(0) ? 1.0 : l01;
            gl_TessLevelInner[0] = max(max(l01, l12), l20);
        } else {
            gl_TessLevelOuter[0] = level;
            gl_TessLevelOuter[1] = level;
            gl_TessLevelOuter[2] = level;
            gl_TessLevelInner[0] = level;
        }
    }
}
