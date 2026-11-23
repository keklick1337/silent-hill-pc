#include "framework/sh_test.h"

#include "formats/common/json.h"
#include "formats/convert/materials/materials_manifest.h"
#include "formats/gltf/gltf_writer.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sh::convert::materials::Report;

namespace {

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "materials" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(root, ec); }
    void write(const fs::path& relative, std::string_view bytes) const {
        fs::create_directories(root / relative.parent_path());
        std::ofstream out(root / relative, std::ios::binary);
        out << bytes;
    }
};

// A glb with one material whose texture image is `uri` relative to the glb.
std::string mini_glb(const std::string& uri) {
    sh::gltf::Document doc;
    doc.textures.push_back({uri, true});
    sh::gltf::Material material;
    material.name = "m";
    material.texture = 0;
    doc.materials.push_back(std::move(material));
    sh::gltf::Mesh mesh;
    mesh.name = "quad";
    sh::gltf::Primitive prim;
    prim.positions = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    prim.indices = {0, 1, 2, 0, 2, 3};
    prim.material = 0;
    mesh.primitives.push_back(std::move(prim));
    doc.meshes.push_back(std::move(mesh));
    sh::gltf::Node node;
    node.name = "n";
    node.mesh = 0;
    doc.nodes.push_back(std::move(node));
    doc.scene_roots.push_back(0);
    auto glb = sh::gltf::write_glb(doc);
    SH_CHECK(glb);
    return std::string(reinterpret_cast<const char*>(glb.value().data()), glb.value().size());
}

}  // namespace

SH_TEST(materials_manifest_reports_consumers_and_variants) {
    Scratch dir("manifest");
    // A model referencing a baked variant, a level referencing a base picture, one an unbaked palette name.
    dir.write("convert/models/HERO.glb", mini_glb("../textures/CHARA/HERO.prs2.png"));
    dir.write("convert/levels/MAP1_S02.glb", mini_glb("../textures/BG/BG_ETC.png"));
    dir.write("convert/levels/MAP1_S03.glb", mini_glb("../textures/BG/BG_ETC.p02.png"));
    dir.write("convert/textures/CHARA/HERO.prs2.png", "x");
    dir.write("convert/textures/BG/BG_ETC.png", "x");
    dir.write("convert/textures/TITLE/TITLE1.png", "x");
    dir.write("convert/textures/CHARA/HERO.png", "x");

    const auto result = sh::convert::materials::write_manifest(dir.root);
    SH_CHECK(result);
    const Report& report = result.value();
    SH_CHECK(report.consumers == 3);
    SH_CHECK(report.referenced == 3);
    SH_CHECK(report.textures == 4);
    SH_CHECK(report.variants == 1);
    SH_CHECK(report.palette_names == 1);
    SH_CHECK(report.warnings.empty());

    // The manifest text names the consumer of each texture and the unused variant.
    std::ifstream in(report.json_path);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = sh::json::parse(text);
    SH_CHECK(parsed);
    const sh::json::Value* consumers = parsed.value().find("consumers");
    SH_CHECK(consumers != nullptr && consumers->is_array());
    const sh::json::Value* textures = parsed.value().find("textures");
    SH_CHECK(textures != nullptr);
    SH_CHECK(textures->find("textures/CHARA/HERO.prs2.png") != nullptr);
    SH_CHECK(textures->find("textures/BG/BG_ETC.png") != nullptr);
    SH_CHECK(parsed.value().find("unused_palette_variants") == nullptr);

    SH_CHECK(sh::convert::materials::format_report_line(report).find("1 baked variants") != std::string::npos);
}

SH_TEST(materials_manifest_without_import_fails) {
    Scratch dir("empty");
    const auto result = sh::convert::materials::write_manifest(dir.root);
    SH_CHECK(!result);
    SH_CHECK(result.error().find("textures") != std::string::npos);
}
