#version 450
// Primitive batch vertex shader. One source for both RHI backends:
//   - SDL3 GPU: glslc -DSH_RHI_VULKAN -DSH_RHI_CLIP_Z01 -> SPIR-V (sets/bindings as SDL3 wants them);
//   - GL 3.3:   the backend replaces the #version line with "#version 330 core" and defines nothing.
// Covers page/CLUT/format decode, texcoord offset, colour * brightness, fog amount, the precise-vertex
// perspective path with per-vertex depth, view position, fade, geom3d, UV bounds, the flashlight shadow receiver
// (v_shadowViewPos) and the soft_fog P3 test (v_fogP3).
// Left out: the per-pixel lighting normal (v_ppN, a second vertex stream).
// Texture coordinates are perspective-correct (centroid only).

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 1, binding = slot)
#define SH_VARYING(n) layout(location = n)
#else
#define SH_UBO(slot) layout(std140)
#define SH_VARYING(n)
#endif

// Attribute locations follow rhi::PrimVertex (primVertexAttributes).
layout(location = 0) in ivec4 a_position;  // x, y (screen), page, clut
layout(location = 1) in float a_zw;
layout(location = 2) in uvec4 a_texcoord;  // u, v, brightness, dither
layout(location = 3) in vec4 a_color;
layout(location = 4) in ivec4 a_extra;     // tcx, tcy, fog (0..127), per-vertex depth marker
layout(location = 5) in vec3 a_pgxp;
layout(location = 6) in vec3 a_normal;     // .z = character fade
layout(location = 7) in vec3 a_viewpos;
layout(location = 8) in float a_geom3d;
layout(location = 9) in uvec4 a_uvlim;

SH_UBO(0) uniform VsFrame {
    mat4 u_projection;
    vec4 u_pgxp;     // x: PGXP enabled (0/1), y: szMax, z: far W clamp, w: world far bias
    vec4 u_vsModes;  // x: soft fog
};

// The world fog ramp as the CPU fog bytes use it: x = fog on, y = 128 / 2^depthShift,
// z = intensity (Q12), w = far cull distance (view Z). The same block in both stages.
SH_UBO(1) uniform WorldFog {
    vec4 u_fogRampP;
    vec4 u_fogRamp[32];
};

SH_VARYING(0) centroid out vec4 v_texcoord;
SH_VARYING(1) out vec4 v_color;
SH_VARYING(2) centroid out vec4 v_page_clut;
SH_VARYING(3) out float v_fogAmount;
SH_VARYING(4) out float v_is3d;
SH_VARYING(5) out vec3 v_viewpos;
SH_VARYING(6) out float v_fade;
SH_VARYING(7) out float v_geom3d;
SH_VARYING(8) centroid out vec4 v_uvlim;
SH_VARYING(9) out float v_texfmt;
SH_VARYING(10) out float v_fogP3;
SH_VARYING(11) out vec4 v_shadowViewPos;

const vec2 c_UVFudge = vec2(0.00025, 0.00025);

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

void main() {
    vec4 position = vec4(a_position);
    vec4 texcoord = vec4(a_texcoord);
    vec4 extra = vec4(a_extra);
    bool pgxpOn = u_pgxp.x > 0.5;

    v_texcoord = texcoord;
    v_texcoord.xy += extra.xy * 0.5;
    v_color = a_color;
    v_color.xyz *= texcoord.z;

    // Vertex page = tpage X/Y in bits 0-4, tpage format in bits 5-6, bit 7 = untextured (v_texfmt 4).
    float pageXY = mod(position.z, 32.0);
    v_texfmt = floor(position.z / 32.0);
    v_page_clut.x = fract(pageXY / 16.0) * 1024.0;
    v_page_clut.y = floor(pageXY / 16.0) * 256.0;
    v_page_clut.z = fract(position.w / 64.0);
    v_page_clut.w = floor(position.w / 64.0) / 512.0;
    v_page_clut.xy += c_UVFudge;
    v_page_clut.zw += c_UVFudge;

    if (pgxpOn && a_pgxp.z > 0.0) {
        vec4 b = u_projection * vec4(a_pgxp.xy, a_zw, 1.0);
        float W = a_pgxp.z;
        if (u_pgxp.z > 0.0) W = min(W, u_pgxp.z);
        if (extra.w > 0.5) {
            float dvz = min(a_pgxp.z + (extra.w > 1.5 ? 0.0 : u_pgxp.w), u_pgxp.y);
            b.z = (2.0 * dvz / u_pgxp.y - 1.0) * b.w;
        }
        gl_Position = vec4(b.xyz * W, b.w * W);
    } else {
        gl_Position = u_projection * vec4(position.xy, a_zw, 1.0);
    }

    v_is3d = pgxpOn ? ((a_pgxp.z > 0.0) ? 1.0 : 0.0) : 1.0;
    v_fogAmount = clamp(extra.z / 127.0, 0.0, 1.0);
    // soft_fog P3: 1 when this vertex's fog byte is the ramp at its own view depth.
    v_fogP3 = (u_vsModes.x > 0.5 && u_fogRampP.x > 0.5 && a_viewpos.z > 0.0 &&
               abs(FogByteAtZ(a_viewpos.z) - extra.z) < 1.5) ? 1.0 : 0.0;
    v_viewpos = a_viewpos;
    v_fade = a_normal.z;
    v_geom3d = a_geom3d;
    v_uvlim = vec4(a_uvlim);
    // Receiver position over view Z, scaled by this vertex's clip W, so the fragment shader can rebuild it
    // perspective-correctly on the affine path too.
    float shadowInvZ = (a_viewpos.z > 0.0) ? (1.0 / a_viewpos.z) : 0.0;
    float shadowClipW = (pgxpOn && a_pgxp.z > 0.0) ? ((u_pgxp.z > 0.0) ? min(a_pgxp.z, u_pgxp.z) : a_pgxp.z) : 1.0;
    v_shadowViewPos = vec4(a_viewpos * shadowInvZ, shadowInvZ) * shadowClipW;

#ifdef SH_RHI_CLIP_Z01
    // The game's projections are GL ones (Z in [-1, 1]); SDL3 GPU clips Z to [0, 1].
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
#endif
}
