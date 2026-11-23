// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The debug console's selection: with the console open a click on a character or a world object selects it and
// a right click drops the selection; the commands select, info, goto, bring, freeze, nocollide and heal then act
// on what is selected. This file is the selection and what those commands keep per character (frozen, out of
// collision, the highest health seen); it knows the world only through PickWorld, so it runs in tests without
// the game. object_pick_host.cpp connects it to the running game.
//
// A click is not resolved when it arrives: the console's update runs before the frame is drawn, and what is
// under the pointer is a question about the picture the player sees. The click is kept and every character and
// object drawn in the next frame is tested against it, so only what is drawn can be picked.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::input {

enum class PickKind { None, Player, Npc, Prop };

// A point in the camera's frame and how the camera projects (the distance of the projection plane and the
// picture's centre in draw coordinates).
struct PickViewPoint {
    int x = 0, y = 0, z = 0;
};
struct PickProjection {
    int plane = 0;
    int centre_x = 0, centre_y = 0;
};
// The projection of a camera-frame point: draw coordinates and a depth, or false when the point is at or behind
// the camera. The same limits as the game's own projection of a single point, so the box a click is tested
// against is where the body is drawn.
bool pick_project(const PickViewPoint& view, const PickProjection& projection, float* x, float* y, int* depth);

struct PickPosition {
    int x = 0, y = 0, z = 0;
};

// What the selection needs from the game. Character slots here are NPC numbers; the player is slot -1.
class PickWorld {
public:
    virtual ~PickWorld() = default;
    virtual int npc_slots() const = 0;
    // The character type in a slot, 0 for an empty one.
    virtual int chara_id(int npc) const = 0;
    virtual PickPosition position(int npc) const = 0;  // world units with 12 fractional bits
    virtual int health(int npc) const = 0;
    // True for the types the player is drawn with: they are not targets of "select nearest".
    virtual bool is_player_type(int chara_id) const = 0;
    // The name the spawn command knows a type by, nullptr when it has none.
    virtual const char* chara_name(int chara_id) const = 0;
    virtual PickViewPoint view_point(const PickPosition& world) const = 0;
    virtual PickProjection projection() const = 0;
    // A window pixel in draw coordinates; false outside the picture.
    virtual bool pointer(int window_x, int window_y, float* x, float* y) const = 0;
    virtual void print(std::string_view line) = 0;
};

// A character about to be drawn.
struct PickCharacter {
    int npc = -1;  // -1: the player
    int chara_id = 0;
    PickPosition position;
    int box_top = 0, box_height = 0;  // its collision box, relative to position.y
    int radius = 0;                   // its collision cylinder
};

class ObjectPick {
public:
    explicit ObjectPick(PickWorld& world);

    // Everything back to nothing: a map change, a new game.
    void reset();

    // --- selecting ---
    void click_at(int window_x, int window_y);
    // After the frame: reports the click the frame just resolved.
    void frame_end();
    void clear(bool announce);
    bool select_npc(int npc);
    void select_player();
    // The living NPC nearest to the player, -1 for none.
    int nearest_npc() const;

    PickKind kind() const { return kind_; }
    int slot() const { return npc_; }  // the NPC number, -1 otherwise
    // The selected NPC's number, -1 when the selection is not an NPC or its slot now holds another character.
    int selected_npc() const;
    // "player", "npc[3] GROANER", "prop @ (1.0, 0.0, 2.5)", "nothing".
    std::string describe() const;
    // The selected object's place in world units; false when the selection is not an object.
    bool prop_position(int* x, int* y, int* z) const;

    // --- what is kept per character ---
    bool set_no_collide(bool on);
    bool no_collide_selected() const;
    bool set_frozen(int npc, bool on);
    bool frozen(int npc) const;
    int npc_max_health(int npc) const;

    // --- asked by the game every frame ---
    bool no_collide(PickKind kind, int npc, int chara_id) const;
    // Before an NPC's update: notes its health, and says whether the NPC is frozen (its update is then skipped).
    bool npc_tick(int npc, int chara_id, int health);
    // Before a character is drawn: tests a waiting click against it.
    void character_drawn(const PickCharacter& character);
    // True while a click waits for the frame that tests it: objects are tested only then.
    bool click_waiting() const { return click_ == Click::Waiting; }
    // The same test for an object at its placement (8 fractional bits).
    void prop_drawn(int x, int y, int z);

private:
    // An object's placement, 8 fractional bits.
    struct Prop {
        int x = 0, y = 0, z = 0;
    };
    // What is kept for an NPC slot. Each value remembers the character type it was set for: slots are reused, and
    // the next monster in a slot must not inherit the last one's state.
    struct NpcState {
        int frozen_for = -1;
        int no_collide_for = -1;
        int max_health = 0;
        int max_health_for = -1;
    };
    struct Hit {
        PickKind kind = PickKind::None;
        int npc = -1;
        int chara_id = -1;
        int depth = 0;
        Prop prop;
    };
    enum class Click { None, Waiting, Tested };

    bool valid_npc(int npc) const { return npc >= 0 && npc < static_cast<int>(npcs_.size()); }
    std::string describe(PickKind kind, int npc, int chara_id) const;
    bool project(const PickPosition& world, float* x, float* y, int* depth) const;
    bool closer(int depth) const { return best_.kind == PickKind::None || depth < best_.depth; }

    PickWorld& world_;

    PickKind kind_ = PickKind::None;
    int npc_ = -1;
    int chara_id_ = -1;
    Prop selected_prop_;

    std::vector<NpcState> npcs_;
    bool player_no_collide_ = false;
    // The collision query runs for every character every frame; this keeps it to one test in a session where
    // nothing was ever taken out of collision.
    bool any_no_collide_ = false;

    Click click_ = Click::None;
    int click_x_ = 0, click_y_ = 0;
    Hit best_;
};

// --- the running game (object_pick_host.cpp) ---

ObjectPick& object_pick();

// The same queries by the game's own character record (sh::entities::CharacterRecord of the session).
bool pick_no_collide(const void* character);
bool pick_npc_tick(const void* character, int npc);
// Before a character is drawn (npc = -1 for the player) and before a world object is drawn at its placement:
// a waiting click is tested against it.
void pick_character_drawn(const void* character, int npc);
void pick_prop_drawn(int x, int y, int z);

}  // namespace sh::input
