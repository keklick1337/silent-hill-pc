// SPDX-License-Identifier: GPL-3.0-or-later
//
// The camera's view of the running game (camera_host.h): Harry and the NPC slots
// from the session's character records, the ground from the character world queries, the map place from the map
// runtime and the live save record, the frame geometry from the renderer.

#include <array>
#include <cstddef>
#include <cstring>
#include <string>

#include "engine/loader/map_list.h"
#include "engine/app/map_info_tables.h"
#include "engine/app/pc_paths.h"
#include "engine/camera/camera_config.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/view_system.h"
#include "engine/chara/game/chara_file_table.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/world/game/character_world.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"
#include "engine/loop/frame_view.h"

extern "C" {
// A fixed-angle shot is on screen (the renderer's projection-centre correction, render/gpu/render_state.h).
// The frame clock of the game loop (Q12 seconds) and the frame's vblank count.
const char* PcPort_GetGameDataPath(void);
// The running map type (map_events_host.cpp).
std::int32_t Map_TypeGet(void);
}

namespace {

namespace cam = ::sh::camera;
namespace ent = ::sh::entities;

constexpr int kHarry = 1;
constexpr int kCharaCount = 256;  // every run-time type number
constexpr std::size_t kCharaNumbers = 256;  // a character number is a byte
constexpr int kHarryHeadBone = 2;
constexpr std::size_t kTagLength = 4;

ent::CharacterSlots& characters() {
    return sh::game::session().system.characters;
}

cam::Vec3 vec3_of(const ent::Vec3& v) {
    return cam::Vec3{v.x, v.y, v.z};
}

class GameCameraHost final : public cam::CameraHost {
public:
    cam::Matrix& world_screen() override { return world_screen_; }

    cam::ScreenGeometry screen_geometry() override {
        cam::ScreenGeometry g;
        g.render_width = PcConfig_RenderWidth();
        g.render_height = PcConfig_RenderHeight();
        sh::gpu::Renderer* r = sh::gpu::renderer();
        g.pixel_aspect = r != nullptr ? r->targets().live_pixel_aspect() : 1.0f;
        g.h_scale = sh::render::picture().world_h_scale;
        g.v_scale = sh::render::picture().world_v_scale;
        g.v_shift = sh::render::picture().world_v_shift;
        return g;
    }

    cam::HorPlusWidths hor_plus_widths() override {
        cam::HorPlusWidths w;
        sh::gpu::Renderer* r = sh::gpu::renderer();
        float authored = 0.0f;
        float installed = 0.0f;
        w.valid = r != nullptr && r->targets().hor_plus_half_widths(&authored, &installed);
        w.half_43 = authored;
        w.half_wide = installed;
        return w;
    }

    std::int32_t delta_time() override { return sh::loop::frame_delta(); }
    std::int32_t vblanks() override { return sh::loop::frame_vblanks(); }
    std::string data_path() override { return PcPort_GetGameDataPath(); }

    std::string mods_path() override {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        return mods;
    }

    cam::HarryView harry() override {
        const ent::CharacterRecord& h = characters().player;
        cam::HarryView v;
        v.position = vec3_of(h.position);
        v.yaw = h.rotation.y;
        v.move_speed = h.move_speed;
        v.heading = h.heading;
        v.turn_speed = h.rotation_speed.y;
        v.anim_status = h.model.anim.status;
        return v;
    }

    cam::FrameRecord* harry_head() override {
        return &characters().player_bones[kHarryHeadBone];
    }

    int harry_clip(std::string_view name, int fallback) override {
        return Pc_CharaAnim(kHarry, std::string(name).c_str(), fallback);
    }

    // Which characters are enemies to the camera (camera.json "enemies", by name) and where it looks on them.
    void npcs(std::vector<cam::NpcView>& out) override {
        resolve_enemies();
        out.assign(ent::kNpcSlotCount, cam::NpcView{});
        for (std::size_t i = 0; i < ent::kNpcSlotCount; ++i) {
            const ent::CharacterRecord& c = characters().npcs[i];
            const int id = c.model.chara_id;
            cam::NpcView& npc = out[i];
            npc.present = id != 0;
            npc.camera_enemy = id >= 0 && enemy_[static_cast<std::size_t>(id)];
            npc.always_counts = id >= 0 && always_active_[static_cast<std::size_t>(id)];
            npc.excluded = (c.flags & ent::kFlagCameraExcluded) != 0;
            npc.passive = (c.flags & ent::kFlagDowned) != 0;
            npc.health = c.health;
            npc.dead_time = c.death_timer;  // before this frame's update, which the engine performs
            npc.position = vec3_of(c.position);
            if (id >= 0 && id < kCharaCount) {
                const sh::chara::game::CharaFileRow& row = sh::chara::game::chara_file_row(id);
                switch (static_cast<sh::chara::game::CameraAnchor>(row.camera_anchor)) {
                    case sh::chara::game::CameraAnchor::Ground:
                        npc.look_anchor = cam::NpcView::LookAnchor::Ground;
                        break;
                    case sh::chara::game::CameraAnchor::Camera:
                        npc.look_anchor = cam::NpcView::LookAnchor::Camera;
                        break;
                    default:
                        npc.look_anchor = cam::NpcView::LookAnchor::Character;
                        break;
                }
                npc.look_offset_y = static_cast<std::int32_t>(row.camera_offset_y_q8) * 16;  // Q8 -> Q12
            }
        }
    }

    void set_npc_dead_time(std::size_t slot, std::int32_t dead_time) override {
        characters().npcs[slot].death_timer = static_cast<std::uint16_t>(dead_time);
    }

    cam::GroundSample ground_at(std::int32_t x, std::int32_t z) override {
        const sh::world::game::SurfaceRecord s = sh::world::game::surface_at(x, z);
        cam::GroundSample out;
        out.found = s.ground_type != 0;
        out.height = s.height;
        return out;
    }

    // Where the player is, in the names the map camera rules (camera.json "map_rules") use: the map_info.json tag
    // of the running map type and which entry with that tag it is, the map and room.
    cam::MapPlace map_place() override {
        cam::MapPlace place;
        const std::int32_t type = Map_TypeGet();
        if (const sh::app::MapTypeRecord* record = sh::app::map_type_record(type)) {
            const char* tag = record->tag;
            place.map_type = std::string_view(tag, strnlen(tag, kTagLength));
            place.map_type_occurrence = 1;
            for (std::int32_t i = 0; i < type; ++i) {
                if (std::strncmp(sh::app::map_type_record(i)->tag, tag, kTagLength) == 0) {
                    ++place.map_type_occurrence;
                }
            }
        }
        const sh::game::SaveRecord& save = sh::game::save_record();
        place.map = sh::loader::map_name(save.map);
        place.room = save.room;
        return place;
    }

    void set_fixed_angle_shot(bool on) override { sh::render::frame_state().fixed_camera = on; }

private:
    // The names of camera.json turned into character numbers once (a mod's character gets its number here).
    void resolve_enemies() {
        if (enemies_resolved_) {
            return;
        }
        enemies_resolved_ = true;
        const cam::CameraConfig& config = cam::camera_config();
        const auto mark = [](const std::vector<std::string>& names, std::array<bool, kCharaNumbers>& out) {
            for (const std::string& name : names) {
                const int number = sh::ids::chara_number(name.c_str());
                if (number < 0 || static_cast<std::size_t>(number) >= kCharaNumbers) {
                    SH_ERR("[CAMERA] camera.json enemies: \"%s\" is no character (ignored)", name.c_str());
                    continue;
                }
                out[static_cast<std::size_t>(number)] = true;
            }
        };
        mark(config.enemy_characters, enemy_);
        mark(config.always_active_characters, always_active_);
    }

    cam::Matrix world_screen_{};  // the world-to-screen matrix the view code builds each frame
    bool enemies_resolved_ = false;
    std::array<bool, kCharaNumbers> enemy_{};
    std::array<bool, kCharaNumbers> always_active_{};
};

GameCameraHost s_host;
[[maybe_unused]] const bool s_registered = (cam::set_camera_host(&s_host), true);

}  // namespace
