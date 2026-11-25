#version 450
// Scene fragment shader (RENDER.md section 6): texture times the lit colour, the per-pixel flashlight (Classic and
// Modern), fog by the world's fog ramp with soft_fog's fragment ramp, knee and cull fade, fog to black for
// additive surfaces, deband. The frame block has the layout of the primitive batch shader's (PrimFsUniforms), so the scene
// takes the values the PS1 prims around it are drawn with.

// SH_SCENE_QUAD (phones, native Vulkan with quad operations in the fragment stage): the exact-depth shader whose
// shadow filters and map lights are shared by the four pixels of each 2x2 quad (RENDER.md 7.9, 7.14).
// SH_SCENE_QUAD_RASTER is the same without the fragment's own depth (the late seams of scene_renderer.cpp).
#ifdef SH_SCENE_QUAD_RASTER
#define SH_SCENE_QUAD 1
#else
#ifdef SH_SCENE_QUAD
#define SH_SCENE_EXACT_DEPTH 1
#endif
#endif
#ifdef SH_SCENE_QUAD
#extension GL_KHR_shader_subgroup_quad : require
#endif

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 3, binding = slot)
#define SH_SAMPLER(slot) layout(set = 2, binding = slot)
#define SH_VARYING(n) layout(location = n)
#else
#define SH_UBO(slot) layout(std140)
#define SH_SAMPLER(slot)
#define SH_VARYING(n)
#endif

SH_VARYING(0) centroid in vec2 v_uv;
SH_VARYING(1) in vec4 v_color;
SH_VARYING(2) in vec3 v_viewpos;
SH_VARYING(3) in float v_fog;
SH_VARYING(4) flat in vec4 v_box;
#ifdef SH_SCENE_EXACT_DEPTH
SH_VARYING(5) in float v_depth;
SH_VARYING(6) flat in vec2 v_setback;
#endif

layout(location = 0) out vec4 fragColor;

SH_SAMPLER(0) uniform sampler2D s_tex;
SH_SAMPLER(1) uniform sampler2D s_shadow;  // flashlight shadow depth map (u_prog.w)
// What the translucent surfaces between the light and the scene let through: rgb the product of their
// transmittances, a the nearest one's distance along the light over the far plane (white and 1 where there is none).
SH_SAMPLER(2) uniform sampler2D s_shadowTint;

SH_UBO(0) uniform FsFrame {
    vec4 u_fog;      // rgb: fog colour, a: fog strength
    vec4 u_modes;    // y: soft fog, z: filter (0 nearest)
    vec4 u_dither;   // z: deband, w: target height (top-left gl_FragCoord flip)
    vec4 u_flMode;   // x: flashlight on, y: style (1 Classic, 0 Modern), z: soft highlights, w: range
    vec4 u_flPos;    // xyz: light position (view space), w: inner cone cosine
    vec4 u_flDir;    // xyz: light direction, w: outer cone cosine
    vec4 u_flColor;  // rgb: light colour
    vec4 u_prog;
    vec4 u_rgbaTex;
    vec4 u_rgbaHalf;
    vec4 u_rgbaFb;
    mat4 u_shadowMatrix;
    vec4 u_shadowP;
    vec4 u_shadowQ;
};

SH_UBO(1) uniform WorldFog {
    vec4 u_fogRampP;
    vec4 u_fogRamp[32];
};

SH_UBO(2) uniform SceneMaterial {
    vec4 u_mat;      // x: blend (scene.h Blend: 0 opaque, 1 additive by STP, 2 average by texel alpha, 3 PS1
                     // average by STP, 4 subtractive, 5 add a quarter), y: 1 textured, 2 the water surface (SceneWater),
                     // z: shadow map pass (1 depth, 2 transmittance),
                     // w: texels drawn (0 all, 1 solid only, 2 STP only, 3 all as STP), plus 4 for an isolated
                     // model (item preview: no world fog, no flashlight)
    vec4 u_rt;       // ray-traced results bound (native Vulkan): x flashlight shadow, y reflection (2: mirror rays,
                     // not only highlights), z bounce light
    vec4 u_mat2;     // x: mip levels of the texture (above 1 only for an upscaled one, texture_upscale), y and z: the
                     // low and high 16 bits of the mask of the map lights that can reach the draw
};

// The frame's ray-traced results at this pixel (RENDER.md 2.2b): rgb, and in a the view z of the surface they were
// traced for, which may be another one than this fragment (a blended surface in front, an edge).
SH_SAMPLER(4) uniform sampler2D s_rtShadow;
SH_SAMPLER(5) uniform sampler2D s_rtReflect;
SH_SAMPLER(6) uniform sampler2D s_rtGi;

vec4 RtAt(sampler2D s) { return texelFetch(s, ivec2(gl_FragCoord.xy), 0); }
bool RtHere(vec4 r) { return r.a > 0.0 && abs(r.a - v_viewpos.z) <= 2.0 + 0.004 * v_viewpos.z; }

// The posed meshes mirrored in the water's plane, drawn with the frame's own projection (SceneRenderer::reflect).
SH_SAMPLER(3) uniform sampler2D s_reflect;

// The water surface (RENDER.md 6.9, gamedata/effects/water.json "surface"), then the map's lights without ray
// tracing (RENDER.md 7.14): one block, since SDL3 GPU has four uniform slots per stage.
SH_UBO(3) uniform SceneFrame {
    mat4 u_wWorld;     // view space to world
    vec4 u_wPlane;     // the surface in view space: unit normal, distance
    vec4 u_wProj;      // ndc = xy * (v.xy / v.z) + zw
    vec4 u_wBase;      // rgb: unlit colour, a: wave phase 0..1
    vec4 u_wLit;       // rgb: lit colour, a: wave contrast
    vec4 u_wWave;      // x: radians per view unit, y: reflection shift per slope (uv), zw: blur (uv)
    vec4 u_wTint;      // rgb: reflection tint times strength, a: 1 = there is a mirrored picture
    vec4 u_wGlow;      // rgb: glow colour times strength, a: picture height over width
    vec4 u_wGlowSize;  // xy: the glow's radii (ndc)
    vec4 u_wLamp;      // xyz: the lamp mirrored in the surface (view space), w: the glow's strength
    vec4 u_wLevel;     // the quality level. x: wave layers 1..3, y: the wave grid's square (wave radians, 0 none),
                       // z: colour steps (0 smooth), w: blur taps 1 / 3 / 5
    vec4 u_lInfo;      // x: map lights, y: sun on, z: sun shadow map on, w: flakes
    vec4 u_sunDir;     // xyz: the way the sunlight travels, w: the sun map's texel (view units)
    vec4 u_sunColor;   // rgb, w: angular radius (radians)
    vec4 u_sunMap;     // x: side (texels), y: depth range (view units), z: flake opacity, w: flakes shade the beam
    mat4 u_sunMatrix;  // view space to the sun map's clip space
    vec4 u_lShadow;    // x: 1 + the map light shaded through the flashlight's shadow map (u_prog.w), 0: none,
                       // y: the sun shadow map's taps
    vec4 u_lPos[32];    // xyz, w: range
    vec4 u_lDir[32];    // a spot's axis (zero: a point light), w: outer cone cosine
    vec4 u_lColor[32];  // rgb, w: inner cone cosine
    vec4 u_flakes[64];  // xyz, w: radius
    // The flakes binned by where their sun shadows can fall (split_renderer.cpp FlakeGrid): a grid of 16x16 cells in
    // the plane across the sunlight, each listing the flakes whose shadow can reach it, in rising order.
    vec4 u_flakeGrid;      // xy: the grid's corner (u, v), z: cells per view unit, w: 1 = the grid is there
    vec4 u_flakeU;         // xyz: the grid's u axis, w: the largest squared distance from the eye it holds for
    vec4 u_flakeV;         // xyz: the grid's v axis, w: the lowest point along the light it holds for
    vec4 u_flakeCells[128]; // two cells per entry: first list index, count
    vec4 u_flakeList[256]; // flake indices, four per entry
    // The lights that can reach each tile of a 16x8 grid over the picture (scene_renderer.cpp fill_light_tiles): bit i
    // of a tile's mask is light i, four tiles per entry.
    uvec4 u_lTiles[32];
};

// The sun's shadow map (sun_shadows): window depth of the nearest caster towards the sun.
SH_SAMPLER(7) uniform sampler2D s_sunShadow;
// The fog ramp's bytes (PrimRenderer::fogRamp): a texel fetch, since indexing u_fogRamp per pixel is a slow path
// on some GPUs. WorldFog keeps the ramp for the vertex stage, and GL links the block of both stages as one. Texel i
// holds entry i and the next one (255 past the end), so one fetch gives both ends of the interpolation.
SH_SAMPLER(8) uniform sampler2D s_fogRamp;

vec2 FogRampPair(int i) { return floor(texelFetch(s_fogRamp, ivec2(i, 0), 0).rg * 255.0 + 0.5); }

float FogAmountAtZ(float z) {
    float fi = z * u_fogRampP.y;
    float fb = 255.0;
    if (fi < 128.0) {
        float f0 = floor(max(fi, 0.0));
        vec2 r = FogRampPair(int(f0));
        fb = mix(r.x, r.y, clamp(fi - f0, 0.0, 1.0));
    }
    return clamp(fb * 16.0 + u_fogRampP.z, 0.0, 4096.0) / 4096.0;
}

// Window depth [0,1] -> linear distance along the light's forward axis (shadow frustum near/far).
float shLinDepth(float zw) {
    float ndc = zw * 2.0 - 1.0;
    return (2.0 * u_shadowQ.z * u_shadowQ.w) / (u_shadowQ.w + u_shadowQ.z - ndc * (u_shadowQ.w - u_shadowQ.z));
}

vec2 fragCoordGL() {
#ifdef SH_RHI_VULKAN
    return vec2(gl_FragCoord.x, u_dither.w - gl_FragCoord.y);
#else
    return gl_FragCoord.xy;
#endif
}

// The flashlight's lens radius in view units (256 per metre). It is larger than a pocket torch's so the
// penumbra of a shadow a few metres behind its caster is visibly soft.
const float kLightRadius = 9.0;
// The widest blocker search and penumbra, in shadow map UV: a caster right at the lens would otherwise smear over
// the whole map.
const float kMaxPenumbraUv = 0.02;

// A Vogel disk of n taps turned by rot: tap i lies sqrt((i + 0.5) / n) out along the angle i * 2.39996323 + rot. The
// loops walk it tap by tap: the direction of tap i + 1 is tap i's turned by the golden angle (its cosine and sine
// below), so a loop pays one rotation per tap instead of a sine and a cosine.
const vec2 kGoldenTurn = vec2(-0.7373688, 0.6754903);
vec2 GoldenTurn(vec2 d) {
    return vec2(d.x * kGoldenTurn.x - d.y * kGoldenTurn.y, d.x * kGoldenTurn.y + d.y * kGoldenTurn.x);
}

// Whether the map's depth sd lies in front of the receiver at shadow map position uv. The receiver is its plane
// through luv with gradient `grad`, the step limited to sqrt(slope2) per UV so the derivatives of a silhouette pixel
// cannot throw the plane across the map: luv.z + clamp(t, -lim, lim) - bias > sd, with the limit compared in squares,
// so no square root is taken per texel.
bool Occludes(vec3 luv, vec2 grad, vec2 uv, float slope2, float bias, float sd) {
    vec2 duv = uv - luv.xy;
    float t = dot(grad, duv);
    float q = sd + bias - luv.z;
    float lim2 = slope2 * dot(duv, duv);
    float q2 = q * q;
    // clamp(t, -lim, lim) > q: always when q is below -lim, never from lim up, else when t > q.
    return (q < 0.0 && q2 > lim2) || ((q < 0.0 || q2 < lim2) && t > q);
}

// The light the translucent surfaces let through to a receiver `recvLin` along the light at shadow map position uv.
// The stored distance has 8 bits, so the surface itself and what lies right behind it (two steps) stay unfiltered.
vec3 TintTap(vec2 uv, float recvLin) {
    vec4 t = texture(s_shadowTint, uv);
    float behind = (recvLin / u_shadowQ.w - t.a) * 255.0;
    return mix(vec3(1.0), t.rgb, smoothstep(1.0, 2.5, behind));
}

// The per-pixel flashlight's shadow, both styles: percentage-closer soft shadows. A search over the part of the map
// the lens can see around the receiver finds the mean blocker distance; the penumbra then widens with the
// receiver's distance behind the blocker and closes at contact. Both loops walk a Vogel disk turned per pixel by a
// fixed screen-space noise, so the grain does not crawl when the light moves. The compare is in window depth with
// the receiver's own plane, and a bias that is a constant distance along the light (a constant window-depth bias
// grows with the square of the distance and detaches far shadows from their casters). The taps follow
// shadow_filter (u_prog.w - 1): 6 + 8, 12 + 20, 16 + 32. rgb: the light that arrives.
vec3 FlashlightShadow(vec3 p, vec3 L, float d) {
    vec3 ps = p + L * (u_shadowP.y * d);
    vec4 lp = u_shadowMatrix * vec4(ps, 1.0);
    if (lp.w <= 0.0) return vec3(1.0);
    vec3 luv = lp.xyz / lp.w * 0.5 + 0.5;
    if (luv.x <= 0.0 || luv.x >= 1.0 || luv.y <= 0.0 || luv.y >= 1.0 || luv.z >= 1.0) return vec3(1.0);
    vec3 dx = dFdx(luv);
    vec3 dy = dFdy(luv);
    float det = dx.x * dy.y - dx.y * dy.x;
    vec2 grad = vec2(0.0);
    if (abs(det) > 1e-12) grad = vec2(dx.z * dy.y - dy.z * dx.y, dy.z * dx.x - dx.z * dy.x) / det;

    float zn = u_shadowQ.z;
    float recv = max(lp.w, zn);
    // The projection's scale: half the map spans recv / scale view units at the receiver.
    float scale = length(vec3(u_shadowMatrix[0][0], u_shadowMatrix[1][0], u_shadowMatrix[2][0]));
    float lens = 0.5 * scale * kLightRadius;
    // d(window depth) / d(distance) at the receiver.
    float perUnit = zn / (recv * recv);
    // One view unit plus half a percent of the distance at the default shadow_bias (console shadowbias scales it).
    float bias = perUnit * (1.0 + 0.005 * recv) * (u_shadowP.x / 0.0018);
    float slope = 12.0 * recv * perUnit / max(scale, 1e-4);
    float texel = u_shadowQ.x;
    int quality = clamp(int(u_prog.w + 0.5) - 1, 0, 2);
    int searchTaps = (quality == 0) ? 6 : ((quality == 1) ? 12 : 16);
    int filterTaps = (quality == 0) ? 8 : ((quality == 1) ? 20 : 32);
    float rot = 6.2831853 * fract(52.9829189 * fract(dot(fragCoordGL(), vec2(0.06711056, 0.00583715))));

    float searchUv = clamp(lens * (recv - zn) / (recv * zn), 2.0 * texel, kMaxPenumbraUv);
    float slope2 = slope * slope;
    vec2 dir0 = vec2(cos(rot), sin(rot));
    vec2 dir = dir0;
    float blockers = 0.0, blockerSum = 0.0;
    for (int i = 0; i < searchTaps; i++) {
        vec2 o = sqrt((float(i) + 0.5) / float(searchTaps)) * dir;
        dir = GoldenTurn(dir);
        vec2 uv = (floor((luv.xy + o * searchUv) / texel) + 0.5) * texel;
        float sd = texture(s_shadow, uv).r;
        if (Occludes(luv, grad, uv, slope2, bias, sd)) {
            blockerSum += shLinDepth(sd);
            blockers += 1.0;
        }
    }
    // Translucent surfaces: their own penumbra, from the nearest one's distance at the receiver's texel.
    float glass = texture(s_shadowTint, luv.xy).a * u_shadowQ.w;
    float tintUv = clamp(lens * max(recv - glass, 0.0) / (recv * max(glass, zn)), texel, kMaxPenumbraUv);
    int tintTaps = (quality == 0) ? 4 : 8;
    vec3 tint = vec3(0.0);
    dir = dir0;
    for (int i = 0; i < tintTaps; i++) {
        tint += TintTap(luv.xy + sqrt((float(i) + 0.5) / float(tintTaps)) * dir * tintUv, recv);
        dir = GoldenTurn(dir);
    }
    tint = mix(vec3(1.0), tint / float(tintTaps), u_shadowP.z);
    if (blockers < 0.5) return tint;

    float blocker = clamp(blockerSum / blockers, zn, recv);
    float filterUv = clamp(lens * (recv - blocker) / (recv * blocker), 1.25 * texel, kMaxPenumbraUv);
    float occ = 0.0;
    dir = dir0;
    for (int i = 0; i < filterTaps; i++) {
        // Each tap is a bilinear compare of four texels, so an edge is smooth below the tap spacing too. SPIR-V
        // fetches them with one gather at the corner they share (w, z, x, y: the texels at +0,0, +1,0, +0,1, +1,1).
        vec2 tp = (luv.xy + sqrt((float(i) + 0.5) / float(filterTaps)) * dir * filterUv) / texel - 0.5;
        dir = GoldenTurn(dir);
        vec2 base = floor(tp);
        vec2 fr = tp - base;
        vec2 uv0 = (base + 0.5) * texel;
        vec2 uv1 = uv0 + texel;
#ifdef SH_RHI_VULKAN
        vec4 g = textureGather(s_shadow, (base + 1.0) * texel, 0);
#else
        vec4 g = vec4(texture(s_shadow, vec2(uv0.x, uv1.y)).r, texture(s_shadow, uv1).r,
                      texture(s_shadow, vec2(uv1.x, uv0.y)).r, texture(s_shadow, uv0).r);
#endif
        if (Occludes(luv, grad, uv0, slope2, bias, g.w)) occ += (1.0 - fr.x) * (1.0 - fr.y);
        if (Occludes(luv, grad, vec2(uv1.x, uv0.y), slope2, bias, g.z)) occ += fr.x * (1.0 - fr.y);
        if (Occludes(luv, grad, vec2(uv0.x, uv1.y), slope2, bias, g.x)) occ += (1.0 - fr.x) * fr.y;
        if (Occludes(luv, grad, uv1, slope2, bias, g.y)) occ += fr.x * fr.y;
    }
    return tint * (1.0 - u_shadowP.z * (occ / float(filterTaps)));
}

#ifdef SH_SCENE_QUAD
// The pixel's place in its 2x2 quad: the quads of a fragment shader are aligned to even window coordinates.
int QuadLane() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    return (f.x & 1) | ((f.y & 1) << 1);
}
// Sums over the quad. Every lane adds the same values (in another order of commutative pairs), so the result is the
// same bits in all four and a branch on it stays uniform over the quad.
float QuadSum(float x) {
    x += subgroupQuadSwapHorizontal(x);
    return x + subgroupQuadSwapVertical(x);
}
vec3 QuadSum(vec3 x) {
    x += subgroupQuadSwapHorizontal(x);
    return x + subgroupQuadSwapVertical(x);
}
uint QuadOr(uint x) {
    x |= subgroupQuadSwapHorizontal(x);
    return x | subgroupQuadSwapVertical(x);
}
bool QuadAny(bool b) { return QuadOr(b ? 1u : 0u) != 0u; }

// The golden angle turned four times: a lane walks every fourth tap of a disk.
const vec2 kGoldenTurn4 = vec2(-0.9847135, -0.1741820);
vec2 GoldenTurn4(vec2 d) {
    return vec2(d.x * kGoldenTurn4.x - d.y * kGoldenTurn4.y, d.x * kGoldenTurn4.y + d.y * kGoldenTurn4.x);
}
// The direction of tap `lane` of a disk turned by rot.
vec2 QuadFirstDir(float rot, int lane) {
    vec2 dir = vec2(cos(rot), sin(rot));
    for (int i = 0; i < lane; ++i) dir = GoldenTurn(dir);
    return dir;
}

// FlashlightShadow for a whole quad: each pixel walks every fourth tap of the quad's disks from its own position, the
// counts are pooled, and the four pixels take the one result: the taps of one pixel per quad, a 2x2 box over the
// filter. The quad has 8 + 16 taps (shadow_filter 0), 16 + 32, 16 + 48. Every pixel of the quad must call it.
vec3 FlashlightShadowQuad(vec3 p, vec3 L, float d) {
    int lane = QuadLane();
    vec3 ps = p + L * (u_shadowP.y * d);
    vec4 lp = u_shadowMatrix * vec4(ps, 1.0);
    vec3 luv = lp.xyz / (lp.w > 0.0 ? lp.w : 1.0) * 0.5 + 0.5;
    bool inside = lp.w > 0.0 && luv.x > 0.0 && luv.x < 1.0 && luv.y > 0.0 && luv.y < 1.0 && luv.z < 1.0;
    vec3 dx = dFdx(luv);
    vec3 dy = dFdy(luv);
    float det = dx.x * dy.y - dx.y * dy.x;
    vec2 grad = vec2(0.0);
    if (abs(det) > 1e-12) grad = vec2(dx.z * dy.y - dy.z * dx.y, dy.z * dx.x - dx.z * dy.x) / det;

    float zn = u_shadowQ.z;
    float recv = max(lp.w, zn);
    float scale = length(vec3(u_shadowMatrix[0][0], u_shadowMatrix[1][0], u_shadowMatrix[2][0]));
    float lens = 0.5 * scale * kLightRadius;
    float perUnit = zn / (recv * recv);
    float bias = perUnit * (1.0 + 0.005 * recv) * (u_shadowP.x / 0.0018);
    float slope = 12.0 * recv * perUnit / max(scale, 1e-4);
    float texel = u_shadowQ.x;
    int quality = clamp(int(u_prog.w + 0.5) - 1, 0, 2);
    int searchTaps = (quality == 0) ? 8 : 16;
    int filterTaps = (quality == 0) ? 16 : ((quality == 1) ? 32 : 48);
    float rot = 6.2831853 * fract(52.9829189 * fract(dot(fragCoordGL(), vec2(0.06711056, 0.00583715))));
    vec2 dir0 = QuadFirstDir(rot, lane);

    float searchUv = clamp(lens * (recv - zn) / (recv * zn), 2.0 * texel, kMaxPenumbraUv);
    float slope2 = slope * slope;
    float blockers = 0.0, blockerSum = 0.0;
    vec3 tint = vec3(0.0);
    int tintTaps = (quality == 0) ? 4 : 8;
    if (inside) {
        vec2 dir = dir0;
        for (int i = lane; i < searchTaps; i += 4) {
            vec2 o = sqrt((float(i) + 0.5) / float(searchTaps)) * dir;
            dir = GoldenTurn4(dir);
            vec2 uv = (floor((luv.xy + o * searchUv) / texel) + 0.5) * texel;
            float sd = texture(s_shadow, uv).r;
            if (Occludes(luv, grad, uv, slope2, bias, sd)) {
                blockerSum += shLinDepth(sd);
                blockers += 1.0;
            }
        }
        float glass = texture(s_shadowTint, luv.xy).a * u_shadowQ.w;
        float tintUv = clamp(lens * max(recv - glass, 0.0) / (recv * max(glass, zn)), texel, kMaxPenumbraUv);
        dir = dir0;
        for (int i = lane; i < tintTaps; i += 4) {
            tint += TintTap(luv.xy + sqrt((float(i) + 0.5) / float(tintTaps)) * dir * tintUv, recv);
            dir = GoldenTurn4(dir);
        }
    } else {
        tint = vec3(float((tintTaps - lane + 3) / 4));
    }
    blockers = QuadSum(blockers);
    blockerSum = QuadSum(blockerSum);
    tint = mix(vec3(1.0), QuadSum(tint) / float(tintTaps), u_shadowP.z);
    if (blockers < 0.5) return tint;

    float occ = 0.0;
    if (inside) {
        float blocker = clamp(blockerSum / blockers, zn, recv);
        float filterUv = clamp(lens * (recv - blocker) / (recv * blocker), 1.25 * texel, kMaxPenumbraUv);
        vec2 dir = dir0;
        for (int i = lane; i < filterTaps; i += 4) {
            vec2 tp = (luv.xy + sqrt((float(i) + 0.5) / float(filterTaps)) * dir * filterUv) / texel - 0.5;
            dir = GoldenTurn4(dir);
            vec2 base = floor(tp);
            vec2 fr = tp - base;
            vec2 uv0 = (base + 0.5) * texel;
            vec2 uv1 = uv0 + texel;
            vec4 g = textureGather(s_shadow, (base + 1.0) * texel, 0);
            if (Occludes(luv, grad, uv0, slope2, bias, g.w)) occ += (1.0 - fr.x) * (1.0 - fr.y);
            if (Occludes(luv, grad, vec2(uv1.x, uv0.y), slope2, bias, g.z)) occ += fr.x * (1.0 - fr.y);
            if (Occludes(luv, grad, vec2(uv0.x, uv1.y), slope2, bias, g.x)) occ += (1.0 - fr.x) * fr.y;
            if (Occludes(luv, grad, uv1, slope2, bias, g.y)) occ += fr.x * fr.y;
        }
    }
    return tint * (1.0 - u_shadowP.z * (QuadSum(occ) / float(filterTaps)));
}
#endif

// The Modern flashlight: a soft spotlight. The cone's edge eases over the whole penumbra between the outer and the
// inner cosine (smootherstep) with a faint spill past the outer edge, so no ring is visible; the beam falls off with
// the inverse square of the distance, windowed to zero at the range; a wrapped Lambert term turns the surfaces to it
// without the hard terminator of a plain N.L on flat-shaded faces.
float ModernCone(float c, float outer, float inner) {
    float t = clamp((c - outer) / max(inner - outer, 1e-4), 0.0, 1.0);
    float cone = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    return cone + 0.06 * smoothstep(outer - 0.2, outer, c) * (1.0 - cone);
}

float ModernFalloff(float d, float range) {
    float r0 = 0.4 * range;
    float x = d / max(range, 1.0);
    float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
    return window * window / (1.0 + (d * d) / (r0 * r0));
}

float ModernTurn(float nl) { return 0.1 + 0.9 * max((nl + 0.3) / 1.3, 0.0); }

// The fog of a stretch of `d` view units, as the surface shading takes it (rt_trace.comp FogAt).
float FogAlong(float d) { return u_fogRampP.x > 0.5 ? clamp(FogAmountAtZ(d) * u_fog.a, 0.0, 1.0) : 0.0; }

// What the falling flakes leave of a light reaching p along unit L from `dist` away (0: the sun). `spread` is the
// emitter's radius over its distance (the sun: its angular radius), so a flake's shadow is the light's cone at the
// flake: sharp close to the receiver, wider and fainter further up. Only ever darkens, by the flake's opacity.
void FlakeStep(int i, vec3 p, vec3 L, float dist, float spread, inout float vis) {
    vec4 f = u_flakes[i];
    vec3 c = f.xyz - p;
    float t = dot(c, L);
    if (t <= f.w || (dist > 0.0 && t >= dist)) return;
    float cone = max(spread * t, 0.5);
    float reach = cone + f.w;
    float e2 = dot(c, c) - t * t;
    if (e2 >= reach * reach) return;
    float cover = min(1.0, (f.w * f.w) / (cone * cone));
    float k = 1.0 - smoothstep(abs(cone - f.w), reach, sqrt(max(e2, 0.0)));
    vis *= 1.0 - u_sunMap.z * cover * k;
}

float FlakeShadow(vec3 p, vec3 L, float dist, float spread) {
    float vis = 1.0;
    int n = int(u_lInfo.w + 0.5);
    for (int i = 0; i < n; ++i) FlakeStep(i, p, L, dist, spread, vis);
    return vis;
}

// The sun's flake shadow through the grid: only the flakes listed for p's cell are tried, in the same order, and every
// flake left out is one whose shadow cannot reach p (the grid keeps a margin for rounding), so the product is the
// one of FlakeShadow. A point outside the grid lies in no flake's shadow; one too far from the eye or too far below
// the flakes along the light takes them all.
float FlakeShadowSun(vec3 p, vec3 L, float spread) {
    if (u_flakeGrid.w < 0.5 || dot(p, p) > u_flakeU.w || dot(p, L) < u_flakeV.w) return FlakeShadow(p, L, 0.0, spread);
    vec2 g = (vec2(dot(p, u_flakeU.xyz), dot(p, u_flakeV.xyz)) - u_flakeGrid.xy) * u_flakeGrid.z;
    if (g.x < 0.0 || g.y < 0.0 || g.x >= 16.0 || g.y >= 16.0) return 1.0;
    int cell = int(g.y) * 16 + int(g.x);
    vec4 entry = u_flakeCells[cell / 2];
    vec2 range = (cell % 2 == 0) ? entry.xy : entry.zw;
    int first = int(range.x + 0.5);
    int count = int(range.y + 0.5);
    float vis = 1.0;
    for (int k = first; k < first + count; ++k) {
        vec4 four = u_flakeList[k / 4];
        int c = k % 4;
        float idx = (c == 0) ? four.x : ((c == 1) ? four.y : ((c == 2) ? four.z : four.w));
        FlakeStep(int(idx + 0.5), p, L, 0.0, spread, vis);
    }
    return vis;
}

// The sun's shadow map at p (facing N): a rotated disc of u_lShadow.y compares (12, 6 at the cheapest shadow_filter),
// 1.5 texels wide; the receiver moves off its
// surface along the normal by more texels the more it slopes away from the sun. Alpha holes of fences and leaves
// were never written, so light passes them.
float SunShadow(vec3 p, vec3 N, float nl, float rot) {
    float slope = sqrt(max(1.0 - nl * nl, 0.0)) / max(nl, 0.2);
    vec3 q = p + N * (u_sunDir.w * (1.0 + slope));
    vec3 c = (u_sunMatrix * vec4(q, 1.0)).xyz * 0.5 + 0.5;
    if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;
    float bias = 2.0 / max(u_sunMap.y, 1.0);
    float r = 1.5 / max(u_sunMap.x, 1.0);
    float lit = 0.0;
    int taps = int(u_lShadow.y + 0.5);
    vec2 dir = vec2(cos(rot), sin(rot));
    for (int i = 0; i < taps; ++i) {
        lit += c.z - bias <= texture(s_sunShadow, c.xy + sqrt((float(i) + 0.5) / float(taps)) * dir * r).r ? 1.0 : 0.0;
        dir = GoldenTurn(dir);
    }
    return lit / float(taps);
}

// The map's lights and the sun on the surface without ray tracing (map_lights): the same unshadowed light as the
// ray tracer's (colour, a spot's cone, the modern beam's windowed falloff, the fog of the way from the light; the sun
// fades with the fog between the eye and the point), Lambert, times the caller's albedo. The sun takes its shadow
// map and the flakes' term; one map light may take the flashlight's shadow map while the flashlight is off (a held
// flame, lights.json "shadow_map"); the others are unshadowed.
// Light i at lp (u_lPos[i]) on p, added to sum; out of reach or facing away it is rejected before the square root and
// the fog.
void MapLight(int i, vec4 lp, vec3 p, vec3 N, int shadowed, vec3 held, inout vec3 sum) {
    vec3 d = lp.xyz - p;
    float d2 = dot(d, d);
    if (d2 >= lp.w * lp.w || dot(N, d) <= 0.0) return;
    float dist = sqrt(d2);
    vec3 l = d / max(dist, 1e-4);
    float nl = dot(N, l);
    if (dist >= lp.w || nl <= 0.0) return;
    float f = ModernFalloff(dist, lp.w);
    if (dot(u_lDir[i].xyz, u_lDir[i].xyz) > 0.5) f *= smoothstep(u_lDir[i].w, u_lColor[i].w, dot(-l, u_lDir[i].xyz));
    sum += u_lColor[i].rgb * (f * nl * (1.0 - FogAlong(dist))) * (i == shadowed ? held : vec3(1.0));
}

vec3 MapLights(vec3 p, vec3 N) {
    vec3 sum = vec3(0.0);
    int count = int(u_lInfo.x + 0.5);
    // Before the loop: the filter's derivatives need every pixel of the quad to take it.
    int shadowed = int(u_lShadow.x + 0.5) - 1;
    vec3 held = vec3(1.0);
    if (shadowed >= 0 && u_prog.w > 0.5) {
        vec3 d = u_lPos[shadowed].xyz - p;
        float dist = length(d);
        if (dist < u_lPos[shadowed].w) held = FlashlightShadow(p, d / max(dist, 1e-4), dist);
    }
    // Only the lights whose reach touches this pixel's tile and the draw's box; every other one would fail the range
    // test below.
    vec2 ndc = u_wProj.xy * (p.xy / p.z) + u_wProj.zw;
    ivec2 tc = clamp(ivec2(floor((ndc * 0.5 + 0.5) * vec2(16.0, 8.0))), ivec2(0), ivec2(15, 7));
    int tile = tc.y * 16 + tc.x;
    uint mask = u_lTiles[tile >> 2][tile & 3] & (uint(u_mat2.y) | (uint(u_mat2.z) << 16));
    // Four lights at a time: their positions are loaded together, so the loads' latency is paid once per group. The
    // sum keeps the order of the lights.
    for (int g = 0; g < count; g += 4) {
        uint gm = (mask >> uint(g)) & 15u;
        if (gm == 0u) continue;
        vec4 a = u_lPos[g], b = u_lPos[g + 1], c = u_lPos[g + 2], e = u_lPos[g + 3];
        if ((gm & 1u) != 0u) MapLight(g, a, p, N, shadowed, held, sum);
        if ((gm & 2u) != 0u) MapLight(g + 1, b, p, N, shadowed, held, sum);
        if ((gm & 4u) != 0u) MapLight(g + 2, c, p, N, shadowed, held, sum);
        if ((gm & 8u) != 0u) MapLight(g + 3, e, p, N, shadowed, held, sum);
    }
    if (u_lInfo.y > 0.5) {
        vec3 l = -normalize(u_sunDir.xyz);
        float nl = dot(N, l);
        if (nl > 0.0) {
            vec3 c = u_sunColor.rgb * (nl * (1.0 - FogAlong(length(p))));
            if (u_lInfo.z > 0.5) {
                float rot = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
                c *= SunShadow(p, N, nl, rot);
            }
            if (u_lInfo.w > 0.5) c *= FlakeShadowSun(p, l, u_sunColor.w);
            sum += c;
        }
    }
    return sum;
}

#ifdef SH_SCENE_QUAD
// SunShadow for a whole quad: the quad's taps (u_lShadow.y rounded up to a multiple of four, at least eight) are
// shared out as in FlashlightShadowQuad. Every pixel of the quad must call it.
float SunShadowQuad(vec3 p, vec3 N, float nl, float rot) {
    int lane = QuadLane();
    float slope = sqrt(max(1.0 - nl * nl, 0.0)) / max(nl, 0.2);
    vec3 q = p + N * (u_sunDir.w * (1.0 + slope));
    vec3 c = (u_sunMatrix * vec4(q, 1.0)).xyz * 0.5 + 0.5;
    bool inside = c.x > 0.0 && c.x < 1.0 && c.y > 0.0 && c.y < 1.0 && c.z < 1.0;
    int taps = max((int(u_lShadow.y + 0.5) + 3) / 4 * 4, 8);
    float lit = 0.0;
    if (inside) {
        float bias = 2.0 / max(u_sunMap.y, 1.0);
        float r = 1.5 / max(u_sunMap.x, 1.0);
        vec2 dir = QuadFirstDir(rot, lane);
        for (int i = lane; i < taps; i += 4) {
            vec2 uv = c.xy + sqrt((float(i) + 0.5) / float(taps)) * dir * r;
            lit += c.z - bias <= texture(s_sunShadow, uv).r ? 1.0 : 0.0;
            dir = GoldenTurn4(dir);
        }
    } else {
        lit = float(taps / 4);
    }
    return QuadSum(lit) / float(taps);
}

// MapLights for a whole quad: the lights that can reach any of its pixels are shared out over its four pixels, each
// evaluates its share at its own position, and the sum is pooled, so a quad pays about a quarter of the lights and its
// four pixels take one light (the map lights at half resolution, the albedo still per pixel). The sun's own term stays
// per pixel, its shadow map taps are shared. Every pixel of the quad must call it.
vec3 MapLightsQuad(vec3 p, vec3 N, bool want) {
    int lane = QuadLane();
    vec3 sum = vec3(0.0);
    int count = int(u_lInfo.x + 0.5);
    int shadowed = int(u_lShadow.x + 0.5) - 1;
    vec3 held = vec3(1.0);
    if (shadowed >= 0 && u_prog.w > 0.5) {
        vec3 d = u_lPos[shadowed].xyz - p;
        float dist = length(d);
        if (QuadAny(want && dist < u_lPos[shadowed].w)) held = FlashlightShadowQuad(p, d / max(dist, 1e-4), dist);
    }
    vec2 ndc = u_wProj.xy * (p.xy / p.z) + u_wProj.zw;
    ivec2 tc = clamp(ivec2(floor((ndc * 0.5 + 0.5) * vec2(16.0, 8.0))), ivec2(0), ivec2(15, 7));
    int tile = tc.y * 16 + tc.x;
    uint mask = want ? (u_lTiles[tile >> 2][tile & 3] & (uint(u_mat2.y) | (uint(u_mat2.z) << 16))) : 0u;
    int groups = (count + 3) / 4 * 4;
    if (groups < 32) mask &= (1u << uint(groups)) - 1u;
    // Light i goes to the pixel i % 4 of the quad: each pixel walks its own quarter of the mask.
    uint mine = QuadOr(mask) & (0x11111111u << uint(lane));
    while (mine != 0u) {
        int i = findLSB(mine);
        mine &= mine - 1u;
        MapLight(i, u_lPos[i], p, N, shadowed, held, sum);
    }
    sum = QuadSum(sum);
    if (u_lInfo.y > 0.5) {
        vec3 l = -normalize(u_sunDir.xyz);
        float nl = dot(N, l);
        float vis = 1.0;
        if (u_lInfo.z > 0.5 && QuadAny(want && nl > 0.0)) {
            float rot = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
            vis = SunShadowQuad(p, N, nl, rot);
        }
        if (nl > 0.0) {
            vec3 c = u_sunColor.rgb * (nl * (1.0 - FogAlong(length(p)))) * vis;
            if (u_lInfo.w > 0.5) c *= FlakeShadowSun(p, l, u_sunColor.w);
            sum += c;
        }
    }
    return sum;
}
#endif

vec4 TexelAt(ivec2 t, ivec2 size, int level) {
    return texelFetch(s_tex, clamp(t, ivec2(0), size - 1), level);
}

// The texture as the PS1 polygon shows it. Map cells carry their triangle's UV box: their UVs are PS1 texel coordinates
// + 0.5, the PS1 shows texels lo .. hi - 1 of the box (texel hi lies on the far edge and is never drawn), and nothing
// outside it may be read, or the next cell of the atlas shows as a light seam along the edge. Filtering weighs the
// four taps by alpha class like the PS1 path's coverage filter: transparent texels give no colour, the cut-out edge
// stays where nearest sampling puts it, and the STP class is the weighted majority, so the edge of a solid cut-out is
// never blended into an additive band (the light rims of blood pools and blinds).
vec4 SampleLevel(int level, bool linear) {
    ivec2 isz = textureSize(s_tex, level);
    vec2 size = vec2(isz);
    bool boxed = v_box.z >= v_box.x;
    vec2 q = v_uv * size;
    vec2 lo = vec2(0.0), hi = vec2(0.0);
    if (boxed) {
        q -= 0.5;
        lo = floor(v_box.xy * size);
        hi = max(lo, floor(v_box.zw * size) - 1.0);
    }
    if (!linear) {
        if (!boxed) {
            if (level == 0) return texture(s_tex, v_uv);
            ivec2 t = ivec2(floor(v_uv * size));
            return texelFetch(s_tex, ((t % isz) + isz) % isz, level);  // the sampler repeats
        }
        return TexelAt(ivec2(clamp(floor(q), lo, hi)), isz, level);
    }
    if (boxed) q = clamp(q, lo + 0.5, hi + 0.5);
    vec2 p = q - 0.5;
    vec2 f = fract(p);
    ivec2 b = ivec2(floor(p));
#ifdef SH_SCENE_QUAD
    // Phones: where none of the four texels is transparent the weighted mean is the plain bilinear one, which the
    // sampler gives in one fetch (its weights have 8 bits); the alpha classes come from one gather.
    if (level == 0 && all(greaterThanEqual(b, ivec2(0))) && all(lessThan(b + 1, isz))) {
        vec2 uv = (vec2(b) + 1.0) / size;
        vec4 ga = textureGather(s_tex, uv, 3);  // w: +0,+0, z: +1,+0, x: +0,+1, y: +1,+1
        if (all(greaterThan(ga, vec4(0.0)))) {
            vec4 w4 = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
            float stpw = dot(w4, vec4(lessThan(ga, vec4(0.75))));
            return vec4(textureLod(s_tex, (p + 0.5) / size, 0.0).rgb, stpw * 2.0 > 1.0 ? 0.5 : 1.0);
        }
    }
#endif
    vec3 rgb = vec3(0.0);
    float cov = 0.0, stpw = 0.0;
    for (int i = 0; i < 4; i++) {
        ivec2 o = ivec2(i & 1, i >> 1);
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
        vec4 c = TexelAt(b + o, isz, level);
        if (c.a > 0.0) {
            rgb += c.rgb * w;
            cov += w;
            if (c.a < 0.75) stpw += w;
        }
    }
    if (cov < 0.5) return vec4(0.0);
    return vec4(rgb / cov, (stpw * 2.0 > cov) ? 0.5 : 1.0);
}

// A texture with one level (every texture but an upscaled one, texture_upscale) is the level as it is. An upscaled
// one is minified at a distance: the level comes from the screen footprint of a texel of level 0, each level is
// filtered as above (its box scaled with it, so no neighbouring cell of the atlas is read), and two levels blend
// for the linear filters. Alpha keeps its classes: it is the nearer level's.
vec4 SampleTexture() {
    bool linear = u_modes.z > 0.5;
    int levels = int(u_mat2.x + 0.5);
    if (levels <= 1) return SampleLevel(0, linear);
    vec2 t0 = v_uv * vec2(textureSize(s_tex, 0));
    vec2 dx = dFdx(t0), dy = dFdy(t0);
    float lod = clamp(0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8)), 0.0, float(levels - 1));
    if (!linear) return SampleLevel(int(lod + 0.5), false);
    int l0 = int(floor(lod));
    float f = lod - float(l0);
    vec4 a = SampleLevel(l0, true);
    if (f < 1.0 / 64.0 || l0 + 1 >= levels) return a;
    vec4 b = SampleLevel(l0 + 1, true);
    vec3 rgb = a.a == 0.0 ? b.rgb : (b.a == 0.0 ? a.rgb : mix(a.rgb, b.rgb, f));
    return vec4(rgb, f < 0.5 ? a.a : b.a);
}

// The water's height 0..1 at p (radians of its widest wave) and time t (radians of the wave cycle): long crests
// along x that bend and drift, a finer slanted set crossing them, and a slow swell that evens some of them out.
// Every term takes a whole number of turns per cycle, so the cycle closes without a jump. A lower quality level
// keeps the first set alone, or the first two.
float WaterHeight(vec2 p, float t) {
    if (u_wLevel.x < 1.5) return 0.5 + 0.4 * sin(p.y * 2.0 + 1.4 * sin(p.x * 0.5 + t) + 2.0 * t);
    float a = sin(p.y * 2.0 + 1.4 * sin(p.x * 0.5 + t) + 0.8 * sin(p.x * 1.3 - 2.0 * t) + 2.0 * t);
    float b = sin(p.y * 3.3 - p.x * 0.7 + 1.2 * sin(p.x * 0.9 + p.y * 0.4 + t) - 3.0 * t);
    if (u_wLevel.x < 2.5) return 0.5 + 0.3 * a + 0.15 * b;
    float c = sin(p.x * 1.1 + p.y * 0.6 + 2.0 * sin(p.y * 0.8 - t));
    return 0.5 + 0.25 * a + 0.15 * b + 0.1 * a * c;
}

// The mirrored picture around uv: one tap, three around it or a tent of five over the blur radius.
vec3 Mirrored(vec2 uv) {
    if (u_wLevel.w < 1.5) return texture(s_reflect, uv).rgb;
    if (u_wLevel.w < 3.5) {
        vec3 s = texture(s_reflect, uv - vec2(0.0, u_wWave.w)).rgb;
        s += texture(s_reflect, uv + vec2(0.87 * u_wWave.z, 0.5 * u_wWave.w)).rgb;
        s += texture(s_reflect, uv + vec2(-0.87 * u_wWave.z, 0.5 * u_wWave.w)).rgb;
        return s * (1.0 / 3.0);
    }
    vec3 c = texture(s_reflect, uv).rgb * 0.36;
    c += texture(s_reflect, uv + vec2(u_wWave.z, 0.0)).rgb * 0.16;
    c += texture(s_reflect, uv - vec2(u_wWave.z, 0.0)).rgb * 0.16;
    c += texture(s_reflect, uv + vec2(0.0, u_wWave.w)).rgb * 0.16;
    c += texture(s_reflect, uv - vec2(0.0, u_wWave.w)).rgb * 0.16;
    return c;
}

void main() {
#ifdef SH_SCENE_EXACT_DEPTH
    // The surface's own depth at this point (scene.vert v_depth): the rasterised one is interpolated along the screen
    // and lies behind a long triangle, so a corner or a ledge in front of a prop would lose to it. An overlay, lifted
    // by its layer on the same plane as its ground, wins against it at every pixel.
    float depth = v_depth;
    if (v_setback.x > 0.0) {
        // A map surface is written as if its plane lay v_setback.x deeper along its own normal: along this pixel's
        // ray that is z * setback / h farther, h the eye's distance to the plane. A view-depth margin of the same
        // size shrinks to nothing along a slanted ray, and a prop authored a hair inside its wall lost to the wall
        // there. Bounded, so a face seen edge-on is not thrown behind the room.
        vec3 n = cross(dFdx(v_viewpos), dFdy(v_viewpos));
        float len = length(n);
        float h = len > 0.0 ? abs(dot(n, v_viewpos)) / len : 0.0;
        float z = max(v_viewpos.z, 0.0);
        float slope = h > z * 0.125 ? z / h : 8.0;
        depth += v_setback.x * slope * v_setback.y;
    }
    gl_FragDepth = clamp(depth, 0.0, 1.0);
#endif
    int blend = int(u_mat.x + 0.5);
    bool water = u_mat.y > 1.5;
    vec4 tex = (u_mat.y > 0.5 && !water) ? SampleTexture() : vec4(1.0);
#ifndef SH_SCENE_QUAD
    if (tex.a < 0.25) discard;
#endif
    // A semi-transparent material is split across the opaque and the blended pass by its texels' STP class.
    int matW = int(u_mat.w + 0.5);
    bool isolated = matW >= 4;
    int texels = isolated ? matW - 4 : matW;
#ifdef SH_SCENE_QUAD
    // The quad's pixels share their light below, so a cut-out pixel stays until the end unless its whole quad goes.
    bool killed = tex.a < 0.25 || (texels == 1 && tex.a < 0.75) || (texels == 2 && tex.a >= 0.75);
    if (QuadSum(killed ? 1.0 : 0.0) > 3.5) discard;
    if (u_mat.z > 0.5 && killed) discard;
#else
    if ((texels == 1 && tex.a < 0.75) || (texels == 2 && tex.a >= 0.75)) discard;
#endif
    bool stp = tex.a < 0.75 || texels == 3;
    if (u_mat.z > 1.5) {
        // Transmittance pass of the shadow map: what this surface lets through, by its PS1 blend, and its distance
        // along the light (clip w). A halved texel passes half and a subtracted one what it does not take. An added
        // texel is glass or haze that only brightens what is behind it: the brighter it is, the denser the pane, so
        // it dims the beam a little instead of leaving no trace on the wall behind.
        vec3 through = vec3(0.0);
        float dense = max(tex.r, max(tex.g, tex.b));
        if (blend == 2) through = vec3(1.0 - tex.a);
        else if (stp && blend == 3) through = vec3(0.5);
        else if (stp && blend == 4) through = clamp(vec3(1.0) - tex.rgb, 0.0, 1.0);
        else if (stp) through = vec3(1.0 - ((blend == 5) ? 0.12 : 0.45) * dense);
        fragColor = vec4(through, clamp((1.0 / gl_FragCoord.w) / u_shadowQ.w, 0.0, 1.0));
        return;
    }
    if (u_mat.z > 0.5) {
        // Depth pass of the shadow map.
        fragColor = vec4(0.0);
        return;
    }
    // A semi-transparent surface blends only its STP texels; added and subtracted ones fade to black in fog like
    // the PS1's additive prims (they then change nothing), the clear ones are solid and fog as usual.
    bool toBlack = (blend == 1 || blend == 4 || blend == 5) && stp;

    vec3 albedo = tex.rgb;
    // What the water shows by itself: its unlit colour, the mirrored meshes and the lamp's mirrored glow.
    vec3 waterOwn = vec3(0.0);
    if (water) {
        vec2 wp = (u_wWorld * vec4(v_viewpos, 1.0)).xz * u_wWave.x;
        float t = 6.2831853 * u_wBase.a;
        const float e = 0.12;
        // Waves narrower than a few pixels fade to their mean instead of shimmering.
        float fine = clamp(1.6 - 2.0 * max(fwidth(wp.x), fwidth(wp.y)), 0.0, 1.0);
        // A coarse level evaluates the waves at the middles of a grid's squares, like texels of a small tile.
        if (u_wLevel.y > 0.0) wp = (floor(wp / u_wLevel.y) + 0.5) * u_wLevel.y;
        float h = WaterHeight(wp, t);
        // The slope only shifts the reflection and frays the glow: a level without distortion skips its samples.
        vec2 slope = vec2(0.0);
        if (u_wWave.y > 0.0)
            slope = (vec2(WaterHeight(wp + vec2(e, 0.0), t), WaterHeight(wp + vec2(0.0, e), t)) - h) * (fine / e);
        h = mix(0.5, h, fine);
        if (u_wLevel.z > 0.5) h = floor(h * u_wLevel.z + 0.5) / u_wLevel.z;
        // The light falls on the wave pattern: the troughs lose a part of the lit colour.
        albedo = u_wLit.rgb * (1.0 - u_wLit.a * (1.0 - h));
        vec2 ndc = u_wProj.xy * (v_viewpos.xy / max(v_viewpos.z, 1.0)) + u_wProj.zw;
        vec2 shift = slope * u_wWave.y * vec2(u_wGlow.a, 1.0);
        // The ray-traced reflection replaces the mirrored picture of the posed meshes only where it traced mirror
        // rays; the map lights' highlights alone (rt_lights without rt_reflections) are added on top of it.
        bool rtMirror = u_rt.y > 1.5 && RtHere(RtAt(s_rtReflect));
        if (u_wTint.a > 0.5 && !rtMirror) waterOwn += Mirrored(ndc * 0.5 + 0.5 + shift) * u_wTint.rgb;
        if (u_wLamp.w > 0.0 && u_wLamp.z > 1.0) {
            // The lamp's glow where its mirror image lies: a soft-edged blot the waves fray, of the glow's colour
            // at the rim and paler in the middle.
            vec2 at = u_wProj.xy * (u_wLamp.xy / u_wLamp.z) + u_wProj.zw;
            vec2 d = (ndc + 3.5 * shift - at) / max(u_wGlowSize.xy, vec2(1e-5));
            float g = 1.0 - smoothstep(0.25, 1.0, length(d));
            if (u_wLevel.z > 0.5) g = floor(g * u_wLevel.z + 0.5) / u_wLevel.z;
            g *= u_wLamp.w;
            waterOwn += u_wGlow.rgb * g * mix(vec3(1.0), vec3(1.05, 1.8, 0.6), g * g);
        }
        waterOwn += u_wBase.rgb;
    }
    vec3 rgb = min(albedo * v_color.rgb, vec3(1.0));

    // SH_SCENE_QUAD takes the block in every pixel of the quad (its shadow is shared by them) and adds the light only
    // where the other variants enter it.
#ifdef SH_SCENE_QUAD
    bool beamLit = !toBlack && v_viewpos.z > 0.0;
    if (u_flMode.x > 0.5 && !isolated) {
        vec3 p = v_viewpos;
        bool classic = u_flMode.y > 0.5;
        {
            if (beamLit) rgb *= classic ? 0.72 : 0.15;
#else
    if (u_flMode.x > 0.5 && !toBlack && !isolated) {
        vec3 p = v_viewpos;
        bool classic = u_flMode.y > 0.5;
        if (p.z > 0.0) {
            rgb *= classic ? 0.72 : 0.15;
#endif
            vec3 dirRaw = u_flDir.xyz;
            vec3 dir = (dot(dirRaw, dirRaw) > 1e-12) ? normalize(dirRaw) : vec3(0.0, 0.0, 1.0);
            vec3 origin = classic ? (u_flPos.xyz - dir * 39.0) : u_flPos.xyz;
            vec3 L = origin - p;
            float d = length(L);
            L /= max(d, 0.0001);
            float cone = smoothstep(u_flDir.w, u_flPos.w, dot(-L, dir));
            float ndl = 1.0;
            float atten;
            if (classic) {
                cone = cone * (2.0 - cone);
                float ad = d * 2.0;
                float inv = 1.0 / max(ad, 1.0);
                atten = max(0.0, 134217728.0 * inv * inv - 16.0) + min(48.0, 32768.0 * inv);
                atten = clamp(atten / 255.0, 0.0, 1.0);
                atten *= 1.0 - smoothstep(u_flMode.w * 0.9, u_flMode.w, ad);
            } else {
                cone = ModernCone(dot(-L, dir), u_flDir.w, u_flPos.w);
                vec3 fn = cross(dFdx(p), dFdy(p));
                float nl = length(fn);
                vec3 N = (nl > 1e-9) ? fn / nl : vec3(0.0, 0.0, -1.0);
                if (dot(N, p) > 0.0) N = -N;
                ndl = ModernTurn(dot(N, L));
                atten = ModernFalloff(d, u_flMode.w);
            }
            vec3 shadow = vec3(1.0);
            // The ray-traced shadow where it was traced for this surface; the shadow map elsewhere.
            vec4 rts = u_rt.x > 0.5 ? RtAt(s_rtShadow) : vec4(0.0);
#ifdef SH_SCENE_QUAD
            bool rtHere = RtHere(rts);
            if (u_prog.w > 0.5 && QuadAny(beamLit && !rtHere && cone * atten * ndl > 0.001)) {
                vec3 s = FlashlightShadowQuad(p, L, d);
                if (!rtHere) shadow = s;
            }
            if (rtHere) shadow = rts.rgb;
#else
            if (RtHere(rts))
                shadow = rts.rgb;
            // Most of the frame lies outside the beam: the filter is only paid where its result can show (the
            // light left is under half an 8-bit step; the derivatives of the pixels on that border do not matter).
            else if (u_prog.w > 0.5 && cone * atten * ndl > 0.001)
                shadow = FlashlightShadow(p, L, d);
#endif
            if (u_lInfo.w > 0.5 && u_sunMap.w > 0.5 && cone * atten * ndl > 0.001)
                shadow *= FlakeShadow(p, L, d, kLightRadius / max(d, 1.0));
#ifdef SH_SCENE_QUAD
            if (beamLit)
                rgb += albedo * u_flColor.rgb * (cone * atten * ndl) * shadow * (1.0 - clamp(v_color.a, 0.0, 1.0));
            if (beamLit && u_flMode.z > 0.5) {
#else
            rgb += albedo * u_flColor.rgb * (cone * atten * ndl) * shadow * (1.0 - clamp(v_color.a, 0.0, 1.0));
            if (u_flMode.z > 0.5) {
#endif
                float pk = max(rgb.r, max(rgb.g, rgb.b));
                if (pk > 0.85) rgb *= (0.85 + 0.15 * (1.0 - exp((0.85 - pk) / 0.15))) / pk;
            }
        }
    }

    // The map's lights and the sun without ray tracing, times the albedo; only ever added, like the ray-traced light.
#ifdef SH_SCENE_QUAD
    if ((u_lInfo.x > 0.5 || u_lInfo.y > 0.5) && !isolated && QuadAny(beamLit)) {
        vec3 fn = cross(dFdx(v_viewpos), dFdy(v_viewpos));
        float nl = length(fn);
        vec3 N = (nl > 1e-9) ? fn / nl : vec3(0.0, 0.0, -1.0);
        if (dot(N, v_viewpos) > 0.0) N = -N;
        vec3 ml = MapLightsQuad(v_viewpos, N, beamLit);
        if (beamLit) rgb += albedo * ml * (1.0 - clamp(v_color.a, 0.0, 1.0));
    }
#else
    if ((u_lInfo.x > 0.5 || u_lInfo.y > 0.5) && !isolated && !toBlack && v_viewpos.z > 0.0) {
        vec3 fn = cross(dFdx(v_viewpos), dFdy(v_viewpos));
        float nl = length(fn);
        vec3 N = (nl > 1e-9) ? fn / nl : vec3(0.0, 0.0, -1.0);
        if (dot(N, v_viewpos) > 0.0) N = -N;
        rgb += albedo * MapLights(v_viewpos, N) * (1.0 - clamp(v_color.a, 0.0, 1.0));
    }
#endif

    // The beam's bounce off the surfaces this one faces and the map lights' light (ray traced), times its albedo.
    // It is only ever added, so where no lit surface and no light is in sight the darkness is the original's.
    if (u_rt.z > 0.5 && !isolated && !toBlack) {
        vec4 rg = RtAt(s_rtGi);
        if (RtHere(rg)) rgb += albedo * rg.rgb * (1.0 - clamp(v_color.a, 0.0, 1.0));
    }

    // The ray-traced reflection, already weighed by the surface's material and Fresnel term and fogged along its
    // own path; blended texels and isolated models take none.
    if (u_rt.y > 0.5 && !isolated && (blend == 0 || water)) {
        vec4 rr = RtAt(s_rtReflect);
        if (RtHere(rr)) rgb += rr.rgb;
    }

    rgb += waterOwn;

    bool softFog = u_modes.y > 0.5;
    float fogSrc = v_fog;
    if (softFog && u_fogRampP.x > 0.5 && v_viewpos.z > 0.0) fogSrc = FogAmountAtZ(v_viewpos.z);
    float fogLin = fogSrc * u_fog.a;
    float fogAmt = clamp(fogLin, 0.0, 1.0);
    if (softFog && fogLin > 0.85) {
        float kt = clamp((fogLin - 0.85) / 0.15, 0.0, 1.0);
        fogAmt = 0.85 + 0.15 * (kt - kt * kt + kt * kt * kt);
    }
    if (softFog && u_fogRampP.x > 0.5 && u_fogRampP.w > 0.0 && v_viewpos.z > 0.0)
        fogAmt = max(fogAmt, smoothstep(u_fogRampP.w * 0.85, u_fogRampP.w, v_viewpos.z));
    if (isolated)
        fogAmt = 0.0;
    if (toBlack)
        rgb *= (1.0 - fogAmt);
    else
        rgb = mix(rgb, u_fog.rgb, fogAmt);

    if (u_dither.z > 0.5) {
        float ign = fract(52.9829189 * fract(dot(fragCoordGL(), vec2(0.06711056, 0.00583715))));
        rgb += vec3((ign - 0.5) * (0.98 / 255.0));
    }

    // Additive pipeline: dst * alpha + src, so an STP texel adds and a clear one is drawn solid. PS1 average:
    // src * alpha + dst * (1 - alpha). Add a quarter: src * alpha + dst (its pass holds STP texels only).
    float a = 1.0;
    if (blend == 1) a = stp ? 1.0 : 0.0;
    else if (blend == 2) a = tex.a;
    else if (blend == 3) a = stp ? 0.5 : 1.0;
    else if (blend == 5) a = 0.25;
#ifdef SH_SCENE_QUAD
    if (killed) discard;
#endif
    fragColor = vec4(rgb, a);
}
