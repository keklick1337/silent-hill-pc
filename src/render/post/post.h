#pragma once

// Post-process, flashlight shadow light and frozen-frame policy of the renderer, as plain C++ with no GL.
// The device object handling stays with the caller (the RHI); this holds the decisions
// and the maths, so both backends share them.

#include <array>

namespace sh::post {

// Image adjustment (brightness/contrast/saturation) is a grade of the composed frame. With world-only scope
// (scope != 0) it is skipped on frames that draw no 3D world, except on the Brightness screen itself.
constexpr bool imageAdjustActive(int scope, bool sceneFrame, bool calibBar) {
    return scope == 0 || sceneFrame || calibBar;
}

struct PostSettings {
    int postProcess = 0;      // look filter id, <= 0 = none
    int tonemap = 0;          // <= 0 = none
    bool fxaa = false;
    float brightness = 1.f, contrast = 1.f, saturation = 1.f;
    bool calibBar = false;
    bool adjustActive = true;
};

// Does this frame need the post pass at all? Off-neutral image adjustment or the calibration bar force it
// even with no filter selected.
bool needsPostPass(const PostSettings& s);

// Neutral values for an out-of-scope adjustment: the pass still runs when a filter or the tonemap needs it.
struct ImageAdjust {
    float brightness, contrast, saturation;
};
ImageAdjust effectiveAdjust(const PostSettings& s);

// Shadow map size: clamped to [256, 8192], and to the driver limit (when known, > 0) above 4096.
int clampShadowMapSize(int requested, int driverMaxTexture);

struct ShadowLightInput {
    float sizeScale = 1.f;            // cone size multiplier of the active flashlight mode
    float outerCos = 0.9f;            // outer cone cosine of the active style at size 1
    float spillCos = 0.f;             // light still reaches this far past the outer cone (a cosine step)
    float range = 5000.f;
    std::array<float, 3> position{};  // the real light position (view space)
    std::array<float, 3> direction{0.f, 0.f, 1.f};
    bool fpsMode = false;
    float fpsDrop = 0.f;              // extra pull-back along -direction in FPS mode
    float zNear = 20.f;               // casters closer to the light are left out (the hand that holds it)
};

struct ShadowLight {
    std::array<float, 16> matrix;     // column-major, view space -> light clip
    float zNear, zFar;
};
ShadowLight buildShadowLight(const ShadowLightInput& in);

// The frozen last frame (pause menu, take screen): when to capture, and that a present suppresses the next
// capture so UI drawn over the frozen image is not baked into it.
class FreezeFrame {
public:
    // True when the backbuffer should be captured now.
    bool shouldCapture(bool holdCapture);
    void resized(int w, int h);  // storage changed: the captured picture is dropped
    void captured() { valid_ = true; }
    void presented() { presentedThisFrame_ = true; }
    bool valid() const { return valid_; }
    bool sizeDiffers(int w, int h) const { return w != w_ || h != h_; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    bool valid_ = false;
    bool presentedThisFrame_ = false;
    int w_ = 0, h_ = 0;
};

}  // namespace sh::post
