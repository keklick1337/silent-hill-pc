// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Sound banks of a character the engine spawns off its maps. The pure plan is
// engine/audio/guest_banks.h; here the data comes together: the character's sounds (character.json "sounds" and
// "footsteps"), the banks that hold them (sounds/sounds.json "banks"), the ambient banks of the maps it lives on
// (character.json "maps" -> the map header's ambient bank -> its load command -> the bank file), and the bank now in
// the ambient slot. Only the engine's spawn asks; the map's own spawner never does, so normal play is unchanged.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/engine.h"
#include "engine/audio/guest_banks.h"
#include "engine/audio/sound_api.h"
#include "engine/audio/sound_driver.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/maps/sh_maps.h"
#include "engine/vfs/vfs_c.h"
#include "audio/game/game_sound_driver.h"

extern "C" {
int Pc_AmbientLoadCommand(std::int32_t idx);
int Pc_SoundIsFile(int sfxId);
}

namespace {

namespace au = sh::audio;

constexpr int kAmbientSlot = 2;

au::GuestBanks& guests() {
    static au::GuestBanks g;
    return g;
}

// Guest sounds already reported as played since the last reset (one log line each).
std::set<std::uint16_t>& reported() {
    static std::set<std::uint16_t> s;
    return s;
}

std::map<std::uint16_t, std::string>& guest_bank_of() {
    static std::map<std::uint16_t, std::string> m;
    return m;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// "SND/MAP200.VAB" -> "MAP200".
std::string bank_at_sector(std::int32_t sector) {
    char path[256];
    if (!ShVfs_PathAtLba(sector, path, sizeof path)) {
        return {};
    }
    std::string name = path;
    if (const std::size_t slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (const std::size_t dot = name.find_last_of('.'); dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    return upper(name);
}

// The ambient bank a map loads with its header ("" when unknown); maps are read once.
std::string ambient_bank_of_map(const std::string& map) {
    static std::map<std::string, std::string> cache;
    if (const auto it = cache.find(map); it != cache.end()) {
        return it->second;
    }
    std::string bank;
    const int idx = ShMaps_AmbientAudioOf(map.c_str());
    const int cmd = idx >= 0 ? Pc_AmbientLoadCommand(idx) : -1;
    const au::DriverTables& t = au::game_sound_driver().tables();
    if (cmd >= t.first_bank_command) {
        const std::size_t row = static_cast<std::size_t>(cmd - t.first_bank_command);
        if (row < t.files.size() && t.files[row].slot == kAmbientSlot) {
            bank = bank_at_sector(t.files[row].sector);
        }
    }
    cache[map] = bank;
    return bank;
}

void close_guests() {
    for (const int slot : guests().reset()) {
        ShSound_CloseBank(slot);
    }
    reported().clear();
    guest_bank_of().clear();
}

}  // namespace

extern "C" {

// Loads the banks of the character's sounds that the current ambient bank lacks. 1 when every sound of the
// character plays on this map now.
int Pc_SoundBanks_EnsureFor(int charaId) {
    const sh::chara::CharaLibrary* lib = sh::chara::game_library();
    const sh::chara::CharacterDef* def =
        lib != nullptr && charaId > 0 ? lib->find(charaId) : nullptr;
    if (def == nullptr) {
        return 0;
    }
    const au::SoundRegistry& registry = lib->sound_registry();
    const au::DriverTables& t = au::game_sound_driver().tables();

    std::vector<au::GuestSound> sounds;
    auto add = [&](const std::string& name, int id) {
        if (id < t.effect_base + 1 || static_cast<std::size_t>(id - t.effect_base) >= t.effects.size() ||
            Pc_SoundIsFile(id) != 0) {
            return;
        }
        const au::EffectEntry& e = t.effects[static_cast<std::size_t>(id - t.effect_base)];
        if (e.slot != kAmbientSlot ||
            std::any_of(sounds.begin(), sounds.end(), [id](const au::GuestSound& s) { return s.id == id; })) {
            return;
        }
        au::GuestSound s;
        s.id = static_cast<std::uint16_t>(id);
        s.program = e.program & 0x7F;
        s.note = e.note;
        s.name = name;
        if (const au::SoundInfo* info = registry.find(name)) {
            for (const std::string& b : info->banks) {
                s.banks.push_back(upper(b));
            }
        }
        sounds.push_back(std::move(s));
    };
    for (const auto& [slot, id] : def->sounds) {
        const auto n = def->sound_names.find(slot);
        add(n != def->sound_names.end() ? n->second : std::to_string(id), id);
    }
    for (const auto& [surface, id] : def->footsteps) {
        const auto n = def->footstep_names.find(surface);
        add(n != def->footstep_names.end() ? n->second : std::to_string(id), id);
    }
    if (sounds.empty()) {
        return 1;
    }

    std::vector<std::string> own;
    for (const std::string& map : def->maps) {
        const std::string bank = ambient_bank_of_map(map);
        if (!bank.empty() && std::find(own.begin(), own.end(), bank) == own.end()) {
            own.push_back(bank);
        }
    }

    au::AudioEngine& engine = au::audio_engine();
    const std::string current = upper(engine.bank_name(kAmbientSlot));
    // Sounds already served by a guest bank of an earlier spawn are not missing.
    std::erase_if(sounds, [](const au::GuestSound& s) { return guests().redirected(s.id); });
    const au::GuestPlan plan = au::plan_guest_banks(current, sounds, own, [&](const std::string& bank, int program,
                                                                                int note) {
        return engine.bank_has_note(bank, program, note);
    });

    bool all = plan.unplayable.empty();
    for (const au::GuestPlan::Load& load : plan.loads) {
        bool fresh = false;
        const int slot = guests().slot_of(load.bank, &fresh);
        if (slot < 0) {
            SH_LOG_WARN(AUDIO, "%s: no free bank slot for its sounds of bank %s", def->folder.c_str(), load.bank.c_str());
            all = false;
            continue;
        }
        if (fresh && !engine.load_bank(slot, load.bank)) {
            SH_LOG_WARN(AUDIO, "%s: bank %s did not load into slot %d", def->folder.c_str(), load.bank.c_str(), slot);
            all = false;
            continue;
        }
        for (const std::uint16_t id : load.ids) {
            guests().redirect(id, slot);
            guest_bank_of()[id] = load.bank;
        }
        SH_LOG_INFO(AUDIO, "%s spawned off its maps: bank %s in slot %d plays %d of its sounds (map bank %s)",
                    def->folder.c_str(), load.bank.c_str(), slot, static_cast<int>(load.ids.size()),
                    current.empty() ? "none" : current.c_str());
    }
    for (const std::string& name : plan.unplayable) {
        // Sounds whose bank tone the data does not know (sounds.json "unbound"): silent in the original as well.
        SH_LOG_INFO(AUDIO, "%s: no bank of its maps has a tone for sound %s", def->folder.c_str(), name.c_str());
    }
    return all ? 1 : 0;
}

// The map changed (or its ambient bank did): the guests belonged to the previous one.
void Pc_SoundBanks_Reset(void) {
    if (!guests().empty()) {
        SH_LOG_DEBUG(AUDIO, "guest sound banks released");
    }
    close_guests();
}

}  // extern "C"

namespace sh::audio {

int guest_effect_slot(std::uint16_t id, int slot) {
    const int to = guests().slot_for(id, slot);
    if (to != slot && reported().insert(id).second) {
        SH_LOG_INFO(AUDIO, "guest sound %d plays from bank %s (slot %d)", static_cast<int>(id),
                    guest_bank_of()[id].c_str(), to);
    }
    return to;
}

}  // namespace sh::audio
