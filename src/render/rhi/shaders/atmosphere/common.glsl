// Shared by the atmosphere passes (vk_atmosphere.cpp, RENDER.md section 7.10): the block they all read, the view
// reconstruction from the world's depth, the soft_fog transmittance and the flashlight's beam as scene.frag lights
// a surface with it (without the surface's turn towards the light).

// A phone's GPU (vk_atmosphere.cpp sets it there only): the occlusion image carries its estimate's depth so the
// composite reads one texel per estimate, and the integration shares the per-slice fog weights of its workgroup.
layout(constant_id = 0) const bool kPhone = false;

layout(std140, set = 0, binding = 0) uniform Atmosphere {
    vec4 u_proj;      // ndc = xy * (v.xy / v.z) + zw
    vec4 u_view;      // the world's viewport in the target: x, y, w, h (pixels)
    vec4 u_depth;     // x: view depth at window depth 1, y: first slice boundary, z: last, w: slices
    vec4 u_grid;      // xyz: froxels across, down and in depth (slices + 1 integrated boundaries), w: AO on: its
                      // step in pixels (1 every pixel, 2 every second one each way), 0 off
    vec4 u_fog;       // rgb: fog colour, a: fog strength
    vec4 u_fogRampP;  // x: fog on, y: 128 / 2^depthShift, z: intensity (Q12), w: cull distance
    vec4 u_fogRamp[32];
    vec4 u_flMode;    // x: flashlight on, y: style (1 Classic, 0 Modern), w: range
    vec4 u_flPos;     // xyz: lens (view space), w: inner cone cosine
    vec4 u_flDir;     // xyz: direction, w: outer cone cosine
    vec4 u_flColor;
    mat4 u_shadowMatrix;
    vec4 u_shadowQ;   // x: map texel, z: light near, w: light far
    vec4 u_ao;        // x: strength, y: radius (view units), z: pixels per unit of x / z, w: volumetric on
    vec4 u_scatter;   // x: scattering, y: anisotropy, z: haze per view unit, w: shadow map on
    vec4 u_lightInfo;  // x: map lights (rt_lights, lights.json)
    vec4 u_lightPos[8];    // xyz: view space, w: range
    vec4 u_lightColor[8];  // rgb
    vec4 u_sunDir;     // xyz: the way the sunlight travels (view space), w: the sun map's depth range (view units)
    vec4 u_sunColor;   // rgb, w: 1 god rays (the sun map is bound)
    mat4 u_sunMatrix;  // view space to the sun map's clip space
};

layout(set = 0, binding = 1) uniform sampler2D s_depth;
layout(set = 0, binding = 2) uniform sampler2D s_shadow;
layout(set = 0, binding = 7) uniform sampler2D s_sunShadow;

// View position of a target pixel at view depth z (the scene's own projection, scene.vert).
vec3 ViewPos(vec2 px, float z) {
    vec2 ndc = vec2(2.0 * (px.x - u_view.x) / u_view.z - 1.0, 1.0 - 2.0 * (px.y - u_view.y) / u_view.w);
    return vec3((ndc - u_proj.zw) / u_proj.xy * z, z);
}

// View depth of a target texel; negative where nothing wrote depth.
float ViewZ(ivec2 t) {
    float d = texelFetch(s_depth, t, 0).r;
    return d >= 1.0 ? -1.0 : d * u_depth.x;
}

float FogRampAt(int i) {
    vec4 r = u_fogRamp[i / 4];
    int c = i - (i / 4) * 4;
    return (c == 0) ? r.x : ((c == 1) ? r.y : ((c == 2) ? r.z : r.w));
}

// What soft_fog leaves of a surface at view depth z (scene.frag: ramp, strength, knee, cull fade).
float Transmittance(float z) {
    if (u_fogRampP.x < 0.5 || z <= 0.0) return 1.0;
    float fi = z * u_fogRampP.y;
    float fb = 255.0;
    if (fi < 128.0) {
        float f0 = floor(max(fi, 0.0));
        int i = int(f0);
        float b = (i < 127) ? FogRampAt(i + 1) : 255.0;
        fb = mix(FogRampAt(i), b, clamp(fi - f0, 0.0, 1.0));
    }
    float fogLin = clamp(fb * 16.0 + u_fogRampP.z, 0.0, 4096.0) / 4096.0 * u_fog.a;
    float amt = clamp(fogLin, 0.0, 1.0);
    if (fogLin > 0.85) {
        float kt = clamp((fogLin - 0.85) / 0.15, 0.0, 1.0);
        amt = 0.85 + 0.15 * (kt - kt * kt + kt * kt * kt);
    }
    if (u_fogRampP.w > 0.0) amt = max(amt, smoothstep(u_fogRampP.w * 0.85, u_fogRampP.w, z));
    return 1.0 - amt;
}

// Slice boundaries: the first slice is linear from the eye, the rest exponential up to where the fog is complete.
float SliceDepth(float s) {
    if (s <= 1.0) return s * u_depth.y;
    return u_depth.y * pow(u_depth.z / u_depth.y, (s - 1.0) / (u_depth.w - 1.0));
}

float SliceOf(float z) {
    if (z <= u_depth.y) return z / u_depth.y;
    return min(1.0 + (u_depth.w - 1.0) * log(z / u_depth.y) / log(u_depth.z / u_depth.y), u_depth.w);
}

vec3 BeamDir() {
    vec3 d = u_flDir.xyz;
    return dot(d, d) > 1e-12 ? normalize(d) : vec3(0.0, 0.0, 1.0);
}

vec3 BeamOrigin() { return u_flMode.y > 0.5 ? (u_flPos.xyz - BeamDir() * 39.0) : u_flPos.xyz; }

// The beam's cone and distance falloff at p, both styles (scene.frag).
float Beam(vec3 p) {
    if (u_flMode.x < 0.5) return 0.0;
    vec3 dir = BeamDir();
    vec3 L = BeamOrigin() - p;
    float d = length(L);
    L /= max(d, 0.0001);
    float c = dot(-L, dir);
    if (u_flMode.y > 0.5) {
        float cone = smoothstep(u_flDir.w, u_flPos.w, c);
        cone = cone * (2.0 - cone);
        float ad = d * 2.0;
        float inv = 1.0 / max(ad, 1.0);
        float atten = max(0.0, 134217728.0 * inv * inv - 16.0) + min(48.0, 32768.0 * inv);
        atten = clamp(atten / 255.0, 0.0, 1.0);
        atten *= 1.0 - smoothstep(u_flMode.w * 0.9, u_flMode.w, ad);
        return cone * atten;
    }
    float t = clamp((c - u_flDir.w) / max(u_flPos.w - u_flDir.w, 1e-4), 0.0, 1.0);
    float cone = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    cone += 0.06 * smoothstep(u_flDir.w - 0.2, u_flDir.w, c) * (1.0 - cone);
    float r0 = 0.4 * u_flMode.w;
    float x = d / max(u_flMode.w, 1.0);
    float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
    return cone * window * window / (1.0 + (d * d) / (r0 * r0));
}

// One compare against the flashlight's shadow map: 1 lit, 0 behind a caster. The bias is a fixed distance along
// the light, as scene.frag's.
float BeamShadow(vec3 p) {
    if (u_scatter.w < 0.5) return 1.0;
    vec4 lp = u_shadowMatrix * vec4(p, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 luv = lp.xyz / lp.w * 0.5 + 0.5;
    if (luv.x <= 0.0 || luv.x >= 1.0 || luv.y <= 0.0 || luv.y >= 1.0 || luv.z >= 1.0) return 1.0;
    float recv = max(lp.w, u_shadowQ.z);
    float bias = u_shadowQ.z / (recv * recv) * (4.0 + 0.01 * recv);
    return luv.z - bias > texture(s_shadow, luv.xy).r ? 0.0 : 1.0;
}

// The phone's occlusion texel: the estimate in 8 bits under the log2 of its view depth in 16 (1/2048 steps, about
// 0.03 % of the depth); integers below 2^24, exact in r32f. 0 where nothing wrote depth.
float PackAo(float ao, float z) {
    if (z <= 0.0) return 255.0;
    float zq = clamp(floor(log2(max(z, 1.0)) * 2048.0 + 0.5), 1.0, 65535.0);
    return zq * 256.0 + floor(clamp(ao, 0.0, 1.0) * 255.0 + 0.5);
}

// x: the estimate, y: its view depth (negative where nothing wrote depth).
vec2 UnpackAo(float v) {
    float zq = floor(v / 256.0);
    return vec2((v - zq * 256.0) / 255.0, zq > 0.5 ? exp2(zq / 2048.0) : -1.0);
}

float Noise(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }
