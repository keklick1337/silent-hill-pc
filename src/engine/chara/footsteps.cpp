// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/footsteps.h"

#include "engine/mods/data_merge.h"
#include "engine/mods/mods.h"
#include "formats/common/json.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

namespace sh::chara {

namespace {

using json::Value;

Result<std::string> read_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return Result<std::string>::fail("cannot read " + file.string());
    }
    return Result<std::string>::ok(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

std::string at(const Value& v) { return "line " + std::to_string(v.line()) + ": "; }

// {"base": -16, "range": 32}
bool read_pitch(const Value* v, StepPitch& out, std::string& error) {
    if (v == nullptr) {
        return true;
    }
    const Value* base = v->is_object() ? v->find("base") : nullptr;
    const Value* range = v->is_object() ? v->find("range") : nullptr;
    if (base == nullptr || range == nullptr || !base->is_integer() || !range->is_integer() || range->integer() <= 0 ||
        base->integer() < -128 || base->integer() > 127 || range->integer() > 256) {
        error = at(*v) + "a step pitch is {\"base\": -128..127, \"range\": 1..256}";
        return false;
    }
    out.base = static_cast<int>(base->integer());
    out.range = static_cast<int>(range->integer());
    return true;
}

std::vector<std::string> names(const Value* v) {
    std::vector<std::string> out;
    if (v != nullptr && v->is_string()) {
        out.push_back(v->string());
    } else if (v != nullptr && v->is_array()) {
        for (const Value& s : v->array()) {
            if (s.is_string()) {
                out.push_back(s.string());
            }
        }
    }
    return out;
}

// [min, max] in metres, either may be null; Q12 exactly like the original's Q12(x) literal.
bool read_bounds(const Value* v, std::optional<std::int32_t>& lo, std::optional<std::int32_t>& hi) {
    if (v == nullptr) {
        return true;
    }
    if (!v->is_array() || v->array().size() != 2) {
        return false;
    }
    const auto one = [](const Value& e, std::optional<std::int32_t>& out) {
        if (e.is_null()) {
            return true;
        }
        if (!e.is_number()) {
            return false;
        }
        out = to_q12(e.number());
        return true;
    };
    return one(v->array()[0], lo) && one(v->array()[1], hi);
}

}  // namespace

// ---- SurfaceTable -----------------------------------------------------------------------------

Result<int> SurfaceTable::apply(std::string_view text, std::string_view origin) {
    const Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<int>::fail(std::string(origin) + ": " + doc.error());
    }
    const Value* list = doc.value().find("surfaces");
    if (list == nullptr || !list->is_object()) {
        return Result<int>::fail(std::string(origin) + ": no \"surfaces\" object");
    }
    std::map<int, Surface> next = by_material_;
    std::string fallback = fallback_;
    if (const Value* f = doc.value().find("fallback"); f != nullptr) {
        if (!f->is_string()) {
            return Result<int>::fail(std::string(origin) + ": \"fallback\" must be a surface name");
        }
        fallback = f->string();
    }
    int count = 0;
    for (const auto& [name, value] : list->members()) {
        if (!name.empty() && name.front() == '_') {
            continue;
        }
        if (!value.is_object()) {
            return Result<int>::fail(std::string(origin) + ": surface \"" + name + "\" must be an object");
        }
        auto existing = std::find_if(next.begin(), next.end(), [&](const auto& kv) { return kv.second.name == name; });
        Surface s = existing != next.end() ? existing->second : Surface{};
        s.name = name;
        if (const Value* m = value.find("material"); m != nullptr) {
            if (!m->is_integer() || m->integer() < 0 || m->integer() > 255) {
                return Result<int>::fail(std::string(origin) + ": surface \"" + name + "\": material must be 0..255");
            }
            s.material = static_cast<int>(m->integer());
        } else if (existing == next.end()) {
            return Result<int>::fail(std::string(origin) + ": new surface \"" + name + "\" has no \"material\"");
        }
        std::string error;
        if (!read_pitch(value.find("walk_pitch"), s.walk, error) || !read_pitch(value.find("run_pitch"), s.run, error)) {
            return Result<int>::fail(std::string(origin) + ": surface \"" + name + "\": " + error);
        }
        if (existing != next.end()) {
            next.erase(existing);
        }
        next[s.material] = std::move(s);  // a material has one name; the newer wins
        ++count;
    }
    by_material_ = std::move(next);
    fallback_ = std::move(fallback);
    return Result<int>::ok(count);
}

Result<SurfaceTable> SurfaceTable::load(const std::filesystem::path& gamedata_dir,
                                        const std::filesystem::path& mods_dir, std::vector<std::string>& warnings) {
    namespace fs = std::filesystem;
    SurfaceTable table;
    std::error_code ec;
    const auto one = [&](const fs::path& file) {
        if (!fs::is_regular_file(file, ec)) {
            return;
        }
        const Result<std::string> text = read_text(file);
        const Result<int> applied = text ? table.apply(text.value(), file.string()) : Result<int>::fail(text.error());
        if (!applied) {
            warnings.push_back(applied.error() + " (file skipped)");
        }
    };
    one(gamedata_dir / "world" / "surfaces.json");
    if (!mods_dir.empty() && fs::is_directory(mods_dir, ec)) {
        std::vector<std::string> mod_warnings;
        for (const mods::ModInfo& mod : mods::discover(mods_dir, mod_warnings)) {
            one(mod.root / "world" / "surfaces.json");
        }
    }
    return Result<SurfaceTable>::ok(std::move(table));
}

const Surface& SurfaceTable::of_material(int material) const {
    if (const auto it = by_material_.find(material); it != by_material_.end()) {
        return it->second;
    }
    if (const Surface* f = find(fallback_)) {
        return *f;
    }
    return none_;
}

const Surface* SurfaceTable::find(std::string_view name) const {
    for (const auto& [material, s] : by_material_) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

// ---- PlaceRules -------------------------------------------------------------------------------

Result<PlaceRules> PlaceRules::parse(std::string_view text, std::string_view origin,
                                     const audio::SoundRegistry& sounds) {
    const Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<PlaceRules>::fail(std::string(origin) + ": " + doc.error());
    }
    const Value* list = doc.value().find("places");
    if (list == nullptr || !list->is_array()) {
        return Result<PlaceRules>::fail(std::string(origin) + ": no \"places\" array");
    }
    PlaceRules out;
    for (const Value& v : list->array()) {
        const auto bad = [&](const std::string& what) {
            return Result<PlaceRules>::fail(std::string(origin) + ": " + at(v) + what);
        };
        if (!v.is_object()) {
            return bad("a place is an object");
        }
        PlaceRule r;
        if (const Value* n = v.find("name"); n != nullptr && n->is_string()) {
            r.name = n->string();
        }
        r.surfaces = names(v.find("surface"));
        r.not_surfaces = names(v.find("not_surface"));
        if (const Value* room = v.find("room"); room != nullptr) {
            if (!room->is_integer()) {
                return bad("\"room\" must be an integer");
            }
            r.room = static_cast<int>(room->integer());
        }
        if (!read_bounds(v.find("x"), r.x_min, r.x_max) || !read_bounds(v.find("z"), r.z_min, r.z_max)) {
            return bad("\"x\"/\"z\" are [min, max] in metres (null = open)");
        }
        if (const Value* y = v.find("y_above"); y != nullptr) {
            if (!y->is_number()) {
                return bad("\"y_above\" must be a number");
            }
            r.y_above = to_q12(y->number());
        }
        const Value* sound = v.find("sound");
        if (sound == nullptr || !(sound->is_string() || sound->is_integer())) {
            return bad("a place needs a \"sound\" (a name of gamedata/sounds)");
        }
        r.sound_name = sound->is_string() ? sound->string() : std::to_string(sound->integer());
        r.sound = sounds.id(r.sound_name);
        if (r.sound < 0) {
            return bad("unknown sound \"" + r.sound_name + "\"");
        }
        out.rules_.push_back(std::move(r));
    }
    return Result<PlaceRules>::ok(std::move(out));
}

const PlaceRule* PlaceRules::match(std::string_view surface, const StepPlace& p) const {
    const PlaceRule* last = nullptr;
    const auto listed = [&](const std::vector<std::string>& list) {
        return std::find(list.begin(), list.end(), surface) != list.end();
    };
    for (const PlaceRule& r : rules_) {
        if ((!r.surfaces.empty() && !listed(r.surfaces)) || listed(r.not_surfaces) ||
            (r.room >= 0 && r.room != p.room) || (r.x_min && p.x < *r.x_min) || (r.x_max && p.x > *r.x_max) ||
            (r.z_min && p.z < *r.z_min) || (r.z_max && p.z > *r.z_max) || (r.y_above && !(p.y > *r.y_above))) {
            continue;
        }
        last = &r;
    }
    return last;
}

// ---- Footsteps --------------------------------------------------------------------------------

std::string map_name(int map_index) {
    static constexpr std::array<int, 8> kSubmaps = {3, 7, 5, 7, 7, 4, 6, 4};  // map0..map7
    if (map_index < 0) {
        return {};
    }
    for (int area = 0; area < static_cast<int>(kSubmaps.size()); ++area) {
        if (map_index < kSubmaps[static_cast<std::size_t>(area)]) {
            char name[16];
            std::snprintf(name, sizeof name, "map%d_s%02d", area, map_index);
            return name;
        }
        map_index -= kSubmaps[static_cast<std::size_t>(area)];
    }
    return {};
}

Footsteps::Footsteps(const CharaLibrary& library, SurfaceTable surfaces, std::filesystem::path gamedata_dir,
                     std::filesystem::path mods_dir)
    : library_(library),
      surfaces_(std::move(surfaces)),
      gamedata_dir_(std::move(gamedata_dir)),
      mods_dir_(std::move(mods_dir)) {}

const PlaceRules* Footsteps::places(std::string_view map) {
    namespace fs = std::filesystem;
    auto it = places_.find(map);
    if (it == places_.end()) {
        std::optional<PlaceRules> rules;
        // Base + every active mod's patch (places by "name"), in load order.
        std::vector<std::string> merge_warnings;
        const Result<mods::MergedFile> merged = mods::read_data_file(
            gamedata_dir_, mods_dir_, fs::path("maps") / std::string(map) / "footsteps.json", merge_warnings);
        for (std::string& w : merge_warnings) {
            warnings_.push_back(std::move(w));
        }
        if (merged || !merged.error().starts_with("no file")) {
            Result<PlaceRules> parsed =
                merged ? PlaceRules::parse(merged.value().text, merged.value().source(), library_.sound_registry())
                       : Result<PlaceRules>::fail(merged.error());
            if (parsed) {
                rules = std::move(parsed.value());
            } else {
                warnings_.push_back(parsed.error() + " (no place rules on this map)");
            }
        }
        it = places_.emplace(std::string(map), std::move(rules)).first;
    }
    return it->second ? &*it->second : nullptr;
}

StepChoice Footsteps::table(int chara, int material, int fallback) const {
    StepChoice choice;
    choice.surface = &surfaces_.of_material(material);
    Character who = library_.character(chara);
    if (!who.has_footsteps()) {
        who = library_.character(kHarry);
    }
    choice.sound = who.footstep(choice.surface->name, fallback);
    return choice;
}

StepChoice Footsteps::choose(int chara, int material, std::string_view map, const StepPlace& place,
                             int fallback) {
    StepChoice choice = table(chara, material, fallback);
    if (const PlaceRules* rules = places(map)) {
        if (const PlaceRule* r = rules->match(choice.surface->name, place)) {
            choice.sound = r->sound;
            choice.place = r;
        }
    }
    return choice;
}

std::vector<std::string> Footsteps::take_warnings() { return std::exchange(warnings_, {}); }

}  // namespace sh::chara
