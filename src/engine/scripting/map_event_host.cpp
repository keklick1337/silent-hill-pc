// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The host of the map event binds (map_event_api.h) over the running game: the session's character records, the
// map variables, the numbered file buffers, the engine's effect pools and the C entry points of other modules. The
// item close-up view, the GPU lights and the 2D gouraud packets go through script_game_link.h.

#include "engine/loader/active_map.h"
#include "engine/scripting/map_event_api.h"

#include <cstring>

#include "engine/chara/runtime/chara_models.h"
#include "engine/effects/named_effects.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/runtime/cell_stream_runtime.h"
#include "engine/script/named_api.h"
#include "engine/script/named_api_calls.h"
#include "engine/scripting/script_buffers.h"
#include "engine/scripting/script_game_link.h"
#include "engine/app/loader/game_files.h"
#include "engine/scripting/cutscene_calls.h"

extern "C" {
std::uint32_t Ipd_ChunkInitCheck(void);
void GameFs_FlameGfxLoad(void);
void vwGetViewPosition(std::int32_t* out);
std::int8_t Vc_StereoBalanceGet(const std::int32_t* at);
void Savegame_EnemyStateUpdate(void* chara);
void Chara_EffectSpawn(void* chara, std::int32_t* at, std::int32_t kind, std::int32_t variant);
void Player_EmptyWeaponHandSet(void);
void Player_WeaponAttackRestore(void);
void Player_SceneWeaponHide(void);
void Player_CutsceneWeaponUnequip(void);
void Gfx_MapEffectsUpdate(std::int32_t preset0, std::int32_t preset1, int value_type, void* value, std::int32_t min,
                          std::int32_t max);
void Event_PaperMapCmd(std::int32_t cmd, std::int32_t paper_map);
void Event_PathWaypointExecuteChara(void* chara, std::int32_t anim, std::int32_t count, std::uint32_t sub_step);
void Screen_BackgroundImgTransition(void* from, void* to, std::int32_t alpha);
void StaffRoll_StartFlat(std::int32_t ending);
int StaffRoll_MusicStarted(void);
int StaffRoll_Finish(void);
void Gfx_Primitive2dTextureSet(std::int32_t x, std::int32_t y, std::int32_t depth, std::int32_t blend);
void Bgm_PlayNewTrack(std::int32_t track);
}

namespace {

namespace ss = sh::scripting;
namespace gg = sh::gameflow::game;
namespace link = sh::scripting::link;

constexpr int kNoValue = -1;

sh::entities::CharacterRecord* chara(int slot) {
    return sh::game::session().system.characters.record(slot);
}

class MapEventsHost final : public ss::MapEventHost {
public:
    void* map_state(const char* name) override {
        void* p = PcScriptNamed_VarAddr(name);
        return p == nullptr && PcScriptNamed_RawApiEnabled() ? sh::loader::active_map_symbol(name) : p;
    }
    void* map_data(const char* symbol) override {
        return sh::loader::active_map_symbol(symbol);
    }
    void* image(const char* name) override {
        if (std::strcmp(name, "item_inspection_img") == 0) {
            return link::picture_record(link::Picture::ItemInspection);
        }
        if (std::strcmp(name, "bg_texture_img") == 0) {
            return link::picture_record(link::Picture::EventSecondary);
        }
        return PcScriptNamed_VarAddr(name);
    }

    int file_read(int file, int buffer) override {
        void* buf = ss::file_buffer(buffer);
        return buf != nullptr ? sh::loader::queue_read(file, buf) : kNoValue;
    }
    void file_wait() override { sh::loader::queue_wait_empty(); }
    int file_seek(int file) override { return sh::loader::queue_seek(file); }
    bool file_loaded(int queue_index) override { return sh::loader::queue_loaded(queue_index) != 0; }
    int file_queue_length() override { return sh::loader::queue_length(); }
    bool chunks_ready() override { return Ipd_ChunkInitCheck() != 0; }
    void load_flame_texture() override { GameFs_FlameGfxLoad(); }
    void world_chunk_set(int file, int cell_x, int cell_z) override {
        sh::world::stream::place_file_at(static_cast<std::int16_t>(file), cell_x, cell_z);
    }

    void cutscene_start(int buffer) override {
        if (void* buf = ss::file_buffer(buffer)) {
            sh::scripting::cutscene_start(buf);
        }
    }
    void cutscene_chara_apply(int slot, const char* track, std::int32_t time, int buffer) override {
        sh::entities::CharacterRecord* c = chara(slot);
        void* buf = ss::file_buffer(buffer);
        if (c != nullptr && buf != nullptr) {
            PcCutscene_CharacterApply(c, track, time, buf);
        }
    }
    void cutscene_camera_apply(std::int32_t time, int buffer) override {
        // The map's camera targets where the map has them, a local elsewhere (the user camera copies them).
        static std::int32_t s_pos[3];
        static std::int32_t s_look[3];
        void* buf = ss::file_buffer(buffer);
        if (buf == nullptr) {
            return;
        }
        auto* pos = static_cast<std::int32_t*>(sh::loader::active_map_symbol("cutscene_camera_position"));
        auto* look = static_cast<std::int32_t*>(sh::loader::active_map_symbol("cutscene_camera_look_at"));
        pos = pos != nullptr ? pos : s_pos;
        look = look != nullptr ? look : s_look;
        vcChangeProjectionValue(static_cast<std::int16_t>(sh::scripting::cutscene_camera_pose(pos, look, nullptr, time, buf)));
        vcUserCamTarget(pos, nullptr, 1);
        vcUserWatchTarget(look, nullptr, 1);
    }

    void camera_target(const ss::Point& p, bool warp) override {
        std::int32_t v[3] = {p.x, p.y, p.z};
        vcUserCamTarget(v, nullptr, warp ? 1 : 0);
    }
    void camera_watch(const ss::Point& p, bool warp) override {
        std::int32_t v[3] = {p.x, p.y, p.z};
        vcUserWatchTarget(v, nullptr, warp ? 1 : 0);
    }
    ss::Point view_position() override {
        std::int32_t v[3];
        vwGetViewPosition(v);
        return {v[0], v[1], v[2]};
    }
    int stereo_balance(const ss::Point& p) override {
        const std::int32_t v[3] = {p.x, p.y, p.z};
        return Vc_StereoBalanceGet(v);
    }
    void flashlight_fix() override { Game_FlashlightAttributesFix(); }

    std::optional<std::int32_t> chara_field(int slot, ss::CharaField f) override {
        const sh::entities::CharacterRecord* c = chara(slot);
        if (c == nullptr) {
            return std::nullopt;
        }
        switch (f) {
            case ss::CharaField::Health: return c->health;
            case ss::CharaField::MoveSpeed: return c->move_speed;
            case ss::CharaField::FallSpeed: return c->fall_speed;
            case ss::CharaField::ControlState: return c->model.control_state;
            case ss::CharaField::StateStep: return c->model.control_step;
            case ss::CharaField::AnimTime: return c->model.anim.time;
            case ss::CharaField::AnimStatus: return c->model.anim.status;
            case ss::CharaField::AnimKeyframe: return c->model.anim.keyframe;
            case ss::CharaField::AnimFlags: return c->model.anim.flags;
            case ss::CharaField::CollisionState: return c->collision.state();
            case ss::CharaField::Flags: return c->flags;
            case ss::CharaField::CharaId: return c->model.chara_id;
            default: return std::nullopt;
        }
    }
    bool set_chara_field(int slot, ss::CharaField f, std::int32_t value) override {
        sh::entities::CharacterRecord* c = chara(slot);
        if (c == nullptr) {
            return false;
        }
        switch (f) {
            case ss::CharaField::Health: c->health = value; break;
            case ss::CharaField::MoveSpeed: c->move_speed = value; break;
            case ss::CharaField::FallSpeed: c->fall_speed = value; break;
            case ss::CharaField::ControlState: c->model.control_state = static_cast<std::uint8_t>(value); break;
            case ss::CharaField::StateStep: c->model.control_step = static_cast<std::uint8_t>(value); break;
            case ss::CharaField::AnimTime: c->model.anim.time = value; break;
            case ss::CharaField::AnimStatus: c->model.anim.status = static_cast<std::uint8_t>(value); break;
            case ss::CharaField::AnimKeyframe: c->model.anim.keyframe = static_cast<std::int16_t>(value); break;
            case ss::CharaField::AnimFlags: c->model.anim.flags = static_cast<std::uint16_t>(value); break;
            case ss::CharaField::CollisionState: c->collision.set_state(value); break;
            case ss::CharaField::Flags: c->flags = static_cast<std::int16_t>(value); break;
            case ss::CharaField::CharaId: c->model.chara_id = static_cast<std::int32_t>(value); break;
            default: return false;
        }
        return true;
    }
    void chara_model_clear(int slot) override {
        if (sh::entities::CharacterRecord* c = chara(slot)) {
            Chara_ModelCharaIdClear(c, 0, 0);
        }
    }
    void chara_motion_reset(int slot) override {
        if (sh::entities::CharacterRecord* c = chara(slot)) {
            // Harry's names on any record: the store's words are shared (CharacterVars).
            sh::entities::HarryVars& v = c->vars.harry;
            v.flex_target_x = 0;
            v.flex_source = 0;
            v.ground_height = 0;
            v.run_ahead = 0;
        }
    }
    int chara_anim_start_keyframe(int slot) override {
        sh::entities::CharacterRecord* c = chara(slot);
        return c != nullptr ? link::chara_anim_start_keyframe(c) : 0;
    }
    void chara_anim_unlock_play_to_end(int slot) override {
        if (sh::entities::CharacterRecord* c = chara(slot)) {
            Event_CharaAnimUnlockPlayToEnd(c);
        }
    }
    // Chara_BonesInit(index): the model anims of NPC group `index` sit one past the player's (slot 0).
    void chara_bones_init(int index) override { gg::chara_bones_init(index + 1); }
    void chara_path_walk(int slot, int anim, int count, bool advance_step) override {
        // Maps without NPC waypoint walking have no walker.
        if (link::chara_path_walker_present()) {
            Event_PathWaypointExecuteChara(chara(slot), anim, count, advance_step ? 1u : 0u);
        }
    }
    int chara_anim_playback_state(int slot) override {
        return gg::overlay_anim(gg::AnimOp::PlaybackState, chara(slot), slot == 0, 0);
    }
    bool chara_anim_reset(int slot, bool to_still) override { return link::chara_anim_reset(chara(slot), to_still); }
    void chara_remove_saved(int slot) override {
        sh::entities::CharacterRecord* c = chara(slot);
        c->model.chara_id = 0;  // no character
        Savegame_EnemyStateUpdate(c);
    }
    void chara_effect_spawn(int slot, const ss::Point& at, int kind, int variant) override {
        std::int32_t pos[4] = {at.x, at.y, at.z, 0};
        Chara_EffectSpawn(chara(slot), pos, kind, variant);
    }
    void chara_model_buffers_assign(int group_mask) override {
        sh::chara::chara_models().assign_buffers(static_cast<std::uint8_t>(group_mask));
    }
    int npc_slot_count() override { return sh::entities::kNpcSlotCount; }

    void weapon_stash() override { Player_EmptyWeaponHandSet(); }
    void weapon_restore() override { Player_WeaponAttackRestore(); }
    bool weapon_hide() override {
        Player_SceneWeaponHide();
        return true;
    }
    bool weapon_unequip() override {
        Player_CutsceneWeaponUnequip();
        return true;
    }
    void extra_anim_flags_set(std::int32_t flags) override {
        auto& anim_flags = sh::game::session().system.characters.player_upper.model.anim.flags;
        anim_flags = static_cast<std::uint16_t>(anim_flags | flags);
    }
    bool item_remove(int item, int count) override {
        return Player_ItemRemove(static_cast<int>(item), static_cast<int>(count)) != 0;
    }

    void map_effects_init(int a, int b) override { Gfx_MapInitMapEffectsUpdate(a, b); }
    void map_effects_update(int a, int b, int primitive, void* data, int c, int d) override {
        Gfx_MapEffectsUpdate(a, b, primitive, data, c, d);
    }
    void weather_update(int a, int map, int phase) override { Map_WeatherUpdate(a, map, phase); }
    void message_with_audio(bool map_variant, int message, std::uint8_t* index, const std::uint16_t* cmds) override {
        if (map_variant) {
            Map_MessageWithAudio(message, index, cmds);
        } else {
            Event_DisplayMapMsgWithAudio(message, index, cmds);
        }
    }
    void message_dimmed(int file, int fade0, int fade1, int message0, int message1) override {
        Event_DisplayMapMsgWithDimmedBg(file, fade0, fade1, message0, message1);
    }
    void paper_map_cmd(int cmd, int paper_map) override { Event_PaperMapCmd(cmd, paper_map); }
    void math_point_from_angle(const ss::Point& p, std::int32_t angle, int a, int b) override {
        std::int32_t v[3] = {p.x, p.y, p.z};
        Math_PointFromAngleCalc(v, static_cast<std::int16_t>(angle), a, b);
    }

    void bg_transition(void* from, void* to, std::int32_t alpha) override {
        Screen_BackgroundImgTransition(from, to, static_cast<std::int16_t>(alpha));
    }
    void credits_roll_start(int ending) override { StaffRoll_StartFlat(ending); }
    bool credits_roll_music_started() override { return StaffRoll_MusicStarted() != 0; }
    bool credits_roll_finish() override { return StaffRoll_Finish() != 0; }

    void item_view_read(int tim, int tmd) override { link::item_view_read(tim, tmd); }
    void item_view_camera_init() override { link::item_view_camera_init(); }
    void item_view_models_map() override { link::item_view_models_map(); }
    void item_view_model_link(int slot, int object) override { link::item_view_model_link(slot, object); }
    bool item_view_model_linked(int slot) override { return link::item_view_model_linked(slot); }
    ss::Point item_view_get(int slot, ss::ItemViewField f) override {
        std::int32_t v[3];
        link::item_view_get(slot, static_cast<int>(f), v);
        return {v[0], v[1], v[2]};
    }
    void item_view_set(int slot, ss::ItemViewField f, const ss::Point& v, int mask) override {
        const std::int32_t xyz[3] = {v.x, v.y, v.z};
        link::item_view_set(slot, static_cast<int>(f), xyz, mask);
    }
    bool item_view_model_rotate(int slot) override { return link::item_view_model_rotate(slot); }
    void item_view_begin() override { link::item_view_begin(); }
    void item_view_finish() override { link::item_view_finish(); }
    void item_view_model_draw(int slot, int mode) override { link::item_view_model_draw(slot, mode); }
    void item_view_precise_depth() override { link::item_view_precise_depth(); }

    void flat_light(int index, const ss::Point& d, int r, int g, int b) override {
        const std::int32_t dir[3] = {d.x, d.y, d.z};
        link::flat_light(index, dir, r, g, b);
    }
    void ambient_light(int r, int g, int b) override { link::ambient_light(r, g, b); }
    void light_mode(int mode) override { link::light_mode(mode); }
    void ui_gouraud(int depth, bool semi, int corners, const int (*p)[3]) override {
        link::ui_gouraud(depth, semi, corners, p);
    }
    void ui_draw_mode(int depth, int blend, int page_x, int page_y) override {
        Gfx_Primitive2dTextureSet(page_x, page_y, depth, blend);
    }
    void ui_frame() override { gg::keep_black_clear(); }

    void heat_field_start(const char* name, const ss::Point& at) override {
        sh::effects::heat_fields().start(name, {at.x, at.y, at.z});
    }
    void heat_field_start_between(const char* name, const ss::Point& from, const ss::Point& to) override {
        sh::effects::heat_fields().start_between(name, {from.x, from.y, from.z}, {to.x, to.y, to.z});
    }
    void heat_field_spawn(const char* name, const ss::Point& around, int count) override {
        sh::effects::heat_fields().spawn(name, {around.x, around.y, around.z}, count);
    }
    void heat_field_update(const char* name, bool advance) override { sh::effects::heat_fields().update(name, advance); }
    void fire_pool_attached_reset(const char* name) override { sh::effects::fire_pools().attached_reset(name); }
    void fire_pool_attach(const char* name) override { sh::effects::fire_pools().attach(name); }
    void fire_pool_draw_attached(const char* name, int slot) override {
        if (const void* bones = chara_bones(slot)) {
            sh::effects::fire_pools().draw_attached(name, bones);
        }
    }
    void bolt_pool_attach(const char* name, const char* owner, std::int32_t x, std::int32_t z) override {
        sh::effects::bolt_pools().attach(name, owner, {x, 0, z}, nullptr);
    }
    void bolt_pool_update(const char* name) override { sh::effects::bolt_pools().update(name); }
    void bgm_play_new(int track) override { Bgm_PlayNewTrack(track); }

private:
    // The bones the character of the slot is posed on: the player's own, an NPC's from the animation slot of its
    // character (the set its model is drawn with).
    static const void* chara_bones(int slot) {
        if (slot == 0) {
            return sh::game::session().system.characters.player_bones;
        }
        const sh::entities::CharacterRecord* c = chara(slot);
        return c != nullptr ? link::chara_anim_bones(c->model.chara_id) : nullptr;
    }
};

MapEventsHost s_host;

void bind_map_event_api(ShScriptGameApi* api) {
    ss::bind_map_event_api(*api, s_host);
}

// Registration order: sh_script.h.
const ShScriptGameBinderEntry s_ScriptBinder(320, bind_map_event_api);

}  // namespace
