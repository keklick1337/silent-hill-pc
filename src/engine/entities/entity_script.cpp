// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/entity_script.h"

#include "engine/entities/entity_world.h"
#include "engine/game_state/session.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sh::entities {
namespace {

using NamedCall = int (*)(const char*, int, const ShScriptArg*, long long*, int, char*, int);
using NamedList = const char* const* (*)(void);

struct Args {
    int argc;
    const ShScriptArg* argv;
    bool has(int i) const { return i < argc && argv[i].kind != 0; }
    long long integer(int i, long long fallback = 0) const {
        return i < argc && (argv[i].kind == 1 || argv[i].kind == 3) ? argv[i].i : fallback;
    }
    const char* text(int i) const { return i < argc && argv[i].kind == 2 ? argv[i].s : nullptr; }
};

int fail(char* err, int cap, const char* msg) {
    std::snprintf(err, static_cast<std::size_t>(cap), "%s", msg);
    return -1;
}

const char* const* chain_list(NamedList next, const char* const* own_begin, const char* const* own_end,
                              std::vector<const char*>& names) {
    if (names.empty()) {
        if (next != nullptr) {
            for (const char* const* n = next(); n != nullptr && *n != nullptr; ++n) {
                names.push_back(*n);
            }
        }
        names.insert(names.end(), own_begin, own_end);
        names.push_back(nullptr);
    }
    return names.data();
}

// ---------------------------------------------------------------------------------------------
// Core group

EntityScriptHost* s_host = nullptr;
NamedCall s_core_next_call = nullptr;
NamedList s_core_next_list = nullptr;

const char* const kCoreNames[] = {
    "entity.create", "entity.remove", "entity.exists", "entity.chara", "entity.position", "entity.set_position",
    "entity.rotation", "entity.set_rotation", "entity.play", "entity.anim", "entity.anim_set", "entity.clip",
    "entity.keyframe_passed", "entity.bone_count", "entity.bone", "entity.sound", "entity.param", "entity.health",
    "entity.set_health", "entity.has_ai", "entity.engine_spawn", "entity.spawn_kind",
};

bool npc_slot(int slot) { return slot >= 1 && slot <= kNpcSlotCount; }

int core_call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    EntityWorld& world = entity_world();
    if (fn == "create") {
        // create(chara, x, z, rot_y[, state[, ai]]): the height comes from the ground; `state` is the behaviour's
        // entry state (nil: character.json "spawn.state", else 3); `ai` "auto" (default), "full", "fallback" or
        // "none". The model and animations load when the map did not.
        const int id = a.text(0) != nullptr ? s_host->chara_id(a.text(0)) : static_cast<int>(a.integer(0, -1));
        const int ai = s_host->ai_mode(a.text(5));
        if (ai < 0) {
            return fail(err, errCap, "unknown ai (auto, full, fallback, none)");
        }
        out[0] = s_host->spawn(id, a.has(4) ? static_cast<int>(a.integer(4)) : -1, static_cast<int>(a.integer(1)),
                               static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)), ai);
        return 1;
    }
    const int slot = static_cast<int>(a.integer(0, -1));
    if (fn == "has_ai") {
        // has_ai(slot): 1 when the character in the slot runs a behaviour this tick (else it stands posed); second
        // value: what an engine spawn chose (0 the map's).
        out[0] = npc_slot(slot) && world.live(slot) && s_host->has_behavior(slot) ? 1 : 0;
        out[1] = npc_slot(slot) ? s_host->spawn_choice(slot) : 0;
        return 2;
    }
    if (fn == "engine_spawn") {
        // engine_spawn(slot): 1 for a character the engine created (console, debug menu, create), not the map's
        // room spawner: it has no spawn row, and the story flags of its home map do not stage it.
        out[0] = npc_slot(slot) && s_host->engine_spawned(slot) ? 1 : 0;
        return 1;
    }
    if (fn == "spawn_kind") {
        // spawn_kind(slot): what the character is (character.json "spawn.kind"): 0 enemy, 1 boss, 2 actor, 3 prop.
        const int id = npc_slot(slot) ? world.storage().record(slot)->model.chara_id : -1;
        const std::string_view kind = id > 0 ? s_host->spawn_kind(id) : "prop";
        out[0] = kind == "enemy" ? 0 : kind == "boss" ? 1 : kind == "actor" ? 2 : 3;
        return 1;
    }
    if (fn == "remove") {
        out[0] = world.remove(slot) ? 1 : 0;
        return 1;
    }
    Character* c = world.character(slot);
    if (fn == "exists") {
        out[0] = c != nullptr ? 1 : 0;
        return 1;
    }
    if (c == nullptr) {
        return fail(err, errCap, "no character in that slot");
    }
    if (fn == "chara") {
        out[0] = c->chara_id();
        return 1;
    }
    if (fn == "position") {
        out[0] = c->position().x, out[1] = c->position().y, out[2] = c->position().z;
        return 3;
    }
    if (fn == "set_position") {
        c->set_position({static_cast<std::int32_t>(a.integer(1)), static_cast<std::int32_t>(a.integer(2)),
                         static_cast<std::int32_t>(a.integer(3))});
        return 0;
    }
    if (fn == "rotation") {
        out[0] = c->rotation().x, out[1] = c->rotation().y, out[2] = c->rotation().z;
        return 3;
    }
    if (fn == "set_rotation") {
        c->set_rotation({static_cast<std::int16_t>(a.integer(1)), static_cast<std::int16_t>(a.integer(2)),
                         static_cast<std::int16_t>(a.integer(3))});
        return 0;
    }
    if (fn == "play") {
        const bool blend = a.integer(2, 1) != 0;
        bool ok;
        if (const char* name = a.text(1)) {
            ok = c->play(std::string_view(name), blend);
        } else {
            c->play(static_cast<int>(a.integer(1, -1)), blend);
            ok = a.integer(1, -1) >= 0;
        }
        out[0] = ok ? 1 : 0;
        return 1;
    }
    if (fn == "anim") {
        const AnimState s = c->anim_state();
        out[0] = s.status >> 1, out[1] = s.status & 1, out[2] = s.keyframe, out[3] = s.time;
        return 4;
    }
    if (fn == "anim_set") {
        AnimState s = c->anim_state();
        s.status = static_cast<std::uint8_t>(a.integer(1, s.status));
        if (a.has(2)) {
            s.time = static_cast<std::int32_t>(a.integer(2));
            s.keyframe = s.time >> 12;
        }
        c->set_anim_state(s);
        return 0;
    }
    if (fn == "clip") {
        const char* name = a.text(1);
        out[0] = name != nullptr ? c->clip_index(name) : -1;
        return 1;
    }
    if (fn == "keyframe_passed") {
        out[0] = keyframe_crossed(static_cast<std::int32_t>(a.integer(1)), c->anim_state().time,
                                  static_cast<std::int32_t>(a.integer(2)))
                     ? 1
                     : 0;
        return 1;
    }
    if (fn == "bone_count") {
        out[0] = c->bone_count();
        return 1;
    }
    if (fn == "bone") {
        const int bone = static_cast<int>(a.integer(1, -1));
        if (bone < 0 || bone >= c->bone_count()) {
            return fail(err, errCap, "bone out of range");
        }
        const auto p = c->bone_world_position(bone);
        out[0] = p[0], out[1] = p[1], out[2] = p[2];
        return 3;
    }
    if (fn == "sound") {
        const char* name = a.text(1);
        out[0] = name != nullptr ? c->sound(name, static_cast<int>(a.integer(2, -1))) : a.integer(2, -1);
        return 1;
    }
    if (fn == "param") {
        const char* name = a.text(1);
        const auto fallback = static_cast<std::int32_t>(a.integer(2, 0));
        out[0] = name != nullptr ? c->param_q12(name, fallback) : fallback;
        return 1;
    }
    if (fn == "health") {
        out[0] = c->health();
        return 1;
    }
    if (fn == "set_health") {
        c->set_health(static_cast<std::int32_t>(a.integer(1)));
        return 0;
    }
    return fail(err, errCap, "unknown function");
}

int core_named_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int outCap, char* err,
                    int errCap) {
    if (name != nullptr && std::strncmp(name, "entity.", 7) == 0) {
        if (outCap < 4) {
            return fail(err, errCap, "output too small");
        }
        return core_call(std::string_view(name + 7), Args{argc, argv}, out, err, errCap);
    }
    return s_core_next_call != nullptr ? s_core_next_call(name, argc, argv, out, outCap, err, errCap)
                                       : fail(err, errCap, "not available");
}

const char* const* core_named_list() {
    static std::vector<const char*> names;
    return chain_list(s_core_next_list, std::begin(kCoreNames), std::end(kCoreNames), names);
}

// ---------------------------------------------------------------------------------------------
// Enemy group

EnemyHost* s_enemy = nullptr;
NamedCall s_enemy_next_call = nullptr;
NamedList s_enemy_next_list = nullptr;

enum class Kind : std::uint8_t { S8, U8, S16, U16, S32, U32 };

struct Field {
    const char* name;
    std::size_t offset;
    Kind kind;
};

// The kind of a field, checked against its size: a field widened in the record without its entry here fails to build.
template <std::size_t Size>
consteval Kind sized_kind(Kind kind) {
    const std::size_t size = (kind == Kind::S8 || kind == Kind::U8)    ? 1
                             : (kind == Kind::S16 || kind == Kind::U16) ? 2
                                                                        : 4;
    return size == Size ? kind : throw "the field kind does not match the size of the field";
}

#define F(name, member, kind) \
    {name, offsetof(CharacterRecord, member), sized_kind<sizeof(std::declval<CharacterRecord&>().member)>(Kind::kind)}

// The player's block: offsets of his variables the enemies read.
constexpr std::size_t kPlayerNoiseVar = 0x24;      // loudness of his movement the enemies hear
constexpr std::size_t kPlayerMoveSpeedVar = 0x3E;  // his forward speed

// Record fields the enemies use beyond the core calls. Each type's own variables are data
// (gamedata/chara/<Name>/state.json).
const Field kFields[] = {
    F("rot_speed_x", rotation_speed.x, S16),
    F("rot_speed_y", rotation_speed.y, S16),
    F("rot_speed_z", rotation_speed.z, S16),
    F("angle_to_target_speed", angle_to_target_speed, S16),
    // NPCs: the row of the map's spawn table the character came from; Harry: the NPC attacking him.
    F("spawn_index", partner, S32),
    F("attacker_index", partner, S32),
    // The attack window of the character's swing (1 while its hit sphere is live) and the attack it performs.
    F("attack_window", attack.phase, S16),
    F("attack_performed", attack.attack, S32),
    F("damage_x", damage.push.x, S32),
    F("damage_y", damage.push.y, S32),
    F("damage_z", damage.push.z, S32),
    F("damage_amount", damage.amount, S32),
    F("box_height", collision.box.height, S16),
    F("box_extent_x", collision.box.extent_x, S16),
    F("box_extent_z", collision.box.extent_z, S16),
    F("collision_knocked_down", collision.pass_stamp, U8),
    {"player_noise", offsetof(CharacterRecord, vars) + kPlayerNoiseVar, Kind::U8},
    {"player_move_speed", offsetof(CharacterRecord, vars) + kPlayerMoveSpeedVar, Kind::S16},
};

#undef F

const Field* field_find(const char* name) {
    static const std::unordered_map<std::string_view, const Field*> index = [] {
        std::unordered_map<std::string_view, const Field*> m;
        for (const Field& f : kFields) {
            m.emplace(f.name, &f);
        }
        return m;
    }();
    const auto it = index.find(name);
    return it != index.end() ? it->second : nullptr;
}

long long field_get(const CharacterRecord& r, const Field& f) {
    const auto* p = reinterpret_cast<const unsigned char*>(&r) + f.offset;
    switch (f.kind) {
        case Kind::S8: { std::int8_t v; std::memcpy(&v, p, 1); return v; }
        case Kind::U8: { std::uint8_t v; std::memcpy(&v, p, 1); return v; }
        case Kind::S16: { std::int16_t v; std::memcpy(&v, p, 2); return v; }
        case Kind::U16: { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
        case Kind::S32: { std::int32_t v; std::memcpy(&v, p, 4); return v; }
        case Kind::U32: { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
    }
    return 0;
}

// Stores like a C assignment to the field: the low bits of `v`.
void field_set(CharacterRecord& r, const Field& f, long long v) {
    auto* p = reinterpret_cast<unsigned char*>(&r) + f.offset;
    switch (f.kind) {
        case Kind::S8:
        case Kind::U8: { const auto x = static_cast<std::uint8_t>(v); std::memcpy(p, &x, 1); break; }
        case Kind::S16:
        case Kind::U16: { const auto x = static_cast<std::uint16_t>(v); std::memcpy(p, &x, 2); break; }
        case Kind::S32:
        case Kind::U32: { const auto x = static_cast<std::uint32_t>(v); std::memcpy(p, &x, 4); break; }
    }
}

const char* const kEnemyNames[] = {
    "entity.root_transform", "entity.clip_table_attach", "entity.bone_point", "entity.player_visible",
    "entity.player_noise", "entity.player_blocked", "entity.path_blocked", "entity.path_hit",
    "entity.clear_heading", "entity.heading_toward", "entity.enemy_defeated", "entity.anim_status_set",
    "entity.sfx_3d", "entity.sfx_falloff", "entity.sys", "entity.set_sys", "entity.props_clear",
    "entity.clip_duration", "entity.clip_start_set", "entity.clip_end_set", "entity.bone_rotate",
};

int fail_named(char* err, int cap, const char* what) {
    std::snprintf(err, static_cast<std::size_t>(cap), "unknown sys variable '%s'", what);
    return -1;
}

// Process-wide state the enemies share, by name: sys(name[, i]) / set_sys(name, v[, i]).
int sys_call(bool set, const Args& a, long long* out, char* err, int errCap) {
    const char* name = a.text(0);
    if (name == nullptr) {
        return fail(err, errCap, "sys: no name");
    }
    sh::game::SystemBlock& sys = sh::game::session().system;
    const std::string_view n(name);
    const long long v = a.integer(1);
    if (n == "chara_group_flags") {
        // set_sys("chara_group_flags", v, group) / sys("chara_group_flags", group)
        const long long i = set ? a.integer(2) : a.integer(1);
        if (i < 0 || i >= static_cast<long long>(std::size(sys.scene.chara_group_flags))) {
            return fail(err, errCap, "chara group out of range");
        }
        if (set) {
            sys.scene.chara_group_flags[i] = static_cast<std::uint16_t>(v);
            return 0;
        }
        out[0] = sys.scene.chara_group_flags[i];
        return 1;
    }
    if (n == "target_npc") {
        if (set) {
            sys.target_npc = static_cast<std::int32_t>(v);
            return 0;
        }
        out[0] = sys.target_npc;
        return 1;
    }
    if (n == "map_effects") {
        // Bit 0/1: the lighting of the area (dark, flashlight), which sets how far enemies see. The word starts
        // at the preset's flags.
        if (set) {
            return fail(err, errCap, "map_effects is read-only");
        }
        std::int32_t word;
        std::memcpy(&word, &sys.environment.current.preset.flags, sizeof(word));
        out[0] = word;
        return 1;
    }
    if (n == "sys_state") {
        if (set) {
            return fail(err, errCap, "sys_state is read-only");
        }
        out[0] = sys.flow.sys_state;
        return 1;
    }
    return fail_named(err, errCap, name);
}

int enemy_call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "sys" || fn == "set_sys") {
        return sys_call(fn == "set_sys", a, out, err, errCap);
    }
    const long long slot_arg = a.integer(0, -1);
    CharacterRecord* r = slot_arg >= 0 && slot_arg <= kNpcSlotCount
                             ? entity_world().storage().record(static_cast<int>(slot_arg))
                             : nullptr;
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    const int slot = static_cast<int>(slot_arg);
    EnemyHost& h = *s_enemy;
    const auto i32 = [&](int i) { return static_cast<std::int32_t>(a.integer(i)); };
    if (fn == "player_visible") {
        out[0] = h.player_visible(slot, i32(1), i32(2), i32(3)) ? 1 : 0;
        return 1;
    }
    if (fn == "player_noise") {
        out[0] = h.player_noise(slot, i32(1), i32(2));
        return 1;
    }
    if (fn == "player_blocked") {
        out[0] = h.player_blocked(slot) ? 1 : 0;
        return 1;
    }
    if (fn == "path_blocked") {
        out[0] = h.path_blocked(slot, i32(1), i32(2)) ? 1 : 0;
        return 1;
    }
    if (fn == "path_hit") {
        out[0] = h.path_hit(slot, i32(1), i32(2)) ? 1 : 0;
        return 1;
    }
    if (fn == "clear_heading") {
        out[0] = h.clear_heading(slot, i32(1), i32(2));
        return 1;
    }
    if (fn == "heading_toward") {
        // heading_toward(slot, dist, x, z, span, nearest): the clear heading towards (x, z) within `span`
        // around the body (4096: all round), nearest or farthest end point; 4096 when none is clear.
        out[0] = h.heading_toward(slot, i32(1), i32(2), i32(3), i32(4), a.integer(5) != 0);
        return 1;
    }
    if (fn == "props_clear") {
        // The type's variable block back to zero (sixteen words), as every enemy does when it starts.
        std::memset(r->vars.bytes, 0, 16 * sizeof(std::int32_t));
        return 0;
    }
    if (fn == "enemy_defeated") {
        h.enemy_defeated(slot);
        return 0;
    }
    if (fn == "anim_status_set") {
        // anim_status_set(slot, status, keyframe): the death/recoil hand-over of the clip.
        h.anim_status_set(slot, i32(1), i32(2));
        return 0;
    }
    if (fn == "sfx_3d") {
        // sfx_3d(slot, sfx, vol_q8, kind): positional sound at the character.
        h.sfx_3d(slot, static_cast<int>(a.integer(1)), i32(2), i32(3));
        return 0;
    }
    if (fn == "sfx_falloff") {
        // sfx_falloff(slot, sfx, vol, falloff_q12, pitch): a looping voice kept up every tick.
        h.sfx_falloff(slot, static_cast<int>(a.integer(1)), i32(2), i32(3), i32(4));
        return 0;
    }
    if (fn == "clip_duration") {
        // clip_duration(slot, status): the speed of the clip table entry (keyframes per second, Q12).
        const long long status = a.integer(1, -1);
        std::int32_t speed = 0;
        if (status < 0 || status > 255 || !h.clip_duration(slot, static_cast<int>(status), &speed)) {
            return fail(err, errCap, "no clip table entry");
        }
        out[0] = speed;
        return 1;
    }
    if (fn == "clip_start_set" || fn == "clip_end_set") {
        // clip_start_set / clip_end_set(slot, status, keyframe): where a clip table entry starts or ends (the
        // recoil clips resume from the keyframe the hit interrupted).
        const long long status = a.integer(1, -1);
        if (status < 0 || status > 255 ||
            !h.clip_bound_set(slot, static_cast<int>(status), fn == "clip_end_set", i32(2))) {
            return fail(err, errCap, "no clip table entry");
        }
        return 0;
    }
    if (fn == "clip_table_attach") {
        // clip_table_attach(slot[, index]): types with several tables pick one by number.
        if (!h.clip_table_attach(slot, static_cast<int>(a.integer(1, 0)))) {
            return fail(err, errCap, "the character has no clip table");
        }
        return 0;
    }
    const int bone_count = h.bone_count(slot);
    if (bone_count < 0) {
        return fail(err, errCap, "the character has no bones");
    }
    if (fn == "root_transform") {
        // The root bone follows the record's position and rotation.
        h.root_transform(slot);
        return 0;
    }
    const long long bone = a.integer(1, -1);
    if (fn == "bone_rotate") {
        // bone_rotate(slot, bone, rx, ry, rz): the bone's local rotation turned further by the angles (order ZXY,
        // as a character's rotation): a head or spine that follows the target on top of the clip's pose.
        if (bone < 0 || bone >= bone_count) {
            return fail(err, errCap, "bone out of range");
        }
        h.bone_rotate(slot, static_cast<int>(bone), static_cast<std::int16_t>(a.integer(2)),
                      static_cast<std::int16_t>(a.integer(3)), static_cast<std::int16_t>(a.integer(4)));
        return 0;
    }
    if (fn == "bone_point") {
        // bone_point(slot, bone, x, y, z): the point (x, y, z) of the bone's space (geometry units) in the
        // world, geometry units (256 = 1 m). The bone's world matrix is brought up to date first.
        if (bone < 0 || bone >= bone_count) {
            return fail(err, errCap, "bone out of range");
        }
        const auto p = h.bone_point(slot, static_cast<int>(bone), static_cast<std::int16_t>(a.integer(2)),
                                    static_cast<std::int16_t>(a.integer(3)), static_cast<std::int16_t>(a.integer(4)));
        out[0] = p[0], out[1] = p[1], out[2] = p[2];
        return 3;
    }
    return fail(err, errCap, "unknown function");
}

int enemy_named_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int outCap, char* err,
                     int errCap) {
    const Args args{argc, argv};
    if (name != nullptr && outCap >= 6) {
        if (std::strcmp(name, "entity.get") == 0 || std::strcmp(name, "entity.set") == 0) {
            const char* field = args.text(1);
            const Field* f = field != nullptr ? field_find(field) : nullptr;
            if (f != nullptr) {
                const long long slot = args.integer(0, -1);
                CharacterRecord* r = slot >= 0 && slot <= kNpcSlotCount
                                         ? entity_world().storage().record(static_cast<int>(slot))
                                         : nullptr;
                if (r == nullptr) {
                    return fail(err, errCap, "bad character slot");
                }
                if (name[7] == 'g') {
                    out[0] = field_get(*r, *f);
                    return 1;
                }
                field_set(*r, *f, args.integer(2));
                return 0;
            }
        } else {
            for (const char* own : kEnemyNames) {
                if (std::strcmp(own, name) == 0) {
                    return enemy_call(std::string_view(name + 7), args, out, err, errCap);
                }
            }
        }
    }
    return s_enemy_next_call != nullptr ? s_enemy_next_call(name, argc, argv, out, outCap, err, errCap)
                                        : fail(err, errCap, "not available");
}

const char* const* enemy_named_list() {
    static std::vector<const char*> names;
    return chain_list(s_enemy_next_list, std::begin(kEnemyNames), std::end(kEnemyNames), names);
}

}  // namespace

void bind_entity_script(ShScriptGameApi& api, EntityScriptHost& host) {
    s_host = &host;
    s_core_next_call = api.named_call;
    s_core_next_list = api.named_list;
    api.named_call = core_named_call;
    api.named_list = core_named_list;
}

void bind_entity_enemy_script(ShScriptGameApi& api, EnemyHost& host) {
    s_enemy = &host;
    s_enemy_next_call = api.named_call;
    s_enemy_next_list = api.named_list;
    api.named_call = enemy_named_call;
    api.named_list = enemy_named_list;
}

}  // namespace sh::entities
