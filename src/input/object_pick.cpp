// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A click is tested in draw coordinates: the space the world's projection puts points in, which the renderer then
// fits to the picture. The pointer is taken into that space instead of assuming a 320x224 frame, so the test
// holds with any field of view, with bars and with a stretched picture.

#include "input/object_pick.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sh::input {
namespace {

constexpr int kEmptySlot = 0;  // character type of an empty slot

// Sizes in world units with 12 fractional bits.
constexpr int kAssumedHeight = 6553;   // 1.6: a body without a usable collision box
constexpr int kLeastBoxHeight = 1024;  // 0.25: under this a box is not usable
constexpr int kLeastRadius = 1228;     // 0.3
constexpr int kPropReach = 2867;       // 0.7: an object has no shape, so a click near its origin counts

// Draw-coordinate slack so that a near miss still selects, and the least half size of a box.
constexpr float kSlack = 6.0f;
constexpr float kLeastHalf = 10.0f;
constexpr float kLeastPropReach = 8.0f;

template <typename T>
int clamped(T value, int low, int high) {
    return static_cast<int>(std::clamp<T>(value, low, high));
}

std::string format(const char* fmt, auto... args) {
    char text[160];
    std::snprintf(text, sizeof(text), fmt, args...);
    return text;
}

float units(int fixed) { return static_cast<float>(fixed) / 4096.0f; }

}  // namespace

bool pick_project(const PickViewPoint& view, const PickProjection& projection, float* x, float* y, int* depth) {
    // 16-bit camera coordinates, a ratio of plane to distance that saturates just under 2, a screen box of
    // +-1024, and a depth key of a quarter of the distance.
    constexpr std::int64_t kMostRatio = 0x1FFFF;
    const int vx = clamped(view.x, -0x8000, 0x7FFF);
    const int vy = clamped(view.y, -0x8000, 0x7FFF);
    const int vz = clamped(view.z, 0, 0xFFFF);
    std::int64_t ratio = kMostRatio;
    if (vz > 0 && projection.plane < vz * 2) {
        ratio = std::min((static_cast<std::int64_t>(projection.plane) << 16) / vz, kMostRatio);
    }
    const int key = vz >> 2;
    if (key <= 8) {
        return false;
    }
    *x = static_cast<float>(
        clamped<std::int64_t>((static_cast<std::int64_t>(projection.centre_x) * 65536 + vx * ratio) >> 16, -0x400, 0x3FF));
    *y = static_cast<float>(
        clamped<std::int64_t>((static_cast<std::int64_t>(projection.centre_y) * 65536 + vy * ratio) >> 16, -0x400, 0x3FF));
    *depth = key;
    return true;
}

ObjectPick::ObjectPick(PickWorld& world) : world_(world) { reset(); }

void ObjectPick::reset() {
    npcs_.assign(static_cast<std::size_t>(world_.npc_slots()), NpcState{});
    player_no_collide_ = false;
    any_no_collide_ = false;
    kind_ = PickKind::None;
    npc_ = -1;
    chara_id_ = -1;
    click_ = Click::None;
}

bool ObjectPick::project(const PickPosition& world, float* x, float* y, int* depth) const {
    return pick_project(world_.view_point(world), world_.projection(), x, y, depth);
}

std::string ObjectPick::describe(PickKind kind, int npc, int chara_id) const {
    switch (kind) {
        case PickKind::Player: return "player";
        case PickKind::Prop:
            return format("prop @ (%.1f, %.1f, %.1f)", selected_prop_.x / 256.0f, selected_prop_.y / 256.0f,
                          selected_prop_.z / 256.0f);
        case PickKind::Npc:
            if (const char* name = world_.chara_name(chara_id)) {
                return format("npc[%d] %s", npc, name);
            }
            return format("npc[%d] chara %d", npc, chara_id);
        default: return "nothing";
    }
}

std::string ObjectPick::describe() const { return describe(kind_, npc_, chara_id_); }

void ObjectPick::click_at(int window_x, int window_y) {
    click_ = Click::Waiting;
    click_x_ = window_x;
    click_y_ = window_y;
    best_.kind = PickKind::None;
    best_.npc = -1;
    best_.chara_id = -1;
    best_.depth = 0;
}

void ObjectPick::clear(bool announce) {
    if (announce) {
        world_.print(kind_ == PickKind::None ? std::string("nothing was selected") : "deselected " + describe());
    }
    kind_ = PickKind::None;
    npc_ = -1;
    chara_id_ = -1;
}

bool ObjectPick::select_npc(int npc) {
    if (!valid_npc(npc)) {
        return false;
    }
    const int id = world_.chara_id(npc);
    if (id == kEmptySlot) {
        return false;
    }
    kind_ = PickKind::Npc;
    npc_ = npc;
    chara_id_ = id;
    return true;
}

void ObjectPick::select_player() {
    kind_ = PickKind::Player;
    npc_ = -1;
    chara_id_ = world_.chara_id(-1);
}

int ObjectPick::nearest_npc() const {
    const PickPosition player = world_.position(-1);
    int nearest = -1;
    std::int64_t nearest_sq = 0;
    for (int npc = 0; npc < static_cast<int>(npcs_.size()); ++npc) {
        const int id = world_.chara_id(npc);
        if (id == kEmptySlot || world_.is_player_type(id) || world_.health(npc) <= 0) {
            continue;
        }
        const PickPosition at = world_.position(npc);
        const std::int64_t dx = static_cast<std::int64_t>(at.x) - player.x;
        const std::int64_t dz = static_cast<std::int64_t>(at.z) - player.z;
        const std::int64_t sq = dx * dx + dz * dz;
        if (nearest < 0 || sq < nearest_sq) {
            nearest = npc;
            nearest_sq = sq;
        }
    }
    return nearest;
}

int ObjectPick::selected_npc() const {
    if (kind_ != PickKind::Npc || !valid_npc(npc_)) {
        return -1;
    }
    const int id = world_.chara_id(npc_);
    return id != kEmptySlot && id == chara_id_ ? npc_ : -1;
}

bool ObjectPick::prop_position(int* x, int* y, int* z) const {
    if (kind_ != PickKind::Prop) {
        return false;
    }
    // Placements have 8 fractional bits, the world 12.
    *x = selected_prop_.x << 4;
    *y = selected_prop_.y << 4;
    *z = selected_prop_.z << 4;
    return true;
}

bool ObjectPick::npc_tick(int npc, int chara_id, int health) {
    if (!valid_npc(npc)) {
        return false;
    }
    NpcState& s = npcs_[static_cast<std::size_t>(npc)];
    // An enemy sets its own health on its first update and no record holds its full health, so the most seen is
    // what "heal" gives back.
    if (s.max_health_for != chara_id) {
        s.max_health_for = chara_id;
        s.max_health = 0;
        s.frozen_for = -1;
    }
    s.max_health = std::max(s.max_health, health);
    return s.frozen_for == chara_id;
}

int ObjectPick::npc_max_health(int npc) const {
    if (!valid_npc(npc)) {
        return 0;
    }
    const NpcState& s = npcs_[static_cast<std::size_t>(npc)];
    return s.max_health_for == world_.chara_id(npc) ? s.max_health : 0;
}

bool ObjectPick::set_no_collide(bool on) {
    if (kind_ == PickKind::Player) {
        player_no_collide_ = on;
    } else if (kind_ == PickKind::Npc && valid_npc(npc_)) {
        if (world_.chara_id(npc_) == kEmptySlot) {
            return false;
        }
        npcs_[static_cast<std::size_t>(npc_)].no_collide_for = on ? chara_id_ : -1;
    } else {
        // Objects are part of the map's collision, not characters: there is nothing of theirs to switch off.
        return false;
    }
    any_no_collide_ = any_no_collide_ || on;
    return true;
}

bool ObjectPick::no_collide(PickKind kind, int npc, int chara_id) const {
    if (!any_no_collide_) {
        return false;
    }
    if (kind == PickKind::Player) {
        return player_no_collide_;
    }
    if (kind != PickKind::Npc || !valid_npc(npc)) {
        return false;
    }
    const int kept_for = npcs_[static_cast<std::size_t>(npc)].no_collide_for;
    return kept_for != -1 && kept_for == chara_id;
}

bool ObjectPick::no_collide_selected() const {
    if (kind_ == PickKind::Player) {
        return player_no_collide_;
    }
    return kind_ == PickKind::Npc && valid_npc(npc_) && no_collide(PickKind::Npc, npc_, world_.chara_id(npc_));
}

bool ObjectPick::set_frozen(int npc, bool on) {
    if (!valid_npc(npc)) {
        return false;
    }
    const int id = world_.chara_id(npc);
    if (id == kEmptySlot) {
        return false;
    }
    npcs_[static_cast<std::size_t>(npc)].frozen_for = on ? id : -1;
    return true;
}

bool ObjectPick::frozen(int npc) const {
    if (!valid_npc(npc)) {
        return false;
    }
    const int id = world_.chara_id(npc);
    return id != kEmptySlot && npcs_[static_cast<std::size_t>(npc)].frozen_for == id;
}

void ObjectPick::character_drawn(const PickCharacter& c) {
    const PickKind kind = c.npc < 0 ? PickKind::Player : PickKind::Npc;

    // The slot now holds another character: the selection was not meant for it.
    if (kind == PickKind::Npc && kind_ == PickKind::Npc && c.npc == npc_ && c.chara_id != chara_id_) {
        clear(false);
    }
    if (click_ != Click::Waiting) {
        return;
    }

    // The body as a box on screen: as tall as its collision box and as wide as its cylinder. The box's two ends
    // are relative to the position with a sign that differs between characters, so they are ordered here.
    int low = c.position.y + std::min(c.box_top, c.box_height);
    int high = c.position.y + std::max(c.box_top, c.box_height);
    if (high - low < kLeastBoxHeight) {
        low = c.position.y - kAssumedHeight;
        high = c.position.y;
    }
    float x_low, y_low, x_high, y_high, pointer_x, pointer_y;
    int depth_low, depth_high;
    if (!project({c.position.x, low, c.position.z}, &x_low, &y_low, &depth_low) ||
        !project({c.position.x, high, c.position.z}, &x_high, &y_high, &depth_high) ||
        !world_.pointer(click_x_, click_y_, &pointer_x, &pointer_y)) {
        return;
    }
    const float centre_x = (x_low + x_high) * 0.5f;
    const float centre_y = (y_low + y_high) * 0.5f;
    float half_height = std::fabs(y_low - y_high) * 0.5f;

    // The width comes from the cylinder and not from the height: a dog is low and wide, and a box derived from
    // its height was a sliver that took several clicks to hit.
    const int radius = std::max(std::abs(c.radius), kLeastRadius);
    float side_x, side_y;
    int side_depth;
    float half_width = half_height * 0.6f;
    if (project({c.position.x + radius, (low + high) / 2, c.position.z}, &side_x, &side_y, &side_depth)) {
        half_width = std::fabs(side_x - centre_x);
    }
    half_height = std::max(half_height + kSlack, kLeastHalf);
    half_width = std::max(half_width + kSlack, kLeastHalf);
    if (pointer_x < centre_x - half_width || pointer_x > centre_x + half_width ||
        pointer_y < centre_y - half_height || pointer_y > centre_y + half_height) {
        return;
    }

    // Of bodies that overlap on screen the nearest is the one clicked.
    const int depth = std::min(depth_low, depth_high);
    if (closer(depth)) {
        best_.kind = kind;
        best_.npc = c.npc;
        best_.chara_id = c.chara_id;
        best_.depth = depth;
    }
}

void ObjectPick::prop_drawn(int x, int y, int z) {
    if (click_ != Click::Waiting) {
        return;
    }

    const PickPosition origin{x << 4, y << 4, z << 4};
    float at_x, at_y, side_x, side_y, pointer_x, pointer_y;
    int depth, side_depth;
    if (!project(origin, &at_x, &at_y, &depth) ||
        !project({origin.x + kPropReach, origin.y, origin.z}, &side_x, &side_y, &side_depth) ||
        !world_.pointer(click_x_, click_y_, &pointer_x, &pointer_y)) {
        return;
    }
    const float reach = std::max(std::fabs(side_x - at_x), kLeastPropReach);
    if (pointer_x < at_x - reach || pointer_x > at_x + reach || pointer_y < at_y - reach || pointer_y > at_y + reach) {
        return;
    }
    // At the same depth a character already found stays: it is the likelier target, and objects are everywhere.
    if (closer(depth)) {
        best_.kind = PickKind::Prop;
        best_.npc = -1;
        best_.depth = depth;
        best_.prop = {x, y, z};
    }
}

void ObjectPick::frame_end() {
    if (click_ == Click::Waiting) {
        // The frame drawn since the click has tested it; the report follows on the next call.
        click_ = Click::Tested;
        return;
    }
    if (click_ != Click::Tested) {
        return;
    }
    click_ = Click::None;
    if (best_.kind == PickKind::None) {
        world_.print("nothing there to select");
        return;
    }

    kind_ = best_.kind;
    npc_ = best_.npc;
    chara_id_ = best_.chara_id;
    if (kind_ == PickKind::Prop) {
        selected_prop_ = best_.prop;
    }
    const std::string what = describe();
    if (kind_ == PickKind::Prop) {
        world_.print("selected " + what);
        return;
    }
    const int slot = kind_ == PickKind::Player ? -1 : npc_;
    const PickPosition at = world_.position(slot);
    world_.print(format("selected %s  hp %.1f  pos (%.1f, %.1f, %.1f)", what.c_str(), units(world_.health(slot)),
                        units(at.x), units(at.y), units(at.z)));
}

}  // namespace sh::input
