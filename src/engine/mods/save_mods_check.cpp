// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/mods/save_mods_check.h"

#include "engine/mods/registry.h"
#include "engine/mods/semver.h"

#include <algorithm>

namespace sh::mods {

namespace {

// -1, 0, 1 as a < b; versions that do not parse compare as text.
int compare_versions(const std::string& a, const std::string& b) {
    const Result<Version> va = Version::parse(a);
    const Result<Version> vb = Version::parse(b);
    if (va && vb) {
        const auto order = va.value() <=> vb.value();
        return order < 0 ? -1 : (order > 0 ? 1 : 0);
    }
    return a < b ? -1 : (a > b ? 1 : 0);
}

const char* change_words(ModChange change) {
    switch (change) {
    case ModChange::Missing: return "not installed";
    case ModChange::NotEnabled: return "not enabled";
    case ModChange::OlderNow: return "older version now";
    case ModChange::NewerNow: return "newer version now";
    case ModChange::AddedNow: return "active now, the save was made without it";
    }
    return "";
}

}  // namespace

ModCheck check_save_mods(const std::optional<std::vector<save::SavedMod>>& saved, const std::vector<RunningMod>& now) {
    ModCheck out;
    if (!saved) {
        return out;
    }
    const auto find_now = [&now](const std::string& id) {
        const auto it = std::find_if(now.begin(), now.end(), [&id](const RunningMod& m) { return m.id == id; });
        return it == now.end() ? nullptr : &*it;
    };
    for (const save::SavedMod& mod : *saved) {
        const RunningMod* here = find_now(mod.id);
        ModDifference d;
        d.id = mod.id;
        d.name = !mod.name.empty() ? mod.name : (here != nullptr && !here->name.empty() ? here->name : mod.id);
        d.saved_version = mod.version;
        if (here == nullptr) {
            d.change = ModChange::Missing;
        } else {
            d.version_now = here->version;
            if (!here->active) {
                d.change = ModChange::NotEnabled;
            } else {
                const int order = compare_versions(here->version, mod.version);
                if (order == 0) {
                    continue;
                }
                d.change = order < 0 ? ModChange::OlderNow : ModChange::NewerNow;
            }
        }
        out.differences.push_back(std::move(d));
    }
    for (const RunningMod& m : now) {
        if (!m.active || !m.gameplay) {
            continue;
        }
        const bool in_save = std::any_of(saved->begin(), saved->end(),
                                         [&m](const save::SavedMod& s) { return s.id == m.id; });
        if (!in_save) {
            out.differences.push_back({ModChange::AddedNow, m.id, m.name.empty() ? m.id : m.name, "", m.version});
        }
    }
    for (const ModDifference& d : out.differences) {
        const bool warns = d.change == ModChange::Missing || d.change == ModChange::NotEnabled ||
                           d.change == ModChange::OlderNow;
        (warns ? out.warn : out.note) = true;
    }
    return out;
}

std::string describe_check(const ModCheck& check) {
    std::string out;
    for (const ModDifference& d : check.differences) {
        if (!out.empty()) {
            out += "; ";
        }
        out += d.id;
        const std::string& version = d.change == ModChange::AddedNow ? d.version_now : d.saved_version;
        if (!version.empty()) {
            out += " " + version;
        }
        out += ": ";
        out += change_words(d.change);
        if (d.change == ModChange::OlderNow || d.change == ModChange::NewerNow) {
            out += " (" + d.version_now + ")";
        }
    }
    return out;
}

std::vector<RunningMod> running_mods() {
    std::vector<RunningMod> out;
    for (const ResolvedMod& m : registry().plan().mods) {
        const Manifest& man = m.candidate.manifest;
        out.push_back({man.id, man.name, man.version_text, m.state == ModState::Active, man.gameplay});
    }
    return out;
}

std::vector<save::SavedMod> active_mods_for_save() {
    std::vector<save::SavedMod> out;
    for (const ResolvedMod* m : registry().active()) {
        const Manifest& man = m->candidate.manifest;
        out.push_back({man.id, man.name, man.version_text, man.gameplay});
    }
    return out;
}

}  // namespace sh::mods
