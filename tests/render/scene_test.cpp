// The engine's scene renderer (render/scene): the cells of an area file in PS1 axes, map textures with the STP bit
// restored from the TIM palettes, and the frame description of the C face. The area files of a real import are read
// when SH_TEST_GAMEDATA points at one.

#include "formats/png/sh_png.h"
#include "framework/sh_test.h"
#include "render/scene/area_cells.h"
#include "render/scene/scene.h"
#include "render/scene/scene_c.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace sc = sh::scene;

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put_floats(std::vector<std::uint8_t>& out, std::initializer_list<float> values) {
    for (float f : values) {
        std::uint32_t u;
        std::memcpy(&u, &f, 4);
        put32(out, u);
    }
}

// The nodes of synthetic_area: one cell root (cell 1, 2) holding a translated triangle, and one root without cell
// extras.
constexpr const char* kAreaNodes = R"("scenes":[{"nodes":[0,2]}],
"nodes":[{"name":"tst0102","children":[1],"extras":{"source":"BG/TST0102.IPD","cell_x":1,"cell_z":2}},
{"name":"M","mesh":0,"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0.5,0,0,1]},{"name":"LIB"}],)";

// Library models: the cell's "/models" group (not part of the cell) and a PLM root with a source file.
constexpr const char* kLibraryNodes = R"("scenes":[{"nodes":[0,4]}],
"nodes":[{"name":"tst0102","children":[1,2],"extras":{"source":"BG/TST0102.IPD","cell_x":1,"cell_z":2}},
{"name":"M","mesh":0,"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0.5,0,0,1]},
{"name":"tst0102/models","children":[3]},
{"name":"door_hid","mesh":0,"extras":{"model_index":3,"ot_group":2}},
{"name":"TST_GLB","children":[5],"extras":{"source":"BG/TST_GLB.PLM"}},
{"name":"ITEM","mesh":0,"extras":{"model_index":0,"ot_group":0}}],)";

// A glb with the nodes `nodes` and one triangle mesh.
std::vector<std::uint8_t> synthetic_area(const char* nodes = kAreaNodes) {
    std::vector<std::uint8_t> bin;
    // Positions (metres, the converter's axes), normals, uvs, indices.
    put_floats(bin, {-41.f, 0.f, 80.f, -42.f, 0.f, 80.f, -41.f, -1.f, 81.f});
    put_floats(bin, {0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f});
    put_floats(bin, {0.f, 0.f, 1.f, 0.f, 0.f, 1.f});
    put32(bin, 0x00010000u);  // u16 indices 0, 1
    put32(bin, 0x00000002u);  // 2, pad
    const std::string json = std::string(R"({"asset":{"version":"2.0"},"scene":0,)") + nodes + R"(
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
"materials":[{"name":"m","pbrMetallicRoughness":{"baseColorFactor":[1,1,1,0.5]},"alphaMode":"BLEND",
"extras":{"png":"textures/BG/TST.p02.png","palette":2,"semi_transparent":true}}],
"buffers":[{"byteLength":104}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},
{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-42,-1,80],"max":[-41,0,81]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    std::string j = json;
    while (j.size() % 4) j += ' ';
    while (bin.size() % 4) bin.push_back(0);
    std::vector<std::uint8_t> glb;
    put32(glb, 0x46546C67u);
    put32(glb, 2);
    put32(glb, static_cast<std::uint32_t>(12 + 8 + j.size() + 8 + bin.size()));
    put32(glb, static_cast<std::uint32_t>(j.size()));
    put32(glb, 0x4E4F534Au);
    glb.insert(glb.end(), j.begin(), j.end());
    put32(glb, static_cast<std::uint32_t>(bin.size()));
    put32(glb, 0x004E4942u);
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

SH_TEST(scene_area_cells_synthetic) {
    auto cells = sc::read_area_cells(synthetic_area(), "TST.glb");
    SH_CHECK(cells);
    if (!cells) return;
    // The library root has no grid square: it is not a cell.
    SH_CHECK(cells.value().size() == 1);
    if (cells.value().size() != 1) return;
    const sc::CellMesh& c = cells.value()[0];
    SH_CHECK(c.stem == "TST0102");
    SH_CHECK(c.cell_x == 1 && c.cell_z == 2);
    SH_CHECK(c.vertices.size() == 3);
    SH_CHECK(c.indices.size() == 3);
    SH_CHECK(c.materials.size() == 1);
    if (c.vertices.size() != 3 || c.materials.size() != 1) return;
    // Node translation applied, then metres -> PS1 axes (x' = -x, y' = -y, z' = z, 256 per metre) relative to the
    // cell corner (40 m squares): x = (41 - 0.5) * 256 - 1 * 10240, z = 80 * 256 - 2 * 10240.
    SH_CHECK(near(c.vertices[0].x, 40.5f * 256.f - 10240.f));
    SH_CHECK(near(c.vertices[0].y, 0.f));
    SH_CHECK(near(c.vertices[0].z, 80.f * 256.f - 20480.f));
    SH_CHECK(near(c.vertices[2].y, 256.f));
    // The file's outward +Y normal is the PS1 data's inward one: (nx, ny, -nz) -> (0, 1, 0).
    SH_CHECK(near(c.vertices[0].ny, 1.f));
    SH_CHECK(near(c.vertices[1].u, 1.f) && near(c.vertices[2].v, 1.f));
    SH_CHECK(c.materials[0].texture == "convert/textures/BG/TST.p02.png");
    SH_CHECK(c.materials[0].palette == 2);
    SH_CHECK(c.materials[0].semi_transparent);
    SH_CHECK(c.ranges.size() == 1 && c.ranges[0].count == 3);
    // An importer material: every corner carries its triangle's UV box.
    SH_CHECK(c.materials[0].texel_center_uv);
    for (const sc::CellVertex& v : c.vertices)
        SH_CHECK(near(v.box_u0, 0.f) && near(v.box_v0, 0.f) && near(v.box_u1, 1.f) && near(v.box_v1, 1.f));
}

// A cell with the triangle twice in place (cells repeat instances) and once 8 units in front of it (a poster on its
// wall): the triangle's outward normal is (0, 1, 1) / sqrt 2 in the file's axes.
constexpr const char* kStackNodes = R"("scenes":[{"nodes":[0]}],
"nodes":[{"name":"tst0102","children":[1,2,3],"extras":{"source":"BG/TST0102.IPD","cell_x":1,"cell_z":2}},
{"name":"WALL","mesh":0},{"name":"WALL","mesh":0},
{"name":"POSTER","mesh":0,"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0.0220971,0.0220971,1]}],)";

SH_TEST(scene_area_cells_overlay_layers) {
    auto cells = sc::read_area_cells(synthetic_area(kStackNodes), "TST.glb");
    SH_CHECK(cells);
    if (!cells || cells.value().size() != 1) return;
    const sc::CellMesh& c = cells.value()[0];
    // The repeated instance is the same surface, not a layer; the one in front lies on both.
    SH_CHECK(c.ranges.size() == 2);
    if (c.ranges.size() != 2) return;
    SH_CHECK(c.ranges[0].layer == 0 && c.ranges[0].count == 6);
    SH_CHECK(c.ranges[1].layer == 1 && c.ranges[1].count == 3);
    // PS1 y is the file's -y: the lifted triangle is the one moved up the file's +y.
    const float ground_y = c.vertices[c.indices[c.ranges[0].first]].y;
    const float poster_y = c.vertices[c.indices[c.ranges[1].first]].y;
    SH_CHECK(near(ground_y - poster_y, 0.0220971f * 256.f));
}

SH_TEST(scene_library_models) {
    const std::vector<std::uint8_t> glb = synthetic_area(kLibraryNodes);
    // The cell is only its instances: the "/models" group is drawn by name.
    auto cells = sc::read_area_cells(glb, "TST.glb");
    SH_CHECK(cells && cells.value().size() == 1 && cells.value()[0].vertices.size() == 3);
    auto models = sc::read_glb_models(glb, "TST.glb");
    SH_CHECK(models);
    if (!models) return;
    SH_CHECK(models.value().size() == 2);
    if (models.value().size() != 2) return;
    const sc::LibraryModel& door = models.value()[0];
    SH_CHECK(door.name == "TST0102/DOOR_HID");
    SH_CHECK(door.index_name == "TST0102/#3");
    SH_CHECK(door.ot_group == 2);
    // In its own space: no cell corner, no instance translation.
    SH_CHECK(door.mesh.vertices.size() == 3 && door.mesh.ranges.size() == 1);
    if (door.mesh.vertices.size() == 3) SH_CHECK(near(door.mesh.vertices[0].x, 41.f * 256.f));
    SH_CHECK(models.value()[1].name == "TST_GLB/ITEM" && models.value()[1].index_name == "TST_GLB/#0");
    SH_CHECK(!sc::read_glb_models(std::vector<std::uint8_t>(64, 0x5A), "junk.glb"));
}

SH_TEST(scene_object_c_face) {
    const std::vector<std::uint8_t> glb = synthetic_area(kLibraryNodes);
    sc::set_file_reader([glb](std::string_view path) -> std::optional<std::vector<std::uint8_t>> {
        if (path == "maps/areas/TSTLIB.glb") return glb;
        return std::nullopt;
    });
    ShScene_FrameEnd();
    const double rot[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    const double trans[3] = {100, -50, 2000};
    int group = -1;
    const unsigned code = ShScene_AddObject("maps/areas/TSTLIB.glb", "tst0102/door_hid", rot, trans, &group);
    SH_CHECK(code == static_cast<unsigned>(sc::kObjectItem) + 1u);
    SH_CHECK(group == 2);
    SH_CHECK(ShScene_AddObject("maps/areas/TSTLIB.glb", "TST_GLB/#0", rot, trans, nullptr) ==
             static_cast<unsigned>(sc::kObjectItem) + 2u);
    SH_CHECK(ShScene_AddObject("maps/areas/TSTLIB.glb", "TST0102/NONE", rot, trans, nullptr) == 0);
    SH_CHECK(ShScene_AddObject("maps/areas/NONE.glb", "TST0102/DOOR_HID", rot, trans, nullptr) == 0);
    const sc::Frame& f = sc::current_frame();
    SH_CHECK(f.objects.size() == 2);
    if (f.objects.size() == 2) {
        SH_CHECK(f.objects[0].mesh && f.objects[0].mesh->vertices.size() == 3);
        SH_CHECK(f.objects[0].rot[2] == 1.0 && f.objects[0].trans[2] == 2000.0);
    }
    ShScene_FrameEnd();
    SH_CHECK(sc::current_frame().objects.empty());
    sc::set_file_reader(nullptr);
}

// A mod's HD mesh keeps its own shape: cells and library models read from a mod's file are marked, so model
// upscaling leaves them plain; the game's own files are upscaled as before.
SH_TEST(scene_mod_meshes_are_not_upscaled) {
    const std::vector<std::uint8_t> glb = synthetic_area(kLibraryNodes);
    sc::forget_assets();
    sc::set_file_reader([glb](std::string_view path) -> std::optional<std::vector<std::uint8_t>> {
        if (path == "maps/areas/TST.glb" || path == "maps/areas/TSTLIB.glb") return glb;
        return std::nullopt;
    });
    sc::set_mod_file_query([](std::string_view path) { return path == "maps/areas/TST.glb"; });
    const auto modded = sc::find_model("maps/areas/TST.glb", "TST0102/DOOR_HID");
    const auto game = sc::find_model("maps/areas/TSTLIB.glb", "TST0102/DOOR_HID");
    SH_CHECK(modded && modded->mesh.from_mod);
    SH_CHECK(game && !game->mesh.from_mod);
    const auto cell = sc::find_cell("TST0102");
    SH_CHECK(cell && cell->from_mod);
    sc::set_mod_file_query(nullptr);
    sc::forget_assets();
    const auto again = sc::find_cell("TST0102");
    SH_CHECK(again && !again->from_mod);
    sc::set_file_reader(nullptr);
    sc::forget_assets();
}

SH_TEST(scene_area_cells_rejects_junk) {
    const std::vector<std::uint8_t> junk(64, 0x5A);
    SH_CHECK(!sc::read_area_cells(junk, "junk.glb"));
}

SH_TEST(scene_tim_json_path) {
    SH_CHECK(sc::tim_json_path("convert/textures/BG/ER_CHRC2.p09.png") == "convert/textures/BG/ER_CHRC2.tim.json");
    SH_CHECK(sc::tim_json_path("convert/textures/BG/X.png") == "convert/textures/BG/X.tim.json");
    SH_CHECK(sc::tim_json_path("convert/textures/BG/X.alt1.png") == "convert/textures/BG/X.tim.json");
    SH_CHECK(sc::tim_json_path("convert/textures/BG/X.p09.png") == "convert/textures/BG/X.tim.json");
}

SH_TEST(scene_texture_baked_rgba_alpha_classes) {
    // A baked true-colour picture: alpha 0 / 128 (STP) / 255 must survive, near values snap to the class.
    sh::png::Image im;
    im.width = 6;
    im.height = 1;
    im.color_type = sh::png::ColorType::Rgba;
    im.pixels = {10, 20, 30, 0,   40, 50, 60, 128, 70, 80, 90, 255,
                 1,  2,  3,  63,  4,  5,  6,  191, 7,  8,  9,  192};
    auto png = sh::png::encode(im);
    SH_CHECK(png);
    if (!png) return;
    auto tex = sc::decode_scene_texture(png.value(), {}, 0);
    SH_CHECK(tex);
    if (!tex) return;
    const auto& p = tex.value().rgba;
    SH_CHECK(p.size() == 24);
    if (p.size() != 24) return;
    SH_CHECK(p[3] == 0 && p[7] == 128 && p[11] == 255);
    SH_CHECK(p[15] == 0 && p[19] == 128 && p[23] == 255);
    SH_CHECK(p[4] == 40 && p[5] == 50 && p[6] == 60);
}

SH_TEST(scene_prefetch_textures_decodes_once_with_mip_chain) {
    sh::png::Image im;
    im.width = 4;
    im.height = 2;
    im.color_type = sh::png::ColorType::Rgba;
    for (int i = 0; i < 8; ++i) im.pixels.insert(im.pixels.end(), {static_cast<std::uint8_t>(i * 30), 20, 30, 255});
    const auto png = sh::png::encode(im);
    SH_CHECK(png);
    if (!png) return;
    int reads = 0;
    sc::set_file_reader([&](std::string_view path) -> std::optional<std::vector<std::uint8_t>> {
        if (path.substr(0, 4) != "tex/" || path.size() < 4 || path.substr(path.size() - 4) != ".png")
            return std::nullopt;
        ++reads;
        return png.value();
    });
    // "up" pictures stand for upscaled ones: they carry their mip chain from the prefetch.
    sc::set_mipmap_query([](std::string_view path) { return path.find("up") != std::string_view::npos; });
    const std::vector<std::pair<std::string, int>> wanted = {
        {"tex/a.png", 0}, {"tex/up.png", 0}, {"tex/a.png", 0}, {"none/b.png", 0}};
    sc::prefetch_textures(wanted);
    SH_CHECK(reads == 2);
    const auto a = sc::find_texture("tex/a.png", 0);
    const auto up = sc::find_texture("tex/up.png", 0);
    SH_CHECK(reads == 2);
    SH_CHECK(a && a->width == 4 && a->height == 2 && !a->mipmapped && a->levels.empty());
    SH_CHECK(up && up->mipmapped);
    if (up) {
        const std::vector<sc::SceneTexture> chain = sc::mip_levels(*up);
        SH_CHECK(up->levels.size() == chain.size() && chain.size() == 2);
        for (std::size_t l = 0; l < chain.size() && l < up->levels.size(); ++l)
            SH_CHECK(up->levels[l].width == chain[l].width && up->levels[l].rgba == chain[l].rgba);
    }
    sc::forget_assets();
    sc::set_mipmap_query(nullptr);
    sc::set_file_reader(nullptr);
}

SH_TEST(scene_texture_stp_from_palette) {
    // 2x2 indexed: 0 transparent (word 0000), 1 solid, 2 STP set, 3 solid.
    sh::png::Image im;
    im.width = 2;
    im.height = 2;
    im.color_type = sh::png::ColorType::Indexed;
    im.pixels = {0, 1, 2, 3};
    im.palette = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    im.palette_alpha = {0};
    auto png = sh::png::encode(im);
    SH_CHECK(png);
    if (!png) return;
    const std::string tim = R"({"palettes": [["0000", "001f", "83e0", "7c00"], ["0000", "0000", "0000", "0000"]]})";
    const std::vector<std::uint8_t> tim_bytes(tim.begin(), tim.end());
    auto tex = sc::decode_scene_texture(png.value(), tim_bytes, 0);
    SH_CHECK(tex);
    if (!tex) return;
    const auto& p = tex.value().rgba;
    SH_CHECK(tex.value().width == 2 && tex.value().height == 2 && p.size() == 16);
    if (p.size() != 16) return;
    SH_CHECK(p[3] == 0);
    SH_CHECK(p[7] == 255);
    SH_CHECK(p[11] == 128);
    SH_CHECK(p[15] == 255);
    // The transparent texel takes its solid neighbours' colour, so filtering does not pull black into the edge.
    SH_CHECK(p[0] == 85 && p[1] == 85 && p[2] == 85);
    // Without the TIM description no texel is STP.
    auto plain = sc::decode_scene_texture(png.value(), {}, 0);
    SH_CHECK(plain && plain.value().rgba[11] == 255);
}

SH_TEST(scene_frame_c_face) {
    ShScene_FrameEnd();
    const std::uint64_t serial = sc::current_frame().serial;
    const double rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const double trans[3] = {10, 20, 30};
    ShScene_SetCamera(rot, trans, 300.f, 0.f, 8.f);
    ShScene_AddCell("er0402", 4, 2);
    const ShScenePrelitVertex v[3] = {{0, 0, 100, 0, 0, 128, 128, 128, 255},
                                      {10, 0, 100, 1, 0, 128, 128, 128, 255},
                                      {0, 10, 100, 0, 1, 128, 128, 128, 255}};
    const unsigned idx[3] = {0, 1, 2};
    const unsigned char rgba[4] = {1, 2, 3, 255};
    ShScenePrelit m{};
    m.textureKey = 77;
    m.rgba = rgba;
    m.textureWidth = m.textureHeight = 1;
    m.vertices = v;
    m.vertexCount = 3;
    m.indices = idx;
    m.indexCount = 3;
    SH_CHECK(ShScene_AddPrelit(&m) == 0);
    SH_CHECK(ShScene_AddPrelit(&m) == 1);
    const sc::Frame& f = sc::current_frame();
    SH_CHECK(f.world);
    SH_CHECK(f.h == 300.f && f.geom_ofs_y == 8.f && f.camera.trans[2] == 30.0);
    SH_CHECK(f.cells.size() == 1 && f.cells[0].stem == "ER0402" && f.cells[0].cell_x == 4);
    SH_CHECK(f.prelit.size() == 2 && f.prelit[0].vertices.size() == 3);
    // Character meshes use the world camera and sample their UVs without a box.
    SH_CHECK(!f.prelit[0].isolated && f.prelit[0].h == 0.f);
    SH_CHECK(f.prelit[0].vertices[1].box_u1 < f.prelit[0].vertices[1].box_u0);
    auto t = sc::prelit_texture(77);
    SH_CHECK(t && t->rgba.size() == 4 && t->rgba[2] == 3);
    ShScene_FrameEnd();
    SH_CHECK(!sc::current_frame().world && sc::current_frame().cells.empty() && sc::current_frame().prelit.empty());
    SH_CHECK(sc::current_frame().serial == serial + 1);
}

// Every area file of a real import parses into cells that have geometry and textures that decode.
SH_TEST(scene_area_cells_real_import) {
    const char* root = std::getenv("SH_TEST_GAMEDATA");
    if (root == nullptr || *root == '\0') SH_SKIP("SH_TEST_GAMEDATA is not set");
    const fs::path areas = fs::path(root) / "maps" / "areas";
    std::error_code ec;
    if (!fs::is_directory(areas, ec)) SH_SKIP("no maps/areas in SH_TEST_GAMEDATA");
    std::size_t cells = 0, textures = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(areas)) {
        if (e.path().extension() != ".glb") continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto area = sc::read_area_cells(bytes, e.path().string());
        SH_CHECK(area);
        if (!area) continue;
        for (const sc::CellMesh& c : area.value()) {
            ++cells;
            SH_CHECK(c.stem.size() > 4);
            bool opaque_done = false;
            for (const sc::CellRange& r : c.ranges) {
                SH_CHECK(r.first + r.count <= c.indices.size());
                const bool semi = c.materials[static_cast<std::size_t>(r.material)].semi_transparent;
                SH_CHECK(!(opaque_done && !semi));
                opaque_done = opaque_done || semi;
            }
            for (std::uint32_t i : c.indices) SH_CHECK(i < c.vertices.size());
            // One texture per cell is enough to know the paths and the STP lookup hold.
            if (!c.materials.empty() && !c.materials[0].texture.empty()) {
                const fs::path png = fs::path(root) / c.materials[0].texture;
                std::ifstream pin(png, std::ios::binary);
                const std::vector<std::uint8_t> pbytes((std::istreambuf_iterator<char>(pin)), std::istreambuf_iterator<char>());
                std::ifstream tin(fs::path(root) / sc::tim_json_path(c.materials[0].texture), std::ios::binary);
                const std::vector<std::uint8_t> tbytes((std::istreambuf_iterator<char>(tin)), std::istreambuf_iterator<char>());
                auto tex = sc::decode_scene_texture(pbytes, tbytes, c.materials[0].palette);
                SH_CHECK(tex);
                if (tex) ++textures;
            }
        }
    }
    SH_CHECK(cells > 400);
    SH_CHECK(textures > 0);
}

}  // namespace
