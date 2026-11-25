#version 450
// Primitive batch fragment shader: the all-format video-memory program and the 32-bit RGBA override program.
// Build modes as in prim_batch.vert. u_prog.x picks the program; it is uniform, so the derivatives in the lit tail
// stay defined.
// Covers 4/8/16-bit CLUT sampling from the RG8 VRAM texture with the tpage format per vertex, untextured
// prims, nearest / coverage-weighted bilinear / anisotropic filtering with UV-bound clamping, the additive
// edge coverage fade, RGBA overrides (hi-res replacements with the per-texel footprint clamp, and the
// framebuffer-feedback reader), the per-pixel flashlight (Classic and Modern styles) with its 3x3 PCF shadow,
// depth fog and soft_fog P2-P4, fog-to-black for additive/subtractive prims, the hardware's 4x4 ordered dither with
// 5-bit quantize, deband, the STP-to-alpha fold for additive blending, and the decal program of surface marks
// (v_geom3d = 2: the override texel is a normal, a darkening and a coverage; render/decals).
// Left out (RENDER.md section 2): per-pixel lighting of the stock geometry (v_color is what it returns while
// that lighting is off; the renderer draws lit splits on another path) and u_voidProbe (a debug view).

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 3, binding = slot)
#define SH_SAMPLER(slot) layout(set = 2, binding = slot)
#define SH_VARYING(n) layout(location = n)
#else
#define SH_UBO(slot) layout(std140)
#define SH_SAMPLER(slot)
#define SH_VARYING(n)
#endif

SH_VARYING(0) centroid in vec4 v_texcoord;
SH_VARYING(1) in vec4 v_color;
SH_VARYING(2) centroid in vec4 v_page_clut;
SH_VARYING(3) in float v_fogAmount;
SH_VARYING(4) in float v_is3d;
SH_VARYING(5) in vec3 v_viewpos;
SH_VARYING(6) in float v_fade;
SH_VARYING(7) in float v_geom3d;
SH_VARYING(8) centroid in vec4 v_uvlim;
SH_VARYING(9) in float v_texfmt;
SH_VARYING(10) in float v_fogP3;
SH_VARYING(11) in vec4 v_shadowViewPos;

layout(location = 0) out vec4 fragColor;

SH_SAMPLER(0) uniform sampler2D s_vram;    // 1024x512 RG8: low byte, high byte of each 16-bit video-memory word
SH_SAMPLER(1) uniform sampler2D s_rgba;    // u_prog.x == 1: the override texture (its own filtering)
SH_SAMPLER(2) uniform sampler2D s_shadow;  // flashlight shadow depth map
// The fog ramp's bytes (PrimRenderer::fogRamp): a texel fetch, since indexing u_fogRamp per pixel is a slow path
// on some GPUs. WorldFog keeps the ramp for the vertex stage, and GL links the block of both stages as one.
SH_SAMPLER(3) uniform sampler2D s_fogRamp;

SH_UBO(0) uniform FsFrame {
    vec4 u_fog;      // rgb: fog colour, a: fog strength
    vec4 u_modes;    // x: fog to black (additive/subtractive split), y: soft fog, z: bilinear gate 0/1/2, w: aniso taps
    vec4 u_dither;   // x: hardware dither force, y: dither cell, z: deband, w: target height (top-left gl_FragCoord flip)
    vec4 u_flMode;   // x: flashlight on, y: style (1 Classic, 0 Modern), z: soft highlights, w: range
    vec4 u_flPos;    // xyz: light position (view space), w: inner cone cosine
    vec4 u_flDir;    // xyz: light direction, w: outer cone cosine
    vec4 u_flColor;  // rgb: light colour
    vec4 u_prog;     // x: program (0 VRAM CLUT, 1 RGBA override), y: feedback mode 0/1/2, z: feedback STP, w: shadows (1 + shadow_filter)
    vec4 u_rgbaTex;  // xy: texelSize (1 / native size), zw: texOffset (native texels)
    vec4 u_rgbaHalf; // xy: half a hi-res texel in native texels, zw: feedback origin (pixels)
    vec4 u_rgbaFb;   // xy: 1 / feedback capture size, z: sharp sampling, 1 for 2D prims, 2 for all (glyphs), w: foliage_edge mode
    mat4 u_shadowMatrix;
    vec4 u_shadowP;  // x: bias, y: normal offset, z: strength, w: fade distance
    vec4 u_shadowQ;  // xy: shadow map texel, zw: light frustum near / far
};

SH_UBO(1) uniform WorldFog {
    vec4 u_fogRampP;
    vec4 u_fogRamp[32];
};

const vec2 c_VRAMTexel = vec2(1.0 / 1024.0, 1.0 / 512.0);

int u_untextured;
int u_fogToBlack;

// Snap each byte at the source so all later index math is exact on every driver.
vec2 VRAM(vec2 uv) { return floor(texture(s_vram, uv).rg * 255.0 + 0.5) * (1.0 / 255.0); }

// The 15-bit colour decode table computed in place: channel v of 5 bits is (v << 3) / 256 in it, which is
// what this returns, and alpha is 0 for the transparent word 0, 0.5 with STP set, 1 otherwise.
vec4 lut(vec2 rg) {
    ivec2 b = ivec2(floor(rg * 255.0 + 0.5));
    int c = b.x | (b.y << 8);
    vec3 col = vec3(float(c & 31), float((c >> 5) & 31), float((c >> 10) & 31)) * (8.0 / 256.0);
    return vec4(col, (rg.x + rg.y == 0.0) ? 0.0 : (((c >> 15) & 1) != 0 ? 0.5 : 1.0));
}

vec2 sampleVram4(vec2 tc) {
    vec2 uv = (tc * vec2(0.25, 1.0) + v_page_clut.xy) * c_VRAMTexel;
    vec2 comp = VRAM(uv);
    int index = int(fract(tc.x / 4.0 + 0.0001) * 4.0);
    float v = comp[index / 2] * (255.0 / 16.0);
    float f = floor(v + 0.001);
    vec2 c = vec2((v - f) * 16.0, f);
    vec2 clut_pos = v_page_clut.zw;
    clut_pos.x += mix(c[0], c[1], mod(float(index), 2.0)) * c_VRAMTexel.x;
    return VRAM(clut_pos);
}

vec2 sampleVram8(vec2 tc) {
    vec2 uv = (tc * vec2(0.5, 1.0) + v_page_clut.xy) * c_VRAMTexel;
    vec2 comp = VRAM(uv);
    vec2 clut_pos = v_page_clut.zw;
    int index = int(mod(tc.x, 2.0));
    clut_pos.x += comp[index] * 255.0 * c_VRAMTexel.x;
    return VRAM(clut_pos);
}

vec2 sampleVram16(vec2 tc) {
    vec2 uv = (tc + v_page_clut.xy) * c_VRAMTexel;
    return VRAM(uv);
}

vec2 sampleVram(vec2 tc) {
    if (v_texfmt < 0.5) return sampleVram4(tc);
    if (v_texfmt < 1.5) return sampleVram8(tc);
    return sampleVram16(tc);
}

// Coverage of the last filtered sample (opaque tap weight / total tap weight).
float g_tapCov;
float g_tapWt;

vec4 TapWeighted(vec2 idx, float w, inout float cov) {
    if (v_uvlim.z > 0.0 || v_uvlim.w > 0.0) idx = clamp(idx, v_uvlim.xy, v_uvlim.zw);
    vec2 rg = sampleVram(idx);
    float o = (rg.x + rg.y > 0.0) ? w : 0.0;
    cov += o;
    g_tapWt += w;
    return lut(rg) * o;
}

vec4 BilinearCov(vec2 P, inout float cov) {
    vec2 tapP = P - 0.5;
    vec2 f = fract(tapP);
    vec2 p = floor(tapP);
    vec4 c = TapWeighted(p, (1.0 - f.x) * (1.0 - f.y), cov);
    c += TapWeighted(p + vec2(1.0, 0.0), f.x * (1.0 - f.y), cov);
    c += TapWeighted(p + vec2(0.0, 1.0), (1.0 - f.x) * f.y, cov);
    c += TapWeighted(p + vec2(1.0, 1.0), f.x * f.y, cov);
    return c;
}

vec4 BilinearTextureSample(vec2 P) {
    float cov = 0.0;
    g_tapWt = 0.0;
    vec4 c = BilinearCov(P, cov);
    if (cov <= 0.0) discard;
    g_tapCov = cov;
    return c / cov;
}

vec4 AnisoTextureSample(vec2 P) {
    vec2 dPdx = dFdx(P);
    vec2 dPdy = dFdy(P);
    float lx = length(dPdx);
    float ly = length(dPdy);
    vec2 major = (lx >= ly) ? dPdx : dPdy;
    float mn = min(lx, ly);
    float ratio = (mn > 0.0001) ? (max(lx, ly) / mn) : 1.0;
    float ext = length(major);
    if (ext > 4.0) {
        major *= 4.0 / ext;
        ext = 4.0;
    }
    float taps = clamp(min(floor(ratio), ceil(ext)), 1.0, u_modes.w);
    if (taps <= 1.0) return BilinearTextureSample(P);
    vec4 acc = vec4(0.0);
    float cov = 0.0;
    g_tapWt = 0.0;
    for (int i = 0; i < 16; i++) {
        if (float(i) >= taps) break;
        float t = (float(i) + 0.5) / taps - 0.5;
        acc += BilinearCov(P + major * t, cov);
    }
    if (cov <= 0.0) discard;
    g_tapCov = cov;
    return acc / cov;
}

vec4 NearestTextureSample(vec2 P) {
    vec2 rg = sampleVram(P);
    if (rg.x + rg.y == 0.0) discard;
    return lut(rg);
}

// Point sampling as an area filter: each texel keeps its own colour across the pixels it covers and only the pixel a
// texel edge runs through mixes the two, by how much of it each side covers. Plain point sampling at a fractional
// scale (2D art at 4.5 pixels per texel on a 1080p screen) draws texels 4 and 5 pixels wide in turn, which reads as
// stripes through letters and edges that look cut. At a whole-number scale no pixel straddles an edge and this is
// point sampling. Taps keep the polygon's UV box and the coverage rule of the filter above; the cut-out edge stays
// where point sampling puts it.
vec4 SharpTextureSample(vec2 P) {
    vec2 box = clamp(fwidth(P), vec2(1e-4), vec2(1.0));
    vec2 tx = P - 0.5 * box;
    vec2 off = clamp((fract(tx) - (vec2(1.0) - box)) / box, 0.0, 1.0);
    vec2 b = floor(tx);
    float cov = 0.0;
    g_tapWt = 0.0;
    vec4 c = TapWeighted(b, (1.0 - off.x) * (1.0 - off.y), cov);
    c += TapWeighted(b + vec2(1.0, 0.0), off.x * (1.0 - off.y), cov);
    c += TapWeighted(b + vec2(0.0, 1.0), (1.0 - off.x) * off.y, cov);
    c += TapWeighted(b + vec2(1.0, 1.0), off.x * off.y, cov);
    if (cov < 0.5) discard;
    g_tapCov = cov;
    return c / cov;
}

// The same for an RGBA override, in its own texels, kept inside the polygon's UV box (native texels, u_rgbaTex.zw
// moves them into the override).
vec4 SharpOverrideSample(vec2 uvn) {
    ivec2 isz = textureSize(s_rgba, 0);
    vec2 hs = vec2(isz);
    vec2 scale = hs * u_rgbaTex.xy;
    vec2 P = uvn * scale;
    vec2 lo = vec2(0.0);
    vec2 hi = hs - 1.0;
    if (v_uvlim.z > 0.0 || v_uvlim.w > 0.0) {
        lo = max(lo, floor((v_uvlim.xy + u_rgbaTex.zw) * scale));
        hi = min(hi, max(lo, floor((v_uvlim.zw + 1.0 + u_rgbaTex.zw) * scale) - 1.0));
    }
    vec2 box = clamp(fwidth(P), vec2(1e-4), vec2(1.0));
    vec2 tx = P - 0.5 * box;
    vec2 off = clamp((fract(tx) - (vec2(1.0) - box)) / box, 0.0, 1.0);
    vec2 b = floor(tx);
    vec4 acc = vec4(0.0);
    float cov = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 o = vec2(float(i & 1), float(i >> 1));
        float w = mix(1.0 - off.x, off.x, o.x) * mix(1.0 - off.y, off.y, o.y);
        vec4 t = texelFetch(s_rgba, ivec2(clamp(b + o, lo, hi)), 0);
        if (t.a >= 0.5) {
            acc += t * w;
            cov += w;
        }
    }
    if (cov < 0.5) discard;
    return acc / cov;
}

// A surface mark's texel (render/decals/bullet_hole.h), filtered in its own texels whatever the texture filter is:
// rg the normal's tilt along u and v, b the share of the surface's light the spot keeps, a the coverage.
vec4 DecalSample(vec2 uvn) {
    vec2 hs = vec2(textureSize(s_rgba, 0));
    vec2 P = uvn * u_rgbaTex.xy * hs - 0.5;
    vec2 b = floor(P);
    vec2 f = P - b;
    ivec2 hi = ivec2(hs) - 1;
    ivec2 i0 = clamp(ivec2(b), ivec2(0), hi);
    ivec2 i1 = clamp(ivec2(b) + 1, ivec2(0), hi);
    vec4 t00 = texelFetch(s_rgba, i0, 0);
    vec4 t10 = texelFetch(s_rgba, ivec2(i1.x, i0.y), 0);
    vec4 t01 = texelFetch(s_rgba, ivec2(i0.x, i1.y), 0);
    vec4 t11 = texelFetch(s_rgba, i1, 0);
    return mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y);
}

// Texel i of the fog ramp holds entry i and the next one (255 past the end): one fetch for both ends.
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

// gl_FragCoord in GL's bottom-left convention, which the dither phase, the deband noise and the feedback
// reader were written in.
vec2 fragCoordGL() {
#ifdef SH_RHI_VULKAN
    return vec2(gl_FragCoord.x, u_dither.w - gl_FragCoord.y);
#else
    return gl_FragCoord.xy;
#endif
}

void main() {
    g_tapCov = 1.0;
    g_tapWt = 1.0;
    u_fogToBlack = (u_modes.x > 0.5) ? 1 : 0;
    int bilinearFilter = int(u_modes.z + 0.5);
    bool rgba = u_prog.x > 0.5;
    bool decal = rgba && v_geom3d > 1.5;
    // A surface mark is painted on what is already in the target: it writes black with the alpha of what it
    // takes away, so the blend leaves surface * keep. The surface's own light (vertex light, flashlight, shadow,
    // darkness, fog) is all in the target already, and a mark can only darken it.
    vec2 decalTilt = vec2(0.0);
    float decalKeep = 1.0;
    float decalCover = 0.0;
    float decalRelief = 1.0;
    float decalLitShare = 0.0;

    if (rgba) {
        u_untextured = 0;
        // Framebuffer feedback: the GPU's arithmetic for a textured, non-dithered
        // rectangle on a 1:1 capture; the lit tail is skipped, as for every RGBA override.
        int fbMode = int(u_prog.y + 0.5);
        bool fbSmooth = fbMode >= 4;
        if (fbSmooth) fbMode -= 4;
        if (fbMode > 0) {
            vec2 ftc = (fbMode == 1) ? (floor(fragCoordGL()) - u_rgbaHalf.zw + 0.5) * u_rgbaFb.xy
                                     : (v_texcoord.xy + u_rgbaTex.zw) * u_rgbaTex.xy;
            if (fbSmooth) {
                // The smooth afterimage: the stored frame times the sprite's fraction (colour / 128) at 8-bit
                // precision, rounded down so dark pixels reach black instead of lingering at the last step.
                vec3 fbK = floor(v_color.rgb * 127.5 + 0.5) * (1.0 / 128.0);
                vec3 fbC = floor(texture(s_rgba, ftc).rgb * 255.0 * fbK) * (1.0 / 255.0);
                if (fbC.r + fbC.g + fbC.b <= 0.0) discard;
                fragColor = vec4(fbC, ((u_prog.z > 0.5) ? 0.5 : 1.0) * v_color.a);
                return;
            }
            vec3 fbL = clamp(floor(texture(s_rgba, ftc).rgb * 31.875 + 0.5), 0.0, 31.0);
            if (fbL.r + fbL.g + fbL.b < 0.5) discard;
            vec3 fbM = floor(v_color.rgb * 127.5 + 0.5);
            fbL = floor(fbL * fbM * (1.0 / 128.0) + 0.0001);
            float fbA = ((u_prog.z > 0.5) ? 0.5 : 1.0) * v_color.a;
            if (u_fogToBlack > 0) fbA = (fbA < 0.75) ? 1.0 : 0.0;
            fragColor = vec4(fbL * (8.0 / 255.0), fbA);
            return;
        }
        vec2 uvn = v_texcoord.xy + u_rgbaTex.zw;
        vec2 cell = floor(uvn);
        vec2 tc = (cell + clamp(uvn - cell, u_rgbaHalf.xy, vec2(1.0) - u_rgbaHalf.xy)) * u_rgbaTex.xy;
        if (decal) {
            vec4 mark = DecalSample(uvn);
            if (mark.a < 0.01) discard;
            decalTilt = mark.rg * 2.0 - 1.0;
            decalKeep = min(mark.b * 2.0, 1.0);
            decalCover = mark.a * v_color.a;
            fragColor = vec4(vec3(mark.b), mark.a);
        } else if ((u_rgbaFb.z > 1.5 || (u_rgbaFb.z > 0.5 && v_geom3d < 0.5)) && bilinearFilter < 2) {
            fragColor = SharpOverrideSample(uvn);
        } else if (bilinearFilter == 0 || (bilinearFilter == 1 && v_geom3d < 0.5)) {
            vec2 hiresSize = vec2(textureSize(s_rgba, 0));
            fragColor = textureLod(s_rgba, (floor(tc * hiresSize) + 0.5) / hiresSize, 0.0);
        } else {
            fragColor = texture(s_rgba, tc);
        }
        // A mark's coverage is soft: no cut-out, and the lit tail brings the vertex alpha in.
        if (!decal) {
            if (fragColor.a < 0.5) discard;
            fragColor.a *= v_color.a;
        }
    } else {
        u_untextured = (v_texfmt > 3.5) ? 1 : 0;
        if (u_untextured > 0) {
            fragColor = lut(vec2(1.0));
        } else {
            if ((bilinearFilter == 1 && v_geom3d > 0.5) || bilinearFilter >= 2)
                fragColor = (u_modes.w > 1.0) ? AnisoTextureSample(v_texcoord.xy) : BilinearTextureSample(v_texcoord.xy);
            else if (u_rgbaFb.z > 1.5 || (u_rgbaFb.z > 0.5 && v_geom3d < 0.5))
                fragColor = SharpTextureSample(v_texcoord.xy);
            else
                fragColor = NearestTextureSample(v_texcoord.xy);
        }
        // Untextured prims draw at the vertex colour exactly, alpha keeps the STP meaning.
        if (u_untextured > 0) fragColor.rgb = vec3(1.0);
        if (u_fogToBlack > 0 && g_tapWt > 0.0) fragColor.rgb *= clamp(g_tapCov / g_tapWt, 0.0, 1.0);
    }

    // Lit tail: vertex colour, flashlight, fog.
    vec3 flAlbedo = fragColor.rgb;
    fragColor *= v_color;
    if (u_untextured > 0) flAlbedo = fragColor.rgb;
    if (u_flMode.x > 0.5 && u_fogToBlack == 0) {
        vec3 flP = v_viewpos;
        bool classic = u_flMode.y > 0.5;
        if (flP.z > 0.0) {
            fragColor.rgb *= classic ? 0.72 : 0.15;
            vec3 flDirRaw = u_flDir.xyz;
            vec3 flDir = (dot(flDirRaw, flDirRaw) > 1e-12) ? normalize(flDirRaw) : vec3(0.0, 0.0, 1.0);
            vec3 flOrigin = classic ? (u_flPos.xyz - flDir * 39.0) : u_flPos.xyz;
            vec3 L = flOrigin - flP;
            float d = length(L);
            L /= max(d, 0.0001);
            float cone = smoothstep(u_flDir.w, u_flPos.w, dot(-L, flDir));
            float ndl = 1.0;
            float atten;
            if (classic) {
                cone = cone * (2.0 - cone);
                float attenD = d * 2.0;
                float invD = 1.0 / max(attenD, 1.0);
                atten = max(0.0, 134217728.0 * invD * invD - 16.0);
                atten += min(48.0, 32768.0 * invD);
                atten = clamp(atten / 255.0, 0.0, 1.0);
                atten *= 1.0 - smoothstep(u_flMode.w * 0.9, u_flMode.w, attenD);
            } else {
                // Modern: the soft spotlight of scene.frag (ModernCone, ModernFalloff, ModernTurn).
                float ct = clamp((dot(-L, flDir) - u_flDir.w) / max(u_flPos.w - u_flDir.w, 1e-4), 0.0, 1.0);
                cone = ct * ct * ct * (ct * (ct * 6.0 - 15.0) + 10.0);
                cone += 0.06 * smoothstep(u_flDir.w - 0.2, u_flDir.w, dot(-L, flDir)) * (1.0 - cone);
                vec3 flN = cross(dFdx(flP), dFdy(flP));
                float nlen = length(flN);
                vec3 N = (nlen > 1e-9) ? flN / nlen : vec3(0.0, 0.0, -1.0);
                if (dot(N, flP) > 0.0) N = -N;
                ndl = 0.1 + 0.9 * max((dot(N, L) + 0.3) / 1.3, 0.0);
                float r0 = 0.4 * u_flMode.w;
                float x = d / max(u_flMode.w, 1.0);
                float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
                atten = window * window / (1.0 + (d * d) / (r0 * r0));
            }
            // Bilinearly interpolated PCF with a perspective-correct receiver; (2r + 1)^2 texels, r = u_prog.w - 1.
            float shadow = 1.0;
            if (u_prog.w > 0.5) {
                vec3 flShadowP = v_shadowViewPos.xyz / max(v_shadowViewPos.w, 1e-9);
                vec3 flPs = flShadowP + L * (u_shadowP.y * d);
                vec4 lp = u_shadowMatrix * vec4(flPs, 1.0);
                if (lp.w > 0.0) {
                    vec3 luv = lp.xyz / lp.w * 0.5 + 0.5;
                    if (luv.x > 0.0 && luv.x < 1.0 && luv.y > 0.0 && luv.y < 1.0 && luv.z < 1.0) {
                        vec3 luvDx = dFdx(luv);
                        vec3 luvDy = dFdy(luv);
                        float det = luvDx.x * luvDy.y - luvDx.y * luvDy.x;
                        float dzdu = 0.0;
                        float dzdv = 0.0;
                        if (abs(det) > 1e-9) {
                            dzdu = (luvDx.z * luvDy.y - luvDy.z * luvDx.y) / det;
                            dzdv = (luvDy.z * luvDx.x - luvDx.z * luvDy.x) / det;
                        }
                        float recvLin = (u_shadowP.w > 0.0) ? shLinDepth(luv.z) : 0.0;
                        float occ = 0.0;
                        // Modern spreads the kernel over twice the texels for a softer penumbra.
                        vec2 shTexel = u_shadowQ.xy * (classic ? 1.0 : 2.0);
                        vec2 texelPos = luv.xy / shTexel - vec2(0.5);
                        vec2 texelBase = floor(texelPos);
                        vec2 texelFrac = fract(texelPos);
                        int kr = clamp(int(u_prog.w + 0.5) - 1, 0, 2);
                        for (int sy = -kr; sy <= kr + 1; sy++) {
                            float wy = (sy == -kr) ? (1.0 - texelFrac.y) : ((sy == kr + 1) ? texelFrac.y : 1.0);
                            for (int sx = -kr; sx <= kr + 1; sx++) {
                                float wx = (sx == -kr) ? (1.0 - texelFrac.x) : ((sx == kr + 1) ? texelFrac.x : 1.0);
                                float weight = wx * wy;
                                vec2 suv = (texelBase + vec2(float(sx), float(sy)) + vec2(0.5)) * shTexel;
                                float sd = texture(s_shadow, suv).r;
                                float receiverDepth = luv.z + dzdu * (suv.x - luv.x) + dzdv * (suv.y - luv.y);
                                if (receiverDepth - u_shadowP.x > sd) {
                                    if (u_shadowP.w > 0.0) {
                                        float gap = recvLin - shLinDepth(sd);
                                        occ += weight * (1.0 - clamp(gap / u_shadowP.w, 0.0, 1.0));
                                    } else {
                                        occ += weight;
                                    }
                                }
                            }
                        }
                        float kn = float(2 * kr + 1);
                        shadow = 1.0 - u_shadowP.z * (occ / (kn * kn));
                    }
                }
            }
            if (decal) {
                // The hole's relief: the surface normal tilted along the texture's axes (a cotangent frame from
                // the derivatives), lit relative to the flat surface so the untouched wall around it matches.
                vec3 dpx = dFdx(flP);
                vec3 dpy = dFdy(flP);
                vec2 dux = dFdx(v_texcoord.xy);
                vec2 duy = dFdy(v_texcoord.xy);
                vec3 Ng = cross(dpx, dpy);
                float glen = length(Ng);
                Ng = (glen > 1e-9) ? Ng / glen : vec3(0.0, 0.0, -1.0);
                if (dot(Ng, flP) > 0.0) Ng = -Ng;
                vec3 perpY = cross(dpy, Ng);
                vec3 perpX = cross(Ng, dpx);
                vec3 T = perpY * dux.x + perpX * duy.x;
                vec3 B = perpY * dux.y + perpX * duy.y;
                float tb = max(dot(T, T), dot(B, B));
                if (tb > 1e-18) {
                    float inv = inversesqrt(tb);
                    float up = sqrt(max(1.0 - dot(decalTilt, decalTilt), 0.0));
                    vec3 Nb = normalize(T * (inv * decalTilt.x) + B * (inv * decalTilt.y) + Ng * up);
                    float flatLit = max(dot(Ng, L), 0.08);
                    decalRelief = clamp(max(dot(Nb, L), 0.0) / flatLit, 0.0, 1.0);
                }
                // The relief shows as far as the flashlight makes up the surface's light there.
                float flLum = dot(u_flColor.rgb, vec3(0.299, 0.587, 0.114)) * cone * atten * ndl * shadow;
                decalLitShare = flLum / (flLum + 0.06);
            }
            vec3 fl = u_flColor.rgb * (cone * atten * ndl * shadow);
            fragColor.rgb += flAlbedo * fl * (1.0 - clamp(v_fade, 0.0, 1.0));
            if (u_flMode.z > 0.5) {
                float pk = max(fragColor.r, max(fragColor.g, fragColor.b));
                if (pk > 0.85) fragColor.rgb *= (0.85 + 0.15 * (1.0 - exp((0.85 - pk) / 0.15))) / pk;
            }
        }
    }
    // soft_fog P3: where all three vertices carry the ramp of their own depth, the fragment evaluates it at
    // its perspective-correct view depth.
    bool softFog = u_modes.y > 0.5;
    float fogSrc = v_fogAmount;
    float fogZ = (v_shadowViewPos.w > 0.0) ? v_shadowViewPos.z / v_shadowViewPos.w : 0.0;
    if (softFog && v_fogP3 > 0.999 && fogZ > 0.0) fogSrc = FogAmountAtZ(fogZ);
    float fogLin = fogSrc * u_fog.a;
    // soft_fog P2: linear up to 0.85, then a C1 cubic knee into full fog.
    float fogAmt = clamp(fogLin, 0.0, 1.0);
    if (softFog && fogLin > 0.85) {
        float kt = clamp((fogLin - 0.85) / 0.15, 0.0, 1.0);
        fogAmt = 0.85 + 0.15 * (kt - kt * kt + kt * kt * kt);
    }
    // soft_fog P4: full fog over the last 15% before the world's cull distance.
    if (softFog && u_fogRampP.x > 0.5 && u_fogRampP.w > 0.0 && fogZ > 0.0)
        fogAmt = max(fogAmt, smoothstep(u_fogRampP.w * 0.85, u_fogRampP.w, fogZ));
    if (decal) {
        // Without the flashlight the relief is a fixed soft shade from above, so a crater still reads.
        float ambientRelief = clamp(1.0 - 0.6 * max(decalTilt.y, 0.0) - 0.3 * abs(decalTilt.x), 0.0, 1.0);
        float keep = decalKeep * mix(ambientRelief, decalRelief, decalLitShare);
        // Fog lies over the paint: the mark fades with the surface instead of cutting a dark spot in the fog.
        fragColor = vec4(vec3(0.0), decalCover * (1.0 - keep) * (1.0 - fogAmt));
        return;
    }
    if (u_fogToBlack > 0)
        fragColor.rgb *= (1.0 - fogAmt);
    else
        fragColor.rgb = mix(fragColor.rgb, u_fog.rgb, fogAmt);

    // Ordered dither and 5-bit quantize.
    mat4 dither = mat4(
        -4.0, +0.0, -3.0, +1.0,
        +2.0, -2.0, +3.0, -1.0,
        -3.0, +1.0, -4.0, +0.0,
        +3.0, -1.0, +2.0, -2.0) / 255.0;
    vec2 fc = fragCoordGL();
    ivec2 dc = ivec2(fract(fc / ((u_dither.y > 0.0) ? u_dither.y : 8.0)) * 4.0);
    float addDither = (u_untextured > 0) ? 1.0 : (1.0 - float(u_fogToBlack));
    float dStrength = u_dither.x * v_is3d * addDither * (1.0 - fogAmt);
    fragColor.xyz += vec3(dither[dc.x][dc.y] * dStrength);
    if (u_dither.x > 0.5 && v_is3d > 0.5) {
        vec3 qcol = floor(fragColor.xyz * 32.0 + 0.5) / 32.0;
        fragColor.xyz = mix(qcol, fragColor.xyz, fogAmt);
    }
    if (u_dither.z > 0.5 && v_is3d > 0.5) {
        float ign = fract(52.9829189 * fract(dot(fc, vec2(0.06711056, 0.00583715))));
        fragColor.xyz += vec3((ign - 0.5) * (0.98 / 255.0));
    }

    // foliage_edge (u_rgbaFb.w, opaque splits only, split_plan.cpp): the cutout edge is the 0.33 contour of the
    // filtered coverage; mode 2 also antialiases it with sample coverage under MSAA.
    if (u_rgbaFb.w > 0.5 && g_tapWt > 0.0 && g_tapCov < 0.33 * g_tapWt) discard;
    if (u_rgbaFb.w > 1.5 && u_fogToBlack == 0 && g_tapWt > 0.0) {
        float a2cR = g_tapCov / g_tapWt;
        fragColor.a = clamp((a2cR - 0.33) / max(fwidth(a2cR), 0.05) + 0.5, 0.0, 1.0);
    }
    // Additive blend is dst * alpha + src. VRAM texels: STP set (0.5) adds, STP clear (1.0) draws solid.
    // Overrides carry no STP and always add.
    if (u_fogToBlack > 0) fragColor.a = rgba ? 1.0 : ((fragColor.a < 0.75) ? 1.0 : 0.0);
}
