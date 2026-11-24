#include "render/post/post.h"

#include <algorithm>
#include <cmath>

namespace sh::post {

bool needsPostPass(const PostSettings& s) {
    const bool adjusted = s.adjustActive && (s.brightness != 1.f || s.contrast != 1.f || s.saturation != 1.f || s.calibBar);
    return s.postProcess > 0 || s.tonemap > 0 || adjusted || s.fxaa;
}

ImageAdjust effectiveAdjust(const PostSettings& s) {
    if (!s.adjustActive) return {1.f, 1.f, 1.f};
    return {s.brightness, s.contrast, s.saturation};
}

int clampShadowMapSize(int requested, int driverMaxTexture) {
    int size = std::clamp(requested, 256, 8192);
    if (size > 4096 && driverMaxTexture > 0 && size > driverMaxTexture) size = driverMaxTexture;
    return size;
}

namespace {

using V3 = std::array<float, 3>;
using M4 = std::array<float, 16>;  // column-major

V3 normalized(V3 v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-8f)
        for (float& c : v) c /= len;
    return v;
}
float dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 cross(const V3& a, const V3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

M4 perspective(float fovy, float aspect, float zn, float zf) {
    M4 m{};
    const float f = 1.f / std::tan(fovy * 0.5f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.f;
    m[14] = (2.f * zf * zn) / (zn - zf);
    return m;
}

M4 lookAt(const V3& eye, const V3& center, const V3& up) {
    const V3 f = normalized({center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]});
    const V3 s = normalized(cross(f, up));
    const V3 u = cross(s, f);
    return {s[0], u[0], -f[0], 0.f, s[1], u[1], -f[1], 0.f, s[2], u[2], -f[2], 0.f,
            -dot(s, eye), -dot(u, eye), dot(f, eye), 1.f};
}

M4 multiply(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1] +
                             a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
    return r;
}

}  // namespace

ShadowLight buildShadowLight(const ShadowLightInput& in) {
    // The lit cone as the shaders build it, with its spill.
    const float outer = std::clamp(std::max(0.05f, 1.f - in.sizeScale * (1.f - in.outerCos)) - in.spillCos, -0.5f, 0.999f);
    // A caster outside the lit cone shadows nothing inside it: the map spans the cone and a margin for the filter
    // kernels, no more, so its texels are not spent on the dark.
    const float fov = std::clamp(std::acos(outer) * 2.f * 1.06f, 0.2f, 2.9f);
    const float zn = std::max(in.zNear, 1.f);
    const float zf = std::max(in.range * 1.3f, zn + 1.f);

    const V3 dir = normalized(in.direction);
    V3 eye = in.position;
    if (in.fpsMode && in.fpsDrop != 0.f)
        for (int i = 0; i < 3; ++i) eye[i] -= dir[i] * in.fpsDrop;
    const V3 center{eye[0] + dir[0], eye[1] + dir[1], eye[2] + dir[2]};
    V3 up{0.f, 1.f, 0.f};
    if (std::fabs(dot(dir, up)) > 0.99f) up = {1.f, 0.f, 0.f};

    return {multiply(perspective(fov, 1.f, zn, zf), lookAt(eye, center, up)), zn, zf};
}

bool FreezeFrame::shouldCapture(bool holdCapture) {
    // A frame that re-presented the capture must not be re-captured, or the UI drawn on top would bake in.
    if (presentedThisFrame_) {
        presentedThisFrame_ = false;
        return false;
    }
    // The pause menu leaves for the save/options screens and must come back to the moment of pausing.
    return !holdCapture;
}

void FreezeFrame::resized(int w, int h) {
    w_ = w;
    h_ = h;
    valid_ = false;
}

}  // namespace sh::post
