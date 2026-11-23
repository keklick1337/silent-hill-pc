#version 450
// Scene vertex shader (RENDER.md section 6): map cells and posed meshes in the PS1 view space.
// Build modes as the RHI's other shaders: SDL3 GPU gets -DSH_RHI_VULKAN -DSH_RHI_CLIP_Z01 (SPIR-V), the GL backend
// replaces the #version line and defines nothing.
//
// Lighting of map cells follows the rules the map data was authored for (section 11.3): a flat tint, a point light
// whose intensity per vertex comes from the spot term, the facing term and the distance falloff, or a directional
// light; the intensity scales the world tint, 128 = unmodulated. Posed meshes arrive lit (a_color).
// Projection: the PS1 screen mapping screen = centre + H * xy / z, then the frame's 2D projection; clip W is the view
// depth. Every scene fragment writes its own window depth z / szMax (v_depth), the depth the PS1 prims drawn around
// the scene test against.

//
// Model upscaling (RENDER.md section 7.11) builds this file in two more ways: with SH_SCENE_PN the vertex stage of
// the tessellation path also hands the PN-triangle inputs on (scene_pn.tesc); with SH_SCENE_LIB it is a library of
// the stages after it (scene_pn.tese, scene_pn.mesh include it through scene_lib.glsl), with no inputs, outputs or
// main.

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 1, binding = slot)
#define SH_VARYING(n) layout(location = n)
#else
#define SH_UBO(slot) layout(std140)
#define SH_VARYING(n)
#endif

#ifndef SH_SCENE_LIB
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;
layout(location = 4) in vec4 a_box;  // the triangle's UV box (normalised), hi < lo = none
#endif

SH_UBO(0) uniform SceneVs {
    mat4 u_modelView;   // vertex space to view space (rotation and translation)
    mat4 u_ortho;       // screen units to clip space
    vec4 u_proj;        // x: H, yz: screen centre (with the draw-area offset), w: szMax
    vec4 u_light;       // x: mode (0 flat, 1 point, 2 directional, 3 prelit), y: ambient, z: max intensity, w: falloff
    vec4 u_tint;        // rgb: tint 0..255, w: fog on
    vec4 u_farColor;    // rgb: far colour 0..255
    vec4 u_lightVec;    // view axes, direction times intensity (Q12)
    vec4 u_lightPos;    // view space
    vec4 u_misc;        // x: near plane, y: map setback (view units), z: shadow caster pass, w: overlay lift (view units)
    mat4 u_casterMatrix; // view space to the flashlight's clip space
    vec4 u_seam;        // xy: NDC shift of this seam-line pass (0 for triangles), z: normal scale (0 = 1)
};

SH_UBO(1) uniform WorldFog {
    vec4 u_fogRampP;    // x: fog on, y: 128 / 2^depthShift, z: intensity (Q12), w: cull distance
    vec4 u_fogRamp[32];
};

#ifndef SH_SCENE_LIB
// centroid: under MSAA an edge sample outside the triangle would extrapolate the UV into the next atlas cell.
SH_VARYING(0) centroid out vec2 v_uv;
SH_VARYING(1) out vec4 v_color;    // rgb: modulation (1 = 128), a: fade
SH_VARYING(2) out vec3 v_viewpos;
SH_VARYING(3) out float v_fog;     // per-vertex fog amount 0..1
SH_VARYING(4) flat out vec4 v_box;
// The vertex's window depth, interpolated over the surface. gl_Position's depth is interpolated along the screen,
// which for a depth that is linear in view z is not the surface's own: on a long floor or wall triangle it lies far
// behind it (SH_SCENE_EXACT_DEPTH of scene.frag).
SH_VARYING(5) out float v_depth;
// x: how far behind its own plane, along the normal, the surface is written (view units; 0 for everything but the
// map), y: 1 / szMax (scene.frag).
SH_VARYING(6) flat out vec2 v_setback;
#endif

#ifdef SH_SCENE_PN
// The PN-triangle inputs (scene_pn.glsl): the view-space position and the patch normal, w = 1 on a crease. A map
// surface (u_pnLevel.y > 0) has two entries per vertex instead: the offsets to its edges' control points
// (scene/model_upscale.h PnSurface), turned into view space; v_pnNormal.w keeps the straight-edge mark.
layout(std430, set = 4, binding = 0) readonly buffer ScenePnNormals { vec4 pn_normal[]; };
SH_UBO(2) uniform ScenePn {
    vec4 u_pnLevel;
    uvec4 u_pnDraw;
};
SH_VARYING(7) out vec3 v_pnPos;
SH_VARYING(8) out vec4 v_pnNormal;
SH_VARYING(9) out vec3 v_pnEdge;
#endif

float FogRampAt(int i) {
    vec4 r = u_fogRamp[i / 4];
    int c = i - (i / 4) * 4;
    return (c == 0) ? r.x : ((c == 1) ? r.y : ((c == 2) ? r.z : r.w));
}

float FogByteAtZ(float z) {
    float fi = floor(z * u_fogRampP.y);
    float fb = (fi < 128.0) ? FogRampAt(int(max(fi, 0.0))) : 255.0;
    float fa = clamp(fb * 16.0 + u_fogRampP.z, 0.0, 4096.0);
    return floor((fa * 127.0 + 2048.0) / 4096.0);
}

// Distance quotient of the light rules: 16 / (|d|^2 / 4096) in 16.16, saturating like the authored data expects.
float LightQuotient(float sz) {
    if (sz * 2.0 <= 16.0) return 131071.0;
    return min(floor((16.0 * 131072.0 / sz + 1.0) * 0.5), 131071.0);
}

float LightTerm(float ir, float q) {
    return clamp(floor(-1024.0 + ir * q / 65536.0), -1024.0, 1023.0) + 1024.0;
}

// The vertex's colour (rgb: modulation, 1 = 128; a: fade) by the frame's light rules, at view position v.
vec4 SceneVertexColor(vec3 v, vec3 normal, vec4 color) {
    vec4 outColor;
    vec3 col;
    int mode = int(u_light.x + 0.5);
    if (mode == 3) {
        col = floor(color.rgb * 255.0 + 0.5);
        outColor.a = color.a;
    } else {
        outColor.a = 0.0;
        if (mode == 0) {
            col = u_tint.rgb;
        } else {
            vec3 n = mat3(u_modelView) * normal;
            if (u_seam.z > 0.0) n *= u_seam.z;
            float inten;
            if (mode == 1) {
                vec3 d = v - u_lightPos.xyz;
                float ir1 = clamp(floor(dot(u_lightVec.xyz, d) / 4096.0), -32768.0, 32767.0);
                float ir2 = clamp(floor(dot(n, d)), -32768.0, 32767.0);
                float sz = clamp(floor(dot(d, d) / 4096.0), 0.0, 65535.0);
                float q = LightQuotient(sz);
                float sx = LightTerm(ir1, q);
                float sy = LightTerm(ir2, q);
                float dp = clamp(floor(u_light.w * q / 4096.0), 0.0, 4096.0);
                float a1 = max(floor((sx * sy + sy * floor(dp / 16.0)) / 32.0) - 16.0, 0.0);
                float a2 = min(floor(sx / 2.0) + floor(dp / 4.0), 48.0);
                inten = min(a1 + floor(u_light.y / 32.0) + a2, u_light.z);
            } else {
                float ir = max(floor(dot(u_lightVec.xyz, n)), 0.0);
                inten = min(floor((ir + u_light.y) / 32.0), 255.0);
            }
            if (u_tint.w > 0.5)
                col = min(floor(u_tint.rgb * inten / 128.0), vec3(255.0));
            else
                col = (inten >= 8.0) ? floor(mix(u_farColor.rgb, u_tint.rgb, inten * 32.0 / 4096.0)) : vec3(0.0);
        }
    }
    outColor.rgb = col / 128.0;
    return outColor;
}

float SceneVertexFog(vec3 v) {
    return (u_fogRampP.x > 0.5 && v.z > 0.0) ? clamp(FogByteAtZ(v.z) / 127.0, 0.0, 1.0) : 0.0;
}

// The clip position of view position v and its window depth (v_depth).
vec4 SceneProject(vec3 v, out float windowDepth) {
    // clip = ortho * (centre * z + H * xy, 0, z): the perspective screen mapping as one linear transform.
    vec4 c = u_ortho * vec4(u_proj.yz * v.z + u_proj.x * v.xy, 0.0, v.z);
    // The fragment writes v_depth (scene.frag): the view depth on the PS1 prims' own scale (window depth = z / szMax),
    // so a map surface and a prop or a character meet where they really are, minus an overlay's lift; the map's
    // setback (misc.y) is added per fragment, along the surface's normal. The rasterised depth only clips at the
    // near plane.
    float near = u_misc.x;
    windowDepth = (v.z - u_misc.w) / u_proj.w;
    float depth = 2.0 * (v.z - near) / max(u_proj.w - near, 1.0) - 1.0;
    vec4 pos = vec4(c.xy + u_seam.xy * v.z, depth * v.z, v.z);
    if (u_misc.z > 0.5) pos = u_casterMatrix * vec4(v, 1.0);
#ifdef SH_RHI_CLIP_Z01
    pos.z = (pos.z + pos.w) * 0.5;
#endif
    return pos;
}

vec2 SceneSetback() { return vec2(u_misc.y, 1.0 / u_proj.w); }

#ifndef SH_SCENE_LIB
void main() {
    vec4 vp = u_modelView * vec4(a_position, 1.0);
    vec3 v = vp.xyz;
    v_viewpos = v;
    v_uv = a_uv;
    v_box = a_box;
    v_color = SceneVertexColor(v, a_normal, a_color);
    v_fog = SceneVertexFog(v);
    v_setback = SceneSetback();
    gl_Position = SceneProject(v, v_depth);
#ifdef SH_SCENE_PN
    v_pnPos = v;
    if (u_pnLevel.y > 0.0) {
        vec4 next = pn_normal[gl_VertexIndex * 2];
        v_pnNormal = vec4(mat3(u_modelView) * next.xyz, next.w);
        v_pnEdge = mat3(u_modelView) * pn_normal[gl_VertexIndex * 2 + 1].xyz;
    } else {
        v_pnNormal = vec4(mat3(u_modelView) * pn_normal[gl_VertexIndex].xyz, pn_normal[gl_VertexIndex].w);
        v_pnEdge = vec3(0.0);
    }
#endif
}
#endif
