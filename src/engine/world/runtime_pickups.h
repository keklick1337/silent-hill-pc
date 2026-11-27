// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Runtime world pickups: items that scripts place on the current map
// while it is loaded. Each behaves like a pickup of the map data: its model lies at its pose, the action button
// facing it runs the game's item prompt, "yes" puts the item in the inventory and sets the pickup's event flag, and a
// set flag hides it for good (the flag is saved by name, so it stays taken across save and load).
//
// This is the registry only (no game types): the game bridge resolves names
// through a Resolver, draws what visible() selects, and asks reachable() when the action button goes down.
// Positions are Q19.12 world units (metres x 4096, y negative up, as the game and `pos` use), angles Q3.12.

#pragma once

#include "engine/world/script_origin.h"
#include "formats/common/status.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::world {

struct PickupSpec {
    std::string id;       // unique on the map; empty: the registry makes one ("<owner>:pickup<N>")
    std::string owner;    // mod id, "silenthill" for the base scripts
    std::string item;     // item name ("Ampoule", "mymod:Crowbar")
    std::string model;    // world model (LM name, <= 8 characters); empty: the item's world_model
    std::string flag;     // event flag name; set = taken
    std::string message;  // prompt message key of the map; empty: the item's common pickup prompt
    int count = 0;        // 0: the game's own amount for the item (ammo: pickup count x ammo multiplier)
    std::array<std::int32_t, 3> position{};
    std::array<std::int16_t, 3> rotation{};
    bool ground = false;  // position[1] is not given: the resolver puts the pickup on the ground at (x, z)
    // model = false in the script: nothing is drawn for it (the mod shows the thing itself, a world object of its own
    // model); it is still taken up where it lies.
    bool unseen = false;
    ScriptOrigin origin;  // the script call that made it (the modding tools show it)
};

// What the game made of the names (filled by the Resolver); the bridge uses these numbers every frame.
struct PickupResolved {
    int item = -1;
    int flag = -1;
    int message = -1;        // -1: the common pickup path (its own prompt)
    int common_kind = -1;    // the item's common pickup kind, -1: none
};

struct Pickup {
    PickupSpec spec;
    PickupResolved resolved;
    std::uint64_t serial = 0;  // creation order across the session
};

class PickupResolver {
public:
    virtual ~PickupResolver() = default;
    // Checks the names of `spec` against the game and fills `out`; may fill spec defaults (model, ground height).
    virtual Result<void> resolve(PickupSpec& spec, PickupResolved& out) = 0;
};

class PickupRegistry {
public:
    // Enough for every mod on one map; the game's draw list holds a few dozen objects per frame.
    static constexpr std::size_t kMaxPickups = 64;
    // Drawn when the player is this close (horizontal metres, Q12) and on about the same storey.
    static constexpr std::int32_t kDrawDistance = 32 * 4096;
    static constexpr std::int32_t kStoreyHeight = 3 * 4096;

    // A new map is being loaded: every pickup of the previous one goes. An empty name means no map (title, menus).
    void begin_map(std::string_view map);
    const std::string& map() const { return map_; }

    void set_resolver(PickupResolver* resolver) { resolver_ = resolver; }

    // The id of the new pickup, or why there is none (no map loaded, duplicate id, unknown names, full).
    Result<std::string> create(PickupSpec spec);
    bool remove(std::string_view id);
    // A pickup created without a height lies on the ground once the host could sample it (collision loads after the
    // map script ran); until then it is neither drawn nor reachable.
    bool place_on_ground(std::string_view id, std::int32_t height);
    // Moves a pickup where it lies now (the modding tools' placement editor); it keeps the height given, not the
    // ground's. False when there is none.
    bool set_pose(std::string_view id, const std::array<std::int32_t, 3>& position,
                  const std::array<std::int16_t, 3>& rotation);
    std::size_t remove_owner(std::string_view owner);

    bool empty() const { return pickups_.empty(); }
    const std::vector<Pickup>& all() const { return pickups_; }
    const Pickup* find(std::string_view id) const;
    // Changes whenever the list changes (the bridge rebuilds its draw records then).
    std::uint64_t revision() const { return revision_; }

    using FlagQuery = std::function<bool(int flag)>;
    // Indices into all() of the pickups to draw this frame: not taken, near the player.
    std::vector<std::size_t> visible(const std::array<std::int32_t, 3>& player, const FlagQuery& flag_set) const;
    // The first pickup (creation order) not taken, on the player's storey, that `in_reach` accepts; null: none.
    const Pickup* reachable(const std::array<std::int32_t, 3>& player, const FlagQuery& flag_set,
                            const std::function<bool(const Pickup&)>& in_reach) const;
    // Pickup `id` if it lies on the map now (not taken, on the ground); null otherwise.
    const Pickup* present(std::string_view id, const FlagQuery& flag_set) const;

private:
    std::string map_;
    bool map_loaded_ = false;
    std::vector<Pickup> pickups_;
    PickupResolver* resolver_ = nullptr;
    std::uint64_t revision_ = 0;
    std::uint64_t serial_ = 0;
};

// The process-wide registry of the running game.
PickupRegistry& pickups();

// A map message a mod's script asked for (sh.world.message): the trigger scan starts it like a message event of the
// map once Harry is free. One waits at a time; a new request replaces it. -1 when none waits.
void request_script_message(int message);
int pending_script_message();
void clear_script_message();

// A place a script makes something happen at (a trigger zone of its own, a save point, a door it shuts): only the
// modding tools' overlay draws it, so the editor shows a mod's live story (sh.world.mark). Gone at a map change.
struct ScriptMark {
    std::string owner;  // mod id
    std::string id;     // unique per owner
    std::string label;
    std::string kind;   // free text the overlay shows ("trigger", "save", "door")
    std::int32_t x = 0, z = 0;  // Q12
    std::int32_t radius = 0;    // Q12
    ScriptOrigin origin;        // the script call that made it
};
void set_script_mark(ScriptMark mark);
// Removes one mark of `owner` (or all of them with an empty id); true when one went.
bool clear_script_marks(std::string_view owner, std::string_view id);
const std::vector<ScriptMark>& script_marks();

// A script asks for pickup `id`'s prompt as if the player had taken it up (sh.world.pickup_take): it starts when no
// map event runs and the player is free. Empty when none waits.
void request_pickup_take(std::string id);
const std::string& pending_pickup_take();
void clear_pickup_take();

// A map message shown over play (sh.world.say): drawn every frame of free gameplay while the world goes on, advanced
// or closed by the confirm button like any message. A map event that takes over drops it. -1 when none shows.
void request_soft_message(int message);
int soft_message();
// The message box set the soft message up (its first frame is done).
bool soft_message_started();
void set_soft_message_started();
void clear_soft_message();

}  // namespace sh::world
