// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/chara_actions.h"

#include "engine/mods/mods.h"

#include <algorithm>
#include <array>
#include <utility>

namespace sh::chara {

CharaActions::CharaActions(const CharaLibrary& library, script::HostConfig config)
    : library_(library), config_(std::move(config)) {}

CharaActions::~CharaActions() = default;

const char* CharaActions::hook_for_slot(std::string_view slot) {
    if (slot.starts_with("footstep")) {
        return "on_footstep";
    }
    if (slot.starts_with("death")) {
        return "on_death";
    }
    if (slot.find("damage") != std::string_view::npos) {
        return "on_damage";
    }
    if (slot.find("attack") != std::string_view::npos) {
        return "on_attack";
    }
    return "on_sound";
}

std::string CharaActions::footstep_surface(std::string_view slot) {
    if (!slot.starts_with("footstep")) {
        return {};
    }
    slot.remove_prefix(8);
    if (slot.starts_with("_")) {
        slot.remove_prefix(1);
    }
    return slot.empty() ? std::string("default") : std::string(slot);
}

script::Host* CharaActions::host(int id) {
    Script& script = scripts_[id];
    if (!script.tried) {
        script.tried = true;
        // Layered (Host::run_layers): the base copy runs in the state's globals, each mod's copy extends it,
        // unless a mod's mod.json "replace" names chara/<folder>/actions.lua.
        mods::LuaLayers layers;
        std::vector<std::string> warnings;
        const std::vector<mods::ModInfo> active =
            config_.mods_dir.empty() ? std::vector<mods::ModInfo>{} : mods::discover(config_.mods_dir, warnings);
        for (const std::filesystem::path& file : library_.script_files(id, "actions.lua")) {
            const mods::ModInfo* owner = nullptr;
            for (const mods::ModInfo& m : active) {
                const std::filesystem::path rel = file.lexically_relative(m.root);
                if (!rel.empty() && *rel.begin() != "..") {
                    owner = &m;
                }
            }
            if (owner == nullptr) {
                layers.base = file;
                continue;
            }
            const std::string relative = "chara/" + file.parent_path().filename().generic_string() + "/actions.lua";
            if (std::find(owner->replace.begin(), owner->replace.end(), relative) != owner->replace.end()) {
                layers.base = file;
                layers.base_owner = owner->id;
            } else {
                layers.extensions.push_back(mods::LuaLayer{*owner, file});
            }
        }
        if (!layers.empty()) {
            auto host = std::make_unique<script::Host>(config_);
            const Result<bool> loaded = host->run_layers(layers);
            if (loaded && loaded.value()) {
                script.host = std::move(host);
            } else if (!loaded && config_.log) {
                config_.log(1, "character actions: " + loaded.error() + " (the character keeps its data sounds)");
            }
        }
    }
    return script.host.get();
}

int CharaActions::sound(int id, std::string_view slot, int chosen) {
    return run_hook(id, slot, hook_for_slot(slot), footstep_surface(slot), {}, chosen);
}

int CharaActions::footstep(int id, std::string_view surface, std::string_view place, int chosen) {
    return run_hook(id, "footstep", "on_footstep", surface, place, chosen);
}

int CharaActions::run_hook(int id, std::string_view slot, const char* hook, std::string_view surface,
                           std::string_view place, int chosen) {
    if (in_hook_) {
        return chosen;
    }
    script::Host* lua = host(id);
    if (lua == nullptr) {
        return chosen;
    }
    if (!lua->has_global(hook)) {
        hook = "on_sound";
        if (!lua->has_global(hook)) {
            return chosen;
        }
    }
    const CharacterDef* def = library_.find(id);
    const audio::SoundRegistry& sounds = library_.sound_registry();
    const std::string slot_text(slot);
    const std::string surface_text(surface);
    const std::string place_text(place);
    const std::string* sound_name = sounds.name(chosen);
    const auto text = [](const std::string& s) { return script::CallArg{2, 0, s.c_str()}; };
    const script::CallArg none{0, 0, nullptr};
    const std::string character = def != nullptr ? def->folder : std::string();
    const std::array<script::Host::HookField, 7> fields = {{
        {"character", text(character)},
        {"chara_id", {1, id, nullptr}},
        {"slot", text(slot_text)},
        {"sound", sound_name != nullptr ? text(*sound_name) : none},
        {"id", {1, chosen, nullptr}},
        {"surface", surface_text.empty() ? none : text(surface_text)},
        {"place", place_text.empty() ? none : text(place_text)},
    }};
    in_hook_ = true;
    const script::Host::HookResult result = lua->call_global_table(hook, fields);
    in_hook_ = false;
    if (result.kind == 1 && result.integer >= 0 && result.integer <= 0xFFFF) {
        return static_cast<int>(result.integer);
    }
    if (result.kind == 2) {
        const int named = sounds.id(result.text);
        if (named >= 0) {
            return named;
        }
        if (config_.log) {
            config_.log(1, character + " " + hook + ": unknown sound \"" + result.text + "\" (keeping " +
                               (sound_name != nullptr ? *sound_name : std::to_string(chosen)) + ")");
        }
    }
    return chosen;
}

}  // namespace sh::chara
