// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "framework/sh_test.h"

#include "engine/mods/save_mods_check.h"

using sh::mods::ModChange;
using sh::mods::RunningMod;
using sh::save::SavedMod;

namespace {

std::vector<RunningMod> run_mods() {
    return {{"story", "Story", "1.2.0", true, true},  {"looks", "Looks", "2.0", true, false},
            {"off", "Off", "1.0", false, true},       {"older", "Older", "0.9.0", true, false},
            {"added", "Added", "1.0", true, true}};
}

}  // namespace

SH_TEST(save_mods_check_none_says_nothing) {
    SH_CHECK(sh::mods::check_save_mods(std::nullopt, run_mods()).empty());
}

SH_TEST(save_mods_check_differences) {
    const std::vector<SavedMod> saved = {{"story", "Story", "1.1.0", true}, {"looks", "Looks", "2.0", false},
                                         {"off", "Off", "1.0", true},       {"older", "Older", "1.0.0", false},
                                         {"gone", "Gone", "3.0", true},     {"added", "Added", "1.0", true}};
    const auto check = sh::mods::check_save_mods(saved, run_mods());
    SH_CHECK(check.warn && check.note);
    SH_CHECK(check.differences.size() == 4);
    SH_CHECK(check.differences[0].id == "story" && check.differences[0].change == ModChange::NewerNow);
    SH_CHECK(check.differences[1].id == "off" && check.differences[1].change == ModChange::NotEnabled);
    SH_CHECK(check.differences[2].id == "older" && check.differences[2].change == ModChange::OlderNow);
    SH_CHECK(check.differences[3].id == "gone" && check.differences[3].change == ModChange::Missing);
    SH_CHECK(sh::mods::describe_check(check).find("off 1.0: not enabled") != std::string::npos);
}

// A save made with no mods, loaded while story mods are on: a note only.
SH_TEST(save_mods_check_story_mods_added) {
    const auto check = sh::mods::check_save_mods(std::vector<SavedMod>{}, run_mods());
    SH_CHECK(!check.warn && check.note);
    SH_CHECK(check.differences.size() == 2);  // story and added are GAMEPLAY CHANGE mods; looks is not
    SH_CHECK(check.differences[0].change == ModChange::AddedNow && check.differences[0].id == "story");
    // Only a newer version: a note.
    const auto newer = sh::mods::check_save_mods(
        std::vector<SavedMod>{{"story", "Story", "1.0", true}, {"added", "Added", "1.0", true}}, run_mods());
    SH_CHECK(!newer.warn && newer.note && newer.differences.size() == 1);
    // The same mods: clean.
    const auto same = sh::mods::check_save_mods(
        std::vector<SavedMod>{{"story", "Story", "1.2.0", true}, {"added", "Added", "1.0", true}}, run_mods());
    SH_CHECK(same.empty() && !same.warn && !same.note);
}
