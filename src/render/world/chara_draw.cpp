// The character draw of a frame (docs/RENDER.md section 6): the posed bones of a character handed to the
// skinned glb drawer (render/skinning/gltf_chara.h), with the player's held item, the fade a vanishing character
// lights itself with, the first-person head cut and the console's draw scale.

#include <cstdint>
#include <cstdlib>

#include "engine/camera/camera_config.h"
#include "engine/camera/camera_driver.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/view_system.h"
#include "engine/game_state/session.h"
#include "engine/world/world_atmosphere.h"
#include "render/frame/frame.h"
#include "render/gpu/render_state.h"
#include "render/precision/pc_smooth_xform.h"
#include "render/skinning/gltf_chara.h"
#include "render/world/world_objects_draw.h"
#include "render/world/world_scene_source.h"
#include "engine/world/game/atmosphere_host.h"
#include "input/camera_style.h"

extern "C" {
// The first-person camera (src/input/camera_style.h) and its melee swing pull-back.
extern int g_PcFpsSwingHeadShow;
extern int g_DebugThirdPersonCam;
extern int g_PcAltCamApplied;
// The player's head is left out of the draw (read by the bone code while the draw runs).
extern int g_PcHideHarryFpsBody;
}

namespace {

namespace cam = sh::camera;

constexpr int kPlayerChara = 1;
constexpr std::int32_t kOne = 4096;

std::int32_t fade_channel(std::int32_t timer, std::uint8_t c) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(kOne - timer) * c) >> 12) << 5;
}

// The first-person view is at the player's eyes: the head is hidden only during interactive play, once the view
// has arrived at the head (the room-entry camera glides there first) and while no melee swing pulls it back.
// The head is located on every character draw: it brings the head bones' world matrices up to date.
bool hide_player_head(int chara) {
    const cam::Vec3 view = cam::view_system().position();
    const cam::Vec3 head =
        cam::head_point(cam::frame_world(cam::camera_host().harry_head()), cam::camera_api_config().subject);
    // Third person: a wall behind Harry presses the eye up to his head, which would fill the view.
    const std::int32_t near = sh::input::camera_styles().first_person() ? 2252 : 1843;  // 0.55 m, 0.45 m
    const bool at_eye =
        std::abs(view.x - head.x) < near && std::abs(view.y - head.y) < near && std::abs(view.z - head.z) < near;
    if (!g_DebugThirdPersonCam || chara != kPlayerChara || !at_eye) return false;
    if (sh::input::camera_styles().first_person() && g_PcFpsSwingHeadShow) return false;
    const sh::game::Session& s = sh::game::session();
    // Only while that camera is the view: a scene's own shot shows all of him.
    return s.game.game_state == sh::game::game_state::kInGame &&
           s.system.flow.sys_state == sh::game::sys_state::kGameplay && g_PcAltCamApplied != 0;
}

// smooth_models: the root's unrounded position, from whichever character owns these bones.
void smooth_root(int chara, void* bones) {
    if (!Pc_Smooth_Active()) return;
    sh::entities::CharacterSlots& slots = sh::game::session().system.characters;
    auto* root = static_cast<sh::entities::BoneCoord*>(bones);
    const sh::entities::CharacterRecord* owner = nullptr;
    if (root == slots.player_bones) {
        owner = &slots.player;
    } else {
        for (const sh::entities::CharacterRecord& npc : slots.npcs) {
            if (npc.model.chara_id == chara && (npc.position.x >> 4) == root->local.t[0] &&
                (npc.position.z >> 4) == root->local.t[2]) {
                owner = &npc;
                break;
            }
        }
    }
    if (owner != nullptr) Pc_Smooth_RootPosition(root, owner->position.x, owner->position.y, owner->position.z);
}

}  // namespace

// Draws character `chara` posed by `bones`; `shift` is the ordering table's depth shift, `fade` (Q12, clamped to
// 0..1) how far it has faded out.
extern "C" void WorldGfx_CharaModelDraw(std::int32_t chara, void* bones, std::int32_t shift, std::int32_t fade,
                                        std::int32_t unused) {
    (void)unused;
    if (chara == 0) return;
    const std::int32_t timer = fade < 0 ? 0 : (fade > kOne ? kOne : fade);

    if (chara == kPlayerChara) {
        // The held item is its own model: it casts a flashlight shadow when the player's body does.
        sh::frame::state().no_shadow_cast = sh::render::flashlight().shadow_player == 0;
        sh::world_scene::draw_held_item_in_hand();
        sh::frame::state().no_shadow_cast = false;
    }

    // A fading character is lit toward black: the world tint scaled down by the fade for its draw.
    sh::world::AtmosphereState& a = sh::world::atmosphere().state();
    const sh::world::Color8 tint = a.world_tint;
    if (timer != 0) {
        sh::world::frame_light_set(a.light_mode, a.ambient, a.light_level_max, fade_channel(timer, tint.r),
                                   fade_channel(timer, tint.g), fade_channel(timer, tint.b), a.screen_brightness);
    }

    // A character without a loaded model is not drawn, and its fade light stays set as it always did.
    const void* skeleton = sh::world_scene::source::chara_skeleton(chara);
    if (skeleton == nullptr) return;
    g_PcHideHarryFpsBody = hide_player_head(chara) ? 1 : 0;
    // The fade also dims the per-pixel flashlight, which would
    // otherwise relight a character the game has faded out (a Larval Stalker's vanish).
    sh::frame::state().no_shadow_cast = chara == kPlayerChara && sh::render::flashlight().shadow_player == 0;
    sh::frame::state().chara_fade = static_cast<float>(timer) / 4096.0f;
    smooth_root(chara, bones);
    Pc_GltfChara_Draw(skeleton, bones, nullptr, shift, g_PcHideHarryFpsBody);
    g_PcHideHarryFpsBody = 0;
    sh::frame::state().no_shadow_cast = false;
    sh::frame::state().chara_fade = 0.0f;

    if (timer != 0) {
        sh::world::frame_light_set(a.light_mode, a.ambient, a.light_level_max, tint.r << 5, tint.g << 5,
                                   tint.b << 5, a.screen_brightness);
    }
}
