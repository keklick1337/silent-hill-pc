// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/combat/combat_script.h"

#include "engine/entities/character_record.h"
#include "engine/world/fixed_math.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

namespace sh::combat {

namespace {

constexpr const char* kNames[] = {
    "entity.attack", "entity.move", "entity.shape_offsets_update", "entity.weapon_damage", "entity.attack_number",
    "math.sqrt",
};

// Every answer fits in this many integers; with a smaller buffer the call goes on down the chain.
constexpr int kAnswerMax = 6;

struct Args {
    int argc;
    const ShScriptArg* argv;
    long long integer(int i, long long fallback = 0) const {
        return i < argc && (argv[i].kind == 1 || argv[i].kind == 3) ? argv[i].i : fallback;
    }
};

int fail(char* err, int cap, const char* msg) {
    std::snprintf(err, static_cast<std::size_t>(cap), "%s", msg);
    return -1;
}

int fail_slot(char* err, int cap, const char* what, long long slot) {
    std::snprintf(err, static_cast<std::size_t>(cap), "%s %lld (0..%d expected)", what, slot,
                  sh::entities::kNpcSlotCount);
    return -1;
}

// Any character slot the engine has, not only the original's six NPCs: engine and mod spawns live past the room
// spawner's slots. The attack loop still counts the original six (attack.cpp).
bool valid_slot(long long slot) {
    return slot >= kPlayerSlot && slot <= sh::entities::kNpcSlotCount;
}

int call(CombatView& combat, MoveView& move, std::string_view fn, const Args& a, long long* out, char* err,
         int err_cap) {
    if (fn == "math.sqrt") {
        out[0] = sh::world::game_sqrt(static_cast<std::int32_t>(a.integer(0)));
        return 1;
    }
    if (fn == "entity.attack_number") {
        // A script's attack names are resolved once at load: an unknown name is an error there, not a silent row 0.
        const char* name = a.argc > 0 && a.argv[0].kind == 2 ? a.argv[0].s : nullptr;
        const int number = name != nullptr ? combat.attack_number(name) : -1;
        if (number < 0) {
            std::snprintf(err, static_cast<std::size_t>(err_cap), "unknown attack \"%s\" (gamedata/config/attacks.json)",
                          name != nullptr ? name : "");
            return -1;
        }
        out[0] = number;
        return 1;
    }
    if (fn == "entity.weapon_damage") {
        const long long i = a.integer(0, -1);
        if (i < 0 || i >= combat.attack_count()) {
            return fail(err, err_cap, "weapon attack out of range");
        }
        out[0] = combat.attack_row(static_cast<int>(i)).power;
        return 1;
    }
    const long long slot = a.integer(0, -1);
    if (!valid_slot(slot)) {
        return fail_slot(err, err_cap, "bad character slot", slot);
    }
    const int s = static_cast<int>(slot);
    if (fn == "entity.move") {
        MoveSurface surface;
        const std::int32_t code = move_character(move, s, static_cast<std::int16_t>(a.integer(1)),
                                                 static_cast<std::int16_t>(a.integer(2)), &surface);
        out[0] = code;
        out[1] = surface.offset.x, out[2] = surface.offset.y, out[3] = surface.offset.z;
        out[4] = surface.ground_height, out[5] = surface.ground_type;
        return 6;
    }
    if (fn == "entity.shape_offsets_update") {
        turn_shape_offsets(move, s);
        return 0;
    }
    // entity.attack
    const long long target = a.integer(6, 0);
    if (!valid_slot(target)) {
        return fail_slot(err, err_cap, "bad target slot", target);
    }
    const Vec3 origin{static_cast<std::int32_t>(a.integer(3)), static_cast<std::int32_t>(a.integer(4)),
                      static_cast<std::int32_t>(a.integer(5))};
    out[0] = request_attack(combat, static_cast<int>(a.integer(1)), static_cast<int>(a.integer(2)), s, origin,
                            static_cast<int>(target), static_cast<std::int16_t>(a.integer(7)),
                            static_cast<std::int16_t>(a.integer(8)));
    return 1;
}

CombatView* s_combat = nullptr;
MoveView* s_move = nullptr;
decltype(ShScriptGameApi::named_call) s_next_call = nullptr;
decltype(ShScriptGameApi::named_list) s_next_list = nullptr;

int chained_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err,
                 int err_cap) {
    if (s_combat != nullptr && s_move != nullptr) {
        const int n = combat_script_call(*s_combat, *s_move, name, argc, argv, out, out_cap, err, err_cap);
        if (n != kNotCombatCall) {
            return n;
        }
    }
    return s_next_call != nullptr ? s_next_call(name, argc, argv, out, out_cap, err, err_cap)
                                  : fail(err, err_cap, "not available");
}

const char* const* chained_list() {
    static std::vector<const char*> names;
    if (names.empty()) {
        if (s_next_list != nullptr) {
            for (const char* const* n = s_next_list(); n != nullptr && *n != nullptr; ++n) {
                names.push_back(*n);
            }
        }
        names.insert(names.end(), std::begin(kNames), std::end(kNames));
        names.push_back(nullptr);
    }
    return names.data();
}

}  // namespace

int combat_script_call(CombatView& combat, MoveView& move, const char* name, int argc, const ShScriptArg* argv,
                       long long* out, int out_cap, char* err, int err_cap) {
    if (name == nullptr || out_cap < kAnswerMax) {
        return kNotCombatCall;
    }
    for (const char* own : kNames) {
        if (std::strcmp(own, name) == 0) {
            return call(combat, move, own, Args{argc, argv}, out, err, err_cap);
        }
    }
    return kNotCombatCall;
}

void bind_combat_script(ShScriptGameApi& api, CombatView& combat, MoveView& move) {
    s_combat = &combat;
    s_move = &move;
    s_next_call = api.named_call;
    s_next_list = api.named_list;
    api.named_call = chained_call;
    api.named_list = chained_list;
}

}  // namespace sh::combat
