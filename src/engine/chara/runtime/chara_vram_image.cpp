// SPDX-License-Identifier: GPL-3.0-or-later
// Where a character's texture goes in video memory (src/engine/world/chara_vram.h, gamedata/config/chara_vram.json):
// Chara_FsImageCalc fills the image place the texture read and the materials use. Character names resolve through
// the character data.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "engine/app/loader/game_files.h"
#include "engine/app/pc_paths.h"
#include "engine/gameflow/flow_data.h"
#include "engine/log/sh_log.h"
#include "engine/world/chara_vram.h"
#include "engine/chara/runtime/chara_data.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace {

namespace sw = sh::world;

// The original layout, kept for a missing or broken file so characters still find their textures.
sw::CharaVramLayout builtin() {
    sw::CharaVramLayout l;
    l.player_characters = {"None", "Harry"};
    l.player = {{0, 27}, 0, 0, 736, 480};
    l.slots = {{{0, 28}, 0, 0, 0, 0}, {{0, 28}, 0, 128, 0, 0}, {{0, 29}, 0, 0, 0, 0}, {{0, 29}, 0, 128, 0, 0}};
    l.clut_x = 704;
    l.clut_step = 16;
    l.clut_y = 464;
    return l;
}

const sw::CharaVramLayout& layout() {
    static const sw::CharaVramLayout loaded = [] {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::vector<std::string> warnings;
        std::string source;
        const sh::Result<std::string> text =
            sh::gameflow::read_config_file(PcPort_GetGameDataPath(), mods, "chara_vram.json", &source, warnings);
        for (const std::string& w : warnings) {
            SH_WARN("[CHARAVRAM] %s", w.c_str());
        }
        if (!text) {
            SH_ERR("[CHARAVRAM] config/chara_vram.json: %s", text.error().c_str());
            return builtin();
        }
        sh::Result<sw::CharaVramLayout> parsed = sw::CharaVramLayout::parse(text.value());
        if (!parsed) {
            SH_ERR("[CHARAVRAM] config/chara_vram.json: %s", parsed.error().c_str());
            return builtin();
        }
        return std::move(parsed.value());
    }();
    return loaded;
}

bool is_player_character(int charaId) {
    static std::set<int> ids;
    static bool built = false;
    if (!built) {
        built = true;
        for (const std::string& name : layout().player_characters) {
            const int id = Pc_CharaIdFind(name.c_str());
            if (id < 0) {
                SH_ERR("[CHARAVRAM] config/chara_vram.json: no character %s", name.c_str());
                continue;
            }
            ids.insert(id);
        }
    }
    return ids.count(charaId) != 0;
}

}  // namespace

extern "C" void Chara_FsImageCalc(sh::loader::ImagePlace* image, std::int32_t charaId, std::int32_t modelIdx)
{
    const sw::VramImage v = layout().image(is_player_character(charaId), modelIdx);
    image->page[0]   = v.tpage[0];
    image->page[1]   = v.tpage[1];
    image->u         = v.u;
    image->v         = v.v;
    image->palette_x = v.clut_x;
    image->palette_y = v.clut_y;
}
