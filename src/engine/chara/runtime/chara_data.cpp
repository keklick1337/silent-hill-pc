// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/runtime/chara_data.h"

#include "engine/app/pc_paths.h"
#include "engine/audio/sound_api.h"
#include "engine/chara/chara_actions.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/chara_sfx_slot.h"
#include "engine/chara/footsteps.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"
#include "engine/script/script_host.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sh::chara {
namespace {

// What the original plays with no data at all: the concrete step.
constexpr int kDefaultFootstep = 1311;

// The data the running game reads, each part created on first use: map and character code may ask before the
// boot-time loaders ran, and the action hooks need the game's script API bound first.
class GameCharaData {
public:
    const CharaLibrary* library() {
        // After `mods reload` the hooks and the steps go with the library they were built over.
        if (sh::mods::data_stale(epoch_)) {
            actions_.reset();
            footsteps_.reset();
            library_.reset();
            char buf[1024];
            gamedata_ = PcPaths_ExeFile(buf, sizeof(buf), "gamedata");
            mods_ = PcPaths_ExeFile(buf, sizeof(buf), "mods");
            std::vector<std::string> warnings;
            Result<CharaLibrary> loaded = CharaLibrary::load(gamedata_, mods_, warnings);
            report(warnings);
            if (loaded) {
                library_.emplace(std::move(loaded.value()));
            } else {
                SH_DBG("[CHARA] character data: %s (keeping the original values)", loaded.error().c_str());
            }
        }
        return library_ ? &*library_ : nullptr;
    }

    CharaActions* actions() {
        const CharaLibrary* lib = library();
        if (lib == nullptr) {
            return nullptr;
        }
        if (!actions_) {
            sh::script::HostConfig config = sh::script::game_host_config();
            if (config.gamedata_dir.empty()) {
                config.gamedata_dir = gamedata_;
                config.mods_dir = mods_;
            }
            actions_ = std::make_unique<CharaActions>(*lib, std::move(config));
        }
        return actions_.get();
    }

    Footsteps* footsteps() {
        const CharaLibrary* lib = library();
        if (lib == nullptr) {
            return nullptr;
        }
        if (!footsteps_) {
            std::vector<std::string> warnings;
            Result<SurfaceTable> surfaces = SurfaceTable::load(gamedata_, mods_, warnings);
            report(warnings);
            footsteps_ = std::make_unique<Footsteps>(*lib, surfaces ? std::move(surfaces.value()) : SurfaceTable{},
                                                     gamedata_, mods_);
        }
        return footsteps_.get();
    }

    // A character's view; empty (every accessor answers its fallback) without data or for a bad id.
    Character character(int id) {
        const CharaLibrary* lib = library();
        if (lib == nullptr || id < 0) {
            return {};
        }
        return lib->character(id);
    }

    static void report(const std::vector<std::string>& warnings) {
        for (const std::string& w : warnings) {
            SH_WARN("[CHARA] %s", w.c_str());
        }
    }

private:
    std::uint32_t epoch_ = sh::mods::kNeverRead;
    std::filesystem::path gamedata_;
    std::filesystem::path mods_;
    std::optional<CharaLibrary> library_;
    std::unique_ptr<CharaActions> actions_;
    std::unique_ptr<Footsteps> footsteps_;
};

GameCharaData& data() {
    static GameCharaData s_data;
    return s_data;
}

bool valid_id(int id) {
    return id >= 0;
}

const sh::audio::SoundInfo* file_sound(int sfx) {
    const CharaLibrary* lib = data().library();
    return lib != nullptr ? lib->sound_registry().file_sound(sfx) : nullptr;
}

void fill_surface(const Surface& s, Pc_FootstepChoice* choice) {
    choice->walkBase = s.walk.base;
    choice->walkRange = s.walk.range;
    choice->runBase = s.run.base;
    choice->runRange = s.run.range;
}

}  // namespace

const CharaLibrary* game_library() { return data().library(); }
const CharacterDef* type_def(int chara_id) {
    const CharaLibrary* lib = game_library();
    if (lib == nullptr || chara_id < 0) {
        return nullptr;
    }
    return lib->find(chara_id);
}
bool is_player_type(int chara_id) {
    const CharacterDef* def = type_def(chara_id);
    return def != nullptr && def->is_player;
}
CharaActions* game_actions() { return data().actions(); }

}  // namespace sh::chara

const sh::chara::CharaLibrary* Pc_CharaData_Library() { return sh::chara::game_library(); }
sh::chara::CharaActions* Pc_CharaData_Actions() { return sh::chara::game_actions(); }

using sh::chara::data;

extern "C" {

int Pc_CharaSfxNamed(int charaId, const char* slot, int originalSfx) {
    if (slot == nullptr) {
        return originalSfx;
    }
    const int chosen = data().character(charaId).sfx(slot, originalSfx);
    sh::chara::CharaActions* hooks = sh::chara::valid_id(charaId) ? data().actions() : nullptr;
    return hooks != nullptr ? hooks->sound(charaId, slot, chosen) : chosen;
}

int Pc_CharaSfx(int charaId, int slot, int originalSfx) {
    if (slot < PC_CHARA_SFX_FOOTSTEP || slot > PC_CHARA_SFX_DAMAGE) {
        return originalSfx;
    }
    return Pc_CharaSfxNamed(charaId, sh::chara::sfx_slot_name(static_cast<Pc_CharaSfxSlot>(slot)), originalSfx);
}

void Pc_FootstepChoose(const Pc_FootstepQuery* query, Pc_FootstepChoice* choice) {
    choice->sfx = sh::chara::kDefaultFootstep;
    sh::chara::fill_surface(sh::chara::Surface{}, choice);
    sh::chara::Footsteps* steps = data().footsteps();
    if (query == nullptr || steps == nullptr || !sh::chara::valid_id(query->charaId)) {
        return;
    }
    const int chara = query->charaId;
    const sh::chara::StepPlace place{query->playerX, query->playerY, query->playerZ, query->roomIdx};
    const sh::chara::StepChoice step = steps->choose(chara, query->material, sh::chara::map_name(query->mapIdx), place,
                                                     sh::chara::kDefaultFootstep);
    sh::chara::GameCharaData::report(steps->take_warnings());
    choice->sfx = step.sound;
    sh::chara::fill_surface(*step.surface, choice);
    if (sh::chara::CharaActions* hooks = data().actions()) {
        choice->sfx = hooks->footstep(chara, step.surface->name, step.place != nullptr ? step.place->name : "", step.sound);
    }
}

int Pc_CharaFootstepSfx(int charaId, int material, int originalSfx) {
    sh::chara::Footsteps* steps = data().footsteps();
    if (steps == nullptr || !sh::chara::valid_id(charaId)) {
        return originalSfx;
    }
    const auto chara = charaId;
    const sh::chara::StepChoice step = steps->table(chara, material, originalSfx);
    sh::chara::CharaActions* hooks = data().actions();
    return hooks != nullptr ? hooks->footstep(chara, step.surface->name, "", step.sound) : step.sound;
}

int Pc_SoundIsFile(int sfxId) {
    return sh::chara::file_sound(sfxId) != nullptr ? 1 : 0;
}

int Pc_SoundFilePlay(int sfxId, int left, int right, int pitchCents) {
    const sh::audio::SoundInfo* info = sh::chara::file_sound(sfxId);
    if (info == nullptr) {
        return 0;
    }
    int gain_l = 0, gain_r = 0;
    info->play_levels(left, right, &gain_l, &gain_r);
    const float semitones = info->pitch_semitones(pitchCents);
    const std::string path = info->file_path.string();
    if (ShSound_PlayFile(path.c_str(), sfxId, gain_l, gain_r, semitones, info->loop ? 1 : 0) == 0) {
        SH_WARN("[SFX] cannot play %s for sound %s (not a 16-bit WAV?)", path.c_str(), info->name.c_str());
        return 1;
    }
    // The first play of each file sound goes to the log, the rest to debug.
    static std::vector<int> announced;
    if (std::find(announced.begin(), announced.end(), sfxId) == announced.end()) {
        announced.push_back(sfxId);
        SH_LOG("[SFX] file %s (%s, id %d) plays instead of a bank tone", path.c_str(), info->name.c_str(), sfxId);
    }
    SH_DBG("[SFX] file %s (%s, id %d) vol %d/%d pitch %.2f", path.c_str(), info->name.c_str(), sfxId, left, right,
           semitones);
    return 1;
}

void Pc_SoundFileStop(int sfxId) {
    ShSound_StopFile(sfxId);
}

void Pc_SoundFileVolume(int sfxId, int left, int right) {
    if (const sh::audio::SoundInfo* info = sh::chara::file_sound(sfxId)) {
        int gain_l = 0, gain_r = 0;
        info->volume_levels(left, right, &gain_l, &gain_r);
        ShSound_SetFileVolume(sfxId, gain_l, gain_r);
    }
}

int Pc_CharaParamQ12(int charaId, const char* name, int originalQ12) {
    return name != nullptr ? data().character(charaId).param_q12(name, originalQ12) : originalQ12;
}

int Pc_CharaParamInt(int charaId, const char* name, int original) {
    return name != nullptr ? data().character(charaId).param_int(name, original) : original;
}

int Pc_CharaAnim(int charaId, const char* name, int originalAnim) {
    return name != nullptr ? data().character(charaId).anim(name, originalAnim) : originalAnim;
}

int Pc_CharaIdFind(const char* name) {
    const sh::chara::CharaLibrary* lib = data().library();
    return lib != nullptr && name != nullptr ? lib->find_id(name) : -1;
}

int Pc_SoundIdFind(const char* name) {
    const sh::chara::CharaLibrary* lib = data().library();
    return lib != nullptr && name != nullptr ? lib->sound_registry().id(name) : -1;
}

}  // extern "C"
