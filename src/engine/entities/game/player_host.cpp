// The player's record in the running game:
// sh::game::PlayerRecord over the session's character slot 0 (CharacterRecord + UpperBodyRecord), Harry's own
// variable block, PlayerState, the progress record and the settings. What belongs to other modules is reached
// through the narrow C declarations below (plain types with the records' layouts): the clip table's playback
// functions, the walls, the map header's tables.

#include <SDL3/SDL_scancode.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <string>

#include "engine/loader/map_list.h"
#include "engine/runtime/world_map.h"
#include "engine/app/loader/game_files.h"
#include "engine/camera/view_system.h"
#include "engine/chara/behavior_host.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/config/gameplay_config.h"
#include "engine/config/pc_config.h"
#include "engine/entities/anim_bank.h"
#include "engine/entities/anim_player.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/item_catalog.h"
#include "engine/inventory/item_view.h"
#include "engine/items/item_library.h"
#include "engine/memory/work_buffers.h"
#include "engine/maps/map_rules.h"
#include "engine/vfs/vfs_data_json.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/loop/game_random.h"
#include "engine/player/player.h"
#include "engine/player/player_script.h"
#include "engine/player/player_state_vars.h"
#include "engine/script/sh_script.h"
#include "engine/timing/vblank.h"
#include "engine/world/ground_point.h"
#include "input/pc_quick_actions.h"

#include "engine/player/player_records.h"
#include "engine/loop/frame_view.h"
#include "engine/mods/data_reload.h"
#include "input/camera_style.h"

// --- other modules (plain-type declarations) ---
extern "C" {
extern sh::game::ClipTableEntry HARRY_BASE_ANIM_INFOS[256];
extern std::int32_t g_TpsCamYaw;
extern int g_PhantomRejectCount;
void Player_WeaponModelStateSet(std::int32_t state, std::int8_t weapon_attack);
void Game_TimerUpdate(void);
void Bone_CoordRotationSet(sh::entities::BoneCoord* bone, std::int16_t rot_z, std::int16_t rot_x, std::int32_t rot_y);
void WorldGfx_HeldItemAttach(int chara_id, std::int32_t model_bone);
void SD_Call(std::uint32_t command);
std::int32_t Inventory_HyperBlasterFunctionalTest(void);
std::int32_t Inventory_WeaponRigInfo(std::int32_t attack, sh::game::EquippedWeapon* info, std::int32_t* hand_reach);
std::int32_t Inventory_WeaponRigClips(std::int32_t attack, sh::game::ClipTableEntry* clips, std::int32_t slots);
std::int32_t Inventory_WeaponRigKeyframes(std::int32_t attack, sh::game::WeaponKeyframes* keyframes, std::int32_t slots);
const char* Inventory_WeaponRigAnimFile(std::int32_t attack);
void Collision_SurfaceGet(sh::game::GroundRecord* ground, std::int32_t x, std::int32_t z);
std::int32_t Collision_WallDetect(sh::game::WallResult* result, const std::int32_t* offset, void* chara);
// The map header's Harry tables.
const sh::game::ClipTableEntry* HarryMap_ClipTable(void);
int HarryMap_ReactionRow(int row, int* status, int* trigger, int* time, int* last);
void HarryMap_PushFeedback(std::int32_t dx, std::int32_t dz);
int Pc_SplitHead_PushTake(void);
int Pc_HarryReactions_RowScan(void);
int Pc_HarryReactions_RowImported(int index);
void Pc_HarryReactions_MapLoaded(void);
void Pc_SoundBanks_Reset(void);
// The map's player hooks.
bool Player_MapUpdate(const char* fn);
void Player_SceneControl(void);
void Player_SceneMove(void);
}

namespace {

// The rules of the map the save says Harry is on (gamedata/maps/<map>/overlay.json), read once per map.
const sh::maps::MapRules& save_map_rules() {
    static std::map<int, sh::maps::MapRules> s_rules;
    static std::uint32_t s_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(s_epoch)) {
        s_rules.clear();
    }
    const int map = sh::game::save_record().map;
    const auto known = s_rules.find(map);
    if (known != s_rules.end()) {
        return known->second;
    }
    sh::maps::MapRules rules;
    const char* name = sh::loader::map_name(map);
    if (name != nullptr && name[0] != '\0') {
        const std::string relative = std::string("maps/") + name + "/overlay.json";
        if (const std::optional<std::string> text = ShVfs_ReadDataJsonText(relative.c_str())) {
            std::string error;
            rules = sh::maps::parse_map_rules(*text, &error);
            if (!error.empty()) {
                SH_ERR("[MAPOVERLAY] %s: %s", relative.c_str(), error.c_str());
            }
        }
    }
    return s_rules.emplace(map, std::move(rules)).first->second;
}

}  // namespace

// The PC's developer flags (src/engine/app/loop/main_loop.cpp, src/input).
extern int g_DebugThirdPersonCam;
extern int g_PcInfiniteAmmo;
extern int g_SH_PostFireTrace;
extern int g_PcGodMode;
extern int g_PcRearLookActive;
extern int g_PcQuickTurnRequest;
extern int g_DebugNoWallCollision;
extern int g_DebugNoFloorCollision;
extern int g_DebugAnimKfView;
extern int g_DebugAnimKf;
extern int g_DebugAnimKfMax;
extern int g_DebugAnimPlaying;
extern int g_DebugAnimKfStart;
extern int g_DebugAnimKfEnd;
extern std::int32_t g_DebugAnimRate;
extern int g_DebugViewNpcSlot;
extern int g_DebugAnimPlayGen;
extern "C" const unsigned char* g_sdlKeyboardState;

namespace {

namespace ent = sh::entities;
using sh::game::ClipTableEntry;
using sh::game::HarryVars;
using sh::game::PlayerVar;
using V = PlayerVar;

constexpr int kHarry = 1;
constexpr int kUnequipped = 0;
constexpr int kNothingHeld = -128;
constexpr int kSaveItemSlots = 40;
constexpr int kHarryBones = 18;
constexpr int kHarryClipSlots = 256;
constexpr int kWeaponClipFirst = 56;
constexpr int kWeaponClipSlots = 20;
constexpr int kWeaponKeyframeRecords = 10;
constexpr int kPlayerClipsShown = 76;
constexpr std::int32_t kNoCeiling = static_cast<std::int32_t>(0xFFFF0000u);
constexpr std::uint8_t kSaveModeTimeBonus = 0x06;  // the two "290 hours" bits of the mode byte
constexpr std::uint16_t kViewerFlags = 0x3;         // advances and visible

// The game's fixed buffers: Harry's base animation file, the weapon's animation, the map's Harry file.
void* harry_anim_file() { return sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerBaseAnim); }
void* weapon_anim_buffer() { return sh::memory::work_buffers().data(sh::memory::WorkBuffer::WeaponAnim); }
void* map_anim_buffer() { return sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerMapAnim); }

sh::game::Session& s() { return sh::game::session(); }
ent::CharacterRecord& body() { return s().system.characters.player; }
ent::UpperBodyRecord& upper() { return s().system.characters.player_upper; }
ent::BoneCoord* bones() { return s().system.characters.player_bones; }
HarryVars& vars() { return sh::game::harry_vars(body()); }
sh::game::SaveRecord& save() { return sh::game::save_record(); }
sh::game::Settings& settings() { return s().game.settings; }
const sh::game::PadState& pad() { return s().game.pads[0]; }

// The fields of the animation file header the record reads (bone count, keyframe count, bind poses).
struct AnimBindPose {
    std::int8_t parent;
    std::int8_t rotation_track;     // -1: the bone keeps the identity rotation
    std::int8_t translation_track;  // -1: the bone keeps its rest translation
    std::int8_t rest[3];
};
struct AnimFileHeader {
    std::uint8_t reserved[6];
    std::uint8_t bone_count;
    std::uint8_t reserved2[9];
    std::uint16_t keyframe_count;
    std::uint8_t reserved3[2];
    AnimBindPose bind_poses[1];
};
static_assert(offsetof(AnimFileHeader, keyframe_count) == 0x10 && offsetof(AnimFileHeader, bind_poses) == 0x14);
const AnimFileHeader* anim_header() { return static_cast<const AnimFileHeader*>(harry_anim_file()); }

std::int64_t var_get(V v) {
    std::int64_t own = 0;
    if (sh::game::player_state_get(s().player, v, &own)) {
        return own;
    }
    const ent::CharacterRecord& c = body();
    const ent::UpperBodyRecord& x = upper();
    const HarryVars& p = vars();
    const sh::game::PlayerCombat& k = s().system.combat;
    const sh::game::SaveRecord& g = save();
    switch (v) {
        case V::LowerBody: return x.lower_state;
        case V::UpperBody: return x.upper_state;
        case V::State: return x.state;
        case V::UpperAnimStatus: return x.model.anim.status;
        case V::UpperAnimKeyframe: return x.model.anim.keyframe;
        case V::UpperAnimTime: return x.model.anim.time;
        case V::UpperStateStep: return x.model.control_step;
        case V::UpperControlState: return x.model.control_state;
        case V::UpperAnimFlags: return x.model.anim.flags;
        case V::UpperAnimAlpha: return x.model.anim.alpha;
        case V::BodyAnimStatus: return c.model.anim.status;
        case V::BodyAnimKeyframe: return c.model.anim.keyframe;
        case V::BodyAnimTime: return c.model.anim.time;
        case V::BodyAnimFlags: return c.model.anim.flags;
        case V::BodyStateStep: return c.model.control_step;
        case V::BodyControlState: return c.model.control_state;
        case V::DisabledBones: return static_cast<std::uint32_t>(x.disabled_bones);
        case V::StopFlags: return s().system.player_stop_flags;
        case V::Aiming: return k.aiming;
        case V::WeaponAttack: return k.weapon_attack;
        case V::TargetNpc: return s().system.target_npc;
        case V::WalkDistance: return static_cast<std::int32_t>(g.walk_distance);
        case V::RunDistance: return static_cast<std::int32_t>(g.run_distance);
        case V::OptionWalkRun: return settings().walk_run_reverse;
        case V::OptionWeaponControl: return settings().weapon_control_press;
        case V::OptionRetreatTurn: return settings().retreat_turn_reverse;
        case V::AltCamera: return g_DebugThirdPersonCam;
        case V::Ammo: return k.ammo_loaded;
        case V::AmmoReserve: return k.ammo_total;
        case V::WeaponSlot: return k.weapon_slot;
        case V::AttackX: return k.attack_position.x;
        case V::AttackY: return k.attack_position.y;
        case V::AttackZ: return k.attack_position.z;
        case V::ReloadRequest: return Pc_ReloadRequestGet();
        case V::PostFireTrace: return g_SH_PostFireTrace;
        case V::Timer2C: return s().system.flow.sys_step_data[1];
        case V::OptionAutoAim: return settings().auto_aim_off;
        case V::FpsCamera: return sh::input::camera_styles().first_person();
        case V::Control2d: return g_PcConfig.control2d;
        case V::Demo: return s().sys_flag(sh::game::sys_flag::kDemo) ? 1 : 0;
        case V::InfiniteAmmo: return g_PcInfiniteAmmo;
        case V::AutoReload: return Pc_AutoReloadEnabled() != 0 ? 1 : 0;
        case V::OptionAutoReload: return g_PcConfig.autoReload;
        case V::AimAssist: return g_PcConfig.aimAssist;
        case V::EquippedGun: return sh::inventory::item_catalog().held_as_gun(g.equipped_weapon) ? 1 : 0;
        case V::EquippedWeapon:
            // Nothing in hand reads as the original's item 0 - 128.
            return g.equipped_weapon == kUnequipped ? kNothingHeld
                                                    : sh::inventory::item_catalog().held_number(g.equipped_weapon);
        case V::GodMode: return g_PcGodMode;
        case V::Saturation: return g.health_saturation;
        case V::Difficulty: return g.difficulty();
        case V::MapIndex: return g.map;
        case V::RoomIndex: return g.room;
        case V::Unequipped: return g.equipped_weapon == kUnequipped ? 1 : 0;
        case V::GrabberFront: return s().system.group_npcs[0];
        case V::GrabberBack: return s().system.group_npcs[1];
        case V::GrabberUnused2: return s().system.group_npcs[2];
        case V::GrabberUnused3: return s().system.group_npcs[3];
        case V::RearLook: return g_PcRearLookActive;
        case V::MatchAnim: return s().system.player_match_anim;
        case V::PositionX: return c.position.x;
        case V::PositionY: return c.position.y;
        case V::PositionZ: return c.position.z;
        case V::RotationY: return c.rotation.y;
        case V::MoveSpeed: return c.move_speed;
        case V::Heading: return c.heading;
        case V::FallSpeed: return c.fall_speed;
        case V::Health: return c.health;
        case V::CharaFlags: return c.flags;
        case V::AngleToTarget: return c.angle_to_target;
        case V::Attacker: return c.partner;
        case V::AttackReceived: return c.attack_received;
        case V::AfkTimer: return p.afk_timer;
        case V::GroundHeight: return p.ground_height;
        case V::FlexTargetX: return p.flex_target_x;
        case V::RunSteps: return p.run_steps;
        case V::Exhaustion: return p.exhaustion;
        case V::AimHold: return p.aim_hold;
        case V::AttackTime: return p.attack_time;
        case V::RunAhead: return p.run_ahead;
        case V::StepSound: return p.step_sound;
        case V::FlagTimer: return p.flag_timer;
        case V::GasWeaponTimer: return p.gas_weapon_timer;
        case V::PlayerFlags: return p.flags;
        case V::QuickTurnHeading: return p.quick_turn_heading;
        case V::AimPitch: return p.aim_pitch;
        case V::LegsHeading: return p.heading;
        case V::LegsMoveSpeed: return p.move_speed;
        case V::CylinderRadius: return c.collision.cylinder.radius;
        case V::CylinderHeight: return c.collision.cylinder.chara_radius;
        case V::BoxTop: return c.collision.box.top;
        case V::BoxBottom: return c.collision.box.bottom;
        case V::BoxOffsetY: return c.collision.box.offset_y;
        case V::HandReach: return c.collision.box.extent_x;
        case V::CylinderOffsetX: return c.collision.offsets.cylinder.x;
        case V::CylinderOffsetZ: return c.collision.offsets.cylinder.z;
        case V::BoxOffsetX: return c.collision.offsets.box.x;
        case V::BoxOffsetZ: return c.collision.offsets.box.z;
        case V::CollisionState: return c.collision.state();
        case V::MapAnimIndex: return s().game.map_anim_index;
        case V::DeltaTime: return sh::loop::frame_delta();
        case V::StickX: return pad().sticks.left_x;
        case V::StickY: return pad().sticks.left_y;
        case V::DebugNoWalls: return g_DebugNoWallCollision;
        case V::DebugNoFloor: return g_DebugNoFloorCollision;
        case V::PadHeld: return static_cast<std::int32_t>(pad().held);
        case V::AnalogLeftX: return pad().raw.left_x;
        case V::AnalogLeftY: return pad().raw.left_y;
        case V::BindRun: return settings().binds.run;
        case V::BindAim: return settings().binds.aim;
        case V::BindAction: return settings().binds.action;
        case V::MovementOriginal: return g_PcConfig.movementOriginal;
        case V::Control2dSnap: return g_PcConfig.control2dSnap;
        case V::AltButtonSprint: return g_PcConfig.altButtonSprint;
        case V::ForceHoldAim: return g_PcConfig.forceHoldAim;
        case V::OrbitYaw: return g_TpsCamYaw;
        case V::QuickTurnRequest: return g_PcQuickTurnRequest;
        default: break;
    }
    return 0;
}

template <class T>
void store(T& field, std::int64_t value) {
    field = static_cast<T>(value);
}

// Stores like a C assignment to the variable (its low bits). Read-only state of other owners (options, settings)
// is never written.
void var_set(V v, std::int64_t value) {
    if (sh::game::player_state_set(s().player, v, value)) {
        return;
    }
    ent::CharacterRecord& c = body();
    ent::UpperBodyRecord& x = upper();
    HarryVars& p = vars();
    sh::game::PlayerCombat& k = s().system.combat;
    sh::game::SaveRecord& g = save();
    switch (v) {
        case V::LowerBody: store(x.lower_state, value); break;
        case V::UpperBody: store(x.upper_state, value); break;
        case V::State: store(x.state, value); break;
        case V::UpperAnimStatus: store(x.model.anim.status, value); break;
        case V::UpperAnimKeyframe: store(x.model.anim.keyframe, value); break;
        case V::UpperAnimTime: store(x.model.anim.time, value); break;
        case V::UpperStateStep: store(x.model.control_step, value); break;
        case V::UpperControlState: store(x.model.control_state, value); break;
        case V::UpperAnimFlags: store(x.model.anim.flags, value); break;
        case V::UpperAnimAlpha: store(x.model.anim.alpha, value); break;
        case V::BodyAnimStatus: store(c.model.anim.status, value); break;
        case V::BodyAnimKeyframe: store(c.model.anim.keyframe, value); break;
        case V::BodyAnimTime: store(c.model.anim.time, value); break;
        case V::BodyAnimFlags: store(c.model.anim.flags, value); break;
        case V::BodyStateStep: store(c.model.control_step, value); break;
        case V::BodyControlState: store(c.model.control_state, value); break;
        case V::DisabledBones: store(x.disabled_bones, static_cast<std::uint32_t>(value)); break;
        case V::StopFlags: store(s().system.player_stop_flags, value); break;
        case V::Aiming: store(k.aiming, value); break;
        case V::WeaponAttack: store(k.weapon_attack, value); break;
        case V::TargetNpc: store(s().system.target_npc, value); break;
        case V::WalkDistance: store(g.walk_distance, value); break;
        case V::RunDistance: store(g.run_distance, value); break;
        case V::Ammo: store(k.ammo_loaded, value); break;
        case V::AmmoReserve: store(k.ammo_total, value); break;
        case V::WeaponSlot: store(k.weapon_slot, value); break;
        case V::AttackX: store(k.attack_position.x, value); break;
        case V::AttackY: store(k.attack_position.y, value); break;
        case V::AttackZ: store(k.attack_position.z, value); break;
        case V::ReloadRequest: Pc_ReloadRequestSet(static_cast<int>(value)); break;
        case V::PostFireTrace: store(g_SH_PostFireTrace, value); break;
        case V::Timer2C: store(s().system.flow.sys_step_data[1], value); break;
        case V::Saturation: store(g.health_saturation, value); break;
        // Only "nothing equipped" can be written (1); 0 leaves the equipped item as it is.
        case V::Unequipped:
            if (value != 0) {
                g.equipped_weapon = kUnequipped;
            }
            break;
        case V::GrabberFront: store(s().system.group_npcs[0], value); break;
        case V::GrabberBack: store(s().system.group_npcs[1], value); break;
        case V::GrabberUnused2: store(s().system.group_npcs[2], value); break;
        case V::GrabberUnused3: store(s().system.group_npcs[3], value); break;
        case V::MatchAnim: store(s().system.player_match_anim, value); break;
        case V::PositionX: store(c.position.x, value); break;
        case V::PositionY: store(c.position.y, value); break;
        case V::PositionZ: store(c.position.z, value); break;
        case V::RotationY: store(c.rotation.y, value); break;
        case V::MoveSpeed: store(c.move_speed, value); break;
        case V::Heading: store(c.heading, value); break;
        case V::FallSpeed: store(c.fall_speed, value); break;
        case V::Health: store(c.health, value); break;
        case V::CharaFlags: store(c.flags, value); break;
        case V::AngleToTarget: store(c.angle_to_target, value); break;
        case V::Attacker: store(c.partner, value); break;
        case V::AttackReceived: store(c.attack_received, value); break;
        case V::AfkTimer: store(p.afk_timer, value); break;
        case V::GroundHeight: store(p.ground_height, value); break;
        case V::FlexTargetX: store(p.flex_target_x, value); break;
        case V::RunSteps: store(p.run_steps, value); break;
        case V::Exhaustion: store(p.exhaustion, value); break;
        case V::AimHold: store(p.aim_hold, value); break;
        case V::AttackTime: store(p.attack_time, value); break;
        case V::RunAhead: store(p.run_ahead, value); break;
        case V::StepSound: store(p.step_sound, value); break;
        case V::FlagTimer: store(p.flag_timer, value); break;
        case V::GasWeaponTimer: store(p.gas_weapon_timer, value); break;
        case V::PlayerFlags: store(p.flags, value); break;
        case V::QuickTurnHeading: store(p.quick_turn_heading, value); break;
        case V::AimPitch: store(p.aim_pitch, value); break;
        case V::LegsHeading: store(p.heading, value); break;
        case V::LegsMoveSpeed: store(p.move_speed, value); break;
        case V::CylinderRadius: store(c.collision.cylinder.radius, value); break;
        case V::CylinderHeight: store(c.collision.cylinder.chara_radius, value); break;
        case V::BoxTop: store(c.collision.box.top, value); break;
        case V::BoxBottom: store(c.collision.box.bottom, value); break;
        case V::BoxOffsetY: store(c.collision.box.offset_y, value); break;
        case V::HandReach: store(c.collision.box.extent_x, value); break;
        case V::CylinderOffsetX: store(c.collision.offsets.cylinder.x, value); break;
        case V::CylinderOffsetZ: store(c.collision.offsets.cylinder.z, value); break;
        case V::BoxOffsetX: store(c.collision.offsets.box.x, value); break;
        case V::BoxOffsetZ: store(c.collision.offsets.box.z, value); break;
        case V::CollisionState: c.collision.set_state(static_cast<int>(value)); break;
        case V::MapAnimIndex: store(s().game.map_anim_index, value); break;
        case V::OptionWeaponControl: store(settings().weapon_control_press, value); break;
        case V::OrbitYaw: store(g_TpsCamYaw, value); break;
        case V::QuickTurnRequest: store(g_PcQuickTurnRequest, value); break;
        default: break;
    }
}

sh::game::GroundSurface ground_of(const sh::game::GroundRecord& g) { return {g.height, g.type, g.tilt_x, g.tilt_z}; }
sh::game::MoveResult result_of(const sh::game::WallResult& w) {
    return {w.offset[0], w.offset[1], w.offset[2], {w.ground_height, w.ground_type, w.tilt_x, w.tilt_z}};
}

sh::game::BoneMatrix to_bone(const ent::BoneMatrix& m) {
    sh::game::BoneMatrix b;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            b.m[i][j] = m.m[i][j];
        }
        b.t[i] = m.t[i];
    }
    return b;
}

// Keyframe viewer (debug, driven by the debug camera's keys): a private animation record loops the selected range
// at the chosen rate without touching anyone's own animation state.
ent::ModelRecord s_viewer_model{};

void viewer_loop_advance(const void* file, ent::BoneCoord* coords) {
    static int seeded = -1;
    if (seeded != g_DebugAnimPlayGen) {
        s_viewer_model.anim.flags = kViewerFlags;
        s_viewer_model.anim.time = g_DebugAnimKfStart * 4096;
        s_viewer_model.anim.keyframe = static_cast<std::int16_t>(g_DebugAnimKfStart);
        s_viewer_model.anim.alpha = 0;
        seeded = g_DebugAnimPlayGen;
    }
    ent::ClipEntry loop;
    loop.kind = ent::ClipKind::Loop;
    loop.next_status = 0;
    loop.start_keyframe = static_cast<std::int16_t>(g_DebugAnimKfStart);
    loop.end_keyframe = static_cast<std::int16_t>(g_DebugAnimKfEnd);
    ent::anim_bank().play(s_viewer_model, file, coords, loop,
                          [] { return ent::anim_timestep(g_DebugAnimRate, sh::loop::frame_delta(), ent::kAnimAdvances); });
    g_DebugAnimKf = s_viewer_model.anim.keyframe;
}

void viewer_pose(const void* file, ent::BoneCoord* coords, int keyframe_count) {
    if (g_DebugAnimPlaying) {
        viewer_loop_advance(file, coords);
        return;
    }
    const int max_kf = keyframe_count > 0 ? keyframe_count - 1 : 0;
    g_DebugAnimKf = g_DebugAnimKf > max_kf ? max_kf : (g_DebugAnimKf < 0 ? 0 : g_DebugAnimKf);
    ent::anim_bank().sample(file, coords, g_DebugAnimKf, g_DebugAnimKf, 0);
}

class HarryRecord final : public sh::game::PlayerRecord {
public:
    std::int64_t get(PlayerVar v) const override { return var_get(v); }
    void set(PlayerVar v, std::int64_t value) override { var_set(v, value); }

    void reset_held_weapon_model() override { Player_WeaponModelStateSet(0, -1); }
    void clear_damage() override {
        body().damage.amount = 0;
        body().damage.push = {};
    }
    void read_from_save() override {
        const sh::game::SaveRecord& g = save();
        ent::CharacterRecord& c = body();
        c.position.x = g.position_x;
        c.position.z = g.position_z;
        c.rotation.y = g.rotation_y;
        c.health = g.health;
    }
    void assign_harry_models() override {
        body().model.chara_id = kHarry;
        upper().model.chara_id = kHarry;
    }
    void reset_save_player() override {
        sh::game::SaveRecord& g = save();
        g.slot_count = 8;
        for (int i = 0; i < kSaveItemSlots; ++i) {
            g.items[i].item = 0xFF;
            g.items[i].count = 0;
        }
        g.health = 100 * 4096;
        g.player_reset = 0;
        g.equipped_weapon = kUnequipped;
        g.health_saturation = 0;
        g.play_time = 0;
        g.run_distance = 0;
        g.walk_distance = 0;
        g.picked_up_items = 0;
        g.clear_count = 0;
        g.mode_bits &= static_cast<std::uint8_t>(~kSaveModeTimeBonus);
    }
    int inventory_slot_count() const override { return save().slot_count; }
    void set_inventory_slot_count(int count) override { save().slot_count = static_cast<std::uint8_t>(count); }
    // `idx` may be one past the last slot: the game's scans read it before their bound check.
    sh::game::InventorySlot inventory_slot(int idx) const override {
        const sh::game::SaveItem& item = save().items[idx];
        return {item.item, item.count};
    }
    int equipped_item() const override { return save().equipped_weapon; }
    void set_inventory_cursor_item(int item) override {
        sh::inventory::inventory_menu().equipped_item = item;
    }
    // The gun's ammo item and the weapon an item is held as, by the item data (weapons/<Name>.json).
    int ammo_item_of(int item) const override {
        return sh::inventory::item_catalog().ammo_item(item);
    }
    int weapon_attack_of_item(int item, bool* gun) const override {
        *gun = sh::inventory::item_catalog().held_as_gun(item);
        return sh::inventory::item_catalog().held_number(item);
    }
    int max_health() const override { return Gameplay_MaxHealthQ12(); }

    void install_map_clips(int dst, int src, int count) override {
        const ClipTableEntry* from = HarryMap_ClipTable() + src;
        for (int i = 0; count < 0 ? from[i].playback != nullptr : i < count; ++i) {
            HARRY_BASE_ANIM_INFOS[dst + i] = from[i];
        }
    }
    void play_clip(sh::game::PlaybackHalf half, int status) override {
        ent::ModelRecord* model = half == sh::game::PlaybackHalf::Body ? &body().model : &upper().model;
        ClipTableEntry* info = &HARRY_BASE_ANIM_INFOS[status];
        info->playback(model, harry_anim_file(), bones(), info);
    }
    int clip_index(const char* name) const override { return Pc_CharaAnim(kHarry, name, -1); }
    int clip_start(int status) const override { return HARRY_BASE_ANIM_INFOS[status].start_keyframe; }
    int clip_end(int status) const override { return HARRY_BASE_ANIM_INFOS[status].end_keyframe; }
    int clip_link(int status) const override { return HARRY_BASE_ANIM_INFOS[status].link_status; }

    bool install_weapon_rig(int attack) override {
        std::int32_t hand = 0;
        if (!Inventory_WeaponRigInfo(attack, &s().player.weapon, &hand)) {
            return false;
        }
        body().collision.box.extent_x = static_cast<std::int16_t>(hand);
        // A shorter clip block (HyperBlaster: 18 of 20) leaves the tail slots as the previous weapon left them.
        (void)Inventory_WeaponRigClips(attack, &HARRY_BASE_ANIM_INFOS[kWeaponClipFirst], kWeaponClipSlots);
        (void)Inventory_WeaponRigKeyframes(attack, s().player.weapon_keyframes, kWeaponKeyframeRecords);
        return true;
    }
    void clear_weapon_info() override { s().player.weapon = sh::game::EquippedWeapon{}; }
    void load_weapon_animation(int attack) override {
        const char* file = Inventory_WeaponRigAnimFile(attack);
        const int idx = file != nullptr ? sh::loader::disc_files().find(file) : sh::loader::DiscFiles::kNone;
        if (idx == sh::loader::DiscFiles::kNone) {
            SH_WARN("[WEP_LOAD] weapon %d has no anim file", attack);
            return;
        }
        sh::loader::queue_read(idx, weapon_anim_buffer());
        sh::loader::queue_wait_empty();
    }
    const char* weapon_name(int attack) const override {
        const sh::items::ItemLibrary* library = sh::inventory::item_catalog().library();
        const sh::items::WeaponDef* w = library != nullptr ? library->find_weapon(attack) : nullptr;
        if (w == nullptr) {
            return nullptr;
        }
        const std::size_t colon = w->id.find(':');
        return w->id.c_str() + (colon == std::string::npos ? 0 : colon + 1);
    }
    int mod_attack_weapon(int attack) const override { return sh::ids::mod_attack_weapon(attack); }
    bool mod_attack_gun(int attack) const override { return sh::ids::attack_is_gun(attack); }
    void sound_command(int command) override { SD_Call(static_cast<std::uint32_t>(command)); }
    // A model bone packs the mesh variant in the low nibble and the joint above it.
    void attach_held_item(int variant, int joint) override {
        WorldGfx_HeldItemAttach(kHarry, static_cast<std::int16_t>(variant | (joint << 4)));
    }

    void receive_damage() override { sh::game::harry_script::receive_damage(); }
    void run_controller() override { sh::game::harry_script::controller(); }
    void run_legs() override { sh::game::harry_script::legs(); }
    void run_footsteps() override { sh::game::harry_script::footsteps(); }
    void run_upper_body() override { sh::game::harry_script::upper_body(); }
    void rest_before() override { sh::game::harry_script::before_tick(); }
    bool rest_idle() override { return sh::game::harry_script::idle(); }
    bool rest_state() override { return sh::game::harry_script::state(); }
    void rest_after() override { sh::game::harry_script::after_tick(); }
    void timer_update() override { Game_TimerUpdate(); }
    void map_player_tick() override {
        for (const std::string& fn : save_map_rules().player_updates) {
            (void)Player_MapUpdate(fn.c_str());
        }
    }
    void scripted_control() override { Player_SceneControl(); }
    void scripted_move() override { Player_SceneMove(); }
    void keyframe_inspector() override;

    const sh::maps::MapRules& map_rules() const override { return save_map_rules(); }
    sh::game::GroundSurface ground_at(std::int32_t x, std::int32_t z) override {
        sh::game::GroundRecord g{};
        Collision_SurfaceGet(&g, x, z);
        return ground_of(g);
    }
    sh::game::MoveResult move_through_walls(std::int32_t dx, std::int32_t dy, std::int32_t dz, bool walls,
                                            const sh::game::GroundSurface& here) override {
        sh::game::WallResult& w = s().player.floor;
        const std::int32_t offset[3] = {dx, dy, dz};
        if (walls) {
            Collision_WallDetect(&w, offset, &body());
        } else {
            w.offset[0] = dx;
            w.offset[1] = dy;
            w.offset[2] = dz;
            w.ground_height = here.height;
            w.ground_type = static_cast<std::int8_t>(here.type);
            w.tilt_z = 0;
            w.tilt_x = 0;
            w.ceiling = kNoCeiling;
        }
        return result_of(w);
    }
    void push_feedback(std::int32_t dx, std::int32_t dz) override { HarryMap_PushFeedback(dx, dz); }
    bool character_push_take() override { return Pc_SplitHead_PushTake() != 0; }
    void count_phantom_floor() override { ++g_PhantomRejectCount; }
    sh::game::MoveResult slide(std::int32_t dx, std::int32_t dz) override {
        sh::game::WallResult r{};
        r.ceiling = kNoCeiling;
        r.ground_height = body().position.y;
        const std::int32_t wish[3] = {dx, 0, dz};
        Collision_WallDetect(&r, wish, &body());
        return result_of(r);
    }
    int speed_zone(std::int32_t x, std::int32_t z) const override { return sh::world::world_map().speed_zone(x, z); }
    int weapon_clip_start(int i) const override { return s().player.weapon_keyframes[i].start; }
    unsigned movement_keys() const override {
        const unsigned char* k = g_sdlKeyboardState;
        if (k == nullptr) {
            return 0;
        }
        return (k[SDL_SCANCODE_W] != 0 ? 1u : 0u) | (k[SDL_SCANCODE_A] != 0 ? 2u : 0u) |
               (k[SDL_SCANCODE_S] != 0 ? 4u : 0u) | (k[SDL_SCANCODE_D] != 0 ? 8u : 0u);
    }
    void view_camera(std::int32_t position[3], std::int32_t* yaw) const override {
        const sh::camera::ViewSystem& view = sh::camera::view_system();
        position[0] = view.position().x;
        position[1] = view.position().y;
        position[2] = view.position().z;
        *yaw = view.angles().y;
    }
    std::uint32_t time_ms() const override { return sh::timing::virtual_time::ticks_ms(); }
    int reaction_row_count() const override { return Pc_HarryReactions_RowScan(); }
    sh::game::ReactionRow reaction_row(int row) const override {
        sh::game::ReactionRow r;
        int last = 0;
        (void)HarryMap_ReactionRow(row, &r.status, &r.trigger, &r.time, &last);
        return r;
    }
    bool reaction_row_imported(int row) const override { return Pc_HarryReactions_RowImported(row) != 0; }
    bool stick_radius() const override { return g_PcConfig.stickRadius != 0; }

    void rotate_bone(int bone, int rot_z, int rot_x, int rot_y, bool refresh) override {
        Bone_CoordRotationSet(&bones()[bone], static_cast<std::int16_t>(rot_z), static_cast<std::int16_t>(rot_x),
                              rot_y);
        if (refresh) {
            bones()[bone].world_current = 0;
        }
    }
    sh::game::BoneMatrix bone_local(int bone) const override { return to_bone(bones()[bone].local); }
    void set_bone_local(int bone, const sh::game::BoneMatrix& m, bool refresh) override {
        ent::BoneMatrix& local = bones()[bone].local;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                local.m[i][j] = m.m[i][j];
            }
            local.t[i] = m.t[i];
        }
        if (refresh) {
            bones()[bone].world_current = 0;
        }
    }
    void pose_keyframes(int kf0, int kf1, int alpha) override {
        ent::anim_bank().sample(harry_anim_file(), bones(), kf0, kf1, alpha);
    }
    void pose_scratch(int kf0, int kf1, int alpha) override {
        ent::anim_bank().sample(harry_anim_file(), scratch_, kf0, kf1, alpha);
    }
    sh::game::BoneMatrix scratch_bone(int bone) const override { return to_bone(scratch_[bone].local); }
    int anim_bone_count() const override { return anim_header()->bone_count; }
    int anim_keyframe_count() const override { return anim_header()->keyframe_count; }
    bool bone_keys_translation(int bone) const override { return pose_of(bone).translation_track >= 0; }
    bool bone_keys_rotation(int bone) const override { return pose_of(bone).rotation_track >= 0; }
    void set_root_translation(std::int32_t x, std::int32_t y, std::int32_t z) override {
        ent::BoneMatrix& m = bones()[0].local;
        m.t[0] = x;
        m.t[1] = y;
        m.t[2] = z;
    }

private:
    static const AnimBindPose& pose_of(int bone) { return anim_header()->bind_poses[bone]; }

    ent::BoneCoord scratch_[kHarryBones] = {};
};

// The inspector's range reaches past the base clips' keyframes: the equipped weapon's clips (Harry's table 0..75)
// sit higher in the same buffer. Publishes the count for the panel, then poses Harry's whole body on the selected
// keyframe (or loops the range) when the viewer is on him.
void HarryRecord::keyframe_inspector() {
    const int keyframes = anim_keyframe_count();
    int max_kf = keyframes > 0 ? keyframes - 1 : 0;
    for (int i = 0; i < kPlayerClipsShown; ++i) {
        const int end = HARRY_BASE_ANIM_INFOS[i].end_keyframe;
        if (end > max_kf && end < 1024) {
            max_kf = end;
        }
    }
    g_DebugAnimKfMax = max_kf + 1;
    if (!g_DebugAnimKfView || g_DebugViewNpcSlot >= 0) {
        return;
    }
    upper().disabled_bones = 0;
    viewer_pose(harry_anim_file(), bones(), max_kf + 1);
}

HarryRecord s_record;

// The remembered ground point asks the game's collision and the save's map.
class GroundPointView final : public sh::world::GroundPointSource {
public:
    void ground(std::int32_t x, std::int32_t z, std::int32_t* height, int* kind) override {
        sh::game::GroundRecord g{};
        Collision_SurfaceGet(&g, x, z);
        *height = g.height;
        *kind = g.type;
    }
    sh::world::VoidDrop void_drop() const override {
        const sh::maps::MapRules::VoidDrop& d = save_map_rules().void_drop;
        return sh::world::VoidDrop{d.no_ground, d.no_ground_bounded, d.no_ground_max_z, d.open_ground};
    }
};

GroundPointView s_ground_points;

// What the player's script binds ask the game (engine/player/player_script.h).
class ScriptView final : public sh::game::PlayerScriptHost {
public:
    int weapon_number(const char* name) const override { return sh::ids::weapon_number(name); }
    int hyper_blaster_mode() const override { return Inventory_HyperBlasterFunctionalTest(); }
    bool set_clip_link(int slot, long long status, int end_keyframe, int link_status, const char** error) override {
        const ent::CharacterRecord* r = s().system.characters.record(slot);
        if (r == nullptr) {
            *error = "bad character slot";
            return false;
        }
        sh::chara::behavior::ClipTable t{};
        if (!sh::chara::behavior::clip_table(r->model.chara_id, 0, &t)) {
            *error = "the character has no clip table";
            return false;
        }
        if (status < 0 || status >= t.count) {
            *error = "clip status out of range";
            return false;
        }
        ClipTableEntry& info = static_cast<ClipTableEntry*>(t.table)[status];
        info.end_keyframe = static_cast<std::int16_t>(end_keyframe);
        info.link_status = static_cast<std::uint8_t>(link_status);
        return true;
    }
};

ScriptView s_script_view;

// Harry's record fields by name for sh.entity.get/set(0, ...) (player_afk_timer, player_ground_height and
// player_heading_angle are in the base list) and his clip table for the clip calls.
#define F(name, member, kind)                                                                     \
    {                                                                                             \
        name, offsetof(ent::CharacterRecord, vars) + offsetof(HarryVars, member),                 \
            SH_FIELD_KIND(HarryVars, member, kind)                                                \
    }
const sh::chara::behavior::Field kHarryFields[] = {
    // Speed the legs ask for (Q12 m/s); the integration moves him by it.
    F("harry_move_speed", move_speed, S16),
    // Grows while he runs, falls while he walks or stands; 10 s and more is the tired idle.
    F("harry_exhaustion", exhaustion, S32),
    // Footsteps of the current run (5 at speed: a stop is a skid into the wall).
    F("harry_run_steps", run_steps, S32),
    // How far ahead the run is free (10 m and more: the wall stop and the stumble look ahead).
    F("harry_run_distance", run_ahead, S32),
    // Pitch class of the last footstep (the upper body's breath follows it).
    F("harry_step_sound", step_sound, U8),
    F("harry_gas_weapon_timer", gas_weapon_timer, S32),
    F("harry_flags", flags, S32),
    // rot_y when the quick turn began: it ends half a turn from there.
    F("harry_quick_turn_heading", quick_turn_heading, S16),
};
#undef F
const sh::chara::behavior::ClipTable kHarryClips[] = {
    {kHarry, HARRY_BASE_ANIM_INFOS, kHarryClipSlots},
};
sh::chara::behavior::Extension s_clip_extension = {
    kHarryFields, std::size(kHarryFields), nullptr, 0, nullptr, kHarryClips, std::size(kHarryClips), nullptr,
};
const sh::chara::behavior::Registrar s_clip_registrar{&s_clip_extension};

const bool s_bound = [] {
    sh::game::bind_player_record(&s_record);
    sh::world::ground_point_cache().bind(&s_ground_points);
    return true;
}();

void bind_script(ShScriptGameApi* api) { sh::game::bind_player_script(*api, s_script_view); }

}  // namespace

extern "C" {

// Keyframe viewer on an NPC (the NPC update): the selected keyframe, or the selected range looping.
void Pc_KeyframeViewerPoseNpc(const void* anim_file, ent::BoneCoord* coords) {
    if (anim_file == nullptr) {
        return;
    }
    viewer_pose(anim_file, coords, static_cast<const AnimFileHeader*>(anim_file)->keyframe_count);
}

// The duration entry of Harry's variable-speed clips (his clip tables point at it).
std::int32_t Player_VariableAnimDurationGet(const ent::ModelRecord* model) {
    return sh::game::player().clip_duration(model->anim.status, model->anim.keyframe);
}

// Harry's animation file of the map; the three maps whose file changes inside the map (the hospital's and the
// amusement park's variants) read it on every load. The map's reaction clips and the guest sound banks loaded for
// spawned enemies belonged to the previous visit.
void GameFs_PlayerMapAnimLoad(std::int32_t map) {
    const int first = sh::loader::disc_files().find("ANIM/HB_M0S00.ANM");
    const int file = first == sh::loader::DiscFiles::kNone ? sh::loader::DiscFiles::kNone : first + map;
    const sh::loader::DiscFiles& files = sh::loader::disc_files();
    if (s().game.map_anim_index != map || file == files.find("ANIM/HB_M6S04.ANM") ||
        file == files.find("ANIM/HB_M7S01.ANM") || file == files.find("ANIM/HB_M7S02.ANM")) {
        s().game.map_anim_index = map;
        sh::loader::queue_read(file, map_anim_buffer());
    }
    sh::loader::queue_wait_empty();
    Pc_HarryReactions_MapLoaded();
    Pc_SoundBanks_Reset();
}

// The step sound is data (engine/chara/footsteps.h) chosen for the place Harry stands
// on; the two pitch jitters are drawn from the game's generator, walk first.
void Player_FootstepSfxGetFor(std::int32_t chara_id, std::int8_t ground_type, std::int32_t* sfx, std::int8_t* pitch0,
                              std::int8_t* pitch1) {
    const sh::game::Player& harry = sh::game::player();
    Pc_FootstepQuery query;
    query.charaId = chara_id;
    query.material = ground_type;
    query.mapIdx = static_cast<int>(harry.get(PlayerVar::MapIndex));
    query.roomIdx = static_cast<int>(harry.get(PlayerVar::RoomIndex));
    query.playerX = static_cast<int>(harry.get(PlayerVar::PositionX));
    query.playerY = static_cast<int>(harry.get(PlayerVar::PositionY));
    query.playerZ = static_cast<int>(harry.get(PlayerVar::PositionZ));
    Pc_FootstepChoice choice;
    Pc_FootstepChoose(&query, &choice);

    sh::loop::GameRandom& dice = sh::loop::game_random();
    *sfx = choice.sfx;
    *pitch0 = static_cast<std::int8_t>(dice.next_int(choice.walkBase, choice.walkBase + choice.walkRange - 1));
    *pitch1 = static_cast<std::int8_t>(dice.next_int(choice.runBase, choice.runBase + choice.runRange - 1));
}

}  // extern "C"

// Registration order: sh_script.h.
static ShScriptGameBinderEntry s_HarryScriptBinder(100, bind_script);
