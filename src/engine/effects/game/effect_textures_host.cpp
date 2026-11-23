// SPDX-License-Identifier: GPL-3.0-or-later
// The map effect textures (engine/effects/effect_textures.h): which pictures a map loads and where
// (gamedata/effects/effect_textures.json); they go through the file queue into the shared image places (fire, blood,
// the effect sheet) the effect drawers sample.

#include <cstdint>
#include <optional>
#include <string>

#include "engine/app/pc_paths.h"
#include "engine/effects/effect_textures.h"
#include "engine/effects/game/effect_link.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/memory/work_buffers.h"
#include "engine/log/sh_log.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
// The blood colour the options wrote last (a stray write on some maps clobbers the settings copy).
extern unsigned char g_PcTrustedBloodColor;
}

namespace {

namespace fx = sh::effects;
namespace fg = sh::effects::game_link;

const fx::EffectTextureConfig* config() {
    static std::optional<fx::EffectTextureConfig> cfg;
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        cfg.reset();
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<fx::EffectTextureConfig> loaded = fx::load_effect_textures(PcPort_GetGameDataPath(), mods, &source);
        if (loaded) {
            cfg.emplace(loaded.value());
            SH_LOG("[FX] effect textures from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - maps load no effect textures", loaded.error().c_str());
        }
    }
    return cfg ? &*cfg : nullptr;
}

fg::EffectImagePlace place_of(fx::EffectImage image) {
    switch (image) {
        case fx::EffectImage::Fire: return fg::EffectImagePlace::Fire;
        case fx::EffectImage::Blood: return fg::EffectImagePlace::Blood;
        default: return fg::EffectImagePlace::Sheet;
    }
}

void load(const fx::EffectTexture& t) {
    const std::int32_t file = sh::loader::disc_files().find(t.file.c_str());
    if (file < 0) {
        SH_ERR("[FX] effect texture %s: no game file \"%s\"", t.name.c_str(), t.file.c_str());
        return;
    }
    fx::effect_world::ImagePlace& place = fg::effect_image_place(place_of(t.image));
    if (t.v) place.v = static_cast<std::uint8_t>(*t.v);
    if (t.palette_row) place.palette_y = static_cast<std::int16_t>(*t.palette_row);
    sh::loader::queue_read_image(file, sh::memory::work_buffers().data(sh::memory::WorkBuffer::SheetImage), &place);
}

}  // namespace

extern "C" void Map_EffectTexturesLoad(std::int32_t mapIdx) {
    // Every map starts from the places' home rows.
    fx::effect_world::ImagePlace& sheet = fg::effect_image_place(fg::EffectImagePlace::Sheet);
    sheet.v = 0;
    sheet.palette_y = 0;
    fg::effect_image_place(fg::EffectImagePlace::Fire).v = 128;

    const fx::EffectTextureConfig* cfg = config();
    if (mapIdx == -1 && cfg != nullptr) {
        load(cfg->boot);
    }

    // The blood colour selects the blood palette rows; a per-map buffer overrun corrupts the settings copy on some
    // maps (#41), so the value the options wrote is put back on every map load.
    std::uint8_t& blood = sh::game::session().game.settings.blood_color;
    if (blood != g_PcTrustedBloodColor) {
        blood = g_PcTrustedBloodColor;
    }

    if (mapIdx == -1 || cfg == nullptr) {
        return;
    }
    const char* map = sh::game_state::map_name(mapIdx);
    if (map == nullptr) {
        return;
    }
    const auto flag_set = [](const std::string& name) {
        const int flag = sh::ids::flag_number(name.c_str());
        return flag >= 0 && sh::game::save_record().event_flag(flag);
    };
    for (const fx::EffectTexture* t : cfg->for_map(map, flag_set)) {
        load(*t);
    }
}
