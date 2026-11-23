// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include <string>
#include <vector>

#include "engine/items/powered_weapon.h"

using sh::items::PoweredWeaponMotor;

namespace {

struct Recorder final : sh::items::MotorHost {
    std::vector<std::string> calls;
    void stop_all_sounds() override { calls.push_back("stop"); }
    void base_sound(std::int32_t v) override { calls.push_back("base " + std::to_string(v)); }
    void motor_sound(bool chainsaw, std::int32_t v, std::int32_t p) override {
        calls.push_back("motor " + std::to_string(chainsaw) + " " + std::to_string(v) + " " + std::to_string(p));
    }
    void drill_bit_sound(std::int32_t v, std::int32_t p) override {
        calls.push_back("drill " + std::to_string(v) + " " + std::to_string(p));
    }
    void clear_power_timer() override { calls.push_back("timer"); }
    void clear_power_effect() override { calls.push_back("effect"); }
};

constexpr std::uint8_t kDrill = 2;
constexpr std::uint8_t kSaw = 5;

}  // namespace

SH_TEST(powered_weapon_reset_silences_everything) {
    PoweredWeaponMotor m;
    Recorder r;
    m.handle(0, -1, 7, 136, r);
    SH_CHECK((r.calls == std::vector<std::string>{"stop", "timer"}));
    SH_CHECK(m.map_index() == 7 && m.power() == 32 && m.pitch() == 0 && m.running() == 0);
}

SH_TEST(powered_weapon_power_on_picks_channel) {
    PoweredWeaponMotor m;
    Recorder r;
    m.handle(kSaw, 0, 3, 136, r);
    SH_CHECK((r.calls == std::vector<std::string>{"motor 1 32 0", "base 0"}));
    SH_CHECK(m.running() == PoweredWeaponMotor::kChainsawRunning);
    r.calls.clear();
    m.handle(kDrill, 0, 3, 136, r);
    SH_CHECK((r.calls == std::vector<std::string>{"motor 0 32 0", "base 0"}));
    SH_CHECK(m.running() == (PoweredWeaponMotor::kChainsawRunning | PoweredWeaponMotor::kDrillRunning));
}

SH_TEST(powered_weapon_run_down_steps_by_frame_time) {
    PoweredWeaponMotor m;
    Recorder r;
    m.handle(kDrill, 0, 0, 136, r);
    r.calls.clear();
    m.handle(kDrill, 2, 0, 136, r);  // 136 / 68 = 2 steps
    SH_CHECK(m.power() == 30 && m.pitch() == -2);
    SH_CHECK((r.calls == std::vector<std::string>{"motor 0 0 0", "base -1", "drill 30 -2"}));
    m.handle(kDrill, 2, 0, 10, r);  // under one step still takes one
    SH_CHECK(m.power() == 29);
    r.calls.clear();
    m.handle(kDrill, 2, 0, 68 * 40, r);  // overshoot clamps to 0 and stops
    SH_CHECK(m.power() == 0 && m.pitch() == 0 && m.running() == 0);
    SH_CHECK((r.calls == std::vector<std::string>{"motor 0 0 0", "base -1", "drill 0 -43", "stop", "timer",
                                                  "effect"}));
    r.calls.clear();
    m.handle(kDrill, 2, 0, 136, r);  // stopped: nothing
    SH_CHECK(r.calls.empty());
}

SH_TEST(powered_weapon_drill_release_needs_bit_disengaged) {
    PoweredWeaponMotor m;
    Recorder r;
    m.handle(kDrill, 0, 0, 136, r);
    r.calls.clear();
    m.handle(kDrill, 3, 0, 136, r);
    SH_CHECK((r.calls == std::vector<std::string>{"motor 0 0 0", "base 0", "timer"}));
    SH_CHECK(m.running() == 0);
    m.handle(kDrill, 0, 0, 136, r);
    m.handle(kDrill, 1, 0, 136, r);
    SH_CHECK(m.running() == (PoweredWeaponMotor::kDrillRunning | PoweredWeaponMotor::kDrillBitEngaged));
    r.calls.clear();
    m.handle(kDrill, 3, 0, 136, r);
    SH_CHECK(r.calls.empty());
}

SH_TEST(powered_weapon_sustain_restarts_on_new_map) {
    PoweredWeaponMotor m;
    Recorder r;
    m.handle(kDrill, -1, 1, 136, r);
    m.handle(kDrill, 2, 1, 136, r);
    r.calls.clear();
    m.handle(kDrill, 4, 1, 136, r);  // same map: keeps the run-down level
    SH_CHECK((r.calls == std::vector<std::string>{"motor 0 30 -2", "drill 30 -2", "base -1"}));
    r.calls.clear();
    m.handle(kDrill, 4, 2, 136, r);
    SH_CHECK((r.calls == std::vector<std::string>{"motor 0 32 0", "drill 32 0", "base -1"}));
    SH_CHECK(m.running() == (PoweredWeaponMotor::kDrillRunning | PoweredWeaponMotor::kDrillBitEngaged));
    m.handle(kSaw, 4, 3, 136, r);
    SH_CHECK(m.running() == PoweredWeaponMotor::kChainsawRunning);
}

SH_TEST(powered_weapon_unknown_event_is_ignored) {
    PoweredWeaponMotor m;
    Recorder r;
    m.handle(kDrill, 5, 1, 136, r);
    m.handle(kDrill, -2, 1, 136, r);
    SH_CHECK(r.calls.empty());
    SH_CHECK(m.map_index() == 0 && m.power() == 0);
}

SH_TEST(hyper_blaster_beam_color_per_eight_items) {
    SH_CHECK(sh::items::hyper_blaster_beam_color(0) == 0);
    SH_CHECK(sh::items::hyper_blaster_beam_color(7) == 0);
    SH_CHECK(sh::items::hyper_blaster_beam_color(8) == 1);
    SH_CHECK(sh::items::hyper_blaster_beam_color(16) == 2);
    SH_CHECK(sh::items::hyper_blaster_beam_color(31) == 3);
}
