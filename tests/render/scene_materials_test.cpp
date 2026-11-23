// Surface materials of the maps (render/scene/materials.h): the shipped gamedata/materials/*.json parse without
// errors, a texture page is matched by its stem with the most specific entry winning, regions are normalised to the
// page, and bad entries are rejected.
#include "framework/sh_test.h"
#include "formats/common/json.h"
#include "render/scene/area_cells.h"
#include "render/scene/materials.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace sc = sh::scene;

SH_TEST(scene_materials_shipped_files_parse) {
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(std::string(SH_PROJECT_ROOT) + "/gamedata/materials")) {
        if (e.path().extension() != ".json") continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::ostringstream text;
        text << f.rdbuf();
        auto doc = sh::json::parse(text.str());
        SH_CHECK(static_cast<bool>(doc));
        if (!doc) continue;
        sc::SurfaceMaterial m;
        m.name = e.path().stem().string();
        std::vector<std::string> errors;
        SH_CHECK(sc::apply_surface_material(doc.value(), &m, &errors));
        SH_CHECK(errors.empty());
        SH_CHECK(!m.match.empty() || !m.regions.empty());
        ++files;
    }
    SH_CHECK(files > 0);
}

SH_TEST(scene_materials_texture_stem) {
    SH_CHECK(sc::texture_stem("convert/textures/BG/SU0101F.p09.png") == "SU0101F");
    SH_CHECK(sc::texture_stem("SU0101F.png") == "SU0101F");
    SH_CHECK(sc::texture_stem("DWAVE") == "DWAVE");
}

SH_TEST(scene_materials_bad_entries_are_rejected) {
    std::vector<std::string> errors;
    const auto list = sc::parse_surface_materials(
        R"({"materials": {"A": {"match": ["X*"], "wet": 2}, "B": {"roughness": 0.5}, "C": {"match": ["Y"]},
            "D": {"regions": [{"page": "P", "rect": [4, 4, 2, 8]}]}}})",
        &errors);
    SH_CHECK(errors.size() == 3);
    SH_CHECK(list.size() == 1 && list[0].name == "C" && list[0].roughness == 1.0f);
}

SH_TEST(scene_materials_most_specific_wins) {
    sc::set_material_source([](std::vector<std::string>*) {
        std::vector<std::string> errors;
        auto list = sc::parse_surface_materials(
            R"({"materials": {"Wide": {"match": ["AB*"], "roughness": 0.5},
                              "Narrow": {"match": ["ABC*"], "roughness": 0.4},
                              "Exact": {"match": ["ABCD"], "roughness": 0.3},
                              "Glass": {"regions": [{"page": "ABCD", "rect": [64, 0, 128, 32]}], "reflective": 0.6}}})",
            &errors);
        // An inferred entry loses against any hand-written one that names the page.
        sc::SurfaceMaterial inferred;
        inferred.name = "ABCD";
        inferred.match = {"ABCD"};
        inferred.roughness = 0.9f;
        inferred.inferred = true;
        list.push_back(inferred);
        return list;
    });
    sc::CellMaterial m;
    m.texture = "convert/textures/BG/ABCD.p01.png";
    SH_CHECK(sc::surface_material(m).name == "Exact");
    m.texture = "convert/textures/BG/ABCE.png";
    SH_CHECK(sc::surface_material(m).name == "Narrow");
    m.texture = "convert/textures/BG/ABZZ.png";
    SH_CHECK(sc::surface_material(m).name == "Wide");
    m.texture = "convert/textures/BG/ZZZZ.png";
    SH_CHECK(sc::surface_material(m).name == "Neutral");
    SH_CHECK(sc::has_surface_regions("ABCD") && !sc::has_surface_regions("ABCE"));
    const auto regions = sc::surface_regions("ABCD", 256, 256);
    SH_CHECK(regions.size() == 1);
    if (!regions.empty()) {
        SH_CHECK(regions[0].rect[0] == 0.25f && regions[0].rect[2] == 0.5f && regions[0].rect[3] == 0.125f);
        SH_CHECK(regions[0].surface[2] == 0.6f);
    }
    sc::set_material_source({});
}
