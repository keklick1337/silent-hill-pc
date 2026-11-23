// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/screen_data.h"

#include <fstream>
#include <iterator>
#include <vector>

#include "engine/mods/data_merge.h"
#include "engine/mods/mods.h"

namespace sh::screens {

Result<std::string> read_screen_file(const std::filesystem::path& gamedata_dir,
                                     const std::filesystem::path& mods_dir,
                                     const std::filesystem::path& relative, std::string* source) {
    // Base + every active mod's patch, in load order.
    return mods::read_data_text(gamedata_dir, mods_dir, relative, source);
}

std::string at_line(const json::Value& value) {
    return "line " + std::to_string(value.line()) + ": ";
}

Result<void> read_rgba(const json::Value& value, const std::string& key, float out[4]) {
    if (!value.is_array() || value.array().size() != 4) {
        return Result<void>::fail(at_line(value) + "\"" + key + "\" must be [r, g, b, a]");
    }
    float rgba[4];
    for (int i = 0; i < 4; ++i) {
        const json::Value& c = value.array()[static_cast<std::size_t>(i)];
        if (!c.is_number() || c.number() < 0.0 || c.number() > 1.0) {
            return Result<void>::fail(at_line(c) + "\"" + key + "\" channels must be numbers in 0..1");
        }
        rgba[i] = static_cast<float>(c.number());
    }
    for (int i = 0; i < 4; ++i) {
        out[i] = rgba[i];
    }
    return Result<void>::ok();
}

}  // namespace sh::screens
