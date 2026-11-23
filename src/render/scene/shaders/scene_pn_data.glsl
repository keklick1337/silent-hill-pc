// Model upscaling, mesh path (RENDER.md section 7.11): what the task and mesh stages read of one draw. Mesh shaders
// have no vertex input, so the draw's vertex and index buffers are read as storage buffers. Included after
// scene_lib.glsl (SceneVs, the light and projection functions).

layout(std140, set = 1, binding = 2) uniform ScenePn {
    vec4 u_pnLevel;  // x: subdivision level (1 = the plain triangle)
    uvec4 u_pnDraw;  // x: first index, y: triangles, z: base vertex
};

layout(std430, set = 4, binding = 0) readonly buffer ScenePnNormals { vec4 pn_normal[]; };
// The scene's vertex (scene_renderer.cpp GpuVertex, 13 words): position, normal, uv, RGBA8 colour, UV box.
layout(std430, set = 4, binding = 1) readonly buffer SceneVertexWords { uint vertex_word[]; };
layout(std430, set = 4, binding = 2) readonly buffer SceneIndexWords { uint index_word[]; };

const uint kVertexWords = 13u;
const uint kPnTasks = 32u;  // triangles per task workgroup

struct PnTaskPayload {
    uint tri[kPnTasks];  // the draw's triangles that survived culling, in their order
};

float VertexFloat(uint v, uint word) { return uintBitsToFloat(vertex_word[v * kVertexWords + word]); }

// The view-space position and patch normal of one corner of triangle `tri`, as the vertex stage gives them.
uint CornerVertex(uint tri, uint corner) { return index_word[u_pnDraw.x + tri * 3u + corner] + u_pnDraw.z; }

vec3 CornerPosition(uint v) {
    vec4 p = u_modelView * vec4(VertexFloat(v, 0u), VertexFloat(v, 1u), VertexFloat(v, 2u), 1.0);
    return p.xyz;
}

vec4 CornerNormal(uint v) { return vec4(mat3(u_modelView) * pn_normal[v].xyz, pn_normal[v].w); }

