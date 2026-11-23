// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The director of the map NPCs: one pass over the spawn rows when the player moves, one review
// per live NPC each tick (free it, show or hide it, offer it to the radio) and the radio's two static channels.
// It never touches a record: everything it reads or changes goes through NpcHost, which the game implements over
// the character records.

#pragma once

#include "engine/entities/npc_rules.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace sh::entities {

// One row of a map's character table.
struct SpawnRow {
    std::int32_t flags = 0;        // zero: the row is empty; else the character's entry state
    std::int32_t chara = 0;        // > 0: the character; else the group's default
    std::int32_t x = 0, z = 0;     // Q12 metres
    std::int32_t rotation_y = 0;   // Q8 turn as the map stores it
    std::int32_t difficulty_min = 0;
};

struct SpawnRequest {
    int row = 0;
    int slot = 0;                  // the NPC slot to fill
    std::int32_t chara = 0;
    SpawnRow data;
};

// What a monster looks like to the director.
struct NpcView {
    std::int32_t chara = 0;
    std::int32_t x = 0, z = 0;     // Q12 metres
    std::int32_t health = 0;       // Q12
    bool no_radio_static = false;
    bool remains = false;          // a kept body (entities/remains.h): never freed by distance, only shown or hidden
};

enum class Visibility : std::uint8_t { Keep, Show, Hide };

struct NpcReview {
    bool free_slot = false;        // the monster is out of reach: remove it
    Visibility visibility = Visibility::Keep;
};

// The game, as the director needs it. All distances are Q12 metres.
class NpcHost {
public:
    virtual ~NpcHost() = default;
    // The player and the camera on the ground plane.
    virtual void player_position(std::int32_t& x, std::int32_t& z) = 0;
    virtual void camera_position(std::int32_t& x, std::int32_t& z) = 0;
    // The map is an interior drawn in one or two chunks (the radio and the despawn distance care).
    virtual bool chunked_interior() = 0;
    // The fog is thinned, so rings and despawn follow the camera's view.
    virtual bool long_view() = 0;
    virtual bool point_in_view(std::int32_t x, std::int32_t z) = 0;
    // How the fog edge is measured: true from the player, false from the camera.
    virtual bool hide_distance_from_player() = 0;

    // The spawn pass.
    virtual std::int32_t frame_vblanks() = 0;
    virtual bool spawning_blocked() = 0;                 // the map forbids new enemies
    virtual bool row_enabled(int row) = 0;               // not yet killed for good
    virtual bool row_occupied(int row) = 0;              // a live NPC already stands for it
    virtual bool difficulty_allows(std::int32_t minimum) = 0;
    virtual bool ground_allows(std::int32_t x, std::int32_t z) = 0;
    virtual std::uint32_t npc_slots_taken() = 0;         // bit set
    virtual std::int32_t npc_slot_limit() = 0;           // how many slots the map lets be live
    virtual void spawn(const SpawnRequest& request) = 0;

    // The radio's channels.
    virtual std::int32_t stereo_balance(std::int32_t x, std::int32_t z) = 0;
    virtual std::int32_t square_root_q12(std::int32_t v) = 0;
    virtual void radio_start(int channel) = 0;
    virtual void radio_update(int channel, std::int32_t balance, std::int32_t volume) = 0;
    virtual void radio_stop(int channel) = 0;
};

// The NPC slots of the game, as the per-tick pass drives them.
class NpcSlots {
public:
    virtual ~NpcSlots() = default;
    virtual int slot_count() = 0;
    virtual NpcView view(int slot) = 0;
    // Frees the slot (and the spawn row it stood for).
    virtual void free(int slot) = 0;
    virtual bool visible(int slot) = 0;
    virtual void set_visible(int slot, bool visible) = 0;
    // Lets the animation advance this tick.
    virtual void unlock_animation(int slot) = 0;
    // The character's animation file is resident.
    virtual bool animation_loaded(int slot) = 0;
    // The map gives the character a behaviour.
    virtual bool has_behaviour(int slot) = 0;
    // A character without a behaviour: loops `idle` (or holds its first keyframe when null) and places the root.
    virtual void pose_still(int slot, const NpcRules::IdleClip* idle, std::int32_t idle_duration) = 0;
    virtual void draw(int slot) = 0;
    virtual std::int32_t control_state(int slot) = 0;
    virtual void restart_state(int slot) = 0;
    // The per-tick record flags before the behaviour (damage, collision zones).
    virtual void before_behaviour(int slot) = 0;
    // An animation is bound, so the behaviour can pose the skeleton.
    virtual bool pose_ready(int slot) = 0;
    // The behaviour and the bookkeeping after it.
    virtual void behave(int slot) = 0;
};

// The radio's two static channels across ticks. They start zeroed (not "nobody"), as the game's memory did.
class RadioChannels {
public:
    std::array<RadioChannel, kRadioSlots>& channels() { return channels_; }
    const std::array<RadioChannel, kRadioSlots>& channels() const { return channels_; }
    // Forgets whom both channels followed and silences each (a room's first spawn pass).
    void forget(NpcHost& host);
    // Silences both loops; a monster still in reach starts its channel again on the next tick.
    void silence(NpcHost& host);

private:
    std::array<RadioChannel, kRadioSlots> channels_{RadioChannel{0, 0, 0, 0}, RadioChannel{0, 0, 0, 0}};
};

// Which characters the rules treat as monsters (ids at or below `last_monster`, but not 0 or the padlock).
struct NpcCharaIds {
    std::int32_t none = 0;
    std::int32_t padlock = 0;
    std::int32_t last_monster = 0;
};

class NpcDirector {
public:
    static constexpr int kRowCount = 32;
    static constexpr int kSlotCapacity = 32;

    NpcDirector(NpcRules rules, NpcCharaIds ids) : rules_(std::move(rules)), ids_(ids), radio_(radio_range()) {}

    const NpcRules& rules() const { return rules_; }
    const RadioTracker& radio() const { return radio_; }
    bool is_actor(std::int32_t chara) const { return chara != ids_.none && chara != ids_.padlock; }
    bool is_monster(std::int32_t chara) const { return chara <= ids_.last_monster; }

    // The spawn pass over a map's rows (run after the room's entry hooks).
    // `inner_ring`: a pass during play, not the room's first one; then a row inside the inner ring does not spawn
    // (the player sees that ground).
    void spawn_pass(NpcHost& host, bool inner_ring, const std::array<SpawnRow, kRowCount>& rows,
                    std::int32_t group_chara_low, std::int32_t group_chara_high);

    // Start of a tick: forget last tick's radio candidates.
    void begin_tick(NpcHost& host);
    // One live NPC.
    NpcReview review(NpcHost& host, int slot, const NpcView& npc);
    // End of a tick: the radio. Returns the pitch state for the music. `channels` are the radio channels'
    // memory; `radio_on` is the item toggle.
    int end_tick(NpcHost& host, bool radio_on, std::array<RadioChannel, kRadioSlots>& channels);

    // One tick of every NPC slot: review, pose or behave, draw; then the radio. Returns the pitch state.
    int update(NpcHost& host, NpcSlots& slots, RadioChannels& radio, bool radio_on);

    // The standing-idle clip of a cutscene actor with no behaviour, or nullptr.
    const NpcRules::IdleClip* idle_clip(std::int32_t chara) const;

    // Resolves idle_clips' "silenthill:Name" ids to the engine's numbers.
    template <typename Numbering>
    void resolve(Numbering&& number_of) {
        for (NpcRules::IdleClip& c : rules_.idle_clips) {
            c.number = number_of(c.chara);
        }
        NpcRules::StateRestart& sr = rules_.state_restart;
        sr.session_numbers.clear();
        sr.spawn_numbers.clear();
        for (const std::string& n : sr.session) {
            sr.session_numbers.push_back(number_of(n));
        }
        for (const std::string& n : sr.spawn) {
            sr.spawn_numbers.push_back(number_of(n));
        }
        session_restarted_.assign(sr.session_numbers.size(), false);
    }

private:
    std::uint32_t radio_range() const { return static_cast<std::uint32_t>(rules_.radio_range_sq); }

    NpcRules rules_;
    NpcCharaIds ids_;
    RadioTracker radio_;
    std::int32_t player_x_ = 0, player_z_ = 0;
    std::array<std::int32_t, 64> cooldown_{};
    // The state restarts: which session characters already had theirs, and per low slot the last restart
    // character it held and whether its restart is spent.
    std::vector<bool> session_restarted_;
    std::array<std::int32_t, kSlotCapacity> restart_chara_plus1_{};  // 0: none yet
    std::array<bool, kSlotCapacity> restart_done_{};

    void restart_if_due(NpcSlots& slots, int slot, std::int32_t chara);
};

}  // namespace sh::entities
