// Item models as scene meshes (render/scene/item_model.h): a hand-built TMD converted by the importer, read back from
// the glb and its face map (positions, packet corner order, texels, colours), a packet fix, the draw (colours,
// culling, the bucket clamp, blend modes) and the sheet colours.

#include "formats/convert/model/model_build.h"
#include "formats/png/sh_png.h"
#include "framework/sh_test.h"
#include "render/scene/item_model.h"
#include "render/scene/scene.h"
#include "render/scene/scene_c.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace sc = sh::scene;

void put16(std::vector<std::uint8_t>& out, std::size_t at, std::uint16_t v) {
    out[at] = static_cast<std::uint8_t>(v);
    out[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}

// One lit gouraud triangle in its colour (mode 0x30, four words): colour, then normal/vertex index pairs.
std::vector<std::uint8_t> flat_triangle(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint16_t v0,
                                        std::uint16_t v1, std::uint16_t v2) {
    std::vector<std::uint8_t> p(20, 0);
    p[0] = 4;
    p[1] = 4;
    p[3] = 0x30;
    p[4] = r;
    p[5] = g;
    p[6] = b;
    put16(p, 10, v0);
    put16(p, 14, v1);
    put16(p, 18, v2);
    return p;
}

// One lit textured gouraud triangle (mode 0x34, six words): uv/clut, uv/tpage, uv, then the index pairs.
std::vector<std::uint8_t> textured_triangle(std::uint16_t clut, std::uint16_t tpage, std::uint8_t mode) {
    std::vector<std::uint8_t> p(28, 0);
    p[0] = 7;
    p[1] = 6;
    p[3] = mode;
    p[4] = 3;
    p[5] = 4;
    put16(p, 6, clut);
    p[8] = 13;
    p[9] = 4;
    put16(p, 10, tpage);
    p[12] = 3;
    p[13] = 14;
    put16(p, 18, 0);
    put16(p, 22, 1);
    put16(p, 26, 2);
    return p;
}

// A one-object TMD: three vertices, one normal, the given packets.
std::vector<std::uint8_t> tmd_of(const std::vector<std::vector<std::uint8_t>>& packets) {
    std::vector<std::uint8_t> prims;
    for (const auto& p : packets) prims.insert(prims.end(), p.begin(), p.end());
    const std::size_t table = 12, object = 28;
    const std::size_t prim_off = object, vert_off = prim_off + prims.size(), norm_off = vert_off + 3 * 8;
    std::vector<std::uint8_t> out(table + norm_off + 8, 0);
    put32(out, 0, 0x41);
    put32(out, 4, 0);
    put32(out, 8, 1);
    put32(out, table + 0, static_cast<std::uint32_t>(vert_off));
    put32(out, table + 4, 3);
    put32(out, table + 8, static_cast<std::uint32_t>(norm_off));
    put32(out, table + 12, 1);
    put32(out, table + 16, static_cast<std::uint32_t>(prim_off));
    put32(out, table + 20, static_cast<std::uint32_t>(packets.size()));
    put32(out, table + 24, 0);
    std::memcpy(&out[table + prim_off], prims.data(), prims.size());
    const std::int16_t v[3][3] = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}};
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) put16(out, table + vert_off + i * 8 + k * 2, static_cast<std::uint16_t>(v[i][k]));
    put16(out, table + norm_off + 4, 4096);
    return out;
}

struct Converted {
    std::vector<std::uint8_t> glb;
    std::vector<std::uint8_t> side;
};

Converted convert(const std::vector<std::uint8_t>& tmd, const char* path) {
    auto r = sh::convert::model::build_tmd(tmd, path, {});
    Converted c;
    if (!r) return c;
    c.glb = r.value().glb;
    c.side.assign(r.value().sidecar_json.begin(), r.value().sidecar_json.end());
    return c;
}

sc::ItemModelPose identity_at(std::int32_t z) {
    sc::ItemModelPose pose;
    pose.m[0][0] = pose.m[1][1] = pose.m[2][2] = 4096;
    pose.t[2] = z;
    return pose;
}

}  // namespace

SH_TEST(scene_item_mesh_reads_faces_in_packet_order) {
    // A textured triangle on the common sheet (page 15, CLUT (176, 0): ITEM/TIM00.TIM row 0) and a coloured one.
    const auto tmd = tmd_of({textured_triangle(11, 15, 0x34), flat_triangle(200, 100, 40, 0, 1, 2)});
    const Converted c = convert(tmd, "ITEM/IT_000.TMD");
    SH_CHECK(!c.glb.empty());
    auto mesh = sc::read_item_mesh(c.glb, c.side, 0, nullptr, "test");
    SH_CHECK(static_cast<bool>(mesh));
    const sc::ItemMesh& m = mesh.value();
    SH_CHECK(m.drawable && m.faces.size() == 2);
    const sc::ItemFace& t = m.faces[0];
    const sc::ItemMaterial& tm = m.materials.at(static_cast<std::size_t>(t.material));
    SH_CHECK(tm.textured && tm.sheet && tm.sheet->tim == "ITEM/TIM00.TIM" && tm.sheet->palette == 0);
    SH_CHECK(t.position[0][0] == 0.0f && t.position[1][0] == 100.0f && t.position[2][1] == 100.0f);
    SH_CHECK(t.texel[0][0] == 3 && t.texel[0][1] == 4 && t.texel[1][0] == 13 && t.texel[2][1] == 14);
    SH_CHECK(!t.semi);
    const sc::ItemFace& f = m.faces[1];
    SH_CHECK(!m.materials.at(static_cast<std::size_t>(f.material)).textured);
    SH_CHECK(f.color[0] == 200 && f.color[1] == 100 && f.color[2] == 40);
    SH_CHECK(f.position[1][0] == 100.0f && f.position[2][1] == 100.0f);

    // Without the sidecar the glb's triangles are the faces, still in packet corner order.
    auto bare = sc::read_item_mesh(c.glb, {}, 0, nullptr, "test");
    SH_CHECK(static_cast<bool>(bare) && bare.value().faces.size() == 2);
    SH_CHECK(!static_cast<bool>(sc::read_item_mesh(c.glb, c.side, 1, nullptr, "test")));
}

SH_TEST(scene_item_mesh_packet_fix_reaches_the_faces) {
    const auto tmd = tmd_of({textured_triangle(11, 15, 0x36), flat_triangle(10, 20, 30, 0, 1, 2)});
    const Converted c = convert(tmd, "ITEM/IT_000.TMD");
    std::uint32_t seen_vertices = 0, seen_packets = 0;
    const sc::ItemPacketFix fix = [&](std::span<std::uint8_t> p, std::uint32_t vertices, std::uint32_t packets) {
        seen_vertices = vertices;
        seen_packets = packets;
        p[3] &= static_cast<std::uint8_t>(~2u);  // the first packet stops blending
        p[8] = 20;                               // its second corner moves
        return true;
    };
    auto plain = sc::read_item_mesh(c.glb, c.side, 0, nullptr, "test");
    SH_CHECK(static_cast<bool>(plain) && plain.value().faces[0].semi);
    auto fixed = sc::read_item_mesh(c.glb, c.side, 0, &fix, "test");
    SH_CHECK(static_cast<bool>(fixed));
    SH_CHECK(seen_vertices == 3 && seen_packets == 2);
    SH_CHECK(!fixed.value().faces[0].semi && fixed.value().faces[0].texel[1][0] == 20);
    SH_CHECK(fixed.value().faces[0].texel[0][0] == 3);
}

SH_TEST(scene_item_model_draws_dimmed_and_culled) {
    ShScene_FrameEnd();
    const Converted c = convert(tmd_of({flat_triangle(200, 100, 40, 0, 1, 2)}), "ITEM/IT_000.TMD");
    auto mesh = sc::read_item_mesh(c.glb, c.side, 0, nullptr, "test");
    SH_CHECK(static_cast<bool>(mesh));
    const sc::ItemModelDraw d = sc::add_item_model(mesh.value(), identity_at(1000), {256, 160, 120}, 128, 1, 2048, {});
    SH_CHECK(d.drawn && d.meshes.size() == 1);
    const sc::PrelitMesh& m = sc::current_frame().prelit.at(static_cast<std::size_t>(d.meshes[0]));
    SH_CHECK(m.isolated && !m.casts_shadow && m.texture_key == 0);
    SH_CHECK(m.vertices[0].r == 100 && m.vertices[0].g == 50 && m.vertices[0].b == 20);
    SH_CHECK(m.vertices[1].x == 100.0f && m.vertices[2].y == 100.0f && m.vertices[0].z == 1000.0f);
    SH_CHECK(d.bucket >= 1 && d.bucket < 2048);
    ShScene_FrameEnd();

    // Wound the other way: back facing on screen; behind the camera: dropped.
    const Converted back = convert(tmd_of({flat_triangle(128, 128, 128, 0, 2, 1)}), "ITEM/IT_000.TMD");
    auto back_mesh = sc::read_item_mesh(back.glb, back.side, 0, nullptr, "test");
    SH_CHECK(static_cast<bool>(back_mesh));
    sc::ItemModelDraw b = sc::add_item_model(back_mesh.value(), identity_at(1000), {256, 0, 0}, 256, 1, 2048, {});
    SH_CHECK(b.drawn && b.meshes.empty());
    b = sc::add_item_model(mesh.value(), identity_at(-10), {256, 0, 0}, 256, 1, 2048, {});
    SH_CHECK(b.drawn && b.meshes.empty());
    SH_CHECK(sc::current_frame().prelit.empty());
}

SH_TEST(scene_item_model_rejects_kinds_and_unresolved_sheets) {
    ShScene_FrameEnd();
    // A blended packet in its colour (mode 0x32): the item drawer draws none of the model.
    auto blended = flat_triangle(128, 128, 128, 0, 1, 2);
    blended[3] = 0x32;
    const Converted u = convert(tmd_of({blended}), "ITEM/IT_000.TMD");
    auto um = sc::read_item_mesh(u.glb, u.side, 0, nullptr, "test");
    SH_CHECK(static_cast<bool>(um) && !um.value().drawable);
    SH_CHECK(!sc::add_item_model(um.value(), identity_at(1000), {256, 0, 0}, 256, 1, 2048, {}).drawn);

    // A key-sheet page (14) of a UNQ model: no TIM at import; without a resolver nothing is drawn.
    const Converted k = convert(tmd_of({textured_triangle(10, 14, 0x34)}), "ITEM/UNQ66.TMD");
    auto km = sc::read_item_mesh(k.glb, k.side, 0, nullptr, "test");
    SH_CHECK(static_cast<bool>(km) && km.value().drawable && !km.value().materials[0].sheet);
    SH_CHECK(!sc::add_item_model(km.value(), identity_at(1000), {256, 0, 0}, 256, 1, 2048, {}).drawn);
    SH_CHECK(sc::current_frame().prelit.empty());
}

SH_TEST(scene_item_sheet_colours_like_the_prim_path) {
    sh::png::Image im;
    im.width = 2;
    im.height = 1;
    im.color_type = sh::png::ColorType::Indexed;
    im.pixels = {1, 2};
    im.palette = {0, 0, 0, 10, 10, 10, 20, 20, 20};
    auto png = sh::png::encode(im);
    SH_CHECK(static_cast<bool>(png));
    const std::string json = R"({"palettes": [["0000", "801F", "7C00"], ["0000", "0000", "0000"]]})";
    int w = 0, h = 0;
    auto rgba = sc::item_sheet_rgba(png.value(), std::span<const std::uint8_t>(
                                                     reinterpret_cast<const std::uint8_t*>(json.data()), json.size()),
                                    0, w, h);
    SH_CHECK(static_cast<bool>(rgba) && w == 2 && h == 1);
    const auto& p = rgba.value();
    // 0x801F: red 31 with STP -> (31 * 8 * 255 + 128) / 256 = 247, alpha 128; 0x7C00: blue 31, solid.
    SH_CHECK(p[0] == 247 && p[1] == 0 && p[2] == 0 && p[3] == 128);
    SH_CHECK(p[4] == 0 && p[6] == 247 && p[7] == 255);
    SH_CHECK(!static_cast<bool>(sc::item_sheet_rgba(
        png.value(), std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(json.data()), json.size()), 5,
        w, h)));
}
