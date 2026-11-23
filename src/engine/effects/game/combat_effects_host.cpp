// SPDX-License-Identifier: GPL-3.0-or-later
// The blood and gunfire effects (engine/effects/combat_effects.h) over the running game: their numbers from
// gamedata/effects/combat_effects.json, and the entry points the characters, the weapons and the Lua binds call
// (the host is slot_hosts.h). A weapon attack becomes the gun name of the numbers file here.

#include <optional>
#include <string>

#include "engine/app/pc_paths.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/effects/combat_effects.h"
#include "engine/effects/game/slot_hosts.h"
#include "engine/inventory/item_catalog.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
}

namespace fx = sh::effects;
namespace hosts = sh::effects::slot_hosts;

const fx::CombatEffectParams* hosts::combat_params() {
    static std::optional<fx::CombatEffectParams> params;
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        params.reset();
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<fx::CombatEffectParams> loaded =
            fx::load_combat_effect_params(PcPort_GetGameDataPath(), mods, &source);
        if (loaded) {
            params.emplace(std::move(loaded.value()));
            SH_LOG("[FX] combat effects from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - no blood, muzzle flashes or gun smoke", loaded.error().c_str());
        }
    }
    return params ? &*params : nullptr;
}

namespace {

using CombatHost = hosts::CombatHostT<fx::CombatEffectHost>;

// The spawners draw nothing: no packet memory is touched.
unsigned char* s_noPackets = nullptr;

// The gun of a packed weapon attack (weapon id + 10 * input; only the tap attacks of the guns and power tools,
// and the two story shooters' attacks, make a shot effect).
const char* gun_name(std::int32_t attack) {
    switch (attack) {
        case 32: return "Handgun";
        case 34: return "Shotgun";
        case 33: return "HuntingRifle";
        case 70: return "KaufmannGun";
        case 63: return "CybilGun";
        case 5: return "Chainsaw";
        case 2: return "RockDrill";
        default: return nullptr;
    }
}

const sh::entities::CharacterRecord& record(const void* chara) {
    return *static_cast<const sh::entities::CharacterRecord*>(chara);
}

// The effect anchors' name of a character: its NPC index, the player after the last NPC, -1 for anything else.
std::uint8_t anchor_index(const void* chara) {
    const int slot = sh::game::session().system.characters.slot_of(chara);
    if (slot == 0) return static_cast<std::uint8_t>(sh::entities::kNpcSlotCount);
    return static_cast<std::uint8_t>(slot > 0 ? slot - 1 : -1);
}

// The blood traits of a character's type (character.json "blood_follows_body", "blood_depth_bias").
void blood_traits(const sh::entities::CharacterRecord& c, fx::Bleeder& who) {
    if (const sh::chara::CharacterDef* def = sh::chara::type_def(c.model.chara_id)) {
        who.follows_body = def->blood_follows_body;
        who.depth_bias = static_cast<std::uint8_t>(def->blood_depth_bias);
    }
}

}  // namespace

extern "C" {

void Chara_EffectSpawn(void* chara, const void* position, std::int32_t strength, std::int32_t color) {
    const auto* pos = static_cast<const std::int32_t*>(position);
    const fx::CombatEffectParams* params = hosts::combat_params();
    if (params == nullptr) return;
    const sh::entities::CharacterRecord& c = record(chara);
    fx::Bleeder who;
    who.chara = anchor_index(chara);
    who.anchor = hosts::anchor_point(c);
    blood_traits(c, who);
    CombatHost host(&s_noPackets);
    fx::CombatEffects::spawn_blood(host, *params, who, fx::Vec3{pos[0], pos[1], pos[2]}, strength, color);
}

void Chara_AnimStatusSet(std::int32_t unused, void* chara, std::int32_t animStatus, std::int32_t color) {
    (void)unused;
    const fx::CombatEffectParams* params = hosts::combat_params();
    if (params == nullptr) return;
    const sh::entities::CharacterRecord& c = record(chara);
    CombatHost host(&s_noPackets);
    fx::CombatEffects::spawn_body_pool(host, *params, hosts::position_of(c), c.rotation.y, anchor_index(chara),
                                       animStatus, color);
}

void Math_PointFromAngleCalc(const std::int32_t* at, std::int16_t size, std::int32_t color, std::int32_t delay) {
    CombatHost host(&s_noPackets);
    fx::CombatEffects::spawn_wound_pool(host, fx::Vec3{at[0], at[1], at[2]}, size, color, delay);
}

void Combat_WeaponEffectSpawn(std::int32_t weaponAttack, std::int16_t rotY, std::int16_t rotX,
                              sh::entities::BoneCoord* bones) {
    const char* name = gun_name(weaponAttack);
    const fx::CombatEffectParams* params = hosts::combat_params();
    if (name == nullptr || params == nullptr) return;
    const auto gun = params->guns.find(name);
    if (gun == params->guns.end()) return;
    hosts::s_muzzleBones[static_cast<int>(gun->second.bones)] = bones;
    if (gun->second.flash && *gun->second.flash >= 0 &&
        static_cast<std::size_t>(*gun->second.flash) < hosts::s_flashLights.size()) {
        const sh::items::ItemLibrary* library = sh::inventory::item_catalog().library();
        const sh::items::WeaponDef* weapon = library ? library->find_weapon(name) : nullptr;
        std::optional<sh::items::WeaponMuzzleLight> light = weapon ? weapon->muzzle_light : std::nullopt;
        if (!weapon && gun->second.light) {
            const fx::GunLight& g = *gun->second.light;
            light =
                sh::items::WeaponMuzzleLight{g.color, g.intensity, g.range, g.radius, g.fade, g.casts_shadows, g.gi};
        }
        hosts::s_flashLights[static_cast<std::size_t>(*gun->second.flash)] = light;
    }
    CombatHost host(&s_noPackets);
    fx::CombatEffects::spawn_shot(host, gun->second, rotY, rotX);
}

void Combat_VectorUnkSet(const std::int32_t* at, std::int8_t variant, std::int16_t size) {
    CombatHost host(&s_noPackets);
    fx::CombatEffects::spawn_spark(host, fx::Vec3{at[0], at[1], at[2]}, static_cast<std::uint8_t>(variant), size);
}

}  // extern "C"
