// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Puppet Nurse's four stat variants (map3_s03 on the PS1): a nurse or doctor record points at the variant it was
// spawned as, and its behaviour (gamedata/chara/PuppetNurse/behavior.lua) asks which one. The values are
// gamedata/chara/PuppetNurse/variants.json; the record's word holds the address of the engine's entry, as the
// original held the address of the overlay's.

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/log/sh_log.h"
#include "formats/common/json.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::chara::game {
namespace {

struct PuppetNurseVariant {
    std::int32_t health = 0;   // Q12
    std::int32_t stagger = 0;  // damage that knocks the nurse down, Q12
    std::int32_t sight = 0;
    std::int32_t wander = 0;
    std::int32_t scale = 0;
    std::int32_t sound_bank = 0;  // 0 nurse, 1 doctor
    std::int32_t attack = 0;
    std::int32_t push = 0;
    std::int32_t clip_table = 0;  // 0 puppet_nurse_a, 1 puppet_nurse_b
};

std::array<PuppetNurseVariant, 4> s_variants;

std::int32_t field(const sh::json::Value& v, const char* name) {
    const sh::json::Value* f = v.find(name);
    return f != nullptr && f->is_integer() ? static_cast<std::int32_t>(f->integer()) : 0;
}

}  // namespace

// A broken or missing file leaves the entries zero and is reported, as the clip tables report theirs.
void puppet_nurse_variants_load() {
    const std::string path = std::string(PcPort_GetGameDataPath()) + "/chara/PuppetNurse/variants.json";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        SH_ERR("[PNURSE] %s cannot be read: the shipped gamedata/ is incomplete", path.c_str());
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = sh::json::parse(text);
    if (!parsed) {
        SH_ERR("[PNURSE] %s: %s", path.c_str(), parsed.error().c_str());
        return;
    }
    const sh::json::Value* variants = parsed.value().find("variants");
    if (variants == nullptr || !variants->is_array()) {
        SH_ERR("[PNURSE] %s: \"variants\" must be an array", path.c_str());
        return;
    }
    if (variants->array().size() != s_variants.size()) {
        SH_ERR("[PNURSE] %s: \"variants\" has %zu entries, the game table has %zu", path.c_str(),
               variants->array().size(), s_variants.size());
        return;
    }
    for (std::size_t i = 0; i < s_variants.size(); ++i) {
        const sh::json::Value& v = variants->array()[i];
        PuppetNurseVariant& out = s_variants[i];
        out.health = field(v, "health");
        out.stagger = field(v, "stagger");
        out.sight = field(v, "sight");
        out.wander = field(v, "wander");
        out.scale = field(v, "scale");
        out.sound_bank = field(v, "sound_bank");
        out.attack = field(v, "attack");
        out.push = field(v, "push");
        out.clip_table = field(v, "clip_table");
    }
}

namespace link {

int puppet_nurse_stats(int slot) {
    const int index = static_cast<int>(record(slot)->type_variant) - 1;
    return index >= 0 && static_cast<std::size_t>(index) < s_variants.size() ? index : -1;
}

void set_puppet_nurse_stats(int slot, int index) {
    record(slot)->type_variant = static_cast<std::uint8_t>(index + 1);
}

}  // namespace link

}  // namespace sh::chara::game

extern "C" void PuppetNurseVariants_Load(void) { sh::chara::game::puppet_nurse_variants_load(); }
