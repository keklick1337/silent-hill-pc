// SPDX-License-Identifier: GPL-3.0-or-later
//
// The host of the engine's alternate cameras (third person, over the shoulder, first person,
// engine/camera/alt_camera.h): reads Harry, his head and arm bones, the pad, the mouse and the player's camera
// options into AltCameraInput, answers its ray queries with the level collision, decides when the camera stands down
// (attract demo, scripted scenes, item pickup) and hands the view, the free-aim ray and the first-person values to
// the rest of the game through the globals they have always used.

#include <cstdint>
#include <optional>
#include <string>

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include "engine/app/game_view.h"
#include "engine/app/pc_paths.h"
#include "engine/camera/alt_camera.h"
#include "engine/camera/game_camera.h"
#include "engine/config/pc_config.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/math/geometry_state.h"
#include "engine/screens/screen_fade.h"
#include "engine/screens/screens_view.h"
#include "input/camera_style.h"
#include "platform/sh_platform.h"
#include "engine/mods/data_reload.h"

namespace {

using sh::entities::Vec3;

// The level collision's line of sight query and its answer.
struct RayTrace {
    std::int8_t hit;
    std::uint8_t ground_type;
    std::int8_t reserved[2];
    Vec3 target;
    const void* character;
    std::int32_t hit_distance;
    std::int32_t ground_height;
    std::int16_t angle;
};

}  // namespace

extern "C" {
extern const unsigned char* g_sdlKeyboardState;
extern int g_PcRearLookActive;
extern int g_PcPickupItemActive;
extern int g_PcConsoleInputActive;
extern int g_PcDebugMenuActive;
const char* PcPort_GetGameDataPath(void);
int Pc_ScriptOwnsScene(void);
bool Ray_TraceQuery(RayTrace* trace, const Vec3* from, const Vec3* to);

// The orbit angles (other code turns them too: the body follows them, a style change reseeds them).
int g_TpsCamYaw = 0;
int g_TpsCamPitch = 0;
// The free-aim ray: from the eye through the screen centre (the player's combat).
Vec3 g_TpsCamPos = {0, 0, 0};
Vec3 g_TpsCamFwd = {0, 0, 4096};
// First person: the eye baseline in Harry's body frame (config fps_head_x/y/z, the tuner keys), the view forward and
// eye for the head-mounted flashlight, the head drawn during a melee dolly, and the head sway reference and head
// position for the tuner log.
Vec3 g_PcFpsOffset = {-29, -6836, 919};
Vec3 g_PcFpsViewFwd = {0, 0, 4096};
Vec3 g_PcFpsEyePos = {0, 0, 0};
int g_PcFpsSwingHeadShow = 0;
Vec3 g_PcFpsHeadRefDbg = {0, 0, 0};
Vec3 g_PcFpsHeadLocalDbg = {0, 0, 0};
// The device that last turned the view, for the aim assist window: 0 mouse, 1 pad.
int g_PcAimDevice = 0;
// The alternate camera made the view at its last call (it did not stand down for a scene or the demo).
int g_PcAltCamApplied = 0;
// The first person eye applied this frame (the L log reads it).
Vec3 g_PcCamAppliedPos = {0, 0, 0};

int Pc_AltCamStateOk(void);
}

namespace {

namespace cam = ::sh::camera;
using sh::game::session;

// Harry's bones the cameras follow.
constexpr int kBoneHead = 2;
constexpr int kBoneLeftForearm = 5;
constexpr int kBoneLeftHand = 6;
constexpr int kBoneRightForearm = 9;
constexpr int kBoneRightHand = 10;

// Pad bits of the walking input: the d-pad and the left stick's directions.
constexpr std::uint32_t kWalkButtons = (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 24) | (1u << 25) |
                                       (1u << 26) | (1u << 27);
// Melee attacks are packed below the handgun's tap attack (weapon id 32).
constexpr std::int32_t kFirstRangedAttack = 32;
constexpr std::int32_t kNoAttack = -1;
// The fade states from the start of a fade-in on (the low three bits of the fade status).
constexpr std::int32_t kFadeInStart = 6;

// The camera stood down for a scene (or the attract demo): its next frame starts fresh at Harry.
bool s_stood_down = false;
// Harry's root at the last camera frame: a jump (a load, a warp, a retry) starts fresh too.
bool s_have_last = false;
Vec3 s_last_harry = {0, 0, 0};
// Farther than any step of one frame, nearer than any warp.
constexpr std::int64_t kWarpDistance = 3 * 4096;

std::optional<cam::AltCamera> s_camera;
std::uint32_t s_camera_epoch = sh::mods::kNeverRead;

cam::AltCamera* camera() {
    if (sh::mods::data_stale(s_camera_epoch)) {
        s_camera.reset();
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<cam::AltCameraTuning> loaded = cam::load_alt_camera_tuning(PcPort_GetGameDataPath(), mods, &source);
        if (loaded) {
            s_camera.emplace(loaded.value());
            SH_LOG("[CAMERA] alternate cameras from %s", source.c_str());
        } else {
            SH_ERR("[CAMERA] %s - the alternate cameras stay off", loaded.error().c_str());
        }
    }
    return s_camera ? &*s_camera : nullptr;
}

class LevelRays final : public cam::AltCameraHost {
public:
    bool ray_hit(const cam::AltVec3& from, const cam::AltVec3& to, std::int32_t* distance) override {
        const Vec3 a{from.x, from.y, from.z};
        const Vec3 b{to.x, to.y, to.z};
        RayTrace trace;
        if (Ray_TraceQuery(&trace, &a, &b) && trace.hit) {
            *distance = trace.hit_distance;
            return true;
        }
        return false;
    }
};

Vec3 vec(const cam::AltVec3& v) { return Vec3{v.x, v.y, v.z}; }

cam::AltBone bone(int index) {
    const sh::entities::BoneMatrix& m = session().system.characters.player_bones[index].world;
    cam::AltBone b;
    b.tx = m.t[0];
    b.ty = m.t[1];
    b.tz = m.t[2];
    b.fx = m.m[0][2];
    b.fy = m.m[1][2];
    b.fz = m.m[2][2];
    return b;
}

cam::AltCameraOptions options(bool ots) {
    cam::AltCameraOptions o;
    o.mouse_sensitivity = g_PcConfig.mouseSensitivity;
    o.pad_sensitivity = g_PcConfig.controllerSensitivity;
    o.invert_mouse_y = g_PcConfig.invertMouseY != 0;
    o.invert_pad_y = g_PcConfig.invertControllerY != 0;
    o.aim_zoom_percent = ots ? g_PcConfig.otsAimZoom : g_PcConfig.tpsAimZoom;
    o.rest_zoom_percent = ots ? g_PcConfig.otsRestZoom : g_PcConfig.tpsRestZoom;
    if (ots) {
        o.rest_x = g_PcConfig.otsRestX;
        o.rest_y = g_PcConfig.otsRestY;
        o.aim_x = g_PcConfig.otsAimX;
        o.aim_y = g_PcConfig.otsAimY;
    } else {
        o.rest_x = g_PcConfig.tpsRestX;
        o.rest_y = g_PcConfig.tpsRestY;
        // Third person swings over the shoulder only while aiming, and only with tps_ots_aim.
        o.aim_x = g_PcConfig.tpsOtsAim ? g_PcConfig.tpsAimX : o.rest_x;
        o.aim_y = g_PcConfig.tpsOtsAim ? g_PcConfig.tpsAimY : o.rest_y;
    }
    o.wall_collision = g_PcConfig.tpsCameraCollision != 0;
    o.follow_smoothing = static_cast<float>(g_PcConfig.tpsCameraSmoothing) / 100.0f;
    o.look_smoothing = static_cast<float>(g_PcConfig.tpsLookSmoothing) / 100.0f;
    o.recenter = g_PcConfig.tpsCameraRecenter != 0;
    o.recenter_delay = g_PcConfig.tpsRecenterDelay;
    o.head_tracking = g_PcConfig.immersiveFpsHeadTracking != 0;
    o.melee_swing = g_PcConfig.fpsMeleeSwing;
    o.eye_offset = cam::AltVec3{g_PcFpsOffset.x, g_PcFpsOffset.y, g_PcFpsOffset.z};
    return o;
}

bool walking() {
    const unsigned char* ks = g_sdlKeyboardState;
    return ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_D] ||
           (session().game.pads[0].held & kWalkButtons) != 0;
}

// The projection's screen distance (the geometry unit's H, 16 bits).
void set_screen_distance(int h) { sh::math::geometry().set_screen_distance(h); }

// The field of view of the active style while its camera body runs; otherwise the game's own projection, restored
// once.
void field_of_view(bool stand_down) {
    static bool applied = false;
    // Item pickup draws the item over a frozen backdrop with the projection it was authored for.
    if (g_PcPickupItemActive) {
        stand_down = true;
    }
    float fov = 0.0f;
    if (Pc_AltCamStateOk() && !stand_down) {
        if (sh::input::camera_styles().first_person()) {
            fov = g_PcConfig.fpsFov;
        } else if (sh::input::camera_styles().style() == sh::input::CameraStyle::OverShoulder) {
            fov = g_PcConfig.otsFov;
        } else if (sh::input::camera_styles().style() == sh::input::CameraStyle::ThirdPerson) {
            fov = g_PcConfig.tpsFov;
        }
    }
    if (fov > 0.0f) {
        set_screen_distance(cam::AltCamera::projection_distance(fov, session().game.screen_height));
        applied = true;
    } else if (applied) {
        // The game's current projection (a letterbox zoom may own it), not the gameplay default.
        set_screen_distance(sh::camera::game_camera().state().geom_screen_dist);
        applied = false;
    }
}

}  // namespace

extern "C" int Pc_AltCamStateOk(void) {
    // The inventory switches the game state while the world is still drawn behind it.
    using sh::game_state::GameState;
    const auto state = static_cast<GameState>(session().game.game_state);
    return state == GameState::InGame || state == GameState::LoadStatusScreen || state == GameState::InventoryScreen;
}

namespace {

// The letterbox bars close for about a second after a scene has handed control back (the scene flag drops only
// with them). The camera returns with the control, not with the bars: until they were gone the game camera
// showed its own shot from behind Harry, and first person then jumped from it to the eyes.
bool only_bars_closing(const sh::game::Session& s) {
    using sh::screens::BorderState;
    const auto border = static_cast<BorderState>(s.system.flow.cutscene_border);
    const bool closing =
        border == BorderState::FadeOutStart || border == BorderState::FadingOut || border == BorderState::Reset;
    return closing && s.game.game_state == sh::game::game_state::kInGame &&
           s.sys_state() == sh::game::sys_state::kGameplay && !sh::app::view::player_control_disabled();
}

}  // namespace

extern "C" void Pc_TpsCamera_Apply(void) {
    sh::game::Session& s = session();
    g_PcAltCamApplied = 0;
    // An attract demo is a recording of the stock game: nothing to steer.
    if (s.sys_flag(sh::game::sys_flag::kDemo)) {
        s_stood_down = true;
        field_of_view(true);
        return;
    }
    // A scripted scene owns the camera, except the fade-in after a load in first person: the eye comes from the
    // already placed head, so it snaps in instead of drifting from a stale camera.
    if (Pc_ScriptOwnsScene() && !only_bars_closing(s)) {
        const bool fps_through_load = sh::input::camera_styles().first_person() && (sh::screens::fade_status() & 0x7) >= kFadeInStart &&
                                      !s.sys_flag(sh::game::sys_flag::kCutscene) &&
                                      s.system.flow.cutscene_border == sh::game::kCutsceneBorderNone;
        if (!fps_through_load) {
            s_stood_down = true;
            field_of_view(true);
            return;
        }
    }
    cam::AltCamera* c = camera();
    if (c == nullptr) {
        field_of_view(true);
        return;
    }

    const sh::entities::CharacterRecord& harry = s.system.characters.player;
    const sh::game::PadState& pad = s.game.pads[0];
    cam::AltCameraInput in;
    in.style = sh::input::camera_styles().first_person() ? cam::AltStyle::FirstPerson
                          : (sh::input::camera_styles().style() == sh::input::CameraStyle::OverShoulder ? cam::AltStyle::OverShoulder : cam::AltStyle::ThirdPerson);
    // A scene leaves Harry facing somewhere new and a warp puts him somewhere new: the view returns at once behind
    // him (first person: in his eyes), looking where he looks, instead of travelling there from the old pose.
    bool warped = false;
    if (s_have_last) {
        const std::int64_t dx = std::int64_t{harry.position.x} - s_last_harry.x;
        const std::int64_t dy = std::int64_t{harry.position.y} - s_last_harry.y;
        const std::int64_t dz = std::int64_t{harry.position.z} - s_last_harry.z;
        warped = dx * dx + dy * dy + dz * dz > kWarpDistance * kWarpDistance;
    }
    s_last_harry = harry.position;
    s_have_last = true;
    const bool reset_asked = sh::input::camera_styles().take_camera_reset();
    in.reset = reset_asked || s_stood_down || warped;
    s_stood_down = false;
    in.yaw = g_TpsCamYaw;
    in.pitch = g_TpsCamPitch;
    in.harry = cam::AltVec3{harry.position.x, harry.position.y, harry.position.z};
    in.harry_yaw = harry.rotation.y;
    in.head = bone(kBoneHead);
    in.arms[0] = bone(kBoneLeftForearm);
    in.arms[1] = bone(kBoneLeftHand);
    in.arms[2] = bone(kBoneRightForearm);
    in.arms[3] = bone(kBoneRightHand);
    // Aim follows the aim state and the raw aim input (combat churn can drop the state for a frame).
    in.aiming = s.system.combat.aiming || s.player.controls.aim;
    in.melee_attack = s.system.combat.weapon_attack != kNoAttack && s.system.combat.weapon_attack < kFirstRangedAttack;
    in.rear_look = g_PcRearLookActive != 0;
    in.shoulder_side = sh::input::camera_styles().shoulder_side();
    in.frozen = g_PcConsoleInputActive || g_PcDebugMenuActive;
    // Always drained, also while frozen, so the view does not jump when the console closes.
    Sh_RelativeMouseState(&in.mouse_dx, &in.mouse_dy);
    in.right_x = pad.raw.right_x;
    in.right_y = pad.raw.right_y;
    in.left_x = pad.raw.left_x;
    in.left_y = pad.raw.left_y;
    in.in_gameplay = s.game.game_state == sh::game::game_state::kInGame && s.sys_state() == sh::game::sys_state::kGameplay;
    in.delta_time = sh::loop::frame_delta();
    in.options = options(sh::input::camera_styles().style() == sh::input::CameraStyle::OverShoulder);
    // The head look delay only reads the walking input and the clock when the head look runs; the orbit's recenter
    // reads the walking input.
    if (sh::input::camera_styles().first_person() && in.options.head_tracking) {
        in.now_ms = SDL_GetTicks();
        in.moving = walking();
    } else if (!sh::input::camera_styles().first_person()) {
        in.moving = walking();
    }

    LevelRays rays;
    const cam::AltCameraOutput out = c->update(in, rays);

    g_TpsCamYaw = out.yaw;
    g_TpsCamPitch = out.pitch;
    if (out.aim_device >= 0) {
        g_PcAimDevice = out.aim_device;
    }
    g_PcFpsSwingHeadShow = out.head_visible ? 1 : 0;
    if (out.first_person) {
        g_PcFpsHeadRefDbg = vec(out.head_reference);
        g_PcFpsHeadLocalDbg = vec(out.head_local);
        g_PcCamAppliedPos = vec(out.eye);
        g_PcFpsViewFwd = vec(out.view_forward);
        g_PcFpsEyePos = vec(out.eye);
    }
    g_TpsCamPos = vec(out.aim_origin);
    g_TpsCamFwd = vec(out.aim_forward);
    const std::int32_t eye[3] = {out.eye.x, out.eye.y, out.eye.z};
    const std::int32_t at[3] = {out.look_at.x, out.look_at.y, out.look_at.z};
    sh::app::view::view_look_at(eye, at);
    field_of_view(false);
    g_PcAltCamApplied = 1;
}
