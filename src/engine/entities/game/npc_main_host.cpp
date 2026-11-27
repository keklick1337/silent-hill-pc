// The game's NPC main loop on the engine (src/engine/entities/npc_director.h): the room's spawn
// pass from the map's spawn table, the per-tick update of every NPC slot (distance review, visibility, behaviour,
// draw), the radio's static and the enemy kill bookkeeping. The NPC slots are the session's character records;
// their record work (pose, draw, behaviour) goes through game_link.h.

#include <array>
#include <cstdint>
#include <string>

#include "engine/app/pc_paths.h"
#include "engine/chara/chara_library.h"
#include "engine/effects/effects_file.h"
#include "engine/entities/game/radio_state.h"
#include "engine/entities/game/remains_host.h"
#include "engine/entities/npc_director.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/log/sh_log.h"
#include "engine/loop/game_random.h"
#include "engine/maps/map_runtime.h"
#include "engine/player/player.h"
#include "engine/screens/screens_view.h"

extern "C" {
void Collision_FlagsLocationUpdate(const void* chara);
void Collision_FlagsUpdate(void);
void Chara_KillCountUpdate(void* chara);
void Chara_DamagedFlagUpdate(void* chara);
}

namespace {

namespace ent = sh::entities;
namespace link = sh::gameflow::game;
namespace gs = sh::game_state;
using sh::game::PlayerVar;

constexpr int kNoChara = 0;
constexpr int kPadlock = 44;
constexpr int kMonsterCybil = 24;
constexpr int kSlotCount = 120;
constexpr int kLegacySlots = 32;
constexpr int kNormalDifficulty = 0;
constexpr std::uint32_t kRadioOn = 1u << 0;
constexpr int kRadioLoop = 1280 + 0x29;
constexpr std::uint32_t kFlagDamaged = 1u << 5;
constexpr std::uint32_t kFlagDead = 1u << 6;

// The rules of gamedata/npc/rules.json; the compiled-in defaults carry the same numbers if the file is unreadable.
ent::NpcDirector& director() {
    static ent::NpcDirector* d = nullptr;
    if (d == nullptr) {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<std::string> text = sh::effects::read_effect_file(link::gamedata_path(), mods, "npc/rules.json", &source);
        sh::Result<ent::NpcRules> rules =
            text ? ent::parse_npc_rules(text.value()) : sh::Result<ent::NpcRules>::fail(text.error());
        ent::NpcRules chosen;
        if (rules) {
            chosen = std::move(rules.value());
            SH_LOG("[NPC] rules from %s", source.c_str());
        } else {
            SH_ERR("[NPC] %s - built-in rules", rules.error().c_str());
        }
        d = new ent::NpcDirector(std::move(chosen), ent::NpcCharaIds{kNoChara, kPadlock, kMonsterCybil});
        const sh::chara::CharaNumbering& numbering = sh::chara::CharaLibrary::default_numbering();
        d->resolve([&](const std::string& id) { return numbering ? numbering(id) : -1; });
    }
    return *d;
}

std::uint32_t* bits_of(std::int32_t& word) { return reinterpret_cast<std::uint32_t*>(&word); }
bool bit(const std::uint32_t* bits, int idx) { return ((bits[idx >> 5] >> (idx & 0x1F)) & 1u) != 0; }

class GameNpcHost final : public ent::NpcHost {
public:
    void player_position(std::int32_t& x, std::int32_t& z) override {
        x = static_cast<std::int32_t>(sh::game::player().get(PlayerVar::PositionX));
        z = static_cast<std::int32_t>(sh::game::player().get(PlayerVar::PositionZ));
    }
    void camera_position(std::int32_t& x, std::int32_t& z) override { link::view_position_xz(x, z); }
    bool chunked_interior() override { return link::map_chunked_interior(); }
    bool long_view() override { return link::long_view_active(); }
    bool point_in_view(std::int32_t x, std::int32_t z) override { return link::world_point_in_view(x, z); }
    bool hide_distance_from_player() override {
        return (sh::game::session().system.environment.current.preset.flags & 0x2) != 0;
    }

    std::int32_t frame_vblanks() override { return link::vblanks(); }
    bool spawning_blocked() override { return sh::game::session().sys_flag(sh::game::sys_flag::kNoEnemySpawn); }
    bool row_enabled(int row) override { return link::saved_enemy_alive(row); }
    bool row_occupied(int row) override { return bit(bits_of(sh::game::session().system.scene.spawned_groups), row); }
    bool difficulty_allows(std::int32_t minimum) override { return gs::save_difficulty() >= minimum; }
    bool ground_allows(std::int32_t x, std::int32_t z) override { return link::spawn_position_ok(x, z); }
    std::uint32_t npc_slots_taken() override { return static_cast<std::uint32_t>(sh::game::session().system.scene.npc_flags); }
    std::int32_t npc_slot_limit() override { return sh::game::session().system.scene.npc_flags_owner; }

    void spawn(const ent::SpawnRequest& request) override {
        const std::int32_t ground = link::collision_ground(request.data.x, request.data.z);
        link::npc_spawn_at(request.slot, request.chara, request.row, request.data.flags, request.data.x, ground,
                           request.data.z, static_cast<std::int16_t>(request.data.rotation_y * 16));
        if (static_cast<std::uint32_t>(request.slot) < static_cast<std::uint32_t>(kLegacySlots)) {
            sh::game::session().system.scene.npc_flags |= 1 << request.slot;
        }
        std::uint32_t* groups = bits_of(sh::game::session().system.scene.spawned_groups);
        groups[request.row >> 5] |= 1u << (request.row & 0x1F);
    }

    std::int32_t stereo_balance(std::int32_t x, std::int32_t z) override { return link::stereo_balance(x, z); }
    std::int32_t square_root_q12(std::int32_t v) override { return link::square_root_q12(v); }
    void radio_start(int channel) override { gs::sound_command(static_cast<std::uint16_t>(kRadioLoop + channel)); }
    void radio_update(int channel, std::int32_t balance, std::int32_t volume) override {
        link::sound_attributes(kRadioLoop + channel, static_cast<std::int8_t>(balance), static_cast<std::uint8_t>(volume));
    }
    void radio_stop(int channel) override { link::stop_sound(kRadioLoop + channel); }
    bool radio_static_allowed(int heard, int nearest_slot, std::uint32_t nearest_sq) override {
        return link::hook_radio_static(heard, nearest_slot, nearest_sq);
    }
};

// The radio channels' memory across ticks and rooms.
ent::RadioChannels& radio() {
    static ent::RadioChannels r;
    return r;
}

// The NPC slots of the session's character records, as the director's tick pass drives them.
class GameNpcSlots final : public ent::NpcSlots {
public:
    // Tick, draw and distance review cover every slot; radio and room rows only the room spawner's.
    int slot_count() override { return kSlotCount; }

    ent::NpcView view(int slot) override {
        const link::NpcRecordView r = link::npc_record_view(slot);
        ent::NpcView v;
        v.chara = r.chara;
        v.x = r.x;
        v.z = r.z;
        v.health = r.health;
        v.no_radio_static = r.no_radio_static;
        v.remains = ent::game::remains::is_statue(slot);
        return v;
    }
    void free(int slot) override {
        ent::game::remains::slot_freed(slot);
        link::npc_free(slot);
    }
    bool visible(int slot) override { return link::npc_visible(slot); }
    void set_visible(int slot, bool on) override { link::npc_set_visible(slot, on); }
    void unlock_animation(int slot) override { link::npc_unlock_animation(slot); }
    // Also latches the record's animation data for the rest of this slot's tick (the behaviour may change the id).
    bool animation_loaded(int slot) override {
        anim_ = link::chara_anim_data_index(link::npc_chara(slot));
        return static_cast<std::int8_t>(anim_) != -1;
    }
    bool has_behaviour(int slot) override { return link::npc_has_behaviour(slot); }
    void pose_still(int slot, const ent::NpcRules::IdleClip* idle, std::int32_t idle_duration) override {
        if (ent::game::remains::is_statue(slot)) {
            link::npc_pose_frozen(slot, anim_);
            return;
        }
        // Without a behaviour nothing writes the root's world transform: pose and place it here. Standing actors
        // breathe instead of freezing at keyframe 0.
        link::npc_pose_still(slot, anim_, idle != nullptr, idle != nullptr ? idle->first : 0,
                             idle != nullptr ? idle->last : 0, idle_duration);
    }
    void draw(int slot) override { link::npc_draw(slot, anim_); }
    std::int32_t control_state(int slot) override { return link::npc_control_state(slot); }
    void restart_state(int slot) override { link::npc_restart_state(slot); }
    void before_behaviour(int slot) override {
        void* npc = link::npc_record(slot);
        link::attack_tick_reset(npc);
        Chara_DamagedFlagUpdate(npc);
        Collision_FlagsLocationUpdate(npc);
    }
    bool pose_ready(int slot) override {
        (void)slot;
        return link::anim_data_ready(anim_);
    }
    void behave(int slot) override {
        void* npc = link::npc_record(slot);
        if (link::pick_npc_tick(slot)) {
            // Console FREEZE: no AI tick, the bones keep last frame's pose.
        } else if (link::keyframe_viewer_on(slot)) {
            link::keyframe_viewer_pose(anim_);
        } else {
            link::npc_behaviour_run(slot, anim_);
            Collision_FlagsUpdate();
            Chara_KillCountUpdate(npc);
            link::attack_settle(npc);
        }
    }

private:
    int anim_ = 0;
};

}  // namespace

sh::entities::game::RadioState sh::entities::game::radio_state() {
    RadioState out;
    out.on = (link::item_toggle_flags() & kRadioOn) != 0;
    const auto& channels = radio().channels();
    for (std::size_t i = 0; i < out.channels.size(); ++i) {
        out.channels[i].sound = kRadioLoop + static_cast<int>(i);
        out.channels[i].playing = channels[i].playing;
        out.channels[i].target = channels[i].target;
    }
    for (std::size_t i = 0; i < out.heard.size(); ++i) {
        const ent::RadioEntry& e = director().radio().entry(static_cast<int>(i));
        out.heard[i].slot = e.slot;
        out.heard[i].distance_sq = e.distance_sq;
    }
    return out;
}

extern "C" {

void Savegame_EnemyStateUpdate(void* chara) {
    // Console and pool spawns reuse the group field as their slot index; they carry no save identity.
    const int slot = link::npc_slot_of(chara);
    if (slot >= 0 && slot < kSlotCount && link::npc_debug_spawned(slot)) {
        return;
    }
    if (ent::saved_kill_persists(director().rules(), gs::save_difficulty() > kNormalDifficulty,
                                 [] { return sh::loop::game_random().next_q12_fraction(); })) {
        link::forget_saved_enemy(link::chara_record_group(chara));
    }
}

void Chara_DamagedFlagUpdate(void* chara) {
    const std::uint32_t flags = link::chara_record_flags(chara);
    link::set_chara_record_flags(chara, ent::damage_pending(link::chara_record_damage(chara)) ? (flags | kFlagDamaged)
                                                                                            : (flags & ~kFlagDamaged));
}

void Chara_KillCountUpdate(void* chara) {
    // The record keeps its flag mask in the heading word.
    if (ent::kill_credit_due(link::chara_record_health(chara), link::chara_record_heading_word(chara))) {
        const int counter = ent::kill_tally_counter(director().rules(), link::chara_record_attack_received(chara),
                                                    [](std::int32_t attack) { return link::attack_kill_class(attack); });
        if (counter >= 0) {
            sh::screens::add_enemy_kill(counter != 0);
        }
        link::set_chara_record_flags(chara, link::chara_record_flags(chara) | kFlagDead);
    }
}

void Game_NpcRoomInitSpawn(unsigned int cond) {
    GameNpcHost host;
    if (cond == false) {
        radio().forget(host);
        link::overlay_npc_spawn_event();
    }

    // The two groups of sixteen rows are one run of 32.
    std::array<ent::SpawnRow, ent::NpcDirector::kRowCount> rows;
    for (int i = 0; i < ent::NpcDirector::kRowCount; i++) {
        const sh::maps::SpawnPoint& info = sh::maps::map_runtime().spawn(i);
        ent::SpawnRow& row = rows[i];
        row.flags = info.flags;
        row.chara = info.chara;
        row.x = info.x;
        row.z = info.z;
        row.rotation_y = info.rotation_y;
        row.difficulty_min = info.difficulty_min;
    }
    director().spawn_pass(host, cond != 0, rows, sh::maps::map_runtime().group_chara(0),
                          sh::maps::map_runtime().group_chara(1));
    if (cond == false) {
        ent::game::remains::room_entered();
    }
}

void Game_NpcUpdate(void) {
    GameNpcHost host;
    GameNpcSlots slots;
    link::demo_random_backup();
    link::demo_random_restore();
    sh::gameflow::map_event_state().radio_pitch =
        director().update(host, slots, radio(), (link::item_toggle_flags() & kRadioOn) != 0);
    ent::game::remains::tick([](int chara) { return director().is_actor(chara) && director().is_monster(chara); });
}

void Radio_NpcInfoClear(void) {
    GameNpcHost host;
    radio().forget(host);
}

void Game_RadioSoundStop(void) {
    GameNpcHost host;
    radio().silence(host);
}

unsigned int Math_Distance2dCheck(const std::int32_t* from, const std::int32_t* to, std::int32_t radius) {
    return ent::outside_radius(from[0], from[2], to[0], to[2], radius);
}

}  // extern "C"
