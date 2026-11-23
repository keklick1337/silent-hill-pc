// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include <cstring>

#include "engine/game_state/session.h"
#include "engine/player/player_script.h"

namespace {

struct FakeHost : sh::game::PlayerScriptHost {
    int weapon_number(const char* name) const override { return name != nullptr && std::strcmp(name, "Handgun") == 0 ? 5 : -1; }
    int hyper_blaster_mode() const override { return 2; }
    bool set_clip_link(int, long long, int, int, const char** error) override {
        *error = "bad character slot";
        return false;
    }
};

int fake_delta() { return 100; }

ShScriptArg text(const char* s) { return ShScriptArg{2, 0, s}; }
ShScriptArg integer(long long i) { return ShScriptArg{1, i, nullptr}; }

struct Bound {
    FakeHost host;
    ShScriptGameApi api{};
    Bound() {
        api.delta_time_get = fake_delta;
        sh::game::bind_player_script(api, host);
    }
    int call(const char* name, std::initializer_list<ShScriptArg> args, long long* out) {
        char err[128] = {};
        return api.named_call(name, static_cast<int>(args.size()), args.begin(), out, 8, err, sizeof err);
    }
};

}  // namespace

SH_TEST(player_script_controls_by_name) {
    Bound b;
    long long out[8] = {};
    auto& c = sh::game::session().player.controls;
    SH_CHECK(b.call("input.set_control", {text("hold_attack"), integer(0x1F)}, out) == 0);
    SH_CHECK(c.hold_attack == 0x1F);
    SH_CHECK(b.call("input.control", {text("hold_attack")}, out) == 1 && out[0] == 0x1F);
    SH_CHECK(b.call("input.set_player_flag", {text("has_move"), integer(1)}, out) == 0 && c.has_move == 1);
    SH_CHECK(b.call("input.player_flag", {text("has_move")}, out) == 1 && out[0] == 1);
    SH_CHECK(b.call("input.controls_clear", {}, out) == 0);
    SH_CHECK(c.hold_attack == 0 && c.has_move == 0);
    SH_CHECK(b.call("input.control", {text("nonsense")}, out) == -1);
}

SH_TEST(player_script_pad_queries) {
    Bound b;
    long long out[8] = {};
    sh::game::PadState& pad = sh::game::session().game.pads[0];
    pad.held = (1u << 14) | (1u << 24);
    pad.sticks.left_x = -128;
    pad.sticks.left_y = 100;
    SH_CHECK(b.call("input.pad_button", {text("cross"), integer(0)}, out) == 1 && out[0] == (1 << 14));
    SH_CHECK(b.call("input.pad_direction", {text("up"), integer(0)}, out) == 1 && out[0] == (1 << 24));
    SH_CHECK(b.call("input.move_stick", {}, out) == 4 && out[0] == -128 && out[1] == 100 && out[2] == 1 && out[3] == 0);
    SH_CHECK(b.call("input.stick_zone", {}, out) == 5 && out[0] == 1 && out[1] == 64 && out[4] == 1);
    pad = sh::game::PadState{};
}

SH_TEST(player_script_tick30_and_host) {
    Bound b;
    long long out[8] = {};
    sh::game::session().player.tick30_attack = 0;
    SH_CHECK(b.call("input.tick30", {text("attack")}, out) == 1 && out[0] == 0);  // 100 of the 136 of a tick
    SH_CHECK(b.call("input.tick30", {text("attack")}, out) == 1 && out[0] == 1);
    SH_CHECK(b.call("input.hyper_blaster_mode", {}, out) == 1 && out[0] == 2);
    SH_CHECK(b.call("entity.weapon_number", {text("Handgun")}, out) == 1 && out[0] == 5);
    SH_CHECK(b.call("entity.weapon_number", {text("Spoon")}, out) == -1);
    SH_CHECK(b.call("entity.clip_link_set", {integer(9), integer(1), integer(2), integer(3)}, out) == -1);
    sh::game::session().player.weapon_keyframes[2].start = 40;
    sh::game::session().player.weapon_keyframes[2].ready = 52;
    SH_CHECK(b.call("entity.harry_weapon_clip", {integer(2)}, out) == 2 && out[0] == 40 && out[1] == 52);
    SH_CHECK(b.call("entity.harry_weapon_clip", {integer(10)}, out) == 2 && out[0] == 0 && out[1] == 0);
}
