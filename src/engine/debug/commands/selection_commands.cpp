// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands over the character picked with the mouse (input/object_pick.h) and over the enemies around
// Harry: select, nocol, health, heal, info, goto, bring, freeze, unfreeze, kill, killall, hurt.
// Where it makes sense a command falls back to Harry when nothing is picked.

#include "engine/chara/runtime/chara_data.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/game_state/game_state.h"
#include "input/object_pick.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace sh::debug::commands {

namespace {

namespace gs = sh::game_state;

constexpr float kOne = 4096.0f;
constexpr std::int32_t kLethal = 99999 * 4096;  // damage, not health = 0: the enemy's own update runs its death
constexpr std::int32_t kNearby = 50 * 4096;     // half side of the square around Harry that "nearby" means
constexpr std::int32_t kGotoGap = 4915;         // 1.2 m short of the target, so Harry does not land inside it
constexpr std::int32_t kBringReach = 2 * 4096;

std::string picked_label() {
    return sh::input::object_pick().describe().substr(0, 63);  // the console has always cut it at 63
}

// The picked character: its slot in the game state (0 = Harry, also when nothing is picked or the picked enemy
// is gone) and what to call it. A picked prop is no character: slot -1.
struct Subject {
    int slot = 0;
    std::string label = "Harry";
};

Subject subject() {
    switch (sh::input::object_pick().kind()) {
        case sh::input::PickKind::Npc:
            if (const int npc = sh::input::object_pick().selected_npc(); npc >= 0) return {npc + 1, picked_label()};
            sh::input::object_pick().clear(false);
            return {};
        case sh::input::PickKind::Prop:
            return {-1, std::string()};
        default:
            return {};
    }
}

bool live_enemy(int npc) {
    const int type = gs::chara_id(npc + 1);
    return type != gs::chara::kNone && !sh::chara::is_player_type(type);
}

// Runs `act` on the slot of every live enemy in the square around Harry; returns how many.
template <typename Act>
int for_enemies_nearby(Act act) {
    const sh::entities::Vec3 harry = gs::chara_position(0);
    int touched = 0;
    for (int npc = 0; npc < gs::kNpcSlots; ++npc) {
        if (!live_enemy(npc) || gs::chara_health(npc + 1) <= 0) continue;
        const sh::entities::Vec3 at = gs::chara_position(npc + 1);
        if (std::abs(at.x - harry.x) > kNearby || std::abs(at.z - harry.z) > kNearby) continue;
        act(npc);
        ++touched;
    }
    return touched;
}

Result<void> run_select(Context& ctx, const Args& args) {
    const std::string& what = args.str(0);
    if (what == "clear" || what == "none") {
        sh::input::object_pick().clear(true);
        return done();
    }
    if (what == "player") {
        sh::input::object_pick().select_player();
    } else if (what == "nearest") {
        const int npc = sh::input::object_pick().nearest_npc();
        if (npc < 0 || !sh::input::object_pick().select_npc(npc)) {
            say(ctx, "select: no live enemy in this room");
            return done();
        }
    }
    if (sh::input::object_pick().kind() == sh::input::PickKind::None) {
        say(ctx, "nothing selected - left-click a character in the scene");
    } else {
        say(ctx, "selected %s", picked_label().c_str());
    }
    return done();
}

Result<void> run_nocol(Context& ctx, const Args& args) {
    // First of all a player cheat: with nothing picked it means Harry.
    if (sh::input::object_pick().kind() == sh::input::PickKind::None) sh::input::object_pick().select_player();
    const bool on = switched(args, 0, sh::input::object_pick().no_collide_selected());
    if (!sh::input::object_pick().set_no_collide(on)) {
        say(ctx, "nocollide: select a character first (props have no character collision)");
        return done();
    }
    say(ctx, "%s nocollide %s%s", picked_label().c_str(), on ? "ON" : "off",
        on ? " - walls, characters and attacks pass through; the ground still holds" : "");
    return done();
}

// `health [value]` and `heal`.
RunFn health_command(bool heal) {
    return [heal](Context& ctx, const Args& args) {
        const int picked_npc = sh::input::object_pick().slot();
        if (sh::input::object_pick().kind() == sh::input::PickKind::Prop) {
            say(ctx, "%s: props have no health", heal ? "heal" : "health");
            return done();
        }
        const Subject who = subject();
        const bool harry = who.slot == 0;
        const bool sets = heal || args.has(0);
        if (sets && gs::chara_health(who.slot) <= 0) {
            say(ctx, "%s is dead", who.label.c_str());
            return done();
        }
        if (heal) {
            const std::int32_t full = harry ? 100 * 4096 : sh::input::object_pick().npc_max_health(picked_npc);
            if (full <= 0) {
                say(ctx, "heal: %s's full health is not known yet", who.label.c_str());
                return done();
            }
            gs::set_chara_health(who.slot, full);
        } else if (sets) {
            double value = real(args.str(0));
            if (value <= 0.0) {
                say(ctx, "health: use a value above 0 (or 'kill')");
                return done();
            }
            if (harry && value > 100.0) value = 100.0;
            gs::set_chara_health(who.slot, static_cast<std::int32_t>(value * 4096.0));
        }
        if (harry) {
            say(ctx, "%s health %.1f / 100", who.label.c_str(), gs::chara_health(who.slot) / kOne);
        } else {
            say(ctx, "%s health %.1f (full %.1f)", who.label.c_str(), gs::chara_health(who.slot) / kOne,
                sh::input::object_pick().npc_max_health(picked_npc) / kOne);
        }
        return done();
    };
}

Result<void> run_info(Context& ctx, const Args&) {
    const int picked_npc = sh::input::object_pick().slot();
    if (sh::input::object_pick().kind() == sh::input::PickKind::Prop) {
        int x = 0, y = 0, z = 0;
        const std::string label = picked_label();
        sh::input::object_pick().prop_position(&x, &y, &z);
        say(ctx, "%s", label.c_str());
        say(ctx, " pos %.2f %.2f %.2f", x / kOne, y / kOne, z / kOne);
        return done();
    }
    const Subject who = subject();
    const sh::entities::Vec3 at = gs::chara_position(who.slot);
    say(ctx, "%s  charaId %d", who.label.c_str(), gs::chara_id(who.slot));
    say(ctx, " health %.1f  pos %.2f %.2f %.2f  facing %d deg", gs::chara_health(who.slot) / kOne, at.x / kOne,
        at.y / kOne, at.z / kOne, ((gs::chara_rotation(who.slot).y & 0xFFF) * 360) >> 12);
    int state = 0, step = 0;
    gs::chara_control(who.slot, &state, &step);
    if (who.slot != 0) {
        say(ctx, " state %d/%d%s", state, step, sh::input::object_pick().frozen(picked_npc) ? "  FROZEN" : "");
    }
    return done();
}

Result<void> run_goto(Context& ctx, const Args&) {
    const sh::entities::Vec3 harry = gs::chara_position(0);
    std::string label;
    std::int32_t x = 0, z = 0;
    if (sh::input::object_pick().kind() == sh::input::PickKind::Npc) {
        const Subject who = subject();
        if (who.slot == 0) {
            say(ctx, "goto: the selection is gone - select again");
            return done();
        }
        label = who.label;
        x = gs::chara_position(who.slot).x;
        z = gs::chara_position(who.slot).z;
    } else if (sh::input::object_pick().kind() == sh::input::PickKind::Prop) {
        int prop_x = 0, prop_y = 0, prop_z = 0;
        label = picked_label();
        sh::input::object_pick().prop_position(&prop_x, &prop_y, &prop_z);
        x = prop_x;
        z = prop_z;
    } else {
        say(ctx, "goto: select an enemy or prop first");
        return done();
    }

    // A little short of the target on Harry's side, facing it.
    const std::int32_t back_x = harry.x - x;
    const std::int32_t back_z = harry.z - z;
    const std::int32_t apart = static_cast<std::int32_t>(
        std::sqrt(static_cast<double>(back_x) * back_x + static_cast<double>(back_z) * back_z));
    if (apart > 0) {
        x += static_cast<std::int32_t>((static_cast<long long>(back_x) * kGotoGap) / apart);
        z += static_cast<std::int32_t>((static_cast<long long>(back_z) * kGotoGap) / apart);
    } else {
        z -= kGotoGap;
    }
    gs::place_player(x, z, nullptr);
    gs::set_chara_rotation_y(0, static_cast<std::int16_t>(gs::atan2_q12(-back_x, -back_z) & 0xFFF));
    say(ctx, "Harry moved to %s", label.c_str());
    return done();
}

Result<void> run_bring(Context& ctx, const Args&) {
    const sh::entities::Vec3 harry = gs::chara_position(0);
    const int heading = gs::chara_rotation(0).y;
    if (sh::input::object_pick().kind() == sh::input::PickKind::Prop) {
        say(ctx, "bring: props cannot be moved");
        return done();
    }
    if (sh::input::object_pick().kind() != sh::input::PickKind::Npc) {
        say(ctx, "bring: select an enemy first");
        return done();
    }
    const Subject who = subject();
    if (who.slot == 0) {
        say(ctx, "bring: the selection is gone - select again");
        return done();
    }
    // A held enemy is not posed again, so it would keep drawing where it was.
    if (sh::input::object_pick().frozen(sh::input::object_pick().slot())) {
        say(ctx, "bring: %s is frozen - 'freeze 0' first", who.label.c_str());
        return done();
    }
    const std::int32_t x =
        harry.x + static_cast<std::int32_t>((static_cast<long long>(kBringReach) * gs::sin_q12(heading)) >> 12);
    const std::int32_t z =
        harry.z + static_cast<std::int32_t>((static_cast<long long>(kBringReach) * gs::cos_q12(heading)) >> 12);
    gs::set_chara_position(who.slot, sh::entities::Vec3{x, gs::ground_height(x, z), z});
    gs::set_chara_rotation_y(who.slot, static_cast<std::int16_t>((heading + 0x800) & 0xFFF));
    say(ctx, "brought %s in front of Harry", who.label.c_str());
    return done();
}

// `word` in capitals: ALL, NONE, something starting with 1 or 0, or anything else to flip the picked enemy.
void freeze(Context& ctx, const std::string& word) {
    if (word == "ALL" || word == "NONE") {
        const bool hold = word == "ALL";
        int changed = 0;
        for (int npc = 0; npc < gs::kNpcSlots; ++npc) {
            if (live_enemy(npc) && sh::input::object_pick().set_frozen(npc, hold)) ++changed;
        }
        say(ctx, "%s %d characters", hold ? "froze" : "unfroze", changed);
        return;
    }
    if (sh::input::object_pick().kind() != sh::input::PickKind::Npc) {
        say(ctx, "freeze: select an enemy first (or 'freeze all' / 'freeze none')");
        return;
    }
    const Subject who = subject();
    if (who.slot == 0) {
        say(ctx, "freeze: the selection is gone - select again");
        return;
    }
    const int npc = sh::input::object_pick().slot();
    const char first = word.empty() ? '\0' : word[0];
    const bool hold = first == '1' ? true : first == '0' ? false : !sh::input::object_pick().frozen(npc);
    sh::input::object_pick().set_frozen(npc, hold);
    say(ctx, "%s %s", who.label.c_str(), hold ? "frozen" : "unfrozen");
}

Result<void> run_kill(Context& ctx, const Args&) {
    switch (sh::input::object_pick().kind()) {
        case sh::input::PickKind::Npc: {
            const std::string label = picked_label();
            const int slot = sh::input::object_pick().slot() + 1;
            if (gs::chara_health(slot) <= 0) {
                say(ctx, "%s is already dead", label.c_str());
                break;
            }
            gs::chara_take_damage(slot, kLethal);
            // The enemy's own update applies the damage, and a held enemy skips its update.
            sh::input::object_pick().set_frozen(sh::input::object_pick().slot(), false);
            say(ctx, "killed %s", label.c_str());
            break;
        }
        case sh::input::PickKind::Prop:
            say(ctx, "%s cannot be killed (select an enemy, or nothing for Harry)", picked_label().c_str());
            break;
        default:
            gs::set_chara_health(0, -4096);
            say(ctx, "killed Harry");
            break;
    }
    return done();
}

}  // namespace

void register_selection_commands(Registry& registry) {
    add(registry, "select", "enemies", "show or change the selection (left-click picks a character)",
        {arg("what", ArgType::Enum, true, "target", {"clear", "none", "player", "nearest"})}, run_select, {"sel"});
    add(registry, "nocol", "player", "selection passes through walls and characters (Harry by default)",
        {arg("state", ArgType::Bool, true, "on/off; omitted toggles")}, run_nocol, {"nocollide"});

    add(registry, "heal", "enemies", "refill the selection's health (Harry when nothing is selected)", {},
        health_command(true));
    add(registry, "health", "enemies", "show or set the selection's health (Harry when nothing is selected)",
        {arg("value", ArgType::Float, true, "new health above 0")}, health_command(false), {"hp"});
    add(registry, "info", "enemies", "name, health, position and state of the selection", {}, run_info);
    add(registry, "goto", "enemies", "move Harry next to the selected enemy or prop", {}, run_goto);
    add(registry, "bring", "enemies", "move the selected enemy in front of Harry", {}, run_bring);

    add(registry, "freeze", "enemies", "hold the selected enemy still, or all / none",
        {arg("state", ArgType::String, true, "on/off, all or none", {"0", "1", "all", "none"})},
        [](Context& ctx, const Args& args) {
            std::string word = lower(args.str(0));
            if (word == "on" || word == "yes" || word == "true") word = "1";
            if (word == "off" || word == "no" || word == "false") word = "0";
            freeze(ctx, upper(word));
            return done();
        });
    add(registry, "unfreeze", "enemies", "release the selected enemy, or all",
        {arg("what", ArgType::String, true, "'all' releases everyone", {"all"})}, [](Context& ctx, const Args& args) {
            const std::string word = upper(args.joined(0));
            freeze(ctx, word.empty() ? "0" : word == "ALL" ? "NONE" : word);
            return done();
        });

    add(registry, "kill", "enemies", "kill the selection, or Harry when nothing is selected", {}, run_kill);
    add(registry, "killall", "enemies", "kill every live enemy within 50 m", {}, [](Context& ctx, const Args&) {
        const int killed = for_enemies_nearby([](int npc) {
            gs::chara_take_damage(npc + 1, kLethal);
            sh::input::object_pick().set_frozen(npc, false);
        });
        say(ctx, "killed %d nearby enemies", killed);
        return done();
    });
    // A hit that does not kill, so a scenario reaches the recoil and stagger reactions.
    add(registry, "hurt", "enemies", "hit every live enemy within 50 m without killing",
        {arg("points", ArgType::Int, true, "damage, default 50")}, [](Context& ctx, const Args& args) {
            const int typed = whole(args.str(0));
            const int points = typed > 0 ? typed : 50;
            const int hurt = for_enemies_nearby([points](int npc) { gs::chara_take_damage(npc + 1, points * 4096); });
            say(ctx, "hurt %d nearby enemies by %d", hurt, points);
            return done();
        });
}

}  // namespace sh::debug::commands
