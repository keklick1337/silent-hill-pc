// SPDX-License-Identifier: GPL-3.0-or-later
//
// The loading screen's host over the session (engine/gameflow/load_screen_run.h): Harry running in
// place behind the loading text (his pose, clip, light and camera), its background picture, the map's environment
// set when a map starts, and the bone transform helper the map code shares. Numbers: gamedata/config/load_screen.json.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/app/game_view.h"
#include "engine/app/pc_paths.h"
#include "engine/chara/game/bone_ops.h"
#include "engine/world/game/atmosphere_host.h"
#include "engine/entities/anim_player.h"
#include "engine/entities/character_record.h"
#include "engine/entities/clip_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/load_screen_run.h"
#include "engine/memory/work_buffers.h"
#include "engine/app/loader/game_files.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/maps/map_runtime.h"
#include "engine/app/loader/picture_places.h"
#include "engine/math/geometry_state.h"
#include "render/gpu/render_state.h"
#include "engine/mods/data_reload.h"

namespace {

using sh::entities::Angles;
using sh::entities::BoneCoord;
using sh::entities::BoneMatrix;
using sh::entities::ModelRecord;
using sh::entities::Vec3;

using ClipInfo = sh::entities::ClipRecord;

}  // namespace

extern "C" {
const char* PcPort_GetGameDataPath(void);

void Gfx_MapEffectsAssign(void);
void Map_WeatherEnvironmentSet(std::int8_t environment, std::uint32_t lightRequest);

void vcUserWatchTarget(Vec3* watch_tgt_pos, void* watch_prm_p, bool warp_watch_f);
void vcUserCamTarget(Vec3* cam_tgt_pos, void* cam_prm_p, bool warp_cam_f);
void vcMoveAndSetCamera(bool in_connect_f, bool change_debug_mode, bool for_f, bool back_f, bool right_f, bool left_f,
                        bool up_f, bool down_f);
void Game_SpotlightLoadScreenAttribsFix(void);
void Gfx_LoadScreenMapEffectsUpdate(std::int32_t arg0, std::int32_t arg1);
std::int32_t Pc_LoadScreenDt(void);
void Anim_PlaybackLoop(ModelRecord* model, void* anmHdr, BoneCoord* bones, ClipInfo* clip);
void Gfx_FlashlightUpdate(void);
void WorldGfx_CharaModelDraw(std::int32_t charaId, BoneCoord* boneCoords, std::int32_t arg2, std::int16_t timer,
                             std::int32_t arg4);
void Screen_BackgroundImgDraw(sh::loader::ImagePlace* image);
}

namespace {

constexpr std::int32_t kHarry = 1;
constexpr std::int32_t kLightFull = 4096;  // Q12(1.0)
constexpr std::uint16_t kAnimVisibleAndUnlocked = sh::entities::kAnimAdvances | sh::entities::kAnimPosed;

// The run cycle the loading screen loops (8 frames at 30 fps over keyframes 26..44).
ClipInfo s_run_clip = {Anim_PlaybackLoop, 0, 0, 0, {8 * 4096}, 26, 44};

const sh::gameflow::LoadScreenParams& params() {
    static sh::gameflow::LoadScreenParams p;
    static std::uint32_t p_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(p_epoch)) {
        p = [] {
            sh::gameflow::LoadScreenParams out = sh::gameflow::LoadScreenParams::builtin();
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::vector<std::string> warnings;
            std::string source;
            const sh::Result<std::string> text =
                sh::gameflow::read_config_file(PcPort_GetGameDataPath(), mods, "load_screen.json", &source, warnings);
            if (text) {
                const sh::Result<sh::gameflow::LoadScreenParams> parsed =
                    sh::gameflow::LoadScreenParams::parse(text.value());
                if (parsed) {
                    out = parsed.value();
                } else {
                    SH_ERR("[LOAD] config/load_screen.json: %s (built-in numbers)", parsed.error().c_str());
                }
            }
            return out;
        }();
    }
    return p;
}

sh::entities::CharacterSlots& characters() { return sh::game::session().system.characters; }

class LoadScreenHost final : public sh::gameflow::LoadScreenHost {
public:
    void area_player_position_update() override { sh::app::view::area_player_position_update(); }
    void camera_init() override { sh::app::view::map_camera_init(); }
    void collision_triggers_set() override { sh::app::view::collision_triggers_set(); }
    void player_placement(std::int32_t& x, std::int32_t& z, std::int32_t& yaw) const override {
        x = harry().position.x;
        z = harry().position.z;
        yaw = harry().rotation.y;
    }
    void camera_watch_target(const sh::gameflow::LoadScreenTarget& t) override {
        Vec3 v = {t.x, t.y, t.z};
        vcUserWatchTarget(&v, nullptr, true);
    }
    void camera_position_target(const sh::gameflow::LoadScreenTarget& t) override {
        Vec3 v = {t.x, t.y, t.z};
        vcUserCamTarget(&v, nullptr, true);
    }
    void spotlight_attribs_fix() override { Game_SpotlightLoadScreenAttribsFix(); }
    void map_effects_update() override { Gfx_LoadScreenMapEffectsUpdate(0, 0); }
    void player_pose(std::int32_t anim_time, std::int32_t height) override {
        harry().model.anim.flags |= kAnimVisibleAndUnlocked;
        characters().player_upper.disabled_bones = 0;
        harry().model.anim.time = anim_time;
        harry().position.y = height;
    }
    void player_status_save() override { s_run_clip.status = harry().model.anim.status; }
    void player_matrix_set() override { sh::chara::game::bones::place_root(*bones(), harry().position, harry().rotation); }
    void player_anim_flags_set() override {
        harry().model.anim.flags |= kAnimVisibleAndUnlocked;
        characters().player_upper.disabled_bones = 0;
    }
    void bones_reset() override {
        for (BoneCoord& b : characters().player_bones) {
            b.world_current = 0;
        }
    }
    void neutral_light_set(std::int32_t tint) override {
        sh::world::frame_light_set(0, kLightFull, 0, tint, tint, tint, 0);
    }
    void fog_off() override { sh::app::view::set_fog_enabled(false); }
    void flashlight_pass_off() override { sh::render::flashlight().active = 0; }
    std::int32_t screen_dt() const override { return Pc_LoadScreenDt(); }
    void anim_run(std::int32_t dt) override {
        std::int32_t& frame_dt = sh::loop::frame_delta();
        const std::int32_t saved = frame_dt;
        frame_dt = dt;
        Anim_PlaybackLoop(&harry().model, sh::memory::work_buffers().as<std::uint8_t>(sh::memory::WorkBuffer::PlayerBaseAnim), bones(), &s_run_clip);
        frame_dt = saved;
    }
    void camera_move() override { vcMoveAndSetCamera(true, false, false, false, false, false, false, false); }
    void flashlight_update() override { Gfx_FlashlightUpdate(); }
    void player_draw() override { WorldGfx_CharaModelDraw(kHarry, bones(), 1, 0, 0); }

private:
    static sh::entities::CharacterRecord& harry() { return characters().player; }
    static BoneCoord* bones() { return characters().player_bones; }
};

}  // namespace

extern "C" {

void Gfx_MapEffectsSet(std::int32_t unused) {
    (void)unused;
    Gfx_MapEffectsAssign();
    Map_WeatherEnvironmentSet(static_cast<std::int8_t>(sh::maps::map_runtime().weather()),
                              static_cast<std::uint32_t>(sh::maps::map_runtime().light_mode()));
}

}  // extern "C"

void sh::app::view::loading_screen_run(int screen) {
    // 1: Harry walking on the load screen, 2: its background picture; 3, the "STAGE" caption, draws nothing.
    if (screen == 1) {
        LoadScreenHost host;
        sh::gameflow::load_screen_player_frame(host, params());
    } else if (screen == 2) {
        Screen_BackgroundImgDraw(&sh::loader::picture_places().loading_screen);
    }
}
