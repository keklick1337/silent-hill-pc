// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The characters of the running map: slot 0 is the player, slots 1.. the
// map's NPCs, the same numbers sh.npc.* and the cutscene code use. The Lua API (sh.entity.*) and the cutscene
// player reach characters only through here.
//
// Storage: the slot records are sh::game::Session's (session().system.characters, or the block a test hands
// set_storage()); a Character handed out is bound to its record, so reads and writes go straight to it.
//
// The game spawns and removes its characters and poses their bones in its own buffers. A Host (installed by
// src/engine/entities/game/entity_host.cpp) answers what this class does not hold: the type's data and variable
// layout, the bones and animation file of a slot, spawning and removal through the game's spawn code.

#pragma once

#include "engine/entities/character.h"
#include "engine/entities/character_record.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace sh::chara {
class BehaviorLayout;
}

namespace sh::entities {

class EntityWorld {
public:
    static constexpr int kSlotCount = 1 + kNpcSlotCount;  // the player and the NPC slots of the engine

    // The bones the game poses for a slot this tick and the animation file they come from.
    struct BoneBinding {
        BoneCoord* bones = nullptr;
        std::shared_ptr<const AnimationSet> animation;
    };

    struct Host {
        virtual ~Host() = default;
        virtual const sh::chara::CharacterDef* def(int chara_id) = 0;
        // The variable layout of the type (state.json), nullptr when it has none.
        virtual const sh::chara::BehaviorLayout* var_layout(int chara_id) = 0;
        // The first layout of any type that names `name`, nullptr when none does.
        virtual const sh::chara::BehaviorLayout* var_layout_naming(std::string_view name) = 0;
        virtual BoneBinding bones(int slot) = 0;
        // Spawns chara_id into a free NPC slot at `position` (y: the ground there) with its behaviour entry
        // state `spawn_state`; returns the slot or -1.
        virtual int create(int chara_id, const Vec3& position, const Angles& rotation, int spawn_state) = 0;
        virtual bool remove(int slot) = 0;
    };

    void set_host(std::unique_ptr<Host> host) { host_ = std::move(host); }
    Host* host() const { return host_.get(); }

    // The slot records (default: the session's).
    void set_storage(CharacterSlots* slots) { storage_ = slots; }
    CharacterSlots& storage();

    // A character lives in `slot` (its record names a type).
    bool live(int slot);
    // The live character in `slot`, bound to its record; nullptr when the slot is empty or out of range.
    Character* character(int slot);
    int create(int chara_id, const Vec3& position, const Angles& rotation, int spawn_state);
    bool remove(int slot);

    // A behaviour variable of the slot by name: its type's layout first, then any type's (a map event sets a
    // boss's variables before the record becomes the boss: the bytes are the same block). Nothing when no layout
    // names it. Slot -1 is the record the original reads before the NPC table when Harry's grab index is unset.
    std::optional<long long> var(int slot, std::string_view name);
    bool set_var(int slot, std::string_view name, long long value);

    // References that outlive a removal: resolve() gives nullptr once the slot's character changed.
    CharacterRef ref(int slot) const;
    Character* resolve(const CharacterRef& ref);

    // The Character object of a slot without binding it.
    Character& slot_object(int slot) { return slots_[static_cast<std::size_t>(slot)]; }

private:
    CharacterRecord* var_record(int slot);

    std::unique_ptr<Host> host_;
    CharacterSlots* storage_ = nullptr;
    std::array<Character, kSlotCount> slots_{};
    std::array<std::uint32_t, kSlotCount> generations_{};
};

// The world of the running game (one per process, like the game's own character records).
EntityWorld& entity_world();

}  // namespace sh::entities
