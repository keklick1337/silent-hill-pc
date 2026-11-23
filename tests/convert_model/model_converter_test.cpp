#include "framework/sh_test.h"
#include "formats/archive/sh_archive.h"
#include "formats/common/json.h"
#include "formats/convert/level/level_textures.h"
#include "formats/convert/model/ilm.h"
#include "formats/convert/model/model_converter.h"
#include "formats/convert/model/tmd.h"
#include "formats/disc/disc_image.h"
#include "formats/gltf/gltf_inspect.h"
#include "formats/iso9660/iso9660.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

// Synthetic files check the geometry conventions exactly; the real image checks every ILM and TMD of the game with
// cgltf and against an independent count taken from the source parse.

namespace {

namespace cm = sh::convert::model;

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, std::size_t at, int v) {
    b[at] = static_cast<std::uint8_t>(v & 0xFF);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}
void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    put16(b, at, static_cast<int>(v & 0xFFFF));
    put16(b, at + 2, static_cast<int>(v >> 16));
}

// One part "01BODY", one mesh: a triangle (v0 v1 v2) and a quad (v0 v1 v2 v3) over four vertices.
//   v0 (0,0,0)  v1 (256,0,0)  v2 (0,256,0)  v3 (256,256,0)   PS1 units, y down
Bytes synthetic_ilm() {
    Bytes b(160, 0);
    b[0] = '0';
    b[1] = 6;
    b[3] = 1;
    put32(b, 4, 20);    // materials
    b[8] = 1;           // parts
    put32(b, 0xC, 44);  // part headers
    put32(b, 0x10, 60); // draw order
    std::memcpy(&b[20], "TEST", 4);
    std::memcpy(&b[44], "01BODY", 6);
    b[44 + 8] = 1;  // meshes
    put32(b, 44 + 0xC, 64);
    b[60] = 0;
    // mesh header at 64
    b[64] = 2;  // prims
    b[65] = 4;  // vertices
    b[66] = 1;  // normals
    put32(b, 64 + 4, 88);
    put32(b, 64 + 8, 128);
    put32(b, 64 + 0xC, 144);
    put32(b, 64 + 0x10, 152);
    // primitives
    const std::uint8_t uv[4][2] = {{0, 0}, {127, 0}, {0, 127}, {127, 127}};
    for (int p = 0; p < 2; ++p) {
        const std::size_t o = 88 + static_cast<std::size_t>(p) * 20;
        b[o] = uv[0][0];
        b[o + 1] = uv[0][1];
        put16(b, o + 2, 0x40);  // CLUT row 1
        b[o + 4] = uv[1][0];
        b[o + 5] = uv[1][1];
        put16(b, o + 6, 0);
        b[o + 8] = uv[2][0];
        b[o + 9] = uv[2][1];
        b[o + 0xA] = uv[3][0];
        b[o + 0xB] = uv[3][1];
        b[o + 0xC] = 0;
        b[o + 0xD] = 1;
        b[o + 0xE] = 2;
        b[o + 0xF] = p == 0 ? 0xFF : 3;
    }
    // vertices: xy at 128, z at 144
    const int xy[4][2] = {{0, 0}, {256, 0}, {0, 256}, {256, 256}};
    for (std::size_t v = 0; v < 4; ++v) {
        put16(b, 128 + v * 4, xy[v][0]);
        put16(b, 128 + v * 4 + 2, xy[v][1]);
    }
    // normal at 152: (0, 0, -128) = pointing at -Z, "into" the face seen from +Z, count 1
    b[152 + 2] = 0x80;
    b[152 + 3] = 1;
    return b;
}

// One object: a lit Gouraud triangle and an unlit flat quad over the same four vertices.
Bytes synthetic_tmd() {
    Bytes b(12 + 28 + 32 + 8 + 16 + 4 + 12 + 4, 0);
    put32(b, 0, 0x41);
    put32(b, 8, 1);
    // object table at 12; offsets relative to 12
    put32(b, 12, 28);        // vertices
    put32(b, 16, 4);
    put32(b, 20, 28 + 32);   // normals
    put32(b, 24, 1);
    put32(b, 28, 28 + 40);   // primitives
    put32(b, 32, 2);
    put32(b, 36, 0);         // scale
    const int v[4][3] = {{0, 0, 0}, {256, 0, 0}, {0, 256, 0}, {256, 256, 0}};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            put16(b, 12 + 28 + i * 8 + k * 2, v[i][k]);
        }
    }
    put16(b, 12 + 28 + 32 + 4, -4096);  // normal (0, 0, -1)
    std::size_t p = 12 + 28 + 40;
    // G3: olen 9, ilen 4, flag 0, mode 0x30; r g b code; n0 v0 n1 v1 n2 v2
    b[p] = 9; b[p + 1] = 4; b[p + 2] = 0; b[p + 3] = 0x30;
    b[p + 4] = 200; b[p + 5] = 100; b[p + 6] = 50; b[p + 7] = 0x30;
    for (std::size_t k = 0; k < 3; ++k) {
        put16(b, p + 8 + k * 4, 0);
        put16(b, p + 10 + k * 4, static_cast<int>(k));
    }
    p += 20;
    // NF4: olen 5, ilen 3, flag 1, mode 0x28; r g b code; v0 v1 v2 v3
    b[p] = 5; b[p + 1] = 3; b[p + 2] = 1; b[p + 3] = 0x28;
    b[p + 4] = 10; b[p + 5] = 20; b[p + 6] = 30; b[p + 7] = 0x28;
    for (std::size_t k = 0; k < 4; ++k) {
        put16(b, p + 8 + k * 2, static_cast<int>(k));
    }
    return b;
}

std::array<float, 3> cross(const float* a, const float* b, const float* c) {
    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    return {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
}

struct Disc {
    std::shared_ptr<const sh::disc::Image> image;
    std::vector<sh::archive::Entry> table;
    std::optional<sh::archive::Archive> silent;

    std::optional<Bytes> read(const std::string& name) const {
        for (const auto& e : table) {
            if (e.name == name) {
                return silent->read(e);
            }
        }
        return std::nullopt;
    }
};

std::optional<Disc> open_disc() {
    namespace fs = std::filesystem;
    const fs::path path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(path)) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    Disc disc;
    disc.image = std::shared_ptr<const sh::disc::Image>(sh::disc::Image::open(path));
    if (!disc.image) {
        return std::nullopt;
    }
    const auto volume = sh::iso9660::Volume::open(disc.image);
    if (!volume) {
        return std::nullopt;
    }
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    if (!slus || !silent || !hill) {
        return std::nullopt;
    }
    const std::shared_ptr<const sh::ByteSource> exe(volume->open_file(*slus));
    const std::shared_ptr<const sh::ByteSource> silent_src(volume->open_file(*silent));
    const auto table = sh::archive::read_file_table(*exe, {silent->lba, (silent->size + 2047) / 2048},
                                                    {hill->lba, (hill->size + 2047) / 2048});
    if (!table) {
        return std::nullopt;
    }
    disc.table = *table;
    disc.silent = sh::archive::Archive::open(silent_src, sh::archive::Region::Silent);
    if (!disc.silent) {
        return std::nullopt;
    }
    return disc;
}

bool has_extension(const std::string& name, const char* ext) {
    return name.size() > 4 && name.compare(name.size() - 4, 4, ext) == 0;
}

}  // namespace

SH_TEST(ModelConverterContract) {
    const auto conv = cm::make_converter();
    SH_CHECK(conv->name() == "model");
    SH_CHECK(!conv->lossless());
    const Bytes ilm = synthetic_ilm();
    const Bytes tmd = synthetic_tmd();
    SH_CHECK(conv->accepts(ilm, "CHARA/TEST.ILM"));
    SH_CHECK(conv->accepts(ilm, "chara\\test.ilm"));
    SH_CHECK(!conv->accepts(ilm, "CHARA/TEST.TIM"));
    SH_CHECK(!conv->accepts(ilm, "CHARA/TEST.TMD"));
    SH_CHECK(conv->accepts(tmd, "ITEM/TEST.TMD"));
    SH_CHECK(!conv->accepts(tmd, "ITEM/TEST.ILM"));
    SH_CHECK(!conv->accepts(Bytes(4, 0), "CHARA/TEST.ILM"));
    Bytes plm(20, 0);
    plm[0] = 0x30;
    plm[1] = 6;
    SH_CHECK(conv->accepts(plm, "ITEM/KNIFE.PLM"));
    SH_CHECK(!conv->accepts(plm, "BG/THR_GLB.PLM"));  // map libraries belong to the level converter

    const auto out = conv->decode(ilm, "CHARA/TEST.ILM");
    SH_CHECK(out);
    if (!out) {
        return;
    }
    SH_CHECK(out.value().size() == 2);
    SH_CHECK(out.value()[0].relative_path == "models/CHARA/TEST.glb");
    SH_CHECK(out.value()[1].relative_path == "models/CHARA/TEST.model.json");
    SH_CHECK(!conv->encode(out.value()));
}

SH_TEST(SyntheticIlmGeometry) {
    const Bytes ilm = synthetic_ilm();
    const auto converted = cm::convert_model(ilm, "CHARA/TEST.ILM", {});
    SH_CHECK(converted);
    if (!converted) {
        return;
    }
    const auto& st = converted.value().stats;
    SH_CHECK(st.kind == "ilm");
    SH_CHECK(st.parts == 1);
    SH_CHECK(st.source_vertices == 4);
    SH_CHECK(st.source_primitives == 2);
    SH_CHECK(st.source_triangles == 3);
    SH_CHECK(st.glb_triangles == 3);
    SH_CHECK(!st.rest_pose);

    const auto info = sh::gltf::inspect_glb(converted.value().glb, true);
    SH_CHECK(info);
    if (!info) {
        return;
    }
    SH_CHECK(info.value().meshes.size() == 1);
    const auto& mesh = info.value().meshes[0];
    SH_CHECK(mesh.name == "01BODY");
    SH_CHECK(mesh.triangles == 3);
    SH_CHECK(mesh.unique_positions == 4);
    // x' = -x/256, y' = -y/256: the square [0,256]^2 becomes [-1,0] x [-1,0]
    SH_CHECK(mesh.min[0] == -1.0f && mesh.max[0] == 0.0f);
    SH_CHECK(mesh.min[1] == -1.0f && mesh.max[1] == 0.0f);
    SH_CHECK(mesh.min[2] == 0.0f && mesh.max[2] == 0.0f);
    SH_CHECK(info.value().image_uris.size() == 1 && info.value().image_uris[0] == "../../textures/CHARA/TEST.png");

    // The source triangle v0 v1 v2 is clockwise for the PS1 (it is seen from -Z); the output is counter-clockwise
    // for that same side, so its geometric normal points to -Z.
    const float* t = mesh.triangle_positions.data();
    const auto n = cross(t, t + 3, t + 6);
    SH_CHECK(n[2] < 0.0f && n[0] == 0.0f && n[1] == 0.0f);
    // Same for the quad's two triangles.
    for (std::size_t k = 1; k < 3; ++k) {
        const float* q = mesh.triangle_positions.data() + k * 9;
        const auto nq = cross(q, q + 3, q + 6);
        SH_CHECK(nq[2] < 0.0f);
    }
}

// CHARA/EI.ILM and CHARA/MAN.ILM name sheets that exist only as TEST/EI.TIM and TEST/DEV.TIM.
SH_TEST(IlmTextureIsFoundInTestDirectory) {
    Bytes tim(24, 0);  // 16 bpp, 2 x 3 words at VRAM (0,0), no CLUT
    tim[0] = 0x10;
    tim[4] = 2;
    put32(tim, 8, 12 + 12);
    put16(tim, 16, 2);
    put16(tim, 18, 3);
    cm::Options options;
    std::vector<std::string> asked;
    options.sibling = [&](std::string_view p) -> std::optional<Bytes> {
        asked.emplace_back(p);
        return p == "TEST/TEST.TIM" ? std::optional<Bytes>(tim) : std::nullopt;
    };
    const auto converted = cm::convert_model(synthetic_ilm(), "CHARA/TEST.ILM", options);
    SH_CHECK(converted);
    if (!converted) {
        return;
    }
    SH_CHECK(converted.value().stats.texture == "TEST/TEST.TIM");
    const auto info = sh::gltf::inspect_glb(converted.value().glb);
    SH_CHECK(info && info.value().image_uris.size() == 1 && info.value().image_uris[0] == "../../textures/TEST/TEST.png");
    // CHARA is searched first.
    const auto chara = std::find(asked.begin(), asked.end(), "CHARA/TEST.TIM");
    const auto test_dir = std::find(asked.begin(), asked.end(), "TEST/TEST.TIM");
    SH_CHECK(chara != asked.end() && test_dir != asked.end() && chara < test_dir);
}

// 4 bpp TIM, 64 x 32 pixels, a CLUT block of 16 words by `rows` palettes.
Bytes synthetic_tim(int rows) {
    const std::size_t clut = 12 + 2 * 16 * static_cast<std::size_t>(rows);
    const std::size_t image = 12 + 2 * 16 * 32;
    Bytes t(8 + clut + image, 0);
    t[0] = 0x10;
    t[4] = 8;
    put32(t, 8, static_cast<std::uint32_t>(clut));
    put16(t, 8 + 8, 16);
    put16(t, 8 + 10, rows);
    put32(t, 8 + clut, static_cast<std::uint32_t>(image));
    put16(t, 8 + clut + 8, 16);
    put16(t, 8 + clut + 10, 32);
    return t;
}

// The faces use CLUT row 1 against the material's base 0: palette 1, the texture converter's TEST.p01.png.
SH_TEST(SyntheticIlmUsesThePaletteOfItsClutRow) {
    const Bytes tim = synthetic_tim(4);
    cm::Options options;
    options.sibling = [&](std::string_view p) -> std::optional<Bytes> {
        return p == "CHARA/TEST.TIM" ? std::optional<Bytes>(tim) : std::nullopt;
    };
    const auto converted = cm::convert_model(synthetic_ilm(), "CHARA/TEST.ILM", options);
    SH_CHECK(converted);
    if (!converted) {
        return;
    }
    const auto info = sh::gltf::inspect_glb(converted.value().glb);
    SH_CHECK(info && info.value().image_uris.size() == 1 &&
             info.value().image_uris[0] == "../../textures/CHARA/TEST.p01.png");

    // A CLUT block of one row has no palette 1: the material falls back to the first PNG and says so.
    const Bytes one = synthetic_tim(1);
    options.sibling = [&](std::string_view p) -> std::optional<Bytes> {
        return p == "CHARA/TEST.TIM" ? std::optional<Bytes>(one) : std::nullopt;
    };
    const auto fallback = cm::convert_model(synthetic_ilm(), "CHARA/TEST.ILM", options);
    SH_CHECK(fallback);
    if (fallback) {
        const auto again = sh::gltf::inspect_glb(fallback.value().glb);
        SH_CHECK(again && again.value().image_uris.size() == 1 &&
                 again.value().image_uris[0] == "../../textures/CHARA/TEST.png");
        const std::string glb(fallback.value().glb.begin(), fallback.value().glb.end());
        SH_CHECK(glb.find("\"palette_in_tim\":false") != std::string::npos);
    }
}

SH_TEST(SyntheticIlmRejectsDamage) {
    Bytes ilm = synthetic_ilm();
    ilm.resize(100);  // cuts the vertex arrays
    SH_CHECK(!cm::parse_ilm(ilm));
    SH_CHECK(!cm::convert_model(ilm, "CHARA/TEST.ILM", {}));
}

SH_TEST(SyntheticTmdGeometry) {
    const Bytes tmd = synthetic_tmd();
    const auto converted = cm::convert_model(tmd, "ITEM/TEST.TMD", {});
    SH_CHECK(converted);
    if (!converted) {
        return;
    }
    const auto& st = converted.value().stats;
    SH_CHECK(st.kind == "tmd");
    SH_CHECK(st.source_vertices == 4);
    SH_CHECK(st.source_primitives == 2);
    SH_CHECK(st.source_triangles == 3);
    const auto info = sh::gltf::inspect_glb(converted.value().glb, true);
    SH_CHECK(info);
    if (!info) {
        return;
    }
    SH_CHECK(info.value().meshes.size() == 1);
    SH_CHECK(info.value().meshes[0].triangles == 3);
    SH_CHECK(info.value().meshes[0].primitives == 2);  // lit gouraud group and unlit flat group
    for (std::size_t k = 0; k < 3; ++k) {
        const float* q = info.value().meshes[0].triangle_positions.data() + k * 9;
        SH_CHECK(cross(q, q + 3, q + 6)[2] < 0.0f);
    }
}

SH_TEST(TmdRejectsUnknownPacketSize) {
    Bytes tmd = synthetic_tmd();
    tmd[12 + 28 + 40 + 1] = 5;  // G3 claims 5 words
    SH_CHECK(!cm::parse_tmd(tmd));
}

SH_TEST(ItemTmdTexturesResolveByVramUpload) {
    const auto clut = [](int x, int row) { return static_cast<std::uint16_t>(row << 6 | x / 16); };
    const auto pack_key = cm::resolve_item_texture("ITEM/IT_001.TMD", 14, clut(160, 3));
    SH_CHECK(pack_key && pack_key->tim == "ITEM/TIM02.TIM" && pack_key->palette == 3 && pack_key->u0 == 0 &&
             pack_key->width == 256 && pack_key->height == 256);
    const auto other_group = cm::resolve_item_texture("ITEM/IT_002.TMD", 14, clut(160, 0));
    SH_CHECK(other_group && other_group->tim == "ITEM/TIM01.TIM");
    const auto common = cm::resolve_item_texture("item\\unq4e.tmd", 15, clut(176, 15));
    SH_CHECK(common && common->tim == "ITEM/TIM00.TIM" && common->palette == 15);
    // TIM07 sits 32 words (128 texels) into page 13.
    const auto model = cm::resolve_item_texture("ITEM/IT_000.TMD", 13, clut(240, 7));
    SH_CHECK(model && model->tim == "ITEM/TIM07.TIM" && model->palette == 7 && model->u0 == 128 &&
             model->width == 128 && model->height == 256);
    SH_CHECK(!cm::resolve_item_texture("ITEM/IT_000.TMD", 13, clut(240, 8)));  // TIM07 has 8 rows
    const auto fook = cm::resolve_item_texture("ITEM/FOOK.TMD", 5, clut(240, 9));
    SH_CHECK(fook && fook->tim == "ITEM/FOOK.TIM" && fook->palette == 1 && fook->v0 == 128 && fook->width == 128);
    SH_CHECK(!cm::resolve_item_texture("ITEM/FOOK.TMD", 15, clut(176, 0)));
    const auto unique = cm::resolve_item_texture("ITEM/UNQ6B.TMD", 14, clut(160, 9));
    SH_CHECK(unique && unique->tim == "ITEM/TIM03.TIM" && unique->palette == 9);
    SH_CHECK(!cm::resolve_item_texture("ITEM/UNQ4E.TMD", 14, clut(160, 0)));  // TIM05 and TIM06 disagree
    SH_CHECK(!cm::resolve_item_texture("ITEM/IT_000.TMD", 14, clut(160, 0)));  // map0_s00 loads no key TIM
    SH_CHECK(!cm::resolve_item_texture("ITEM/ITEM_00.TMD", 14, clut(160, 0)));  // never loaded by the game
    SH_CHECK(!cm::resolve_item_texture("TEST/IT_001.TMD", 14, clut(160, 0)));
    SH_CHECK(!cm::resolve_item_texture("ITEM/IT_001.TMD", 14 | 1 << 7, clut(160, 0)));  // 8-bit page
    SH_CHECK(!cm::resolve_item_texture("ITEM/IT_001.TMD", 14, clut(176, 0)));  // CLUT of another slot
}

namespace {

// One object, one lit flat textured triangle on texture page `tpage` with CLUT word `clut`.
Bytes textured_tmd(std::uint16_t tpage, std::uint16_t clut, std::array<std::array<int, 2>, 3> uv) {
    Bytes b(12 + 28 + 24 + 8 + 24, 0);
    put32(b, 0, 0x41);
    put32(b, 8, 1);
    put32(b, 12, 28);
    put32(b, 16, 3);
    put32(b, 20, 28 + 24);
    put32(b, 24, 1);
    put32(b, 28, 28 + 32);
    put32(b, 32, 1);
    put16(b, 12 + 28 + 8, 256);
    put16(b, 12 + 28 + 18, 256);
    put16(b, 12 + 28 + 24 + 4, -4096);
    std::size_t p = 12 + 28 + 32;
    b[p] = 7; b[p + 1] = 5; b[p + 2] = 0; b[p + 3] = 0x24;  // FT3: uv/clut/tpage block, n0 v0 v1 v2
    for (std::size_t k = 0; k < 3; ++k) {
        b[p + 4 + k * 4] = static_cast<std::uint8_t>(uv[k][0]);
        b[p + 5 + k * 4] = static_cast<std::uint8_t>(uv[k][1]);
    }
    put16(b, p + 6, clut);
    put16(b, p + 10, tpage);
    for (std::size_t k = 0; k < 3; ++k) {
        put16(b, p + 18 + k * 2, static_cast<int>(k));
    }
    return b;
}

struct GlbView {
    sh::json::Value json;
    std::vector<float> texcoords;
};

std::optional<GlbView> view_glb(const Bytes& glb) {
    std::uint32_t json_len = 0;
    std::memcpy(&json_len, glb.data() + 12, 4);
    auto parsed = sh::json::parse(std::string_view(reinterpret_cast<const char*>(glb.data() + 20), json_len));
    if (!parsed) {
        return std::nullopt;
    }
    GlbView out{std::move(parsed.value()), {}};
    const std::uint8_t* bin = glb.data() + 20 + json_len + 8;
    const auto& prim = out.json.find("meshes")->array()[0].find("primitives")->array()[0];
    const auto* uv = prim.find("attributes")->find("TEXCOORD_0");
    if (uv == nullptr) {
        return out;
    }
    const auto& acc = out.json.find("accessors")->array()[static_cast<std::size_t>(uv->integer())];
    const auto& view = out.json.find("bufferViews")->array()[static_cast<std::size_t>(acc.find("bufferView")->integer())];
    std::size_t at = 0;
    if (const auto* o = view.find("byteOffset")) {
        at += static_cast<std::size_t>(o->integer());
    }
    if (const auto* o = acc.find("byteOffset")) {
        at += static_cast<std::size_t>(o->integer());
    }
    out.texcoords.resize(static_cast<std::size_t>(acc.find("count")->integer()) * 2);
    std::memcpy(out.texcoords.data(), bin + at, out.texcoords.size() * 4);
    return out;
}

}  // namespace

SH_TEST(ItemTmdMaterialPointsAtItsTim) {
    // Page 13 is TIM07, uploaded 128 texels into the page: page texel 130 is TIM texel 2.
    const Bytes tmd = textured_tmd(13, static_cast<std::uint16_t>(5 << 6 | 240 / 16), {{{130, 10}, {200, 10}, {130, 90}}});
    const auto converted = cm::convert_model(tmd, "ITEM/IT_003.TMD", {});
    SH_CHECK(converted);
    if (!converted) {
        return;
    }
    SH_CHECK(converted.value().stats.texture == "ITEM/TIM07.TIM");
    const auto glb = view_glb(converted.value().glb);
    SH_CHECK(glb.has_value());
    if (!glb) {
        return;
    }
    const auto& mat = glb->json.find("materials")->array()[0];
    const auto* extras = mat.find("extras");
    SH_CHECK(extras->find("tim")->string() == "ITEM/TIM07.TIM");
    SH_CHECK(extras->find("palette")->integer() == 5);
    SH_CHECK(extras->find("palette_in_tim")->boolean());
    SH_CHECK(extras->find("png")->string() == "textures/ITEM/TIM07.png");
    SH_CHECK(extras->find("tim_width")->integer() == 128 && extras->find("tim_height")->integer() == 256);
    SH_CHECK(extras->find("tpage")->integer() == 13 && extras->find("clut_row")->integer() == 5);  // kept
    SH_CHECK(mat.find("pbrMetallicRoughness")->find("baseColorTexture") != nullptr);
    SH_CHECK(glb->json.find("images")->array()[0].find("uri")->string() == "../../textures/ITEM/TIM07.png");
    std::set<std::pair<float, float>> uvs;
    for (std::size_t i = 0; i + 1 < glb->texcoords.size(); i += 2) {
        uvs.insert({glb->texcoords[i], glb->texcoords[i + 1]});
    }
    SH_CHECK(uvs.count({2.5f / 128.0f, 10.5f / 256.0f}) == 1);
    SH_CHECK(uvs.count({72.5f / 128.0f, 10.5f / 256.0f}) == 1);
    SH_CHECK(uvs.count({2.5f / 128.0f, 90.5f / 256.0f}) == 1);

    // Coordinates left of the TIM picture: the page holds something else there, so the material stays page-relative.
    const Bytes outside = textured_tmd(13, static_cast<std::uint16_t>(240 / 16), {{{100, 10}, {200, 10}, {130, 90}}});
    const auto kept = cm::convert_model(outside, "ITEM/IT_003.TMD", {});
    SH_CHECK(kept);
    if (!kept) {
        return;
    }
    SH_CHECK(kept.value().stats.texture.empty());
    const auto plain = view_glb(kept.value().glb);
    SH_CHECK(plain && plain->json.find("materials")->array()[0].find("extras")->find("tim") == nullptr);
    SH_CHECK(plain && plain->json.find("images") == nullptr);
    SH_CHECK(plain && !plain->texcoords.empty() && plain->texcoords[0] == 100.5f / 256.0f);
}

SH_TEST(EveryIlmAndTmdOfTheImageConverts) {
    const auto disc = open_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    cm::Options options;
    options.sibling = [&](std::string_view p) { return disc->read(std::string(p)); };
    const auto conv = cm::make_converter(options);

    std::size_t ilm_files = 0, tmd_files = 0, parts = 0, source_vertices = 0, source_triangles = 0, glb_triangles = 0;
    std::size_t rest_poses = 0;
    for (const auto& e : disc->table) {
        const bool ilm = has_extension(e.name, ".ILM");
        const bool tmd = has_extension(e.name, ".TMD");
        if (!ilm && !tmd) {
            continue;
        }
        const auto raw = disc->read(e.name);
        SH_CHECK(raw.has_value());
        if (!raw) {
            continue;
        }
        SH_CHECK(conv->accepts(*raw, e.name));
        const auto out = conv->decode(*raw, e.name);
        SH_CHECK(out);
        if (!out) {
            continue;
        }
        const auto info = sh::gltf::inspect_glb(out.value()[0].bytes);
        SH_CHECK(info);
        if (!info) {
            continue;
        }

        // Counts from the source parse, which shares no code with the glb writer or the cgltf reader.
        std::size_t src_tris = 0, src_verts = 0;
        if (ilm) {
            const auto src = cm::parse_ilm(*raw);
            SH_CHECK(src);
            if (!src) {
                continue;
            }
            for (const auto& part : src.value().parts) {
                for (const auto& mesh : part.meshes) {
                    src_verts += mesh.vertices.size();
                    for (const auto& prim : mesh.prims) {
                        src_tris += prim.triangle ? 1 : 2;
                    }
                }
            }
            parts += src.value().parts.size();
            ++ilm_files;
        } else {
            const auto src = cm::parse_tmd(*raw);
            SH_CHECK(src);
            if (!src) {
                continue;
            }
            for (const auto& obj : src.value().objects) {
                src_verts += obj.vertices.size();
                for (const auto& prim : obj.prims) {
                    src_tris += prim.quad ? 2 : 1;
                }
            }
            parts += src.value().objects.size();
            ++tmd_files;
        }
        SH_CHECK(info.value().triangles == src_tris);
        const auto stats = cm::convert_model(*raw, e.name, options);
        SH_CHECK(stats);
        if (stats) {
            SH_CHECK(stats.value().stats.source_vertices == src_verts);
            SH_CHECK(stats.value().stats.referenced_vertices == src_verts);  // no unused vertices in the game's files
            SH_CHECK(stats.value().stats.glb_vertices == info.value().vertices);
            rest_poses += stats.value().stats.rest_pose ? 1 : 0;
        }
        source_vertices += src_verts;
        source_triangles += src_tris;
        glb_triangles += info.value().triangles;
    }
    SH_CHECK(ilm_files == 51);
    SH_CHECK(tmd_files == 90);
    SH_CHECK(parts == 1338);
    SH_CHECK(source_vertices == 33529);
    SH_CHECK(source_triangles == 64281);
    SH_CHECK(glb_triangles == source_triangles);
    SH_CHECK(rest_poses == 47);  // BOS2, TDRA, TEST/PRS2 and TEST/PRS3 have no animation file
}

SH_TEST(HeroHasHumanSizeAndFacing) {
    const auto disc = open_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    cm::Options options;
    options.sibling = [&](std::string_view p) { return disc->read(std::string(p)); };
    const auto raw = disc->read("CHARA/HERO.ILM");
    SH_CHECK(raw.has_value());
    if (!raw) {
        return;
    }
    const auto converted = cm::convert_model(*raw, "CHARA/HERO.ILM", options);
    SH_CHECK(converted);
    if (!converted) {
        return;
    }
    SH_CHECK(converted.value().stats.rest_pose);
    SH_CHECK(converted.value().stats.rest_pose_source == "ANIM/HB_BASE.ANM keyframe 0");
    SH_CHECK(converted.value().stats.texture == "CHARA/HERO.TIM");

    const auto info = sh::gltf::inspect_glb(converted.value().glb);
    SH_CHECK(info);
    if (!info) {
        return;
    }
    float top = -100.0f, bottom = 100.0f;
    float left_x = 0.0f, right_x = 0.0f;
    float head_z_front = 0.0f, head_z_back = 0.0f;
    for (const auto& m : info.value().meshes) {
        top = std::max(top, m.max[1]);
        bottom = std::min(bottom, m.min[1]);
        if (m.name == "03LSHOUL") {
            left_x = (m.min[0] + m.max[0]) / 2;
        }
        if (m.name == "07RSHOUL") {
            right_x = (m.min[0] + m.max[0]) / 2;
        }
        if (m.name == "02HEAD1") {
            head_z_front = m.max[2];
            head_z_back = m.min[2];
        }
    }
    // Harry stands about 1.7 m tall, on the floor, with his left side at +X and his face towards +Z.
    SH_CHECK(top > 1.6f && top < 1.8f);
    SH_CHECK(bottom > -0.05f && bottom < 0.1f);
    SH_CHECK(left_x > 0.1f);
    SH_CHECK(right_x < -0.1f);
    SH_CHECK(head_z_front > -head_z_back);
    // HERO.TIM has 15 palettes; the body uses CLUT rows 0-4, 8 and 9 (the items take the others).
    const std::vector<std::string> uris(info.value().image_uris.begin(), info.value().image_uris.end());
    SH_CHECK(uris.size() == 7);
    SH_CHECK(std::find(uris.begin(), uris.end(), "../../textures/CHARA/HERO.png") != uris.end());
    SH_CHECK(std::find(uris.begin(), uris.end(), "../../textures/CHARA/HERO.p09.png") != uris.end());
    SH_CHECK(std::find(uris.begin(), uris.end(), "../../textures/CHARA/HERO.p05.png") == uris.end());
}

// The 17 held items (ITEM/*.PLM): every one converts, cgltf reads it, and every texture is a PNG the texture converter
// writes: an existing TIM and a palette inside its CLUT block.
SH_TEST(EveryHeldItemPlmConverts) {
    const auto disc = open_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    cm::Options options;
    options.sibling = [&](std::string_view p) { return disc->read(std::string(p)); };
    const auto conv = cm::make_converter(options);
    std::size_t files = 0, triangles = 0;
    std::set<std::string> uris;
    for (const auto& e : disc->table) {
        if (!has_extension(e.name, ".PLM") || e.name.rfind("ITEM/", 0) != 0) {
            continue;
        }
        const auto raw = disc->read(e.name);
        SH_CHECK(raw && conv->accepts(*raw, e.name));
        if (!raw) {
            continue;
        }
        const auto out = conv->decode(*raw, e.name);
        SH_CHECK(out);
        if (!out) {
            continue;
        }
        SH_CHECK(out.value().size() == 2 && out.value()[1].relative_path.ends_with(".model.json"));
        const auto info = sh::gltf::inspect_glb(out.value()[0].bytes);
        SH_CHECK(info);
        if (!info) {
            continue;
        }
        SH_CHECK(info.value().triangles > 0);
        triangles += info.value().triangles;
        for (const std::string& uri : info.value().image_uris) {
            uris.insert(uri);
            if (e.name == "ITEM/KNIFE.PLM") {
                SH_CHECK(uri == "../../textures/CHARA/HERO.p05.png");  // CLUT 0x0140 on Harry's sheet
            }
        }
        ++files;
    }
    SH_CHECK(files == 17);
    std::fprintf(stderr, "held items: %zu files, %zu triangles, %zu textures\n", files, triangles, uris.size());
    for (const std::string& uri : uris) {
        SH_CHECK(uri.rfind("../../textures/", 0) == 0 && uri.ends_with(".png"));
        std::string stem = uri.substr(15, uri.size() - 15 - 4);
        int palette = 0;
        const auto dot = stem.rfind(".p");
        if (dot != std::string::npos && stem.find('/', dot) == std::string::npos) {
            palette = std::stoi(stem.substr(dot + 2));
            stem.resize(dot);
        }
        const auto tim = disc->read(stem + ".TIM");
        SH_CHECK(tim.has_value());
        if (tim) {
            const auto header = sh::convert::level::read_tim_info(*tim, stem + ".TIM");
            SH_CHECK(header && palette < header.value().palette_count);
        }
    }
}

SH_TEST(ConversionIsDeterministic) {
    const Bytes ilm = synthetic_ilm();
    const auto a = cm::convert_model(ilm, "CHARA/TEST.ILM", {});
    const auto b = cm::convert_model(ilm, "CHARA/TEST.ILM", {});
    SH_CHECK(a && b);
    if (a && b) {
        SH_CHECK(a.value().glb == b.value().glb);
        SH_CHECK(a.value().sidecar_json == b.value().sidecar_json);
    }
}

// Every textured material of the inventory packs and FOOK resolves to an ITEM TIM; of the UNQ models only the ones
// whose key TIM is ambiguous or unknown stay page-relative.
SH_TEST(InventoryTmdMaterialsResolveOnTheImage) {
    const auto disc = open_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    cm::Options options;
    options.sibling = [&](std::string_view p) { return disc->read(std::string(p)); };
    std::size_t textured = 0, resolved = 0;
    std::set<std::string> unresolved_files;
    for (const auto& e : disc->table) {
        if (e.name.rfind("ITEM/", 0) != 0 || !has_extension(e.name, ".TMD")) {
            continue;
        }
        const auto raw = disc->read(e.name);
        SH_CHECK(raw.has_value());
        if (!raw) {
            continue;
        }
        const auto converted = cm::convert_model(*raw, e.name, options);
        SH_CHECK(converted);
        if (!converted) {
            continue;
        }
        const auto glb = view_glb(converted.value().glb);
        SH_CHECK(glb.has_value());
        if (!glb) {
            continue;
        }
        for (const auto& mat : glb->json.find("materials")->array()) {
            const auto* extras = mat.find("extras");
            if (!extras->find("textured")->boolean()) {
                continue;
            }
            ++textured;
            if (extras->find("tim") != nullptr) {
                ++resolved;
            } else {
                unresolved_files.insert(e.name);
            }
        }
    }
    const std::set<std::string> expected = {"ITEM/ITEM_00.TMD", "ITEM/UNQ30.TMD", "ITEM/UNQ31.TMD", "ITEM/UNQ32.TMD",
                                            "ITEM/UNQ4E.TMD",   "ITEM/UNQ4F.TMD", "ITEM/UNQ51.TMD", "ITEM/UNQ52.TMD",
                                            "ITEM/UNQ65.TMD",   "ITEM/UNQ66.TMD"};
    std::fprintf(stderr, "inventory TMD materials: %zu textured, %zu resolved\n", textured, resolved);
    SH_CHECK(unresolved_files == expected);
    SH_CHECK(textured - resolved == expected.size() + 1);  // UNQ66 has an opaque and a semi-transparent material
    SH_CHECK(textured == 303);
}
