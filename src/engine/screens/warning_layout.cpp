// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/warning_layout.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

using json::Value;

struct IntKey {
    const char* name;
    int WarningLayout::*field;
    int lo, hi;
};

constexpr IntKey kKeys[] = {
    {"fade_step", &WarningLayout::fade_step, 1, 255},
    {"hold_frames", &WarningLayout::hold_frames, 0, 100000},
    {"black_frames", &WarningLayout::black_frames, 0, 100},
    {"quad_y", &WarningLayout::quad_y, -2048, 2048},
    {"quad_h", &WarningLayout::quad_h, 1, 4096},
    {"first_page", &WarningLayout::first_page, 0, 31},
    {"tex_height", &WarningLayout::tex_height, 1, 256},
};

}  // namespace

Result<WarningLayout> parse_warning_layout(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<WarningLayout>::fail(doc.error());
    }
    const Value& root = doc.value();
    if (!root.is_object()) {
        return Result<WarningLayout>::fail("warning screen layout must be an object");
    }
    WarningLayout l;
    for (const auto& [key, value] : root.members()) {
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        const std::string where = "line " + std::to_string(value.line()) + ": ";
        if (key == "quads") {
            if (!value.is_array() || value.array().empty() || value.array().size() > 8) {
                return Result<WarningLayout>::fail(where + "\"quads\" must be an array of 1..8 objects");
            }
            l.quads.clear();
            for (const Value& q : value.array()) {
                WarningQuad out{};
                int seen = 0;
                if (!q.is_object()) {
                    return Result<WarningLayout>::fail(where + "quad must be an object");
                }
                for (const auto& [qk, qv] : q.members()) {
                    int* dst = qk == "x" ? &out.x : qk == "width" ? &out.w : qk == "u_start" ? &out.u0
                             : qk == "u_end" ? &out.u1 : nullptr;
                    if (dst == nullptr || !qv.is_integer() || qv.integer() < -2048 || qv.integer() > 2048) {
                        return Result<WarningLayout>::fail(where + "bad quad key \"" + qk + "\"");
                    }
                    *dst = static_cast<int>(qv.integer());
                    ++seen;
                }
                if (seen != 4 || out.w <= 0 || out.u0 < 0 || out.u1 > 256 || out.u1 <= out.u0) {
                    return Result<WarningLayout>::fail(where + "quad needs x, width>0, 0<=u_start<u_end<=256");
                }
                l.quads.push_back(out);
            }
            continue;
        }
        const IntKey* found = nullptr;
        for (const IntKey& k : kKeys) {
            if (key == k.name) {
                found = &k;
            }
        }
        if (found == nullptr) {
            return Result<WarningLayout>::fail(where + "unknown key \"" + key + "\"");
        }
        if (!value.is_integer() || value.integer() < found->lo || value.integer() > found->hi) {
            return Result<WarningLayout>::fail(where + "\"" + key + "\" must be an integer in " +
                                               std::to_string(found->lo) + ".." + std::to_string(found->hi));
        }
        l.*(found->field) = static_cast<int>(value.integer());
    }
    return Result<WarningLayout>::ok(l);
} catch (const std::exception& e) {
    return Result<WarningLayout>::fail(e.what());
}

Result<WarningLayout> load_warning_layout(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "warning_screen.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<WarningLayout>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<WarningLayout> parsed = parse_warning_layout(text);
    if (!parsed) {
        return Result<WarningLayout>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
