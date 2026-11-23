// SPDX-License-Identifier: GPL-3.0-or-later
// The effect textures a map loads (effect_textures.h).

#include "engine/effects/effect_textures.h"

#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

EffectTexture texture_of(const json::Value& v) {
    if (!v.is_object()) parse::fail(v, "a texture is an object");
    parse::reject_unknown(v, {"name", "file", "image", "v", "palette_row"});
    EffectTexture t;
    const json::Value* name = v.find("name");
    if (name != nullptr) {
        if (!name->is_string() || name->string().empty()) parse::fail(*name, "\"name\" is a non-empty string");
        t.name = name->string();
    }
    const json::Value& file = parse::member(v, "file");
    if (!file.is_string() || file.string().empty()) parse::fail(file, "\"file\" is a game file name");
    t.file = file.string();
    const json::Value& image = parse::member(v, "image");
    if (!image.is_string()) parse::fail(image, "\"image\" is \"effects\", \"fire\" or \"blood\"");
    if (image.string() == "effects") {
        t.image = EffectImage::Effects;
    } else if (image.string() == "fire") {
        t.image = EffectImage::Fire;
    } else if (image.string() == "blood") {
        t.image = EffectImage::Blood;
    } else {
        parse::fail(image, "\"image\" is \"effects\", \"fire\" or \"blood\"");
    }
    if (v.find("v") != nullptr) t.v = parse::integer_of(v, "v", 0, 255);
    if (v.find("palette_row") != nullptr) t.palette_row = parse::integer_of(v, "palette_row", 0, 511);
    return t;
}

}  // namespace

std::vector<const EffectTexture*> EffectTextureConfig::for_map(
    std::string_view map, const std::function<bool(const std::string&)>& flag_set) const {
    std::vector<const EffectTexture*> out;
    const Map* entry = nullptr;
    for (const Map& m : maps) {
        if (m.map == map) entry = &m;
    }
    if (entry == nullptr) return out;
    // Loaded in the order of the texture list, whatever the map's order.
    for (const EffectTexture& t : textures) {
        for (const MapEffectTexture& want : entry->textures) {
            if (want.texture != t.name) continue;
            if (!want.unless_flag.empty() && flag_set(want.unless_flag)) continue;
            out.push_back(&t);
            break;
        }
    }
    return out;
}

Result<EffectTextureConfig> parse_effect_textures(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) return Result<EffectTextureConfig>::fail(doc.error());
    const json::Value& root = doc.value();
    if (!root.is_object()) throw std::runtime_error("the file must be an object");
    parse::reject_unknown(root, {"boot", "textures", "maps"});

    EffectTextureConfig cfg;
    cfg.boot = texture_of(parse::member(root, "boot"));
    const json::Value& textures = parse::member(root, "textures");
    if (!textures.is_array()) parse::fail(textures, "\"textures\" is a list");
    for (const json::Value& v : textures.array()) {
        EffectTexture t = texture_of(v);
        if (t.name.empty()) parse::fail(v, "a texture of the list needs a \"name\"");
        for (const EffectTexture& other : cfg.textures) {
            if (other.name == t.name) parse::fail(v, "texture \"" + t.name + "\" is listed twice");
        }
        cfg.textures.push_back(std::move(t));
    }
    const json::Value& maps = parse::member(root, "maps");
    if (!maps.is_object()) parse::fail(maps, "\"maps\" is an object of map names");
    for (const auto& [map, list] : maps.members()) {
        if (map.rfind("_", 0) == 0) continue;
        if (!list.is_array()) parse::fail(list, "a map's textures are a list");
        EffectTextureConfig::Map m;
        m.map = map;
        for (const json::Value& e : list.array()) {
            MapEffectTexture want;
            if (e.is_string()) {
                want.texture = e.string();
            } else if (e.is_object()) {
                parse::reject_unknown(e, {"texture", "unless_flag"});
                const json::Value& t = parse::member(e, "texture");
                if (!t.is_string()) parse::fail(t, "\"texture\" is a texture name");
                want.texture = t.string();
                if (const json::Value* f = e.find("unless_flag")) {
                    if (!f->is_string() || f->string().empty()) parse::fail(*f, "\"unless_flag\" is a flag name");
                    want.unless_flag = f->string();
                }
            } else {
                parse::fail(e, "a map's texture is a name or {\"texture\", \"unless_flag\"}");
            }
            bool known = false;
            for (const EffectTexture& t : cfg.textures) known = known || t.name == want.texture;
            if (!known) parse::fail(e, "unknown texture \"" + want.texture + "\"");
            m.textures.push_back(std::move(want));
        }
        cfg.maps.push_back(std::move(m));
    }
    return Result<EffectTextureConfig>::ok(std::move(cfg));
} catch (const std::exception& e) {
    return Result<EffectTextureConfig>::fail(e.what());
}

Result<EffectTextureConfig> load_effect_textures(const std::filesystem::path& gamedata_dir,
                                                 const std::filesystem::path& mods_dir, std::string* source) {
    const std::filesystem::path relative = std::filesystem::path("effects") / "effect_textures.json";
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) *source = where;
    if (!read) return Result<EffectTextureConfig>::fail(read.error());
    Result<EffectTextureConfig> parsed = parse_effect_textures(read.value());
    if (!parsed) return Result<EffectTextureConfig>::fail(where + ": " + parsed.error());
    return parsed;
}

}  // namespace sh::effects
