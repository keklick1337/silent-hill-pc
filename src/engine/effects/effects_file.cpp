// SPDX-License-Identifier: GPL-3.0-or-later
// Effect files of gamedata/effects/ and the mods; shared parse helpers.

#include "engine/effects/effects_file.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "engine/ids/game_numbers.h"
#include "engine/mods/data_merge.h"

namespace sh::effects {

Result<std::string> read_effect_file(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                     const std::filesystem::path& relative, std::string* source) {
    // Base + every active mod's patch, in load order.
    return mods::read_data_text(gamedata_dir, mods_dir, relative, source);
}

namespace parse {

void fail(const json::Value& at, std::string_view what) {
    throw std::runtime_error("line " + std::to_string(at.line()) + ", column " + std::to_string(at.column()) + ": " +
                             std::string(what));
}

const json::Value& member(const json::Value& parent, std::string_view key) {
    const json::Value* v = parent.find(key);
    if (v == nullptr) {
        fail(parent, "missing \"" + std::string(key) + "\"");
    }
    return *v;
}

void reject_unknown(const json::Value& object, std::initializer_list<std::string_view> known) {
    if (!object.is_object()) {
        fail(object, "an object is expected");
    }
    for (const auto& [key, value] : object.members()) {
        bool found = key.rfind("_", 0) == 0 || key == "description";
        for (std::string_view k : known) {
            found = found || key == k;
        }
        if (!found) {
            fail(value, "unknown key \"" + key + "\"");
        }
    }
}

std::int32_t integer(const json::Value& v, std::int64_t lo, std::int64_t hi) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(v, "an integer in " + std::to_string(lo) + ".." + std::to_string(hi) + " is expected");
    }
    return static_cast<std::int32_t>(v.integer());
}

std::int32_t integer_of(const json::Value& parent, std::string_view key, std::int64_t lo, std::int64_t hi) {
    return integer(member(parent, key), lo, hi);
}

std::int32_t sound(const json::Value& v) {
    if (!v.is_string()) {
        return integer(v, 0, 0xFFFF);  // a sound number: the older form, still read
    }
    const int number = ids::sound_number(v.string().c_str());
    if (number < 0) {
        fail(v, "unknown sound \"" + v.string() + "\" (a name of gamedata/sounds)");
    }
    return number;
}

std::int32_t sound_of(const json::Value& parent, std::string_view key) { return sound(member(parent, key)); }

bool boolean_of(const json::Value& parent, std::string_view key) {
    const json::Value& v = member(parent, key);
    if (!v.is_bool()) {
        fail(v, "\"" + std::string(key) + "\" must be true or false");
    }
    return v.boolean();
}

void rgb(const json::Value& v, std::uint8_t out[3]) {
    if (!v.is_array() || v.array().size() != 3) {
        fail(v, "a colour is [r, g, b]");
    }
    for (std::size_t k = 0; k < 3; k++) {
        out[k] = static_cast<std::uint8_t>(integer(v.array()[k], 0, 255));
    }
}

}  // namespace parse

}  // namespace sh::effects
