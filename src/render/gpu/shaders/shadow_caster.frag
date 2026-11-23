#version 450
// Flashlight shadow casters: depth only. The RHI has no polygon offset state, so the fragment applies
// glPolygonOffset(1.5, 1.0) itself: 1.5 times the depth slope plus one step of a 24-bit depth buffer.

layout(location = 0) out vec4 fragColor;  // the colour target is write-masked off

void main() {
    float z = gl_FragCoord.z;
    float slope = max(abs(dFdx(z)), abs(dFdy(z)));
    gl_FragDepth = clamp(z + 1.5 * slope + 1.0 / 16777216.0, 0.0, 1.0);
    fragColor = vec4(0.0);
}
