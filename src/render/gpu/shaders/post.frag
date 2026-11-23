#version 450
// Post pass of the frame (src/render/gpu/post_pass.cpp): the look filters, tonemap, FXAA-lite, calibration bar and
// image adjustment, with the uniforms packed into one std140 block. Order: look filter on the (FXAA'd) source mixed by its intensity, the
// tonemap mixed by its intensity, the calibration bar, then saturation / contrast / brightness.

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 3, binding = slot)
#define SH_SAMPLER(slot) layout(set = 2, binding = slot)
#define SH_VARYING(n) layout(location = n)
#else
#define SH_UBO(slot) layout(std140)
#define SH_SAMPLER(slot)
#define SH_VARYING(n)
#endif

SH_VARYING(0) in vec2 v_uv;

layout(location = 0) out vec4 fragColor;

SH_SAMPLER(0) uniform sampler2D s_scene;

SH_UBO(0) uniform PostFs {
    vec4 u_mode;    // x: look filter (0 none, 1..8), y: tonemap (0 none, 1..3), z: fxaa, w: calibration bar
    vec4 u_mix;     // x: look intensity, y: tonemap intensity, z: grain time, w: > 0 = target height, flip gl_FragCoord
    vec4 u_adjust;  // x: brightness, y: contrast, z: saturation (1 = neutral), w: 1 = the source has v = 0 at the top
    vec4 u_texel;   // xy: 1 / source size, z: largest value written (0 = 1.0; HDR10 keeps highlights above white)
};

const vec3 kLuma = vec3(0.299, 0.587, 0.114);
const float kPi = 3.14159;

vec3 scene(vec2 uv) {
    return texture(s_scene, u_adjust.w > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv).rgb;
}

// gl_FragCoord with GL's bottom-left origin on every backend, so mask and dither phases do not flip.
vec2 fragXY() {
    vec2 p = gl_FragCoord.xy;
    if (u_mix.w > 0.0) p.y = u_mix.w - p.y;
    return p;
}

// Time is its own hash dimension: every pixel keeps its place and only its value changes, which is what film
// grain looks like (offsetting the lookup by time slides or jumps the spatially coherent pattern).
float grain(vec2 p, float t) {
    vec3 q = fract(vec3(p, t) * vec3(443.897, 441.423, 437.195));
    q += dot(q, q.yzx + 19.19);
    return fract((q.x + q.y) * q.z);
}

vec3 grade(vec3 c) {
    c = (c - 0.5) * 1.12 + 0.5;
    c = mix(vec3(dot(c, kLuma)), c, 1.15);
    return c * vec3(1.06, 1.0, 0.94);
}

vec3 vignette(vec3 c, vec2 uv, float k) {
    vec2 d = uv - 0.5;
    return c * clamp(1.0 - dot(d, d) * k, 0.0, 1.0);
}

vec2 barrel(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec2 o = abs(uv.yx) / vec2(6.0, 5.0);
    uv += uv * o * o;
    return uv * 0.5 + 0.5;
}

// Luma edge test on the four neighbours, then a blend along the edge: softer and cheaper than FXAA 3.11.
vec3 fxaaLite(vec2 uv) {
    vec2 t = u_texel.xy;
    vec3 c = scene(uv);
    float lc = dot(c, kLuma);
    float ln = dot(scene(uv + vec2(0.0, t.y)), kLuma);
    float ls = dot(scene(uv - vec2(0.0, t.y)), kLuma);
    float le = dot(scene(uv + vec2(t.x, 0.0)), kLuma);
    float lw = dot(scene(uv - vec2(t.x, 0.0)), kLuma);
    float lo = min(lc, min(min(ln, ls), min(le, lw)));
    float hi = max(lc, max(max(ln, ls), max(le, lw)));
    if (hi - lo < max(0.0625, hi * 0.125)) return c;
    vec2 dir = vec2(ls - ln, le - lw);
    dir = clamp(dir / (max(abs(dir.x), abs(dir.y)) + 0.0078), -2.0, 2.0) * t;
    vec3 a = 0.5 * (scene(uv + dir * (1.0 / 3.0 - 0.5)) + scene(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 b = a * 0.5 + 0.25 * (scene(uv - dir * 0.5) + scene(uv + dir * 0.5));
    float lb = dot(b, kLuma);
    return (lb < lo || lb > hi) ? a : b;
}

vec3 tonemap(vec3 c, int op) {
    if (op == 1) return c / (c + vec3(1.0));  // Reinhard
    if (op == 2) {                            // ACES (Narkowicz)
        c *= 0.6;
        return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    }
    if (op == 3) {  // filmic (Hejl / Burgess)
        vec3 x = max(vec3(0.0), c - 0.004);
        return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    }
    return c;
}

// The look filter. CRT curves uv for the steps after it too (the calibration bar), and returns false for a
// pixel outside the curved screen: solid black, no further steps.
bool look(int lookId, inout vec2 uv, vec3 base, out vec3 col) {
    vec2 fc = fragXY();
    col = base;
    if (lookId == 1) {  // CRT
        uv = barrel(uv);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return false;
        col = scene(uv) * (0.75 + 0.25 * abs(sin(uv.y * 240.0 * kPi)));
        int m = int(mod(fc.x, 3.0));
        vec3 mask = m == 0 ? vec3(1.0, 0.72, 0.72) : (m == 1 ? vec3(0.72, 1.0, 0.72) : vec3(0.72, 0.72, 1.0));
        col = vignette(col * mask * 1.25, uv, 1.1);
    } else if (lookId == 2) {  // scanlines
        col = scene(uv) * (0.7 + 0.3 * abs(sin(uv.y * 240.0 * kPi)));
    } else if (lookId == 3) {  // vignette
        col = vignette(scene(uv), uv, 1.3);
    } else if (lookId == 4) {  // colour grade
        col = grade(scene(uv));
    } else if (lookId == 5) {  // film grain
        col = scene(uv) + (grain(fc, u_mix.z) - 0.5) * 0.10;
    } else if (lookId == 6) {  // sharpen
        vec2 t = u_texel.xy;
        vec3 c = scene(uv);
        vec3 blur = (scene(uv + vec2(t.x, 0.0)) + scene(uv - vec2(t.x, 0.0)) + scene(uv + vec2(0.0, t.y)) +
                     scene(uv - vec2(0.0, t.y))) * 0.25;
        col = c + (c - blur) * 0.85;
    } else if (lookId == 7) {  // PSX retro: 320x240 grid, 4x4 ordered dither, 5-bit quantize
        vec2 grid = vec2(320.0, 240.0);
        col = scene((floor(uv * grid) + 0.5) / grid);
        mat4 dither = mat4(-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0, -3.0, 1.0, -4.0, 0.0, 3.0, -1.0, 2.0, -2.0) / 255.0;
        ivec2 cell = ivec2(mod(fc, 4.0));
        col = floor((col + vec3(dither[cell.x][cell.y])) * 32.0 + 0.5) / 32.0;
    } else if (lookId == 8) {  // cinematic: grade + vignette + light grain
        col = vignette(grade(scene(uv)), uv, 0.9) + (grain(fc, u_mix.z) - 0.5) * 0.045;
    }
    return true;
}

// Brightness screen reference: a black-to-white ramp with a primary-colour strip, drawn before the adjustments
// so it is graded exactly like the game image.
vec3 calibrationBar(vec2 uv, vec3 col) {
    if (uv.y < 0.09) return vec3(clamp(uv.x, 0.0, 1.0));
    if (uv.y >= 0.15) return col;
    float s = uv.x * 6.0;
    if (s < 1.0) return vec3(1.0, 0.0, 0.0);
    if (s < 2.0) return vec3(0.0, 1.0, 0.0);
    if (s < 3.0) return vec3(0.0, 0.0, 1.0);
    if (s < 4.0) return vec3(1.0, 1.0, 0.0);
    if (s < 5.0) return vec3(0.0, 1.0, 1.0);
    return vec3(1.0, 0.0, 1.0);
}

void main() {
    vec2 uv = v_uv;
    vec3 base = u_mode.z > 0.5 ? fxaaLite(uv) : scene(uv);
    vec3 col;
    if (!look(int(u_mode.x + 0.5), uv, base, col)) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    col = mix(base, col, u_mix.x);
    col = mix(col, tonemap(col, int(u_mode.y + 0.5)), u_mix.y);
    if (u_mode.w > 0.5) col = calibrationBar(uv, col);
    col = mix(vec3(dot(col, kLuma)), col, u_adjust.z);
    col = (col - 0.5) * u_adjust.y + 0.5;
    col *= u_adjust.x;
    fragColor = vec4(clamp(col, 0.0, u_texel.z > 0.0 ? u_texel.z : 1.0), 1.0);
}
