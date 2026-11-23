#include "framework/sh_test.h"
#include "formats/gltf/gltf_inspect.h"
#include "formats/gltf/gltf_writer.h"
#include "formats/common/json.h"

#include <cstdint>
#include <string>
#include <vector>

// The writer is checked with cgltf (gltf_inspect), which parses the file, validates it and reads every accessor.

namespace {

using namespace sh::gltf;

Primitive quad_primitive(int material) {
    Primitive p;
    p.positions = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    p.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    p.texcoords = {0, 1, 1, 1, 1, 0, 0, 0};
    p.colors = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};
    p.indices = {0, 1, 2, 0, 2, 3};
    p.material = material;
    return p;
}

Document sample_document() {
    Document doc;
    doc.textures.push_back({"textures/CHARA/HERO.png", true});
    Material mat;
    mat.name = "mat00_row00";
    mat.texture = 0;
    mat.alpha_mode = AlphaMode::Blend;
    mat.extras = "{\"clut_row\":3}";
    doc.materials.push_back(mat);

    Mesh mesh;
    mesh.name = "quad";
    mesh.primitives.push_back(quad_primitive(0));
    mesh.primitives.push_back(quad_primitive(-1));
    doc.meshes.push_back(mesh);

    Node root;
    root.name = "root";
    root.children = {1};
    Node child;
    child.name = "child";
    child.mesh = 0;
    child.translation = {1, 2, 3};
    child.extras = "{\"bone\":4}";
    doc.nodes = {root, child};
    return doc;
}

}  // namespace

SH_TEST(JsonWriterEscapesAndNests) {
    sh::json::Writer w;
    w.begin_object();
    w.member("text", "a\"b\\c\n\t\x01");
    w.key("list").begin_array().value(1).value(2.5).value(true).null().end_array();
    w.key("empty").begin_object().end_object();
    w.member_float("f", 0.1f);
    w.end_object();
    SH_CHECK(w.str() == "{\"text\":\"a\\\"b\\\\c\\n\\t\\u0001\",\"list\":[1,2.5,true,null],\"empty\":{},\"f\":0.100000001}");
}

SH_TEST(JsonWriterPrettyLayout) {
    sh::json::Writer w(true);
    w.begin_object().member("a", 1).key("b").begin_array().value(1).value(2).end_array().end_object();
    SH_CHECK(w.str() == "{\n  \"a\": 1,\n  \"b\": [\n    1,\n    2\n  ]\n}");
}

SH_TEST(GlbRoundTripsThroughCgltf) {
    const Document doc = sample_document();
    const auto glb = write_glb(doc);
    SH_CHECK(glb);
    if (!glb) {
        return;
    }
    SH_CHECK(glb.value().size() % 4 == 0);
    const auto stats = inspect_glb(glb.value());
    SH_CHECK(stats);
    if (!stats) {
        return;
    }
    const GlbStats& s = stats.value();
    SH_CHECK(s.nodes == 2);
    SH_CHECK(s.materials == 1);
    SH_CHECK(s.meshes.size() == 1);
    SH_CHECK(s.meshes[0].name == "quad");
    SH_CHECK(s.meshes[0].primitives == 2);
    SH_CHECK(s.meshes[0].vertices == 8);
    SH_CHECK(s.meshes[0].triangles == 4);
    SH_CHECK(s.meshes[0].unique_positions == 4);
    SH_CHECK(s.image_uris.size() == 1 && s.image_uris[0] == "textures/CHARA/HERO.png");
}

SH_TEST(LargePrimitiveUsesIndexType32) {
    Document doc;
    Primitive p;
    const std::size_t n = 70000;
    p.positions.resize(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        p.positions[i * 3] = static_cast<float>(i);
    }
    p.indices = {0, 1, static_cast<std::uint32_t>(n - 1)};
    Mesh mesh;
    mesh.primitives.push_back(p);
    doc.meshes.push_back(mesh);
    Node node;
    node.mesh = 0;
    doc.nodes.push_back(node);
    const auto glb = write_glb(doc);
    SH_CHECK(glb);
    if (!glb) {
        return;
    }
    const auto stats = inspect_glb(glb.value());
    SH_CHECK(stats);
    if (stats) {
        SH_CHECK(stats.value().meshes[0].vertices == n);
        SH_CHECK(stats.value().meshes[0].triangles == 1);
    }
}

SH_TEST(SkinWithJointsAndInverseBind) {
    Document doc;
    Node joint0;
    joint0.name = "j0";
    joint0.children = {1};
    Node joint1;
    joint1.name = "j1";
    joint1.translation = {0, 1, 0};
    Node body;
    body.name = "body";
    body.mesh = 0;
    body.skin = 0;
    doc.nodes = {joint0, joint1, body};

    Primitive p = quad_primitive(-1);
    p.joints = {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
    p.weights = {1, 0, 0, 0, 0.5f, 0.5f, 0, 0, 0.5f, 0.5f, 0, 0, 1, 0, 0, 0};
    Mesh mesh;
    mesh.primitives.push_back(p);
    doc.meshes.push_back(mesh);

    Skin skin;
    skin.joints = {0, 1};
    skin.skeleton = 0;
    const std::array<float, 16> id = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> ibm1 = id;
    ibm1[13] = -1;
    skin.inverse_bind = {id, ibm1};
    doc.skins.push_back(skin);

    const auto glb = write_glb(doc);
    SH_CHECK(glb);
    if (!glb) {
        return;
    }
    const auto stats = inspect_glb(glb.value());
    SH_CHECK(stats);
    if (stats) {
        SH_CHECK(stats.value().skins == 1);
    }
}

SH_TEST(InconsistentDocumentsAreRejected) {
    {
        Document doc = sample_document();
        doc.meshes[0].primitives[0].indices[2] = 9;
        SH_CHECK(!write_glb(doc));
    }
    {
        Document doc = sample_document();
        doc.meshes[0].primitives[0].normals.pop_back();
        SH_CHECK(!write_glb(doc));
    }
    {
        Document doc = sample_document();
        doc.meshes[0].primitives[0].indices.pop_back();
        SH_CHECK(!write_glb(doc));
    }
    {
        Document doc = sample_document();
        doc.nodes[1].children = {0};  // cycle: 0 -> 1 -> 0
        SH_CHECK(!write_glb(doc));
    }
    {
        Document doc = sample_document();
        doc.nodes[1].mesh = 5;
        SH_CHECK(!write_glb(doc));
    }
    {
        Document doc = sample_document();
        doc.materials[0].texture = 3;
        SH_CHECK(!write_glb(doc));
    }
    {
        Document doc = sample_document();
        doc.meshes[0].primitives[0].positions[0] = 1.0f / 0.0f;
        SH_CHECK(!write_glb(doc));
    }
}

SH_TEST(InspectRejectsGarbage) {
    const std::vector<std::uint8_t> junk(64, 0x42);
    SH_CHECK(!inspect_glb(junk));
}
