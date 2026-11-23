// The shadow settings from the render state (shadow_pass.h shadow_settings_from_state).
#include "render/gpu/render_state.h"
#include "render/gpu/shadow_pass.h"

namespace sh::gpu {

ShadowSettings shadow_settings_from_state() {
    const render::Flashlight& light = render::flashlight();
    ShadowSettings s;
    s.use_shadows = light.shadows != 0;
    s.config_shadows = render::graphics().flashlight_shadows != 0;
    s.per_pixel = light.per_pixel != 0;
    s.flashlight_active = light.active != 0;
    s.allowed = light.shadows_allowed != 0;
    s.frozen = render::feedback_control().present_last_frame != 0;
    s.map_size = light.shadow_map_size;
    s.filter = light.shadow_filter;
    s.bias = light.shadow_bias;
    s.normal_offset = light.shadow_normal_offset;
    s.strength = light.shadow_strength;
    s.fade_distance = light.shadow_fade_distance;
    s.near_z = render::precise().near_z;

    // First person pins the cone at the eye; the shadow still starts at the real chest/hand light.
    const bool fps = light.fps_mode != 0;
    const float* pos = fps ? light.shadow_pos : light.pos;
    s.light.sizeScale = fps ? light.size_fps : light.size;
    // The cone of the active style (split_plan.cpp flashlight_block).
    s.light.outerCos = light.style != 0 ? light.outer_cos : kModernOuterCos;
    s.light.spillCos = light.style != 0 ? 0.f : kModernSpillCos;
    s.light.range = light.range;
    s.light.position = {pos[0], pos[1], pos[2]};
    s.light.direction = {light.dir[0], light.dir[1], light.dir[2]};
    s.light.fpsMode = fps;
    s.light.fpsDrop = light.shadow_fps_drop;
    return s;
}

}  // namespace sh::gpu
