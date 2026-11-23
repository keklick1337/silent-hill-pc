// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/chara/behavior_host.h"
#include "engine/chara/chara_library.h"
#include "engine/game_state/game_ids.h"
#include "engine/script/sh_script.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace behavior = sh::chara::behavior;
namespace ids = sh::game_state::chara;

namespace {

// Two fake records: slot 1 and 2; anything else has none.
std::array<std::array<std::uint8_t, 64>, 2> s_records{};
std::optional<long long> s_var;

std::uint8_t* fake_record(int slot) {
    return slot == 1 || slot == 2 ? s_records[static_cast<std::size_t>(slot - 1)].data() : nullptr;
}
int fake_type(int slot) {
    return fake_record(slot) != nullptr ? static_cast<std::int8_t>(fake_record(slot)[0]) : -1;
}
std::optional<long long> fake_var_get(int, std::string_view name) {
    return name == "test_var" ? s_var : std::nullopt;
}
bool fake_var_set(int, std::string_view name, long long v) {
    if (name != "test_var") {
        return false;
    }
    s_var = v;
    return true;
}

const behavior::Field kBaseFields[] = {
    {"chara_id", 0, behavior::Kind::S8},
    {"health", 4, behavior::Kind::S32},
};
const behavior::Field kExtFields[] = {
    {"health", 8, behavior::Kind::S16},  // shadowed by the base
    {"test_flags", 12, behavior::Kind::U16},
};

int s_base_calls = 0;
int s_ext_calls = 0;

int base_call(std::string_view fn, const behavior::Args& a, long long* out, char*, int) {
    ++s_base_calls;
    if (fn == "echo") {
        out[0] = a.integer(0, 7);
        return 1;
    }
    return behavior::kNotMine;
}
int ext_call(std::string_view fn, const behavior::Args& a, long long* out, char* err, int cap) {
    ++s_ext_calls;
    if (fn == "echo") {
        out[0] = a.integer(0) * 2;
        return 1;
    }
    if (fn == "variant") {
        out[0] = behavior::update_variant();
        return 1;
    }
    return behavior::fail(err, cap, "no %s", "luck");
}

const char* const kBaseNames[] = {"entity.echo"};
const char* const kExtNames[] = {"entity.echo", "entity.ext_only", "entity.variant"};

int s_clips_a[4];
int s_clips_b[4];
const behavior::ClipTable kExtClips[] = {
    {ids::kStalker, s_clips_a, 4},
    {ids::kStalker, s_clips_b, 2},
};

behavior::Extension s_base = {kBaseFields, 2, kBaseNames, 1, base_call, nullptr, 0, nullptr};
behavior::Extension s_ext = {kExtFields, 2, kExtNames, 3, ext_call, kExtClips, 2, nullptr};

int s_next_calls = 0;
int next_call(const char*, int, const ShScriptArg*, long long*, int, char* err, int cap) {
    ++s_next_calls;
    std::snprintf(err, static_cast<std::size_t>(cap), "next");
    return -1;
}
const char* const kNextNames[] = {"sound.play", nullptr};
const char* const* next_list() {
    return kNextNames;
}

ShScriptGameApi& api() {
    static ShScriptGameApi s = [] {
        ShScriptGameApi a{};
        a.named_call = next_call;
        a.named_list = next_list;
        behavior::register_extension(&s_ext);
        behavior::register_base_extension(&s_base);
        behavior::Ports ports;
        ports.record = fake_record;
        ports.chara_id = fake_type;
        ports.var_get = fake_var_get;
        ports.var_set = fake_var_set;
        behavior::install_ports(ports);
        behavior::bind_script_api(&a);
        return a;
    }();
    return s;
}

ShScriptArg integer(long long v) {
    return {1, v, nullptr};
}
ShScriptArg text(const char* s) {
    return {2, 0, s};
}

int call(const char* name, std::vector<ShScriptArg> args, long long* out, std::string* error = nullptr) {
    char err[128] = {};
    const int n = api().named_call(name, static_cast<int>(args.size()), args.data(), out, 8, err, sizeof(err));
    if (error != nullptr) {
        *error = err;
    }
    return n;
}

}  // namespace

SH_TEST(behavior_host_fields_base_first_then_vars) {
    long long out[8] = {};
    s_records[0].fill(0);
    s_records[0][0] = static_cast<std::uint8_t>(ids::kStalker);
    SH_CHECK(call("entity.set", {integer(1), text("health"), integer(0x12345)}, out) == 0);
    SH_CHECK(call("entity.get", {integer(1), text("health")}, out) == 1 && out[0] == 0x12345);
    // The base's "health" won: the extension's s16 at 8 is untouched.
    SH_CHECK(s_records[0][8] == 0 && s_records[0][9] == 0);
    // A store keeps the low bits; the read zero-extends a u16.
    SH_CHECK(call("entity.set", {integer(1), text("test_flags"), integer(-1)}, out) == 0);
    SH_CHECK(call("entity.get", {integer(1), text("test_flags")}, out) == 1 && out[0] == 0xFFFF);
    // Not a field: a behaviour variable.
    SH_CHECK(call("entity.set", {integer(1), text("test_var"), integer(-5)}, out) == 0);
    SH_CHECK(call("entity.get", {integer(1), text("test_var")}, out) == 1 && out[0] == -5);
    std::string error;
    SH_CHECK(call("entity.get", {integer(1), text("nope")}, out, &error) == -1);
    SH_CHECK(error == "unknown character field 'nope'");
    SH_CHECK(call("entity.get", {integer(9), text("health")}, out, &error) == -1);
    SH_CHECK(error == "bad character slot");
}

SH_TEST(behavior_host_dispatch_order_and_chain) {
    long long out[8] = {};
    s_base_calls = s_ext_calls = 0;
    // The base serves "echo" before the extension.
    SH_CHECK(call("entity.echo", {integer(3)}, out) == 1 && out[0] == 3);
    SH_CHECK(s_base_calls == 1 && s_ext_calls == 0);
    // kNotMine passes the name on to the next extension that lists it.
    SH_CHECK(call("entity.ext_only", {}, out) == -1);
    SH_CHECK(s_base_calls == 1 && s_ext_calls == 1);
    // Names outside "entity." go down the chain.
    s_next_calls = 0;
    std::string error;
    SH_CHECK(call("sound.play", {}, out, &error) == -1 && error == "next" && s_next_calls == 1);
    SH_CHECK(call("entity.unknown", {}, out) == -1 && s_next_calls == 2);
    // A too small output buffer is not ours either.
    char err[32];
    const ShScriptArg arg = integer(1);
    SH_CHECK(api().named_call("entity.echo", 1, &arg, out, 4, err, sizeof(err)) == -1 && s_next_calls == 3);
}

SH_TEST(behavior_host_name_list) {
    std::vector<std::string> names;
    for (const char* const* n = api().named_list(); *n != nullptr; ++n) {
        names.emplace_back(*n);
    }
    const std::vector<std::string> expected = {"sound.play",  "entity.get",      "entity.set",    "entity.echo",
                                               "entity.echo", "entity.ext_only", "entity.variant"};
    SH_CHECK(names == expected);
}

SH_TEST(behavior_host_clip_tables) {
    api();
    behavior::ClipTable t{};
    SH_CHECK(behavior::clip_table(ids::kStalker, 0, &t) && t.table == s_clips_a && t.count == 4);
    SH_CHECK(behavior::clip_table(ids::kStalker, 1, &t) && t.table == s_clips_b && t.count == 2);
    SH_CHECK(!behavior::clip_table(ids::kStalker, 2, &t));
    SH_CHECK(behavior::active_clip_table(ids::kStalker, s_clips_b, &t) && t.table == s_clips_b);
    SH_CHECK(behavior::active_clip_table(ids::kStalker, nullptr, &t) && t.table == s_clips_a);
    SH_CHECK(!behavior::active_clip_table(ids::kGroaner, s_clips_a, &t));
    SH_CHECK(behavior::has_clips(ids::kStalker));
    SH_CHECK(!behavior::has_clips(ids::kGroaner));
    SH_CHECK(!behavior::has_clips(ids::kNone) && !behavior::has_clips(ids::kCount));
}

SH_TEST(behavior_host_choose_update) {
    api();
    using behavior::SlotUpdate;
    using behavior::SpawnRun;
    // A map-planned slot runs the map's update or nothing.
    SH_CHECK(behavior::choose_update(ids::kStalker, true, false, SpawnRun::None) == SlotUpdate::Map);
    SH_CHECK(behavior::choose_update(ids::kStalker, false, false, SpawnRun::Own) == SlotUpdate::None);
    SH_CHECK(behavior::choose_update(ids::kNone, true, false, SpawnRun::Unset) == SlotUpdate::None);
    // An engine spawn.
    SH_CHECK(behavior::choose_update(ids::kStalker, true, true, SpawnRun::None) == SlotUpdate::None);
    SH_CHECK(behavior::choose_update(ids::kStalker, true, true, SpawnRun::Fallback) == SlotUpdate::Fallback);
    SH_CHECK(behavior::choose_update(ids::kGroaner, true, true, SpawnRun::Fallback) == SlotUpdate::None);
    SH_CHECK(behavior::choose_update(ids::kStalker, true, true, SpawnRun::Own) == SlotUpdate::Map);
    // No script runs in the tests, so the own behaviour is never found: Own falls back when the type has clips.
    SH_CHECK(behavior::choose_update(ids::kStalker, false, true, SpawnRun::Own) == SlotUpdate::Fallback);
    SH_CHECK(behavior::choose_update(ids::kStalker, false, true, SpawnRun::Map) == SlotUpdate::None);
    SH_CHECK(behavior::choose_update(ids::kGroaner, false, true, SpawnRun::Own) == SlotUpdate::None);
}

SH_TEST(behavior_host_behaviour_names_from_data) {
    std::vector<std::string> warnings;
    const auto lib = sh::chara::CharaLibrary::load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, warnings);
    SH_CHECK(static_cast<bool>(lib));
    if (!lib) {
        return;
    }
    // The types that run another type's update in every map header name its folder.
    const std::array<std::pair<int, const char*>, 14> expected = {{
        {ids::kStalker, "Stalker"},
        {ids::kGreyChild, "Stalker"},
        {ids::kMumbler, "Stalker"},
        {ids::kPuppetNurse, "PuppetNurse"},
        {ids::kDummyNurse, "PuppetNurse"},
        {ids::kPuppetDoctor, "PuppetNurse"},
        {ids::kDummyDoctor, "PuppetNurse"},
        {ids::kNightFlutter, "AirScreamer"},
        {ids::kWormhead, "Groaner"},
        {ids::kEndingCybil, "Cybil"},
        {ids::kEndingDahlia, "Dahlia"},
        {ids::kEndingKaufmann, "Kaufmann"},
        {ids::kUnknown23, "Unknown23"},
        {ids::kGhostChildAlessa, "GhostChildAlessa"},
    }};
    for (const auto& [id, name] : expected) {
        SH_CHECK(behavior::behaviour_name(lib.value(), id) == name);
    }
    SH_CHECK(behavior::behaviour_name(lib.value(), ids::kNone).empty());
    SH_CHECK(behavior::behaviour_name(lib.value(), ids::kCount).empty());
}

SH_TEST(behavior_host_args) {
    const ShScriptArg args[] = {integer(5), text("x"), {3, 1, nullptr}, {0, 0, nullptr}};
    const behavior::Args a{4, args};
    SH_CHECK(a.integer(0) == 5 && a.integer(1, -1) == -1 && a.integer(2) == 1 && a.integer(3, 9) == 9);
    SH_CHECK(a.integer(7, 4) == 4);
    SH_CHECK(a.text(1) != nullptr && std::strcmp(a.text(1), "x") == 0 && a.text(0) == nullptr);
}
