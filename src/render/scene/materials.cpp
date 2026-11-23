// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "render/scene/materials.h"

#include "formats/common/json.h"
#include "render/scene/area_cells.h"
#include "render/scene/scene.h"

#include <algorithm>
#include <map>
#include <optional>

namespace sh::scene {

namespace {

struct Table {
    bool loaded = false;
    std::uint32_t generation = 0;
    MaterialSource source;
    PictureSize picture_size;
    std::vector<SurfaceMaterial> materials;
    std::map<std::string, std::pair<int, int>, std::less<>> sizes;  // picture sizes asked so far
};

Table& table() {
    static Table t;
    return t;
}

const SurfaceMaterial& neutral() {
    static const SurfaceMaterial n{"Neutral", 1.0f, 0.0f, 0.0f, 0.0f, {}, {}, false};
    return n;
}

// The water surface reflects without data (water.json "surface" names its material).
const SurfaceMaterial& default_water() {
    static const SurfaceMaterial w{"Water", 0.04f, 0.0f, 0.8f, 0.0f, {}, {}, false};
    return w;
}

// How specifically `pattern` names `name`: 0 not at all; an exact name beats any prefix, a longer prefix a shorter.
int match_score(const std::string& pattern, std::string_view name) {
    if (name.empty() || pattern.empty()) return 0;
    if (pattern.back() == '*') {
        const std::string_view prefix = std::string_view(pattern).substr(0, pattern.size() - 1);
        return name.substr(0, prefix.size()) == prefix ? 1 + static_cast<int>(prefix.size()) : 0;
    }
    return name == pattern ? 1000 : 0;
}

const std::vector<SurfaceMaterial>& materials() {
    Table& t = table();
    if (t.loaded && t.generation == asset_generation()) return t.materials;
    t.loaded = true;
    t.generation = asset_generation();
    t.materials.clear();
    t.sizes.clear();
    if (!t.source) return t.materials;
    std::vector<std::string> errors;
    t.materials = t.source(&errors);
    for (const std::string& e : errors) log(1, "[SCENE] materials: " + e);
    return t.materials;
}

}  // namespace

void set_picture_size_query(PictureSize query) { table().picture_size = std::move(query); }

std::vector<RegionMaterial> surface_regions_of(const std::string& texture) {
    const std::string_view stem = texture_stem(texture);
    if (!has_surface_regions(stem)) return {};
    Table& t = table();
    auto it = t.sizes.find(texture);
    if (it == t.sizes.end()) {
        std::pair<int, int> size{0, 0};
        if (t.picture_size && !t.picture_size(texture, &size.first, &size.second)) size = {0, 0};
        it = t.sizes.emplace(texture, size).first;
    }
    return surface_regions(stem, it->second.first, it->second.second);
}

void set_material_source(MaterialSource source) {
    Table& t = table();
    t.source = std::move(source);
    t.loaded = false;
}

std::string_view texture_stem(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
    const std::size_t dot = path.find('.');
    return dot == std::string_view::npos ? path : path.substr(0, dot);
}

bool apply_surface_material(const json::Value& object, SurfaceMaterial* s, std::vector<std::string>* errors) {
    if (!object.is_object()) {
        errors->push_back(s->name + ": a material is an object");
        return false;
    }
    bool ok = true;
    auto unit = [&](const char* key, float* field) {
        const json::Value* v = object.find(key);
        if (!v) return;
        if (!v->is_number() || v->number() < 0.0 || v->number() > 1.0) {
            errors->push_back(s->name + ": \"" + key + "\" must be a number in 0..1");
            ok = false;
            return;
        }
        *field = static_cast<float>(v->number());
    };
    unit("roughness", &s->roughness);
    unit("metalness", &s->metalness);
    unit("reflective", &s->reflective);
    unit("wet", &s->wet);
    if (const json::Value* match = object.find("match")) {
        s->match.clear();
        if (!match->is_array()) {
            errors->push_back(s->name + ": \"match\" is a list of texture or material names");
            ok = false;
        } else {
            for (const json::Value& v : match->array())
                if (v.is_string() && !v.string().empty()) s->match.push_back(v.string());
        }
    }
    if (const json::Value* regions = object.find("regions")) {
        s->regions.clear();
        if (!regions->is_array()) {
            errors->push_back(s->name + ": \"regions\" is a list of {\"page\", \"rect\"}");
            return false;
        }
        for (const json::Value& r : regions->array()) {
            const json::Value* page = r.is_object() ? r.find("page") : nullptr;
            const json::Value* rect = r.is_object() ? r.find("rect") : nullptr;
            SurfaceRegion out;
            bool good = page && page->is_string() && !page->string().empty() && rect && rect->is_array() &&
                        rect->array().size() == 4;
            if (good) {
                out.page = page->string();
                for (std::size_t i = 0; i < 4; ++i) {
                    const json::Value& v = rect->array()[i];
                    good = good && v.is_number() && v.number() >= 0.0 && v.number() <= 4096.0;
                    if (good) out.rect[i] = static_cast<float>(v.number());
                }
                good = good && out.rect[2] > out.rect[0] && out.rect[3] > out.rect[1];
            }
            if (!good) {
                errors->push_back(s->name +
                                  ": a region is {\"page\": \"<stem>\", \"rect\": [x0, y0, x1, y1]} in "
                                  "pixels of the page, x1 > x0 and y1 > y0");
                ok = false;
                continue;
            }
            s->regions.push_back(std::move(out));
        }
    }
    return ok;
}

std::vector<SurfaceMaterial> parse_surface_materials(std::string_view text, std::vector<std::string>* errors) {
    std::vector<SurfaceMaterial> out;
    auto doc = json::parse(text);
    if (!doc) {
        errors->push_back(doc.error());
        return out;
    }
    const json::Value* list = doc.value().find("materials");
    if (!list || !list->is_object()) {
        errors->push_back("no \"materials\" object");
        return out;
    }
    for (const json::Value::Member& m : list->members()) {
        SurfaceMaterial s;
        s.name = m.first;
        bool ok = apply_surface_material(m.second, &s, errors);
        if (ok && s.match.empty() && s.regions.empty()) {
            errors->push_back(s.name + ": names no texture, region or material");
            ok = false;
        }
        if (ok) out.push_back(std::move(s));
    }
    return out;
}

const SurfaceMaterial& surface_material(const CellMaterial& m) {
    const std::string_view key = m.texture.empty() ? std::string_view(m.unpictured) : texture_stem(m.texture);
    const SurfaceMaterial* best = nullptr;
    int best_score = 0;
    for (const SurfaceMaterial& s : materials()) {
        for (const std::string& p : s.match) {
            int score = match_score(p, key);
            if (score > 0 && !s.inferred) score += 10000;
            if (score > best_score) {
                best_score = score;
                best = &s;
            }
        }
    }
    if (best) return *best;
    if (!m.unpictured.empty() && m.unpictured == water_surface().material) return default_water();
    return neutral();
}

bool has_surface_regions(std::string_view stem) {
    for (const SurfaceMaterial& s : materials())
        for (const SurfaceRegion& r : s.regions)
            if (r.page == stem) return true;
    return false;
}

std::vector<RegionMaterial> surface_regions(std::string_view stem, int width, int height) {
    std::vector<RegionMaterial> out;
    if (width <= 0 || height <= 0) return out;
    for (const SurfaceMaterial& s : materials()) {
        for (const SurfaceRegion& r : s.regions) {
            if (r.page != stem) continue;
            const float w = static_cast<float>(width), h = static_cast<float>(height);
            out.push_back({{r.rect[0] / w, r.rect[1] / h, r.rect[2] / w, r.rect[3] / h}, s.packed()});
        }
    }
    return out;
}

}  // namespace sh::scene
