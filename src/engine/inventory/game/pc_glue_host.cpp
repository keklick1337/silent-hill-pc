// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The PC features around the inventory over the session: the quick actions' host (src/engine/items/quick_actions.h),
// the health screen cues' place in the frame (src/engine/screens/health_cues.h), the free-aim assist's candidates
// and the player bullet's hitbox globals.

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "audio/game/game_sound_driver.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/config/gameplay_config.h"
#include "engine/config/pc_config.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/effects/map_lighting.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/game/inventory_host.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/inventory/item_catalog.h"
#include "engine/inventory/item_view.h"
#include "engine/items/quick_actions.h"
#include "engine/vfs/vfs_data_json.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/player/player_records.h"
#include "engine/runtime/game_hooks.h"
#include "engine/screens/health_cues.h"
#include "engine/screens/screens_view.h"
#include "engine/world/aim_assist.h"
#include "engine/world/game/character_world.h"
#include "input/pc_quick_actions.h"
#include "render/draw2d/draw2d.h"
#include "engine/loop/frame_view.h"
#include "render/gpu/render_state.h"
#include "engine/mods/data_reload.h"

extern int g_PcDebugMenuActive;
extern int g_DebugAnimKfView;
extern int g_PcAimDevice;

extern "C" {
int Pc_ConsoleIsOpen(void);
bool Player_ItemRemove(int item, int count);
void Player_WeaponModelStateSet(std::int32_t held_attack, std::int8_t event);
void Gfx_PlayerHeldItemAttach(std::int32_t weapon_attack);
std::int32_t WorldGfx_PlayerPrevHeldItem(sh::game::PlayerCombat* combat);
void WorldGfx_HeldItemShow(void);
std::uint32_t Ray_TraceQuery(sh::world::game::RayTraceRecord* out, const sh::world::Vec3* from,
                             const sh::world::Vec3* to);

// The player bullet's "fat hitbox" (bullet_fat_hitbox): while the player's gun ray is traced (set around the trace by
// the combat view) the enemy's narrow bullet cylinder grows by these radius-relative amounts, so the whole visible
// body is hittable; the aim assist clamps its aim point to the same span.
std::int32_t g_PcBulletHitActive = 0;
std::int32_t g_PcBulletVertMul = 2 * 4096;  // vertical reach added each side = 2x collision radius
std::int32_t g_PcBulletRadMul = 4096;       // horizontal radius added = 1x collision radius
}

namespace {

namespace inv = sh::inventory;
using sh::game::save_record;
using sh::game::session;

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr std::uint16_t kHealSound = 1325;
constexpr int kUpperBodySidestepRightStumble = 18;
constexpr int kUpperBodyReload = 26;
constexpr int kNoNpc = -1;
// The weapons' own rows of the attack table count up by 10 per input; weapon_on_open keeps a row.
constexpr int kInputRowStride = 10;
constexpr std::int32_t kInGame = 11;

bool in_gameplay() { return session().game.game_state == kInGame && session().sys_state() == sh::game::sys_state::kGameplay; }

sh::game::PlayerCombat& combat() { return session().system.combat; }

const sh::items::ItemLibrary* items() { return inv::item_catalog().library(); }

// The weapon carried as item `item` (weapons/<Name>.json "item"), nullptr for anything else.
const sh::items::WeaponDef* weapon_of(int item)
{
    return items() != nullptr && item > 0 ? items()->weapon_of_item(static_cast<std::uint16_t>(item)) : nullptr;
}

// The weapon numbered `number`, nullptr for none.
const sh::items::WeaponDef* weapon_numbered(int number)
{
    return items() != nullptr ? items()->find_weapon(number) : nullptr;
}

class GameQuickActions final : public sh::items::QuickActionsHost {
public:
    bool player_in_control() override { return in_gameplay() && session().player.control_disabled == 0; }
    bool action_safe() override
    {
        return player_in_control() && session().system.characters.player_upper.upper_state <= kUpperBodySidestepRightStumble;
    }
    bool reload_possible() override
    {
        const sh::game::PlayerCombat& c = combat();
        // A gun in hand (its weapon file's class) with ammo left and room in the magazine.
        const sh::items::WeaponDef* in_hand = weapon_numbered(c.weapon_attack);
        return in_hand != nullptr && in_hand->is_gun() && c.ammo_total != 0 && is_gun(save_record().equipped_weapon) &&
               c.ammo_loaded < inv::item_catalog().gun_clip(c.weapon_attack) &&
               session().system.characters.player_upper.upper_state != kUpperBodyReload;
    }
    std::int32_t frame_time() override { return sh::loop::frame_delta(); }
    bool console_open() override { return Pc_ConsoleIsOpen() != 0; }
    bool debug_menu_open() override { return g_PcDebugMenuActive != 0; }
    bool keyframe_viewer() override { return g_DebugAnimKfView != 0; }
    bool quick_weapons_enabled() override { return g_PcQuickWeaponsEnabled != 0; }

    int slot_count() override { return sh::game_state::inventory_shown_slots(); }
    int slot_item(int slot) override { return sh::game_state::inventory_slot(slot).item; }
    int slot_amount(int slot) override { return sh::game_state::inventory_slot(slot).count; }
    int equipped_item() override { return save_record().equipped_weapon; }
    int item_number(std::string_view name) override
    {
        const int n = sh::ids::item_number(std::string(name).c_str());
        return n > 0 ? n : -1;
    }
    const sh::items::QuickActionsData& quick_data() override
    {
        static sh::items::QuickActionsData s_data;
        static std::uint32_t s_data_epoch = sh::mods::kNeverRead;
        if (sh::mods::data_stale(s_data_epoch)) {
            s_data = [] {
                const std::optional<std::string> text = ShVfs_ReadDataJsonText("config/quick_actions.json");
                if (!text)
                {
                    SH_ERR("[QUICK] gamedata/config/quick_actions.json is missing: no quick weapons, cycle or heal");
                    return sh::items::QuickActionsData{};
                }
                sh::Result<sh::items::QuickActionsData> parsed = sh::items::QuickActionsData::parse(*text);
                if (!parsed)
                {
                    SH_ERR("[QUICK] %s", parsed.error().c_str());
                    return sh::items::QuickActionsData{};
                }
                return std::move(parsed.value());
            }();
        }
        return s_data;
    }
    // The items of the "guns" group (the guns and the scene props filed with them).
    bool is_gun(int item) override
    {
        const sh::items::ItemDef* def = item > 0 ? inv::game::item_def(item) : nullptr;
        return def != nullptr && def->group == "guns";
    }
    // The gun's "ammo" item; -1 for an item no weapon file gives ammo.
    int ammo_item(int item) override
    {
        const sh::items::WeaponDef* w = weapon_of(item);
        if (w == nullptr || w->ammo.empty())
            return -1;
        const int n = sh::ids::item_number(w->ammo.c_str());
        return n > 0 ? n : -1;
    }
    bool fills_saturation(int item) override
    {
        const sh::items::ItemDef* def = inv::game::item_def(item);
        return def != nullptr && def->inventory.fills_saturation;
    }

    void equip(int item, int slot, int ammo, int reserve) override
    {
        const int id = item;
        const sh::items::WeaponDef* weapon = weapon_of(item);
        if (weapon == nullptr || weapon->number < 0)
        {
            SH_ERR("[QUICK] item %d is no weapon with a number; not equipped", item);
            return;
        }
        // Silence the outgoing weapon as opening the inventory does (the chainsaw kept running otherwise).
        Player_WeaponModelStateSet(0, -1);
        inv::inventory_menu().equipped_item = id;
        save_record().equipped_weapon = id;
        combat().weapon_attack = weapon->number;
        combat().weapon_slot = static_cast<std::int8_t>(slot);
        combat().ammo_loaded = ammo;
        combat().ammo_total = reserve;
        sh::game::player().update_weapon();

        // The held model too, as the inventory exit does; the model binds when its read lands.
        Gfx_PlayerHeldItemAttach(combat().weapon_attack);
        WorldGfx_PlayerPrevHeldItem(&combat());
        WorldGfx_HeldItemShow();

        // Switching off a gas weapon (weapons/<Name>.json "gas": the chainsaw, the drill) stops its running state
        // (the drill's smoke), as the inventory's exit does; the stage's weapon_on_open still names the outgoing
        // weapon's row here, and the row past the input is the weapon (the exit's own reading, guns included).
        const int prev = inv::item_stage().weapon_on_open % kInputRowStride;
        const sh::items::WeaponDef* outgoing = weapon_numbered(prev);
        if (outgoing != nullptr && outgoing->gas && combat().weapon_attack != prev)
        {
            sh::entities::CharacterRecord& player = session().system.characters.player;
            player.attack.phase = 0;
            sh::game::harry_vars(player).gas_weapon_timer = 0;
        }
    }

    std::int32_t health() override { return session().system.characters.player.health; }
    void set_health(std::int32_t health) override { session().system.characters.player.health = health; }
    void set_saturation(std::int32_t saturation) override { save_record().health_saturation = saturation; }
    std::int32_t heal_amount(int item) override { return Gameplay_HealAmountQ12(item); }
    std::int32_t max_health() override { return Gameplay_MaxHealthQ12(); }
    std::int32_t max_saturation() override { return Gameplay_MaxSaturationQ12(); }
    bool quick_heal_allowed(int item, std::int32_t health, std::int32_t& amount) override
    {
        int heal = amount;
        const bool allowed = sh::hooks::game_quick_heal(item, health, heal);
        amount = heal;
        return allowed;
    }
    bool spend_item(int item) override { return Player_ItemRemove(item, 1); }
    void heal_feedback() override
    {
        sh::audio::game_sound_driver().play_effect(kHealSound, -0x40, 0x40);  // the inventory heal's sound
        sh::screens::heal_flash().start();
    }

    void toggle_flashlight() override { sh::effects::map_lighting().toggle_flashlight(); }
    void set_quick_turn_request(bool request) override { g_PcQuickTurnRequest = request ? 1 : 0; }

    void log(const std::string& line) override { SH_DBG("%s", line.c_str()); }
    void toast(const std::string& line) override { sh::debug::console_notice(line.c_str()); }
};

// Packet identities of the cues' commands in the fade layer (nothing is built there).
struct CueKeys {
    unsigned char flash[2][2][16];
    unsigned char glow[2][5][16];
} s_cue_keys;

void submit_cue(const sh::screens::CueCommands& cue, unsigned char (*keys)[16])
{
    for (int i = 0; i < cue.count; i++)
        sh::screens::link_2d(sh::screens::Table2d::Hud, 4, keys[i], cue.list[static_cast<std::size_t>(i)]);
}

int frame_buffer() { return sh::game_state::frame_buffer_index(); }

struct AimOcclusion {
    const sh::world::Vec3* cam;
};

int aim_occluded(void* ctx, std::int32_t ax, std::int32_t ay, std::int32_t az, std::int32_t free_dist)
{
    sh::world::game::RayTraceRecord occ{};
    const sh::world::Vec3 aim{ax, ay, az};
    return Ray_TraceQuery(&occ, static_cast<AimOcclusion*>(ctx)->cam, &aim) && occ.distance < free_dist;
}

}  // namespace
namespace sh::items {

QuickActionsHost& game_quick_actions_host()
{
    static GameQuickActions s_host;
    return s_host;
}

}  // namespace sh::items

namespace sh::screens {

HealFlash& heal_flash()
{
    static HealFlash s_flash;
    return s_flash;
}

void run_heal_flash()
{
    const CueCommands cue = heal_flash().update(sh::loop::frame_delta(), kScreenWidth, kScreenHeight);
    submit_cue(cue, s_cue_keys.flash[frame_buffer()]);
}

void run_low_health_glow()
{
    static LowHealthGlow s_glow;
    LowHealthGlowInput in;
    in.enabled = g_PcConfig.lowHealthGlow != 0;
    in.gameplay = in_gameplay();
    in.cutscene = sh::render::frame_state().cutscene != 0 || sh::render::feedback_control().present_last_frame != 0;
    in.health = session().system.characters.player.health;
    in.frame_time = sh::loop::frame_delta();
    in.half_height = kScreenHeight / 2;
    in.half_width = glow_half_width(kScreenWidth, kScreenHeight, sh::render::picture().hor_plus != 0,
                                    PcConfig_RenderWidth(), PcConfig_RenderHeight(), sh::render::picture().pixel_aspect,
                                    sh::render::picture().world_h_scale);
    submit_cue(s_glow.update(in), s_cue_keys.glow[frame_buffer()]);
}

}  // namespace sh::screens

// ---- free-aim assist (TPS) ----
//
// Free aim casts one screen-centre ray, and an enemy only takes the hit when both that ray and the hand-to-target
// damage ray thread its narrow cylinder. Over an enemy's body (mouse) or near it (controller), the aim point snaps onto
// the enemy's vertical axis at the aimed height. The choice is the engine's (sh::world::aim_assist_pick); this turns the
// NPC records into candidates with the bullet trace's own filters, and answers the occlusion query
// with the level geometry ray. Returns the NPC index or -1.
extern "C" std::int32_t Pc_AimAssistFind(const std::int32_t* cam_pos, const std::int32_t* cam_fwd, std::int32_t range,
                                         std::int32_t* aim_point)
{
    constexpr int kNpcs = sh::entities::kNpcSlotCount;
    sh::world::AimAssistTarget targets[kNpcs];
    std::int32_t npc_index[kNpcs];
    std::int32_t count = 0;

    for (int i = 0; i < kNpcs; i++)
    {
        const sh::entities::CharacterRecord& npc = session().system.characters.npcs[i];
        const sh::chara::CharacterDef* type = sh::chara::type_def(npc.model.chara_id);
        if (type == nullptr || !type->enemy)
            continue;  // damageable enemies only (character.json "enemy"), as the gun rays
        if (npc.collision.state() == 0)
            continue;  // downed but alive enemies (state 1) still take the snap
        if (npc.health <= 0)
            continue;

        const std::int32_t radius = npc.collision.cylinder.chara_radius;
        if (radius <= 0)
            continue;

        const std::int32_t ya = npc.position.y + npc.collision.box.top;
        const std::int32_t yb = npc.position.y + npc.collision.box.height;

        targets[count].x = npc.position.x + npc.collision.offsets.box.x;
        targets[count].z = npc.position.z + npc.collision.offsets.box.z;
        targets[count].y_low = ya < yb ? ya : yb;
        targets[count].y_high = ya < yb ? yb : ya;
        targets[count].radius = radius;
        npc_index[count] = i;
        count++;
    }

    sh::world::AimAssistParams params;
    params.magnetic_cone = g_PcAimDevice == 1;
    params.range_q12 = range;
    params.expand_vertical = g_PcConfig.bulletFatHitbox != 0;
    params.expand_mul_q12 = g_PcBulletVertMul;
    const sh::world::Vec3 cam{cam_pos[0], cam_pos[1], cam_pos[2]};
    AimOcclusion occ{&cam};

    const sh::world::AimAssistResult res =
        sh::world::aim_assist_pick(targets, static_cast<std::size_t>(count), cam_pos[0], cam_pos[1], cam_pos[2],
                                   cam_fwd[0], cam_fwd[1], cam_fwd[2], params, aim_occluded, &occ);
    if (res.index < 0)
        return kNoNpc;
    aim_point[0] = res.aim_x;
    aim_point[1] = res.aim_y;
    aim_point[2] = res.aim_z;
    return npc_index[res.index];
}
