// SPDX-License-Identifier: GPL-3.0-or-later
// The mesh part variants of skeletal characters (src/engine/world/part_variants.h): WorldGfx_HeldItemAttach takes
// the character's table, gamedata/chara/<Name>/part_variants.json (the highest-priority mod's copy first), and
// shows or hides the skeleton parts it names. A character without the file keeps all parts as they are.

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "engine/chara/chara_library.h"
#include "engine/log/sh_log.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/world/part_variants.h"
#include "engine/world/skeleton.h"
#include "engine/chara/runtime/chara_data.h"


namespace {

namespace sw = sh::world;

const sw::PartVariants* table(int charaId) {
    // By character number; a number with no entry has not been tried yet.
    static std::map<int, std::optional<sw::PartVariants>> tables;
    if (charaId < 0) {
        return nullptr;
    }
    if (tables.find(charaId) == tables.end()) {
        tables[charaId] = std::nullopt;
        const sh::chara::CharaLibrary* lib = Pc_CharaData_Library();
        // Base + every mod's copy, merged in load order.
        std::string source;
        std::vector<std::string> warnings;
        const sh::Result<std::string> text =
            lib != nullptr ? lib->data_file(charaId, "part_variants.json", &source, warnings)
                           : sh::Result<std::string>::fail("no file");
        for (const std::string& w : warnings) {
            SH_ERR("[PARTS] %s", w.c_str());
        }
        if (text) {
            sh::Result<sw::PartVariants> parsed = sw::PartVariants::parse(text.value());
            if (parsed) {
                tables[charaId] = std::move(parsed.value());
            } else {
                SH_ERR("[PARTS] %s: %s", source.c_str(), parsed.error().c_str());
            }
        } else if (!text.error().starts_with("no file")) {
            SH_ERR("[PARTS] %s", text.error().c_str());
        }
    }
    return tables[charaId] ? &*tables[charaId] : nullptr;
}

}  // namespace

extern "C" void WorldGfx_HeldItemAttach(std::int32_t charaId, std::int32_t modelBone)
{
    sw::SkeletonRecord* skel = sh::chara::chara_models().skeleton(charaId);

    // Not resident yet (async CD read pending): the caller attaches again once it is.
    if (skel == nullptr) return;

    const sw::PartVariants* variants = table(charaId);
    if (variants == nullptr) return;

    std::vector<sw::PartToggle> toggles;
    variants->toggles(static_cast<std::uint32_t>(modelBone), toggles);
    for (const sw::PartToggle& t : toggles)
    {
        std::int32_t& flags = skel->parts[t.part].model.flags;
        const std::uint32_t word = static_cast<std::uint32_t>(flags);
        flags = static_cast<std::int32_t>(t.visible ? (word & ~sw::kPartHidden) : (word | sw::kPartHidden));
    }
}
