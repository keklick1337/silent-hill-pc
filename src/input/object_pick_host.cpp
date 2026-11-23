// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The console's selection (object_pick.h) in the running game: the world it asks is the session's characters and
// the frame's camera, its lines go to the console, and the game's own code asks it by character record.

#include "input/object_pick.h"

#include "engine/chara/runtime/chara_data.h"
#include "engine/debug/command_services.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "render/gpu/render_queries.h"
#include "render/gpu/render_state.h"

#include <string>

namespace sh::input {
namespace {

namespace gs = sh::game_state;
namespace ent = sh::entities;

// The session numbers the player 0 and the NPCs from 1.
int session_slot(int npc) { return npc + 1; }

class SessionPickWorld final : public PickWorld {
public:
    int npc_slots() const override { return gs::kNpcSlots; }
    int chara_id(int npc) const override { return gs::chara_id(session_slot(npc)); }
    PickPosition position(int npc) const override {
        const ent::Vec3 p = gs::chara_position(session_slot(npc));
        return {p.x, p.y, p.z};
    }
    int health(int npc) const override { return gs::chara_health(session_slot(npc)); }
    bool is_player_type(int chara_id) const override { return sh::chara::is_player_type(chara_id); }
    const char* chara_name(int chara_id) const override { return sh::debug::spawn_name_of(chara_id); }

    PickViewPoint view_point(const PickPosition& world) const override {
        std::int32_t v[3];
        gs::view_point(world.x, world.y, world.z, v);
        return {v[0], v[1], v[2]};
    }
    PickProjection projection() const override {
        const gs::ViewProjection p = gs::view_projection();
        return {p.h, p.offset_x, p.offset_y};
    }
    bool pointer(int window_x, int window_y, float* x, float* y) const override {
        float fx = 0.f;
        float fy = 0.f;
        if (!sh::gpu::window_to_picture(window_x, window_y, &fx, &fy)) {
            return false;
        }
        // The rect of draw coordinates the world pass fits to the picture: left, right, top, bottom.
        const float* rect = sh::render::published().world_ortho_rect;
        *x = rect[0] + fx * (rect[1] - rect[0]);
        *y = rect[2] + fy * (rect[3] - rect[2]);
        return true;
    }
    void print(std::string_view line) override { sh::debug::console_print(std::string(line).c_str()); }
};

// Whose record this is: the kind, the NPC number and the character type.
struct Subject {
    PickKind kind = PickKind::None;
    int npc = -1;
    int chara_id = 0;
};

Subject subject_of(const void* character) {
    Subject s;
    if (character == nullptr) {
        return s;
    }
    const int slot = sh::game::session().system.characters.slot_of(character);
    if (slot < 0) {
        return s;
    }
    s.kind = slot == 0 ? PickKind::Player : PickKind::Npc;
    s.npc = slot - 1;
    s.chara_id = static_cast<const ent::CharacterRecord*>(character)->model.chara_id;
    return s;
}

}  // namespace

ObjectPick& object_pick() {
    static SessionPickWorld world;
    static ObjectPick pick(world);
    return pick;
}

bool pick_no_collide(const void* character) {
    const Subject s = subject_of(character);
    return object_pick().no_collide(s.kind, s.npc, s.chara_id);
}

bool pick_npc_tick(const void* character, int npc) {
    if (character == nullptr) {
        return false;
    }
    const auto* record = static_cast<const ent::CharacterRecord*>(character);
    return object_pick().npc_tick(npc, record->model.chara_id, record->health);
}

void pick_character_drawn(const void* character, int npc) {
    if (character == nullptr) {
        return;
    }
    const auto* record = static_cast<const ent::CharacterRecord*>(character);
    PickCharacter c;
    c.npc = npc;
    c.chara_id = record->model.chara_id;
    c.position = {record->position.x, record->position.y, record->position.z};
    c.box_top = record->collision.box.top;
    c.box_height = record->collision.box.height;
    c.radius = record->collision.cylinder.chara_radius;
    object_pick().character_drawn(c);
}

void pick_prop_drawn(int x, int y, int z) {
    ObjectPick& pick = object_pick();
    if (pick.click_waiting()) {
        pick.prop_drawn(x, y, z);
    }
}

}  // namespace sh::input
