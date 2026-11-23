#include "override_tex.h"

namespace sh::render {

OverrideTexTerms ComputeOverrideTexTerms(int width, int height, int offsetX, int offsetY, int hiresW, int hiresH)
{
    OverrideTexTerms t{};
    // An aborted slot registration leaves a zero native size; 1/0 would give NaN coordinates that some drivers
    // sample as arbitrary texels, so the divisor is clamped.
    t.texelSize[0] = 1.0f / static_cast<float>(width > 0 ? width : 1);
    t.texelSize[1] = 1.0f / static_cast<float>(height > 0 ? height : 1);
    t.offset[0] = static_cast<float>(offsetX);
    t.offset[1] = static_cast<float>(offsetY);
    const float hx = (hiresW > 0 && width > 0) ? 0.5f * static_cast<float>(width) / static_cast<float>(hiresW) : 0.0f;
    const float hy = (hiresH > 0 && height > 0) ? 0.5f * static_cast<float>(height) / static_cast<float>(hiresH) : 0.0f;
    t.halfHires[0] = hx > 0.5f ? 0.5f : hx;
    t.halfHires[1] = hy > 0.5f ? 0.5f : hy;
    return t;
}

}  // namespace sh::render
