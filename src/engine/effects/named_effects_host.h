// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared pieces of the named effects (named_effects.h): the registry that loads a file of named effects once, the
// view a drawer starts from and leaves, the world-screen frame and skeleton frames, and the submission of polygons
// and draw modes into the world layer.

#include <cstdint>
#include <map>
#include <string>

#include "engine/effects/effect_prims.h"
#include "engine/effects/effect_view.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"
#include "formats/common/status.h"
#include "render/draw3d/draw3d.h"

namespace sh::effects::named {

// Where the files are: the game data directory and the mods directory.
std::string gamedata_dir();
std::string mods_dir();

// Effects of type T by name, loaded on first use with `load` (gamedata dir, mods dir, &source).
template <class T>
class Registry {
public:
    template <class Load>
    T* find(const char* name, const char* file, const char* what, Load load) {
        if (mods::data_stale(epoch_)) {
            items_.clear();
            warned_.clear();
            std::string source;
            auto loaded = load(gamedata_dir(), mods_dir(), &source);
            if (loaded) {
                for (auto& [n, p] : loaded.value()) items_.emplace(n, T(p));
                SH_LOG("[FX] %zu %s from %s", items_.size(), what, source.c_str());
            } else {
                SH_ERR("[FX] %s - %s are not drawn", loaded.error().c_str(), what);
            }
        }
        if (name == nullptr) return nullptr;
        auto it = items_.find(name);
        if (it == items_.end()) {
            if (warned_ != name) {
                warned_ = name;
                SH_ERR("[FX] no %s \"%s\" in effects/%s", what, name, file);
            }
            return nullptr;
        }
        return &it->second;
    }

private:
    std::map<std::string, T> items_;
    std::uint32_t epoch_ = mods::kNeverRead;
    std::string warned_;
};

// The view a drawer starts from (the game's projection and current frame) and hands back when it is done.
EffectView begin_view();
void end_view(const EffectView& view);

// The world-screen frame, and that frame moved to a world point (Q12).
math::Transform world_screen();
math::Transform world_screen_at(const Vec3& at);
// The world frame of bone `bone` of a skeleton, and the bone's translation in its parent.
math::Transform bone_world(const void* skeleton, int bone);
math::Vec3i bone_local_translation(const void* skeleton, int bone);

// A reference the script handed over: a world position (Q12), a rotation (Q12 angles) or a scene timer (Q12).
Vec3 position_at(const void* ref);
math::Vec3s angles_at(const void* ref);
std::int32_t timer_at(const void* ref);

inline draw3d::Rgb rgb(std::uint32_t word) {
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8),
            static_cast<std::uint8_t>(word >> 16)};
}

// A polygon into world slot `slot`, its depth from the view's newest projections.
void submit(const draw3d::Polygon& polygon, const EffectView& view, std::int32_t slot);
// A draw mode (texture page, dither) into world slot `slot`.
void submit_mode(std::int32_t slot, std::int32_t tpage, bool dither);

}  // namespace sh::effects::named
