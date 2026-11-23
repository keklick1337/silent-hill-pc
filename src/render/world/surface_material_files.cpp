// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The surface material table of the scene (render/scene/materials.h) from its files: the import's inferred defaults
// (convert/surface_materials.json, one entry per page), then gamedata/materials/<Name>.json and every active mod's
// materials/<Name>.json, merged key by key (data_merge.h): a file of the same name as an inferred entry changes only
// the keys it gives, a mod's file of a base name patches it, any other name is a new material.

#include "render/world/surface_material_files.h"

#include <filesystem>
#include <map>
#include <set>
#include <system_error>

#include "engine/mods/data_merge.h"
#include "engine/mods/mods.h"
#include "formats/common/json.h"

namespace sh::world {

namespace fs = std::filesystem;

namespace {

void list_names(const fs::path& dir, std::set<std::string>* names) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".json") continue;
        names->insert(e.path().stem().string());
    }
}

}  // namespace

std::vector<scene::SurfaceMaterial> load_surface_materials(const fs::path& gamedata_dir, const fs::path& mods_dir,
                                                           std::vector<std::string>* errors) {
    std::map<std::string, scene::SurfaceMaterial> by_name;
    std::vector<std::string> order;
    std::error_code ec;
    const fs::path inferred = gamedata_dir / "convert" / "surface_materials.json";
    if (fs::is_regular_file(inferred, ec)) {
        auto text = mods::read_data_text(gamedata_dir, mods_dir, "convert/surface_materials.json");
        if (text) {
            std::vector<std::string> parse_errors;
            for (scene::SurfaceMaterial& m : scene::parse_surface_materials(text.value(), &parse_errors)) {
                m.inferred = true;
                order.push_back(m.name);
                by_name.emplace(m.name, std::move(m));
            }
            for (const std::string& e : parse_errors) errors->push_back("convert/surface_materials.json: " + e);
        }
    }

    std::set<std::string> names;
    list_names(gamedata_dir / "materials", &names);
    std::vector<std::string> warnings;
    for (const mods::ModInfo& mod : mods::discover(mods_dir, warnings)) list_names(mod.root / "materials", &names);
    for (const std::string& name : names) {
        const fs::path relative = fs::path("materials") / (name + ".json");
        std::string source;
        auto text = mods::read_data_text(gamedata_dir, mods_dir, relative, &source);
        if (!text) {
            errors->push_back(text.error());
            continue;
        }
        auto doc = json::parse(text.value());
        if (!doc) {
            errors->push_back(source + ": " + doc.error());
            continue;
        }
        auto [it, added] = by_name.try_emplace(name);
        scene::SurfaceMaterial& m = it->second;
        if (added) {
            m.name = name;
            order.push_back(name);
        }
        // A hand-written file makes the entry its own, even where it only patches an inferred one.
        m.inferred = false;
        std::vector<std::string> bad;
        if (!scene::apply_surface_material(doc.value(), &m, &bad))
            for (const std::string& e : bad) errors->push_back(source + ": " + e);
        if (m.match.empty() && m.regions.empty()) errors->push_back(source + ": names no texture, region or material");
    }
    for (const std::string& w : warnings) errors->push_back(w);

    std::vector<scene::SurfaceMaterial> out;
    out.reserve(order.size());
    for (const std::string& name : order) {
        scene::SurfaceMaterial& m = by_name[name];
        if (!m.match.empty() || !m.regions.empty()) out.push_back(std::move(m));
    }
    return out;
}

}  // namespace sh::world
