// The game flow link (game_link.h) over the engine: the session's records (character slots, the running save, the
// map event state), the engine's camera, screens, pads, sound, ids, saves and PC features. What lives in other
// modules is called through the narrow C declarations below (plain numbers and opaque pointers); the accessors
// over the running map, the animation slots, the pictures and VRAM are game_link_records.cpp.

#include "engine/gameflow/game/game_link.h"
#include "audio/voice/game_message_voice.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/maps/map_runtime.h"
#include "engine/runtime/map_world_objects.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string_view>

#include "engine/camera/camera_types.h"
#include "engine/combat/combat_math.h"
#include "engine/config/gameplay_config.h"
#include "engine/debug/command_services.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/item_catalog.h"
#include "engine/log/sh_log.h"
#include "engine/player/player_script.h"
#include "engine/runtime/game_hooks.h"
#include "engine/runtime/pickup_runtime.h"
#include "engine/save/game_saves.h"
#include "engine/screens/screens_view.h"
#include "engine/script/named_api_calls.h"
#include "engine/scripting/binds/game_links.h"
#include "engine/text/game_text.h"
#include "input/camera_style.h"
#include "input/object_pick.h"
#include "ui/hud/crosshair.h"
#include "ui/pause/pause_menu.h"
#include "engine/loop/frame_view.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/app/game_view.h"
#include "engine/app/gameflow/game_flow.h"
#include "engine/app/loader/game_files.h"
#include "engine/loader/queue/disc_files.h"
#include "render/gpu/render_state.h"

using CBool = sh::script::named_calls::CBool;

extern "C" {
// Flags the frame loop and the PC renderer read (src/engine/app/loop, src/render).
extern int g_PcFreezeReleasePending;
extern int g_PcWorldDrawnThisFrame;
extern int g_PcMapScreenActive;
extern int g_PcWorldHorPlus;
extern int g_PcPickupItemActive;
extern int g_PcTxnStartMs;
extern std::int32_t g_Pc2dBackgroundActive;
extern int g_DebugThirdPersonCam;
extern std::int32_t g_TpsCamYaw;
extern std::int32_t g_TpsCamPitch;
extern int g_DebugAnimKfView;
extern int g_DebugViewNpcSlot;
extern std::uint8_t g_PcNpcDebugSpawned[];

// The camera and view (src/engine/camera).
void vwGetViewPosition(sh::camera::Vec3* position);
std::int8_t Vc_StereoBalanceGet(const sh::camera::Vec3* position);
void vcMoveAndSetCamera(std::uint32_t in_connect, std::uint32_t change_debug_mode, std::uint32_t forward,
                        std::uint32_t back, std::uint32_t right, std::uint32_t left, std::uint32_t up,
                        std::uint32_t down);
void vcWorkSetFlags(std::uint32_t enable, std::uint32_t disable);
void DebugCamera_Update(void);

// Harry's control, movement and animation state.
void Player_ControlFreeze(void);
void Player_ControlUnfreeze(CBool set_idle);
CBool Player_MoveSpeedIsZero(void);
std::int32_t Player_AnimPlaybackStateGet(void);
void Player_AnimLock(void);
void Player_AnimUnlock(void);
void Player_AnimStateSet(std::int32_t state);
void Player_AnimReset(void);
bool Player_PathWaypointExecute(std::int32_t state, const std::int32_t* points, std::int16_t angle, std::int32_t count);

// The map's world objects, rooms and chunks.
void Game_MapRoomIdxUpdate(void);
CBool Ipd_ChunkInitCheck(void);

// Characters.
using NpcBehaviourFn = void (*)(void* chara, void* anim_header, void* bones);
NpcBehaviourFn Pc_CharaBehavior_ForSlot(int slot);
int Pc_CharaBehavior_Run(void* chara, void* anim_header, void* bones, NpcBehaviourFn behaviour);
void Pc_KeyframeViewerPoseNpc(void* anim_header, void* bones);
void WorldGfx_CharaModelDraw(std::int32_t chara, void* bones, std::int32_t visible, std::int16_t timer,
                             std::int32_t palette);
void PcCombat_AttackTickReset(void* chara);
void PcCombat_AttackSettle(void* chara);
void PcCombat_GasSoundContact(int volume);
void PcCombat_GasSoundMotor(int chainsaw, int volume, int pitch);
void PcCombat_GasSoundDrillRun(int volume, int pitch);
void Anim_BoneInit(void* anim_header, void* bones);

// World, collision, effects, drawing.
std::uint16_t Collision_FlagsGet(void);
void Collision_FlagsSet(std::uint16_t flags);
CBool Demo_SpawnPositionCheck(std::int32_t x, std::int32_t z);
int Pc_LongViewActive(void);
int Pc_WorldPointInView(int x, int z);
void Game_FlashlightToggle(void);
void Gfx_FlashlightUpdate(void);
void Gfx_2dEffectsUpdate(void);
void Pc_3dAudio_SustainPositionalLoops(void);
void Gfx_InGameDraw(CBool draw);

// Frame, random, boot.
void Demo_DemoRandSeedBackup(void);
void Demo_DemoRandSeedRestore(void);
void Demo_DemoRandSeedAdvance(void);
void Game_WarmBoot(void);
void Screen_Init(std::int32_t width, CBool interlaced);
void Screen_Refresh(std::int32_t width, CBool interlaced);
void Screen_RectInterlacedClear(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, std::uint8_t r,
                                std::uint8_t g, std::uint8_t b);
CBool Screen_BackgroundMotionBlur(std::int32_t vblanks);

// Files.
void GameFs_OptionBinLoad(void);
void GameFs_StreamBinLoad(void);
void GameFs_MapItemsTextureLoad(std::int32_t map);
void GameFs_UniqueItemModelLoad(int item);
void open_main(std::int32_t file, std::int16_t frames);
void Inventory_UnkS8Set(std::int32_t map);
void ShScript_OnNewGame(void);
int ShScript_CallMapHandler(int param);

// Messages, pickups, sound.
void Gfx_Items_PickupInit(int item);
void Sfx_WithFlagsPlay(std::int32_t sound, const std::int32_t* position, std::int32_t volume, std::int32_t flags);
void Sd_SfxStop(std::uint16_t sound);
std::uint8_t Sd_AudioStreamingCheck(void);
void Bgm_TrackUpdate(CBool keep);
void Bgm_TrackChange(std::int32_t map);

// The pad driver (src/input/pad_driver.cpp) and the mouse.
int PadChkVsync(void);
int PadGetState(int port);
int PadInfoMode(int port, int term, int offset);
int PadSetMainMode(int port, int mode, int lock);
void PadSetAct(int port, unsigned char* table, int length);
}

namespace sh::gameflow::game {

namespace {

namespace ent = sh::entities;
using sh::game::session;

constexpr int kNpcFlagSlots = 32;  // the NPC flag words cover the first 32 slots
constexpr int kCharaHarry = 1;
constexpr int kCharaMonsterCybil = 24;  // the last character that fights
constexpr std::uint16_t kAnimUnlocked = 1u << 0;
constexpr std::uint16_t kAnimVisible = 1u << 1;
constexpr std::int32_t kFileTypeImage = 0;
constexpr std::int32_t kFileBlock = 0x100;  // file table sizes count in these
constexpr int kScreenWidth = 320;
constexpr int kInterlacedHeight = 2 * 224;
constexpr std::int32_t kBlurNow = 1;
constexpr std::int32_t kBlurWait = 2;
constexpr std::int32_t kHalfVolume = 128;  // Q8 0.5
constexpr std::uint32_t kCameraVisibleChara = 1u << 7;
constexpr int kTriggerEnd = -1;
constexpr std::size_t kEventBytes = 12;

ent::CharacterSlots& slots() { return session().system.characters; }
ent::CharacterRecord& npc(int slot) { return slots().npcs[slot]; }
ent::CharacterRecord* record(void* p) { return static_cast<ent::CharacterRecord*>(p); }
const ent::CharacterRecord* record(const void* p) { return static_cast<const ent::CharacterRecord*>(p); }
sh::game::SaveRecord& save() { return sh::game::save_record(); }

void set_npc_flag(int slot, bool on) {
    if (static_cast<unsigned>(slot) < kNpcFlagSlots) {
        const std::uint32_t bit = 1u << slot;
        auto& flags = reinterpret_cast<std::uint32_t&>(session().system.scene.npc_flags);
        flags = on ? (flags | bit) : (flags & ~bit);
    }
}

// The fields of a map event record (12 bytes as the map data stores it): two flag words, a byte of trigger type
// (signed low nibble) and activation, the point and the item, then a packed word: state 5 bits, parameter 8,
// options 6, door sound pair 5, camera rail 1, map 6.
int trigger_type_of(const std::uint8_t* e) { return static_cast<std::int8_t>(static_cast<std::uint8_t>(e[4] << 4)) >> 4; }

const std::uint8_t* event_bytes(const void* record) { return static_cast<const std::uint8_t*>(record); }

ent::CharacterRecord& player_chara() { return slots().player; }

void* picture_buffer(Picture p) { return sh::script::links::file_buffer(p == Picture::PaperMap ? 2 : 1); }

}  // namespace

// --- Data, names and files ---
const char* gamedata_path() { return PcPort_GetGameDataPath(); }
int sound_number(const char* name) { return sh::ids::sound_number(name); }
int flag_number(const char* name) { return sh::ids::flag_number(name); }
int item_number(const char* name) { return sh::ids::item_number(name); }
int common_pickup_number(const char* name) { return sh::ids::common_pickup_number(name); }
int paper_map_number(const char* name) { return sh::ids::paper_map_number(name); }
std::int32_t find_image_file(const char* name) { return sh::loader::disc_files().next_named(name, kFileTypeImage, 0); }

// --- The map overlay ---
bool map_events_present() { return sh::maps::map_runtime().loaded(); }
bool map_points_present() { return sh::maps::map_runtime().loaded(); }
int map_event_count(int max) {
    const auto* table = static_cast<const std::uint8_t*>(map_event_table());
    if (table == nullptr) {
        return 0;
    }
    int n = 0;
    while (n < max && trigger_type_of(table + n * kEventBytes) != kTriggerEnd) {
        ++n;
    }
    return n;
}
void* map_event_record(int index) { return static_cast<std::uint8_t*>(map_event_table()) + index * kEventBytes; }
int map_event_index(const void* record, int count) {
    const auto* e = static_cast<const std::uint8_t*>(record);
    const auto* base = static_cast<const std::uint8_t*>(map_event_table());
    if (e != nullptr && base != nullptr && e >= base && e < base + count * kEventBytes) {
        return static_cast<int>((e - base) / static_cast<std::ptrdiff_t>(kEventBytes));
    }
    return -1;
}
EventInfo map_event(const void* record) {
    const std::uint8_t* e = event_bytes(record);
    std::int16_t required = 0, done = 0;
    std::uint32_t word = 0;
    std::memcpy(&required, e, 2);
    std::memcpy(&done, e + 2, 2);
    std::memcpy(&word, e + 8, 4);
    EventInfo i;
    i.required_flag = required;
    i.done_flag = done;
    i.trigger_type = trigger_type_of(e);
    i.activation = e[4] >> 4;
    i.point = e[5];
    i.item = e[6];
    i.sys_state = static_cast<int>(word & 0x1F);
    i.param = static_cast<int>((word >> 5) & 0xFF);
    i.options = static_cast<int>((word >> 13) & 0x3F);
    i.sfx_pair = static_cast<int>((word >> 19) & 0x1F);
    i.camera_rail = ((word >> 24) & 1) != 0;
    i.map = static_cast<int>((word >> 25) & 0x3F);
    return i;
}
MapPointRecord map_point(int index) { return map_point_table()[index]; }
void overlay_freeze_player() { Player_ControlFreeze(); }
void overlay_unfreeze_player() { Player_ControlUnfreeze(0); }
bool overlay_player_stopped() { return Player_MoveSpeedIsZero() != 0; }
void overlay_update_world_objects() { sh::world::map_objects_update(); }
void overlay_frame_hook() { sh::world::map_objects_post_camera(); }
void overlay_npc_spawn_event() { sh::world::map_objects_npc_spawn(); }
void overlay_particles_update(int map) { Map_WeatherUpdate(0, map, 1); }
bool overlay_player_path(std::int32_t anim, std::int32_t* waypoints, std::int16_t heading, std::int32_t count) {
    return Player_PathWaypointExecute(anim, waypoints, heading, count);
}
std::int32_t overlay_anim(AnimOp op, void* chara, bool player, std::int32_t state) {
    if (!player) {
        return overlay_npc_anim(op, chara, state);
    }
    switch (op) {
    case AnimOp::SetState: Player_AnimStateSet(state); return 0;
    case AnimOp::PlaybackState: return Player_AnimPlaybackStateGet();
    case AnimOp::Lock: Player_AnimLock(); return 0;
    case AnimOp::Unlock: Player_AnimUnlock(); return 0;
    case AnimOp::Reset: Player_AnimReset(); return 0;
    }
    return 0;
}
void map_room_update() { Game_MapRoomIdxUpdate(); }
bool map_chunks_idle() { return Ipd_ChunkInitCheck() != 0; }
void close_range_chunks_init() { Ipd_CloseRangeChunksInit(); }

// --- The running save ---
int saved_map() { return save().map; }
void set_saved_map(int map) { save().map = map; }
void set_save_paper_map(int page) { save().paper_map = page; }
void save_mark_paper_map(int page) {
    // The page's bit counts from the paper map word on, as the game's flag arrays do.
    (&save().paper_maps)[page >> 5] |= 1u << (page & 0x1F);
}
void save_mark_paper_maps(std::uint32_t pages) { save().paper_maps |= pages; }
SavedPlayer saved_player() {
    const sh::game::SaveRecord& s = save();
    SavedPlayer p;
    p.location = s.location;
    p.x = s.position_x;
    p.z = s.position_z;
    p.rotation_y = s.rotation_y;
    p.health = s.health;
    return p;
}
void store_saved_player(const SavedPlayer& p) {
    sh::game::SaveRecord& s = save();
    s.location = static_cast<std::int8_t>(p.location);
    s.position_x = p.x;
    s.position_z = p.z;
    s.rotation_y = p.rotation_y;
    s.health = p.health;
}
std::int32_t autosave_health() { return sh::game::autosave_record().health; }
void autosave_count_continue(int max) {
    sh::game::SaveRecord& a = sh::game::autosave_record();
    if (a.continues < max) {
        a.continues++;
    }
}
bool saved_enemy_alive(int row) {
    const sh::game::SaveRecord& s = save();
    const auto* bits = reinterpret_cast<const std::uint32_t*>(&s.enemy_states[s.map]);
    return ((bits[row >> 5] >> (row & 0x1F)) & 1u) != 0;
}
void forget_saved_enemy(int row) {
    sh::game::SaveRecord& s = save();
    s.enemy_states[s.map] &= ~(1 << row);
}
std::uint32_t item_toggle_flags() { return save().item_toggles; }
void poll_saves() { sh::save::game_saves().poll_catalog(); }
int save_catalog_count() { return sh::save::game_saves().catalog_count(); }
int save_listed_count() { return sh::save::game_saves().listed_count(); }

// --- Character records ---
void* player_record() { return &player_chara(); }
int player_used_item() { return slots().player_upper.last_used_item; }
void set_player_used_item(int item) { slots().player_upper.last_used_item = item; }
void set_player_rotation(std::int16_t x, std::int16_t y, std::int16_t z) {
    ent::Angles& r = player_chara().rotation;
    r.x = x;
    r.y = y;
    r.z = z;
}
bool player_visible() { return (player_chara().model.anim.flags & kAnimVisible) != 0; }
void draw_player() {
    // Stale cached bone matrices made Harry shrink on alternate frames; recompute the hierarchy.
    for (ent::BoneCoord& bone : slots().player_bones) {
        bone.world_current = 0;
    }
    ent::CharacterRecord& harry = player_chara();
    sh::input::pick_character_drawn(&harry, -1);
    WorldGfx_CharaModelDraw(kCharaHarry, slots().player_bones, 1, harry.timer, 0);
    PcCombat_AttackTickReset(&harry);
    sh::game::harry_script::combat_update();
    PcCombat_AttackSettle(&harry);
}
bool hostile_npc_alive() {
    for (const ent::CharacterRecord& n : slots().npcs) {
        if (n.model.chara_id >= kCharaHarry && n.model.chara_id <= kCharaMonsterCybil && n.health > 0) {
            return true;
        }
    }
    return false;
}
void* npc_record(int slot) { return &npc(slot); }
int npc_slot_of(const void* p) { return static_cast<int>(record(p) - slots().npcs); }
int npc_chara(int slot) { return npc(slot).model.chara_id; }
int npc_group(int slot) { return npc(slot).partner; }
void npc_fill(int slot, const NpcSlotFill& fill) {
    ent::CharacterRecord& c = npc(slot);
    std::memset(static_cast<void*>(&c), 0, sizeof(c));
    // A spawn from the map's own table again: the savegame bookkeeping applies to the slot.
    g_PcNpcDebugSpawned[slot] = 0;
    c.model.chara_id = fill.chara_id;
    c.partner = fill.spawn_group;
    set_npc_flag(slot, true);
    c.model.control_state = 0;
    c.model.control_step = static_cast<std::uint8_t>(fill.state_step);
    c.position = {fill.x, fill.y, fill.z};
    c.rotation.y = fill.yaw;
    c.model.anim.flags |= kAnimVisible;
}
void npc_spawn_at(int slot, int chara_id, int row, int state_step, std::int32_t x, std::int32_t y, std::int32_t z,
                  std::int16_t yaw) {
    ent::CharacterRecord& c = npc(slot);
    std::memset(static_cast<void*>(&c), 0, sizeof(c));
    // A stale debug-spawn flag would make the kill record skip this enemy.
    g_PcNpcDebugSpawned[slot] = 0;
    c.model.chara_id = chara_id;
    c.partner = row;
    c.model.control_state = 0;
    c.model.control_step = static_cast<std::uint8_t>(state_step);
    c.position = {x, y, z};
    c.rotation.y = yaw;
    c.model.anim.flags |= kAnimVisible;
}
NpcRecordView npc_record_view(int slot) {
    const ent::CharacterRecord& c = npc(slot);
    NpcRecordView v;
    v.chara = c.model.chara_id;
    v.x = c.position.x;
    v.z = c.position.z;
    v.health = c.health;
    v.no_radio_static = (static_cast<std::uint16_t>(c.flags) & ent::kFlagNoRadioStatic) != 0;
    return v;
}
void npc_free(int slot) {
    ent::CharacterRecord& c = npc(slot);
    c.model.chara_id = 0;
    set_npc_flag(slot, false);
    if (static_cast<unsigned>(c.partner) < kNpcFlagSlots) {
        auto& groups = reinterpret_cast<std::uint32_t&>(session().system.scene.spawned_groups);
        groups &= ~(1u << c.partner);
    }
    g_PcNpcDebugSpawned[slot] = 0;  // its kill record was written when it died
}
bool npc_visible(int slot) { return (npc(slot).model.anim.flags & kAnimVisible) != 0; }
void npc_set_visible(int slot, bool on) {
    std::uint16_t& flags = npc(slot).model.anim.flags;
    flags = on ? static_cast<std::uint16_t>(flags | kAnimVisible) : static_cast<std::uint16_t>(flags & ~kAnimVisible);
}
void npc_unlock_animation(int slot) { npc(slot).model.anim.flags |= kAnimUnlocked; }
bool npc_has_behaviour(int slot) { return Pc_CharaBehavior_ForSlot(slot) != nullptr; }
void npc_draw(int slot, int anim) {
    ent::CharacterRecord& c = npc(slot);
    void* bones = anim_bones(anim);
    sh::input::pick_character_drawn(&c, slot);
    WorldGfx_CharaModelDraw(c.model.chara_id, bones, 1, c.timer, static_cast<std::int8_t>(c.model.palette));
}
std::int32_t npc_control_state(int slot) { return npc(slot).model.control_state; }
void npc_restart_state(int slot) { npc(slot).model.control_step = 0; }
void npc_behaviour_run(int slot, int anim) {
    Pc_CharaBehavior_Run(&npc(slot), anim_header(anim), anim_bones(anim), Pc_CharaBehavior_ForSlot(slot));
}
bool npc_debug_spawned(int slot) { return g_PcNpcDebugSpawned[slot] != 0; }
bool anim_data_ready(int anim) { return anim_header(anim) != nullptr; }
int chara_anim_data_index(int chara_id) { return sh::gameflow::anim_slot_table().chara_slot_of(chara_id); }
int chara_record_id(const void* c) { return record(c)->model.chara_id; }
void set_chara_record_id(void* c, int id) { record(c)->model.chara_id = id; }
int chara_record_group(const void* c) { return record(c)->partner; }
void chara_record_xz(const void* c, std::int32_t& x, std::int32_t& z) {
    x = record(c)->position.x;
    z = record(c)->position.z;
}
std::int32_t chara_record_health(const void* c) { return record(c)->health; }
std::int32_t chara_record_damage(const void* c) { return record(c)->damage.amount; }
std::uint32_t chara_record_flags(const void* c) { return static_cast<std::uint32_t>(record(c)->flags); }
void set_chara_record_flags(void* c, std::uint32_t flags) { record(c)->flags = static_cast<std::int16_t>(flags); }
std::uint32_t chara_record_heading_word(const void* c) {
    // The heading and the flags as one word (low half the heading), as the radio's distance pass reads them.
    const sh::entities::CharacterRecord& r = *record(c);
    return static_cast<std::uint16_t>(r.heading) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(r.flags)) << 16;
}
int chara_record_attack_received(const void* c) { return record(c)->attack_received; }
void attack_tick_reset(void* c) { PcCombat_AttackTickReset(c); }
void attack_settle(void* c) { PcCombat_AttackSettle(c); }
void chara_load(std::int32_t model, std::int32_t chara_id, void* bones, std::int8_t force_free, void* lm, void* tex) {
    sh::gameflow::anim_slots().alloc(model + 1, chara_id, nullptr, bones);
    sh::chara::chara_models().assign_buffers(static_cast<std::uint8_t>(force_free));
    sh::chara::chara_models().load(chara_id, model, static_cast<sh::world::LmHeader*>(lm),
                                   static_cast<const sh::world::TextureImage*>(tex));
}
void chara_process_loads() {
    sh::loader::queue_wait_empty();
    sh::chara::chara_models().process_loads();
}
void chara_bones_init(std::int32_t anim) { Anim_BoneInit(anim_header(anim), anim_bones(anim)); }

// --- World, collision, camera ---
std::int32_t collision_ground(std::int32_t x, std::int32_t z) {
    // The surface record: ground height, two tilt angles, the ground type (12 bytes).
    struct {
        std::int32_t ground;
        std::int16_t tilt[2];
        std::int8_t type;
        std::uint8_t reserved[3];
    } surface{};
    Collision_SurfaceGet(&surface, x, z);
    return surface.ground;
}
std::uint16_t collision_flags() { return Collision_FlagsGet(); }
void set_collision_flags(std::uint16_t flags) { Collision_FlagsSet(flags); }
namespace {
// The collision trigger bits the map scripts set (one word, kept for the whole run).
std::uint16_t g_collision_trigger_flags = 0;
}  // namespace
std::uint16_t collision_trigger_flags() { return g_collision_trigger_flags; }
void set_collision_trigger_flags(std::uint16_t flags) { g_collision_trigger_flags = flags; }
bool spawn_position_ok(std::int32_t x, std::int32_t z) { return Demo_SpawnPositionCheck(x, z) != 0; }
void view_position_xz(std::int32_t& x, std::int32_t& z) {
    sh::camera::Vec3 cam;
    vwGetViewPosition(&cam);
    x = cam.x;
    z = cam.z;
}
bool long_view_active() { return Pc_LongViewActive() != 0; }
bool world_point_in_view(std::int32_t x, std::int32_t z) { return Pc_WorldPointInView(x, z) != 0; }
std::int32_t stereo_balance(std::int32_t x, std::int32_t z) {
    const sh::camera::Vec3 pos{x, 0, z};
    return Vc_StereoBalanceGet(&pos);
}
std::int32_t square_root_q12(std::int32_t v) { return sh::combat::sqrt_q12(v); }
bool fps_camera() { return sh::input::camera_styles().first_person(); }
void reseat_third_person_camera(std::int32_t yaw) {
    if (g_DebugThirdPersonCam) {
        g_TpsCamYaw = yaw;
        g_TpsCamPitch = 0;
        sh::input::camera_styles().request_camera_reset();
    }
}
void camera_move_and_set() { vcMoveAndSetCamera(0, 0, 0, 0, 0, 0, 0, 0); }
void debug_camera_update() { DebugCamera_Update(); }
void camera_move(const Waypoint& target, const CameraParams& p, bool warp) {
    sh::camera::Vec3 pos{target.x, target.y, target.z};
    sh::camera::CamMoveParams params;
    params.accel_xz = p.accel_a;
    params.accel_y = p.accel_b;
    params.max_speed_xz = p.max_a;
    params.max_speed_y = p.max_b;
    vcUserCamTarget(&pos, &params, warp ? 1 : 0);
}
void camera_watch(const Waypoint& target, const CameraParams& p, bool warp) {
    sh::camera::Vec3 pos{target.x, target.y, target.z};
    sh::camera::WatchMoveParams params;
    params.accel_x = p.accel_a;
    params.accel_y = p.accel_b;
    params.max_speed_x = static_cast<std::int16_t>(p.max_a);
    params.max_speed_y = static_cast<std::int16_t>(p.max_b);
    vcWorkSetFlags(0, kCameraVisibleChara);
    vcUserWatchTarget(&pos, &params, warp ? 1 : 0);
}
void flashlight_toggle() { Game_FlashlightToggle(); }
void flashlight_update() { Gfx_FlashlightUpdate(); }
void effects_2d_update() { Gfx_2dEffectsUpdate(); }
void sustain_positional_loops() { Pc_3dAudio_SustainPositionalLoops(); }
void draw_world() {
    Gfx_InGameDraw(1);
    // The world is in this frame's OT, so the fog-coloured clear behind it is right.
    g_PcWorldDrawnThisFrame = 1;
}
void frame_world_widescreen(bool cutscene_live) {
    if (!g_PcMapScreenActive && !(sh::render::feedback_control().skip_store && !cutscene_live)) {
        sh::render::picture().hor_plus = 1;
    }
    g_PcWorldHorPlus = sh::render::picture().hor_plus;
}
void crosshair_draw() { sh::ui::draw_crosshair(); }

// --- Frame, timing, random ---
void set_delta_time(std::int32_t delta) { sh::loop::frame_delta() = delta; }
std::int32_t vblanks() { return sh::loop::frame_vblanks(); }
void set_interval_vblanks(int n) { sh::screens::set_frame_interval(n); }
void wait_draw() { sh::screens::wait_frame_done(false); }
void demo_random_backup() { Demo_DemoRandSeedBackup(); }
void demo_random_restore() { Demo_DemoRandSeedRestore(); }
void demo_random_advance() { Demo_DemoRandSeedAdvance(); }
void game_timer_update() { Game_TimerUpdate(); }
void set_transition_start_ms(int ms) { g_PcTxnStartMs = ms; }
void warm_boot() { Game_WarmBoot(); }
void flush_debug_log() {
    if (g_ShDebugLog) {
        std::fflush(g_ShDebugLog);
    }
}

// --- Screen ---
bool screen_fade_none() { return sh::screens::fade_status() == static_cast<std::int32_t>(sh::screens::ScreenFadeState::None); }
bool screen_fade_white() { return (sh::screens::fade_status() & sh::screens::kFadeWhiteBit) != 0; }
void screen_fade_reset() { sh::screens::set_fade_status(static_cast<std::int32_t>(sh::screens::ScreenFadeState::Reset)); }
void cutscene_borders_in() {
    session().system.flow.cutscene_border = static_cast<std::int32_t>(sh::screens::BorderState::FadeInStart);
}
void cutscene_borders_out() {
    session().system.flow.cutscene_border = static_cast<std::int32_t>(sh::screens::BorderState::FadeOutStart);
}
void screen_init(bool interlaced) { Screen_Init(kScreenWidth, interlaced ? 1 : 0); }
void screen_refresh() { Screen_Refresh(kScreenWidth, 0); }
void clear_interlaced_screen() { Screen_RectInterlacedClear(0, 32, kScreenWidth, kInterlacedHeight, 0, 0, 0); }
void keep_black_clear() { g_Pc2dBackgroundActive = 2; }
std::int32_t background_gamma() { return sh::screens::fade_control().background_gamma; }
void set_background_gamma(std::int32_t gamma) { sh::screens::fade_control().background_gamma = static_cast<std::uint8_t>(gamma); }
void background_motion_blur_now() { Screen_BackgroundMotionBlur(kBlurNow); }
void background_motion_blur_wait() { Screen_BackgroundMotionBlur(kBlurWait); }
void hold_last_frame() { sh::render::feedback_control().present_last_frame = 1; }
void release_frozen_frame() { g_PcFreezeReleasePending = 1; }
void cancel_frozen_frame_release() { g_PcFreezeReleasePending = 0; }
void arm_feedback_store() { sh::render::feedback_control().store_allowed = 2; }
void set_map_screen_active(bool on) { g_PcMapScreenActive = on ? 1 : 0; }
ImagePlace picture_place(Picture p) {
    ImagePlace place;
    std::memcpy(static_cast<void*>(&place), picture_record(p), sizeof(place));
    return place;
}
void read_image(std::int32_t file, Picture p) { sh::loader::queue_read_image(file, picture_buffer(p), static_cast<const sh::loader::ImagePlace*>(picture_record(p))); }
void draw_picture(Picture p) { Screen_BackgroundImgDraw(picture_record(p)); }
void draw_picture_alt(Picture p) { Screen_BackgroundImgDrawAlt(picture_record(p)); }
void draw_paused_text() {
    sh::text::game_text().set_position(screen_position_x(39.25f), screen_position_y(43.5f));
    sh::text::game_text().draw(const_cast<char*>("\x07PAUSED"), kDefaultMessageLength);
}

// --- Files ---
bool file_chunks_loaded() { return sh::loader::queue_load_chunks() != 0; }
void seek_file(std::int32_t file) { sh::loader::queue_seek(file); }
void wait_file_queue() { sh::loader::queue_wait_empty(); }
void reset_file_queue() { sh::loader::queue_reset(); }
int file_queue_length() { return sh::loader::queue_length(); }
void load_options_screen() { GameFs_OptionBinLoad(); }
void load_movie_stream() { GameFs_StreamBinLoad(); }
void play_event_movie(int param) {
    // The movie file is counted down from the last XA file by the event parameter.
    const std::int32_t file = sh::loader::disc_files().find_in_run("XA/ZC_14392", -param);
    open_main(file, static_cast<std::int16_t>(sh::loader::disc_files().bytes(file) / kFileBlock));
}
void load_map_item_textures(int map) { GameFs_MapItemsTextureLoad(map); }
void load_unique_item_model(int item) { GameFs_UniqueItemModelLoad(item); }
void inventory_map_set(int map) { Inventory_UnkS8Set(map); }
void boot_map_load(int map) { sh::app::view::map_load(map); }
void boot_player_init() { sh::app::view::player_init(); }
void boot_game_startup() { sh::app::game_startup(); }
void boot_savegame_initialize(int map, std::int32_t difficulty) {
    sh::app::savegame_initialize(map, difficulty);
}
void new_game_hooks() {
    ShScript_OnNewGame();
    sh::hooks::game_new(save().difficulty());
}
void console_apply_pending_flags() { sh::debug::apply_remembered_flags(); }

// --- Messages, items ---
int map_message_draw(int message) { return Gfx_MapMsg_Draw(message); }
void set_message_game_voiced(bool on) { sh::audio::message_voice().set_game_voiced(on); }
void stop_message_voice() { sh::audio::message_voice().box_closed(); }
void start_item_pickup(int item) { Gfx_Items_PickupInit(item); }
bool animate_pickup_item(int item) { return Gfx_PickupItemAnimate(item) != 0; }
void set_pickup_item_active(bool on) { g_PcPickupItemActive = on ? 1 : 0; }
int ammo_pickup_count(int item) {
    return sh::inventory::item_catalog().ammo_pickup_count(item);
}
void play_sound_at(int sound, const Waypoint* at) {
    std::int32_t pos[3] = {};
    if (at != nullptr) {
        pos[0] = at->x;
        pos[1] = at->y;
        pos[2] = at->z;
    }
    Sfx_WithFlagsPlay(sound, at != nullptr ? pos : nullptr, kHalfVolume, 0);
}

// --- Sound ---
void stop_sound(int sound) { Sd_SfxStop(static_cast<std::uint16_t>(sound)); }
void sound_attributes(int sound, std::int8_t balance, std::uint8_t volume) {
    Sd_SfxAttributesUpdate(static_cast<std::uint16_t>(sound), balance, volume, 0);
}
bool audio_streaming() { return Sd_AudioStreamingCheck() != 0; }
void music_track_update() { Bgm_TrackUpdate(0); }
void music_track_change(int map) { Bgm_TrackChange(map); }
void gas_sound_contact(int volume) { PcCombat_GasSoundContact(volume); }
void gas_sound_motor(bool chainsaw, int volume) { PcCombat_GasSoundMotor(chainsaw ? 1 : 0, volume, 0); }
void gas_sound_drill_run(int volume) { PcCombat_GasSoundDrillRun(volume, 0); }

// --- Pad ---
bool pad_exchange_done() { return PadChkVsync() != 0; }
int pad_state(int port) { return PadGetState(port); }
int pad_info(int port, int term) { return PadInfoMode(port, term, 0); }
int pad_set_main_mode(int port, int mode, int lock) { return PadSetMainMode(port, mode, lock); }
void pad_set_actuators(int port, std::uint8_t* bytes, int count) { PadSetAct(port, bytes, count); }

// --- PC features ---
bool pause_menu_native() { return sh::ui::pause_menu().replaces_pause(); }
void pause_menu_enter() { sh::ui::pause_menu().note_entry(); }
void pause_menu_reset() { sh::ui::pause_menu().reset(); }
void pause_menu_update() { sh::ui::pause_menu().update(); }
bool pause_menu_stops_clock() { return sh::ui::pause_menu().stops_clock(); }
bool pause_menu_return_pending() { return sh::ui::pause_menu().return_pending(); }
bool hook_map_event_allowed(int param) { return sh::hooks::game_map_event_allowed(param); }
bool hook_item_pickup(int item, int count, int flag) { return sh::hooks::game_item_pickup(item, count, flag); }
int hook_item_pickup_count(int item, int flag, int count) { return sh::hooks::game_item_pickup_count(item, flag, count); }
void hook_item_picked(int item, int count, int flag) { sh::hooks::game_item_picked(item, count, flag); }
int hook_item_given(int item, int count, const char* source) {
    return sh::hooks::game_item_given(item, count, source);
}
bool hook_radio_static(int heard, int nearest_slot, std::uint32_t nearest_sq) {
    return sh::hooks::game_radio_static(heard, nearest_slot, nearest_sq);
}
bool script_map_handler(int param) { return ShScript_CallMapHandler(param) != 0; }
bool runtime_pickups_run_event() { return sh::world::runtime_pickups_run_event(); }
void runtime_pickups_draw() { sh::world::runtime_pickups_draw(); }
bool runtime_pickups_start(std::string_view id) { return sh::world::runtime_pickups_start(id); }
bool runtime_pickups_try_start(std::int32_t x, std::int32_t y, std::int32_t z,
                               const std::function<bool(std::int32_t, std::int32_t)>& facing) {
    return sh::world::runtime_pickups_try_start({x, y, z}, facing);
}
bool pick_npc_tick(int slot) { return sh::input::pick_npc_tick(&npc(slot), slot); }
bool keyframe_viewer_on(int slot) { return g_DebugAnimKfView && g_DebugViewNpcSlot == slot; }
void keyframe_viewer_pose(int anim) { Pc_KeyframeViewerPoseNpc(anim_header(anim), anim_bones(anim)); }
int difficulty_count() { return Difficulty_Count(); }
const char* difficulty_label(int index) { return Difficulty_Label(index); }
int difficulty_default() { return Difficulty_DefaultIndex(); }
std::int32_t difficulty_base(int index) { return Difficulty_BaseDifficulty(index); }
const char* difficulty_start_map(int index) { return Difficulty_StartMap(index); }

}  // namespace sh::gameflow::game
