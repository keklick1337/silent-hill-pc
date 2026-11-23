// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "formats/convert/materials/materials_manifest.h"

#include "formats/common/json.h"
#include "formats/gltf/gltf_inspect.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace sh::convert::materials {

namespace fs = std::filesystem;

namespace {

bool is_palette_variant(const std::string& name, int* number = nullptr) {
    // NAME.pNN.png — the importer's palette-row variants (tim_converter's png_path).
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || name.substr(dot) != ".png") {
        return false;
    }
    // rfind from dot - 1, or the ".p" of ".png" itself matches.
    const std::size_t p = name.rfind(".p", dot == 0 ? 0 : dot - 1);
    if (p == std::string::npos || p + 2 > dot) {
        return false;
    }
    const std::string digits = name.substr(p + 2, dot - p - 2);
    if (digits.empty() || digits.size() > 3 ||
        !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    if (number != nullptr) {
        *number = std::stoi(digits);
    }
    return true;
}

// "../textures/CHARA/HERO.p07.png", relative to the glb: the part from the last
// "textures/" component on, so the manifest names textures the same way the importer does.
std::string texture_relative_to_convert(const fs::path& glb_path, std::string_view uri) {
    if (uri.empty()) {
        return {};
    }
    const std::string text = (glb_path.parent_path() / uri).lexically_normal().generic_string();
    const std::size_t at = text.rfind("textures/");
    if (at == std::string::npos) {
        return text;
    }
    return text.substr(at);  // "textures/...", however deep the glb sits under convert/
}

std::vector<fs::path> glbs_under(const fs::path& dir, std::vector<std::string>& warnings) {
    std::vector<fs::path> found;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return found;
    }
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".glb") {
            found.push_back(it->path());
        }
    }
    if (ec) {
        warnings.push_back("walk " + dir.string() + ": " + ec.message());
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::string read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

std::string build_manifest_text(const std::vector<fs::path>& consumer_dirs, const fs::path& textures_dir,
                                Report& report) {
    // consumer -> texture set, texture -> consumer set, in file order.
    std::vector<std::pair<std::string, std::set<std::string>>> consumers;
    std::map<std::string, std::set<std::string>> by_texture;

    for (const fs::path& dir : consumer_dirs) {
        for (const fs::path& glb : glbs_under(dir, report.warnings)) {
            const std::string bytes = read_file(glb);
            if (bytes.empty()) {
                report.warnings.push_back("cannot read " + glb.string());
                continue;
            }
            const auto stats = sh::gltf::inspect_glb(
                {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}, false);
            if (!stats) {
                report.warnings.push_back(glb.string() + ": " + stats.error());
                continue;
            }
            std::set<std::string> used;
            for (const std::string& uri : stats.value().image_uris) {
                std::string texture = texture_relative_to_convert(glb, uri);
                if (texture.empty() || texture.rfind("textures/", 0) != 0) {
                    continue;  // an image of something else (a composite, an icon)
                }
                used.insert(texture);
                by_texture[texture].insert(glb.generic_string());
            }
            if (!used.empty()) {
                consumers.emplace_back(glb.generic_string(), std::move(used));
                ++report.consumers;
            }
        }
    }
    report.referenced = static_cast<unsigned>(by_texture.size());

    for (const auto& [texture, files] : by_texture) {
        const std::size_t slash = texture.rfind('/');
        const std::string name = texture.substr(slash == std::string::npos ? 0 : slash + 1);
        if (is_palette_variant(name)) {
            ++report.palette_names;
        } else if (std::count(name.begin(), name.end(), '.') >= 2) {
            ++report.variants;  // NAME.<label>.png of the texture bake
        }
    }

    std::error_code ec;
    for (fs::recursive_directory_iterator it(textures_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".png") {
            continue;
        }
        ++report.textures;
    }
    if (ec) {
        report.warnings.push_back("walk " + textures_dir.string() + ": " + ec.message());
    }

    json::Writer j(true);
    j.begin_object();
    j.key("description")
        .value("Which texture PNGs of convert/textures/ each converted model, level and map area (glb) uses, and "
               "which glbs use each picture. Written by the import after the texture bake: the pictures are "
               "true colour, NAME.png shows every texel in the CLUT row most geometry draws it with and "
               "NAME.<label>.png is a variant for the consumers that draw some texels with another row "
               "(convert/texture_bake.json lists rows and consumers per picture). A texture modder replaces the "
               "referenced files. A .pNN.png name still referenced here was not baked; it is built on demand from "
               "indexed/NAME.png and the CLUT row NN of NAME.tim.json, and a file with that name replaces it.");
    j.key("consumers").begin_array();
    for (const auto& [file, used] : consumers) {
        j.begin_object().key("file").value(file).key("textures").begin_array();
        for (const std::string& texture : used) {
            j.value(texture);
        }
        j.end_array().end_object();
    }
    j.end_array();
    j.key("textures").begin_object();
    for (const auto& [texture, files] : by_texture) {
        j.key(texture).begin_array();
        for (const std::string& file : files) {
            j.value(file);
        }
        j.end_array();
    }
    j.end_object();
    j.member("stats", std::to_string(report.consumers) + " consumers, " +
                          std::to_string(report.referenced) + " referenced, " +
                          std::to_string(report.textures) + " textures, " +
                          std::to_string(report.variants) + " baked variants, " +
                          std::to_string(report.palette_names) + " palette-row names");
    j.end_object();
    return j.take();
}

Result<Report> write_manifest(const fs::path& gamedata_dir) {
    Report report;
    const fs::path convert = gamedata_dir / "convert";
    std::vector<fs::path> consumer_dirs;
    for (const char* dir : {"models", "levels"}) {
        if (fs::is_directory(convert / dir)) {
            consumer_dirs.push_back(convert / dir);
        }
    }
    // The unified map files (gamedata/maps/areas/*.glb) reference convert/textures/ the same way.
    if (fs::is_directory(gamedata_dir / "maps" / "areas")) {
        consumer_dirs.push_back(gamedata_dir / "maps" / "areas");
    }
    if (!fs::is_directory(convert / "textures")) {
        return Result<Report>::fail("no " + (convert / "textures").string() + "; run the import first");
    }

    const std::string text = build_manifest_text(consumer_dirs, convert / "textures", report);
    std::string out = (convert / "materials.json").string();
    std::ofstream file(out, std::ios::binary);
    if (!file) {
        return Result<Report>::fail("cannot write " + out);
    }
    file << text;
    if (!file) {
        return Result<Report>::fail("cannot write " + out);
    }
    report.json_path = out;
    return Result<Report>::ok(std::move(report));
}

std::string format_report_line(const Report& report) {
    return "materials: " + std::to_string(report.consumers) + " consumers, " +
           std::to_string(report.referenced) + "/" + std::to_string(report.textures) +
           " textures referenced (" + std::to_string(report.variants) + " baked variants, " +
           std::to_string(report.palette_names) + " palette-row names)";
}

}  // namespace sh::convert::materials
