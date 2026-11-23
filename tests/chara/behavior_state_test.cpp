// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/chara/behavior_state.h"
#include "engine/chara/chara_library.h"
#include "engine/entities/character.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

using sh::chara::BehaviorLayout;
using sh::chara::BehaviorLayouts;
using sh::chara::BehaviorVars;
using sh::chara::CharaLibrary;
using sh::chara::FieldKind;

namespace {

const char* kLayoutJson = R"({
    "fields": [
        { "name": "hanged_scratcher_flags", "kind": "s16" },
        { "name": "hanged_scratcher_timer", "kind": "u16" },
        { "name": "hanged_scratcher_collision_clip", "kind": "u8" },
        { "name": "hanged_scratcher_spawn_x", "kind": "s32" },
        { "name": "hanged_scratcher_wake", "kind": "s32" }
    ]
})";

BehaviorLayout parse_ok(const char* json) {
    std::vector<std::string> warnings;
    BehaviorLayout layout;
    SH_CHECK(BehaviorLayout::parse(json, "<test>", warnings, &layout));
    SH_CHECK(warnings.empty());
    return layout;
}

}  // namespace

SH_TEST(behavior_state_layout_parse_and_offsets) {
    const BehaviorLayout layout = parse_ok(kLayoutJson);
    SH_CHECK(layout.fields().size() == 5);
    // Natural alignment in declaration order: s16@0, u16@2, u8@4, pad to s32@8, s32@12.
    SH_CHECK(layout.fields()[0].offset == 0);
    SH_CHECK(layout.fields()[1].offset == 2);
    SH_CHECK(layout.fields()[2].offset == 4);
    SH_CHECK(layout.fields()[3].offset == 8);
    SH_CHECK(layout.fields()[4].offset == 12);
    SH_CHECK(layout.size() == 16);
    SH_CHECK(layout.find("hanged_scratcher_wake") == &layout.fields()[4]);
    SH_CHECK(layout.find("no_such_field") == nullptr);
}

// "at" places a field; two names may share bytes (a flag word and its low half); a field without it follows
// the previous one.
SH_TEST(behavior_state_layout_explicit_offsets) {
    const BehaviorLayout layout = parse_ok(R"({"fields": [
        {"name": "flags", "kind": "u16", "at": 0},
        {"name": "flags_word", "kind": "s32", "at": 0},
        {"name": "home_z", "kind": "s32", "at": 32},
        {"name": "home_x", "kind": "s32", "at": 40},
        {"name": "start_keyframe", "kind": "u8", "at": 36},
        {"name": "lost_timer", "kind": "u16"}
    ]})");
    SH_CHECK(layout.find("flags")->offset == 0);
    SH_CHECK(layout.find("flags_word")->offset == 0);
    SH_CHECK(layout.find("home_x")->offset == 40);
    SH_CHECK(layout.find("start_keyframe")->offset == 36);
    SH_CHECK(layout.find("lost_timer")->offset == 38);  // after the u8 at 36, aligned to 2
    SH_CHECK(layout.size() == 44);

    std::vector<std::string> warnings;
    BehaviorLayout bad;
    SH_CHECK(BehaviorLayout::parse(R"({"fields": [{"name": "a", "kind": "s32", "at": -4},
        {"name": "b", "kind": "s32", "at": 4094}, {"name": "c", "kind": "s32", "at": "x"},
        {"name": "d", "kind": "u8", "at": 3}]})", "<test>", warnings, &bad));
    SH_CHECK(bad.fields().size() == 1);
    SH_CHECK(warnings.size() == 3);
}

SH_TEST(behavior_state_vars_assignment_semantics) {
    const BehaviorLayout layout = parse_ok(kLayoutJson);
    BehaviorVars vars;
    vars.own(&layout);
    SH_CHECK(vars.owned());

    SH_CHECK(vars.get("hanged_scratcher_flags") == 0);
    // A C assignment: the low bits, sign- or zero-extended on the way back.
    SH_CHECK(vars.set("hanged_scratcher_flags", 0x12345));
    SH_CHECK(vars.get("hanged_scratcher_flags") == 0x2345);
    SH_CHECK(vars.set("hanged_scratcher_flags", -2));
    SH_CHECK(vars.get("hanged_scratcher_flags") == -2);
    SH_CHECK(vars.set("hanged_scratcher_timer", -1));
    SH_CHECK(vars.get("hanged_scratcher_timer") == 0xFFFF);
    SH_CHECK(vars.set("hanged_scratcher_collision_clip", 0x1FF));
    SH_CHECK(vars.get("hanged_scratcher_collision_clip") == 0xFF);
    SH_CHECK(vars.set("hanged_scratcher_spawn_x", -100000));
    SH_CHECK(vars.get("hanged_scratcher_spawn_x") == -100000);

    SH_CHECK(!vars.set("no_such_field", 1));
    SH_CHECK(!vars.get("no_such_field").has_value());
    SH_CHECK(!vars.has("no_such_field"));

    // A copy of an owned block is independent.
    BehaviorVars copy = vars;
    SH_CHECK(copy.set("hanged_scratcher_spawn_x", 7));
    SH_CHECK(vars.get("hanged_scratcher_spawn_x") == -100000);

    vars.own(&layout);
    SH_CHECK(vars.get("hanged_scratcher_flags") == 0);
    SH_CHECK(vars.get("hanged_scratcher_spawn_x") == 0);
}

// Bound to storage kept elsewhere (the game's record): reads and writes go to those bytes;
// a field past the storage is not served.
SH_TEST(behavior_state_vars_bound_storage) {
    const BehaviorLayout layout = parse_ok(kLayoutJson);
    std::array<std::uint8_t, 12> storage{};
    BehaviorVars vars;
    vars.bind(&layout, storage);
    SH_CHECK(!vars.owned());
    SH_CHECK(vars.set("hanged_scratcher_spawn_x", 0x01020304));
    SH_CHECK(storage[8] == 0x04 && storage[11] == 0x01);
    storage[0] = 0xFE;
    storage[1] = 0xFF;
    SH_CHECK(vars.get("hanged_scratcher_flags") == -2);
    SH_CHECK(!vars.has("hanged_scratcher_wake"));  // offset 12: outside the 12 bytes
    BehaviorVars view = vars;                     // a copy views the same bytes
    SH_CHECK(view.set("hanged_scratcher_timer", 5));
    SH_CHECK(vars.get("hanged_scratcher_timer") == 5);
    vars.clear();
    SH_CHECK(vars.layout() == nullptr && !vars.has("hanged_scratcher_flags"));
}

SH_TEST(behavior_state_layout_rejects_bad_fields) {
    std::vector<std::string> warnings;
    BehaviorLayout layout;

    SH_CHECK(!BehaviorLayout::parse("{\"fields\": 3}", "<test>", warnings, &layout));
    SH_CHECK(!BehaviorLayout::parse("{\"fields\": []}", "<test>", warnings, &layout));
    SH_CHECK(!BehaviorLayout::parse("not json", "<test>", warnings, &layout));

    warnings.clear();
    SH_CHECK(BehaviorLayout::parse(
        "{\"fields\":[{\"name\":\"a\",\"kind\":\"s16\"},{\"name\":\"a\",\"kind\":\"s16\"},"
        "{\"name\":\"b\",\"kind\":\"float\"},{\"kind\":\"s16\"},{\"name\":\"c\",\"kind\":\"u8\"}]}",
        "<test>", warnings, &layout));
    SH_CHECK(layout.fields().size() == 2);  // the duplicate, the bad kind and the nameless dropped
    SH_CHECK(layout.find("a") != nullptr);
    SH_CHECK(layout.find("c") != nullptr);
    SH_CHECK(warnings.size() == 3);
}

// Every shipped state.json parses cleanly, fits the variable block of a character (72 bytes: the size of the
// block the game's record keeps) and names its fields with the type's prefix; no name means two places.
SH_TEST(behavior_state_shipped_files) {
    std::vector<std::string> warnings;
    auto loaded = CharaLibrary::load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, warnings);
    SH_CHECK(loaded);
    if (!loaded) {
        return;
    }
    const CharaLibrary& library = loaded.value();
    warnings.clear();
    BehaviorLayouts layouts;
    layouts.load(library, warnings);
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "  %s\n", w.c_str());
    }
    SH_CHECK(warnings.empty());
    int files = 0;
    std::set<std::string> names;
    std::set<const BehaviorLayout*> seen;
    for (const auto& def : library.characters()) {
        const BehaviorLayout* layout = layouts.of(def.number);
        if (layout == nullptr || !seen.insert(layout).second) {
            continue;  // none, or another type's file this one shares
        }
        ++files;
        SH_CHECK(layout->size() <= 72);
        for (const auto& f : layout->fields()) {
            SH_CHECK(names.insert(f.name).second);
            SH_CHECK(layouts.naming(f.name) == layout);
        }
    }
    SH_CHECK(files == 14);
    const BehaviorLayout* romper = layouts.naming("romper_home_x");
    SH_CHECK(romper != nullptr && romper->find("romper_home_x")->offset == 40);
    SH_CHECK(romper->find("romper_home_x")->kind == FieldKind::S32);
    SH_CHECK(layouts.naming("no_such_variable") == nullptr);
}

// The types that run another type's behaviour use its variables.
SH_TEST(behavior_state_shared_layouts) {
    std::vector<std::string> warnings;
    auto loaded = CharaLibrary::load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, warnings);
    SH_CHECK(loaded);
    if (!loaded) {
        return;
    }
    const CharaLibrary& library = loaded.value();
    std::uint8_t stalker = 0, grey_child = 0, incubus = 0, unknown23 = 0;
    for (const auto& def : library.characters()) {
        if (def.folder == "Stalker") stalker = def.number;
        if (def.folder == "GreyChild") grey_child = def.number;
        if (def.folder == "Incubus") incubus = def.number;
        if (def.folder == "Unknown23") unknown23 = def.number;
    }
    SH_CHECK(stalker != 0 && grey_child != 0);
    BehaviorLayouts layouts;
    layouts.load(library, warnings);
    SH_CHECK(layouts.of(grey_child) != nullptr);
    SH_CHECK(layouts.of(grey_child) == layouts.of(stalker));  // its "behavior"
    SH_CHECK(layouts.of(unknown23) != nullptr);
    SH_CHECK(layouts.of(unknown23) == layouts.of(incubus));  // its "state_layout"

    // Character vars: the type's layout over its block.
    sh::entities::Character c(1, stalker, nullptr);
    c.vars().own(layouts.of(stalker));
    SH_CHECK(c.vars().set("stalker_flags", 0x10));
    SH_CHECK(c.vars().get("stalker_flags") == 0x10);
    SH_CHECK(!c.vars().has("romper_flags"));
}
