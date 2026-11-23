#include "framework/sh_test.h"
#include "formats/archive/sh_archive.h"
#include "formats/common/byte_source.h"
#include "formats/convert/level/level_converter.h"
#include "formats/convert/level/level_flat.h"
#include "formats/convert/level/level_gltf.h"
#include "formats/convert/level/level_json.h"
#include "formats/convert/level/level_parse.h"
#include "formats/convert/level/map_areas.h"
#include "formats/convert/level/level_textures.h"
#include "formats/gltf/gltf_inspect.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;
namespace lvl = sh::convert::level;
namespace arc = sh::archive;

// ---- synthetic LM -------------------------------------------------------------------------------------------------

void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    put16(b, at, v & 0xFFFF);
    put16(b, at + 2, v >> 16);
}

// One material, one model "BOX" (vertex window starts at 2), one mesh with one quad, 4 vertices, 2 normals.
// Layout: header 0, material 0x14, model 0x2C, order 0x3C, mesh 0x40, prim 0x58, xy 0x6C, z 0x7C, normals 0x84, shade 0x8C.
std::vector<std::uint8_t> make_plm() {
    std::vector<std::uint8_t> b(0x90, 0);
    b[0] = 0x30;
    b[1] = 6;
    b[3] = 1;
    put32(b, 4, 0x14);
    b[8] = 1;
    put32(b, 12, 0x2C);
    put32(b, 16, 0x3C);
    std::memcpy(&b[0x14], "SHEET", 5);
    put16(b, 0x14 + 0x10, 0x1234);
    std::memcpy(&b[0x2C], "BOX", 3);
    b[0x2C + 8] = 1;   // meshes
    b[0x2C + 9] = 2;   // vertex window start
    b[0x2C + 10] = 0;  // normal window start
    put32(b, 0x2C + 12, 0x40);
    b[0x3C] = 0;
    b[0x40] = 1;  // primitives
    b[0x41] = 4;  // vertices
    b[0x42] = 2;  // normals
    b[0x43] = 4;  // shade entries
    put32(b, 0x40 + 4, 0x58);
    put32(b, 0x40 + 8, 0x6C);
    put32(b, 0x40 + 12, 0x7C);
    put32(b, 0x40 + 16, 0x84);
    put32(b, 0x40 + 20, 0x8C);
    // quad: uv corners, clut 0x0140, flags: material 0, transparent
    const std::uint8_t uv[4][2] = {{0, 0}, {255, 0}, {0, 255}, {255, 255}};
    const std::size_t uv_at[4] = {0, 4, 8, 10};
    for (int c = 0; c < 4; ++c) {
        b[0x58 + uv_at[c]] = uv[c][0];
        b[0x58 + uv_at[c] + 1] = uv[c][1];
    }
    put16(b, 0x58 + 2, 0x0140);
    put16(b, 0x58 + 6, 0x8000);
    const std::uint8_t verts[4] = {2, 3, 4, 5};
    const std::uint8_t slots[4] = {0, 1, 2, 3};
    for (int c = 0; c < 4; ++c) {
        b[0x58 + 12 + c] = verts[c];
        b[0x58 + 16 + c] = slots[c];
    }
    // vertices (x, y) then z; a unit square in the XZ plane at y = 0, Q7.8
    const std::int16_t xy[4][2] = {{0, 0}, {256, 0}, {0, 0}, {256, 0}};
    const std::int16_t z[4] = {0, 0, 256, 256};
    for (int v = 0; v < 4; ++v) {
        put16(b, 0x6C + 4 * v, static_cast<std::uint16_t>(xy[v][0]));
        put16(b, 0x6C + 4 * v + 2, static_cast<std::uint16_t>(xy[v][1]));
        put16(b, 0x7C + 2 * v, static_cast<std::uint16_t>(z[v]));
    }
    // normal 0 covers shade slots 0..1 (vertices 2, 3), normal 1 covers 2..3 (vertices 4, 5)
    const std::int8_t normals[2][4] = {{0, 127, 0, 2}, {0, 127, 0, 2}};
    for (int n = 0; n < 2; ++n) {
        for (int k = 0; k < 4; ++k) {
            b[0x84 + 4 * n + k] = static_cast<std::uint8_t>(normals[n][k]);
        }
    }
    for (int s = 0; s < 4; ++s) {
        b[0x8C + s] = verts[s];
    }
    return b;
}

// ---- image access -------------------------------------------------------------------------------------------------

struct Disc {
    std::vector<arc::Entry> table;
    std::optional<arc::Archive> silent;
};

std::shared_ptr<Disc> open_disc() {
    const fs::path path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(path)) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    const std::shared_ptr<const sh::disc::Image> image(sh::disc::Image::open(path));
    if (!image) {
        return nullptr;
    }
    const auto volume = sh::iso9660::Volume::open(image);
    if (!volume) {
        return nullptr;
    }
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    if (!slus || !silent || !hill) {
        return nullptr;
    }
    auto sectors_for = [](std::uint32_t size) { return (size + 2047) / 2048; };
    const arc::Layout silent_layout{silent->lba, sectors_for(silent->size)};
    const arc::Layout hill_layout{hill->lba, sectors_for(hill->size)};
    const std::shared_ptr<const sh::ByteSource> exe(volume->open_file(*slus));
    const std::shared_ptr<const sh::ByteSource> silent_source(volume->open_file(*silent));
    if (!exe || !silent_source) {
        return nullptr;
    }
    auto table = arc::read_file_table(*exe, silent_layout, hill_layout);
    auto archive = arc::Archive::open(silent_source, arc::Region::Silent);
    if (!table || !archive) {
        return nullptr;
    }
    auto disc = std::make_shared<Disc>();
    disc->table = std::move(*table);
    disc->silent = std::move(archive);
    return disc;
}

// Cached for the whole run: reading the image takes a moment.
std::shared_ptr<Disc> shared_disc() {
    static std::shared_ptr<Disc> disc;
    static bool tried = false;
    if (!tried) {
        tried = true;
        disc = open_disc();
    }
    if (!disc) {
        SH_SKIP("image could not be opened");
    }
    return disc;
}

struct MapFile {
    std::string path;
    std::vector<std::uint8_t> bytes;
};

std::vector<MapFile> read_files(const Disc& disc, const char* extension) {
    std::vector<MapFile> files;
    for (const arc::Entry& e : disc.table) {
        if (e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, extension) == 0) {
            auto bytes = disc.silent->read(e);
            if (bytes) {
                files.push_back({e.name, std::move(*bytes)});
            } else {
                files.push_back({e.name, {}});
            }
        }
    }
    return files;
}

bool starts_with(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

// ---- synthetic tests ----------------------------------------------------------------------------------------------

SH_TEST(LevelSyntheticPlmParsesEveryField) {
    const auto bytes = make_plm();
    auto plm = lvl::parse_plm(bytes);
    SH_CHECK(plm);
    if (!plm) {
        return;
    }
    const lvl::LmFile& lm = plm.value();
    SH_CHECK(lm.materials.size() == 1);
    SH_CHECK(lm.materials[0].name == "SHEET");
    SH_CHECK(lm.materials[0].clut == 0x1234);
    SH_CHECK(lm.models.size() == 1);
    SH_CHECK(lm.models[0].name == "BOX");
    SH_CHECK(lm.models[0].vertex_offset == 2);
    SH_CHECK(lm.models[0].meshes.size() == 1);
    const lvl::LmMesh& mesh = lm.models[0].meshes[0];
    SH_CHECK(mesh.primitives.size() == 1);
    SH_CHECK(mesh.vertices.size() == 4);
    SH_CHECK(mesh.normals.size() == 2);
    SH_CHECK(mesh.shade.size() == 4);
    const lvl::LmPrimitive& p = mesh.primitives[0];
    SH_CHECK(!p.is_triangle());
    SH_CHECK(p.transparent());
    SH_CHECK(p.material_index() == 0);
    SH_CHECK(p.clut == 0x0140);
    SH_CHECK(p.uv[3][0] == 255 && p.uv[3][1] == 255);
    SH_CHECK(p.vertex[2] == 4);
    SH_CHECK(mesh.vertices[1].x == 256 && mesh.vertices[3].z == 256);
    // the whole file is accounted for
    SH_CHECK(lm.layout.overlap_bytes == 0);
    SH_CHECK(lm.layout.gaps.size() == 1);  // the three padding bytes 0x3D..0x40 after the model order
}

SH_TEST(LevelSyntheticPlmFlattensToIndexedTriangles) {
    const auto bytes = make_plm();
    auto plm = lvl::parse_plm(bytes);
    SH_CHECK(plm);
    if (!plm) {
        return;
    }
    const auto models = lvl::flatten_plm(plm.value());
    SH_CHECK(models.size() == 1);
    SH_CHECK(models[0].skipped_primitives == 0);
    SH_CHECK(models[0].meshes.size() == 1);
    const lvl::FlatMesh& m = models[0].meshes[0];
    SH_CHECK(m.material_name == "SHEET");
    // The model is on the flat chain: its bit-15 primitive keeps solid STP texels.
    SH_CHECK(!m.transparent);
    SH_CHECK(m.clut == 0x0140);
    SH_CHECK(m.positions.size() == 12);
    SH_CHECK(m.normals.size() == 12);
    SH_CHECK(m.uvs.size() == 8);
    SH_CHECK(m.indices.size() == 6);
    SH_CHECK(m.clut_base == 0);  // the material's clut (0x1234) is the loader's, not the base
    // GPU strip (0,1,2) + (1,3,2), each reversed: (0,2,1) + (1,2,3)
    SH_CHECK(m.indices[0] == 0 && m.indices[1] == 2 && m.indices[2] == 1);
    SH_CHECK(m.indices[3] == 1 && m.indices[4] == 2 && m.indices[5] == 3);
    // vertex 1 is (x = 256, y = 0, z = 0) -> (-1, -0, 0); stored normal (0, 127, 0) -> outward (0, 1, -0)
    SH_CHECK(m.positions[3] == -1.0f && m.positions[5] == 0.0f);
    SH_CHECK(m.normals[4] > 0.999f && m.normals[4] <= 1.0f);
    SH_CHECK(m.uvs[2] == 255.5f && m.uvs[3] == 0.5f);  // texel centres
}

SH_TEST(LevelSyntheticPlmLitModelKeepsSemiTransparency) {
    auto bytes = make_plm();
    bytes[0x2C + 11] = 1;  // the lit chain turns bit 15 into the GPU's semi-transparency bit
    auto plm = lvl::parse_plm(bytes);
    SH_CHECK(plm);
    if (!plm) {
        return;
    }
    const auto models = lvl::flatten_plm(plm.value());
    SH_CHECK(models.size() == 1 && models[0].meshes.size() == 1);
    if (models.size() == 1 && models[0].meshes.size() == 1) {
        SH_CHECK(models[0].meshes[0].transparent);
    }
}

SH_TEST(LevelRejectsTruncatedAndGarbageInput) {
    auto bytes = make_plm();
    for (std::size_t cut : {std::size_t{0}, std::size_t{10}, std::size_t{0x30}, std::size_t{0x60}, std::size_t{0x8F}}) {
        const std::vector<std::uint8_t> shorter(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
        auto plm = lvl::parse_plm(shorter);
        SH_CHECK(!plm);
        if (!plm) {
            SH_CHECK(!plm.error().empty());
        }
    }
    bytes[1] = 7;  // version 7 is a mod format of another port
    SH_CHECK(!lvl::parse_plm(bytes));
    bytes[1] = 6;
    bytes[0] = 0x31;
    SH_CHECK(!lvl::parse_plm(bytes));

    std::vector<std::uint8_t> ipd(0x188, 0);
    SH_CHECK(!lvl::parse_ipd(ipd));  // magic
    ipd[0] = 0x14;
    put32(ipd, 4, 0x4000);  // LM far outside the file
    SH_CHECK(!lvl::parse_ipd(ipd));
    SH_CHECK(!lvl::parse_ipd(std::vector<std::uint8_t>(10, 0x14)));
}

SH_TEST(LevelConverterContract) {
    const auto converter = lvl::make_converter();
    SH_CHECK(converter != nullptr);
    if (!converter) {
        return;
    }
    SH_CHECK(converter->name() == "level");
    SH_CHECK(!converter->lossless());

    const auto plm = make_plm();
    SH_CHECK(converter->accepts(plm, "BG/TEST_GLB.PLM"));
    SH_CHECK(converter->accepts(plm, "bg/test_glb.plm"));
    SH_CHECK(!converter->accepts(plm, "ITEM/KNIFE.PLM"));  // held items belong to the model converter
    SH_CHECK(!converter->accepts(plm, "BG/TEST_GLB.TIM"));
    SH_CHECK(!converter->accepts(std::vector<std::uint8_t>{0x10, 0, 0, 0}, "BG/THR0000.IPD"));
    SH_CHECK(!converter->accepts({}, "BG/THR0000.IPD"));

    auto out = converter->decode(plm, "BG/TEST_GLB.PLM");
    SH_CHECK(out);
    if (out) {
        SH_CHECK(out.value().size() == 2);
        SH_CHECK(out.value()[0].relative_path == "levels/BG/TEST_GLB.level.json");
        SH_CHECK(out.value()[1].relative_path == "levels/BG/TEST_GLB.glb");
        SH_CHECK(sh::gltf::inspect_glb(out.value()[1].bytes));
        const std::string json(out.value()[0].bytes.begin(), out.value()[0].bytes.end());
        SH_CHECK(json.find("\"kind\": \"plm\"") != std::string::npos);
        SH_CHECK(json.find("\"name\": \"BOX\"") != std::string::npos);
        SH_CHECK(json.find("\"name\": \"SHEET\"") != std::string::npos);
        SH_CHECK(json.back() == '\n');
        auto again = converter->encode(out.value());
        SH_CHECK(!again);
    }
    SH_CHECK(!converter->decode(std::vector<std::uint8_t>{0x30, 6, 0}, "BG/TEST_GLB.PLM"));
}

// 4 bpp TIM, 256 x 128 pixels, a CLUT block of 16 words by `rows` palettes.
std::vector<std::uint8_t> make_tim(int rows) {
    const std::size_t clut = 12 + 2 * 16 * static_cast<std::size_t>(rows);
    const std::size_t image = 12 + 2 * 64 * 128;
    std::vector<std::uint8_t> t(8 + clut + image, 0);
    t[0] = 0x10;
    t[4] = 8;
    put32(t, 8, static_cast<std::uint32_t>(clut));
    put16(t, 8 + 8, 16);
    put16(t, 8 + 10, static_cast<std::uint32_t>(rows));
    put32(t, 8 + clut, static_cast<std::uint32_t>(image));
    put16(t, 8 + clut + 8, 64);
    put16(t, 8 + clut + 10, 128);
    return t;
}

SH_TEST(LevelTexturePalettesAndNames) {
    const auto tim = lvl::read_tim_info(make_tim(8), "BG/SHEET.TIM");
    SH_CHECK(tim);
    if (!tim) {
        return;
    }
    const lvl::TimInfo& t = tim.value();
    SH_CHECK(t.bpp == 4 && t.width == 256 && t.height == 128 && t.palette_size == 16 && t.palette_count == 8);
    SH_CHECK(lvl::palette_index(t, 0x0000, 0) == 0);
    SH_CHECK(lvl::palette_index(t, 0x0140, 0) == 5);       // row 5
    SH_CHECK(lvl::palette_index(t, 0x0140 + 0x40 * 30, 0x40 * 30) == 5);  // relative to the material's base
    SH_CHECK(lvl::palette_index(t, 0x0200, 0) == -1);      // row 8: outside the block
    SH_CHECK(lvl::palette_index(t, 0x0001, 0) == -1);      // 16 words to the right: outside a 16-word block
    SH_CHECK(lvl::palette_png(t, 0) == "textures/BG/SHEET.png");
    SH_CHECK(lvl::palette_png(t, 5) == "textures/BG/SHEET.p05.png");
    lvl::TimInfo many = t;
    many.palette_count = 150;
    SH_CHECK(lvl::palette_png(many, 7) == "textures/BG/SHEET.p007.png");
    SH_CHECK(lvl::relative_uri("levels/BG/X.glb", "textures/BG/SHEET.png") == "../../textures/BG/SHEET.png");
    SH_CHECK(lvl::relative_uri("levels/areas/THR.glb", "textures/A.png") == "../../textures/A.png");
    SH_CHECK(!lvl::read_tim_info(std::vector<std::uint8_t>(8, 0), "X.TIM"));

    // 8 bpp: one 256-word palette per row; a 4 bpp block 64 words wide holds four palettes per row.
    lvl::TimInfo wide = t;
    wide.clut_words = 64;
    wide.clut_rows = 2;
    wide.palette_count = 8;
    SH_CHECK(lvl::palette_index(wide, 0x0042, 0) == 6);  // row 1, word 32
}

SH_TEST(LevelSyntheticPlmGlbUsesItsPalette) {
    const auto tim = make_tim(8);
    lvl::Options options;
    std::vector<std::string> asked;
    options.sibling = [&](std::string_view p) -> std::optional<std::vector<std::uint8_t>> {
        asked.emplace_back(p);
        return p == "BG/SHEET.TIM" ? std::optional<std::vector<std::uint8_t>>(tim) : std::nullopt;
    };
    const auto converter = lvl::make_converter(options);
    auto out = converter->decode(make_plm(), "BG/TEST_GLB.PLM");
    SH_CHECK(out && out.value().size() == 2);
    if (!out || out.value().size() != 2) {
        return;
    }
    const auto info = sh::gltf::inspect_glb(out.value()[1].bytes);
    SH_CHECK(info);
    if (!info) {
        return;
    }
    SH_CHECK(info.value().triangles == 2);
    SH_CHECK(info.value().image_uris.size() == 1 && info.value().image_uris[0] == "../../textures/BG/SHEET.p05.png");
    // File-table order: 1ST and ANIM come before BG.
    SH_CHECK(asked.size() >= 3 && asked[0] == "1ST/SHEET.TIM" && asked[2] == "BG/SHEET.TIM");
}

SH_TEST(LevelMapAreasCover43Maps) {
    const auto maps = lvl::map_areas();
    SH_CHECK(maps.size() == 43);
    std::set<std::string_view> names;
    std::set<std::string_view> areas;
    for (const auto& m : maps) {
        names.insert(m.map);
        areas.insert(m.area);
    }
    SH_CHECK(names.size() == 43);
    SH_CHECK(areas.size() == 13);
    SH_CHECK(lvl::area_of_ipd("BG/THR0000.IPD") == "THR");
    SH_CHECK(lvl::area_of_ipd("DRU0200") == "DRU");
    SH_CHECK(lvl::area_of_ipd("BG/DRFFFF.IPD") == "DR");
    SH_CHECK(lvl::area_of_ipd("TEST/BTT_00FF.IPD") == "BTT_");
    SH_CHECK(lvl::global_plm_of_area("THR") == "BG/THR_GLB.PLM");
    SH_CHECK(lvl::global_plm_of_area("ER").empty());
}

// ---- tests on the image -------------------------------------------------------------------------------------------

namespace {

struct Totals {
    std::uint64_t files = 0, models = 0, meshes = 0, primitives = 0, triangles = 0, vertices = 0, normals = 0,
                  materials = 0, model_infos = 0, model_buffers = 0, instances = 0, billboards = 0, split_vertices = 0,
                  surfaces = 0, subcells = 0, elements18 = 0;
};

void add(Totals& t, const lvl::LmFile& lm) {
    t.models += lm.models.size();
    t.materials += lm.materials.size();
    for (const auto& model : lm.models) {
        t.meshes += model.meshes.size();
        for (const auto& mesh : model.meshes) {
            t.primitives += mesh.primitives.size();
            t.vertices += mesh.vertices.size();
            t.normals += mesh.normals.size();
            for (const auto& p : mesh.primitives) {
                t.triangles += p.is_triangle() ? 1 : 0;
            }
        }
    }
}

void add(Totals& t, const lvl::IpdFile& ipd) {
    ++t.files;
    add(t, ipd.lm);
    t.model_infos += ipd.model_infos.size();
    t.model_buffers += ipd.model_buffers.size();
    for (const auto& b : ipd.model_buffers) {
        t.instances += b.instances.size();
        t.billboards += b.billboards.size();
    }
    t.split_vertices += ipd.collision.split_vertices.size();
    t.surfaces += ipd.collision.surfaces.size();
    t.subcells += ipd.collision.subcells.size();
    t.elements18 += ipd.collision.elements18.size();
}

}  // namespace

SH_TEST(LevelParsesAllIpdOfTheImage) {
    const auto disc = shared_disc();
    const auto files = read_files(*disc, ".IPD");
    SH_CHECK(files.size() == 498);

    Totals totals;
    std::map<std::string, Totals> per_area;
    std::uint32_t failures = 0;
    std::uint32_t gaps_nonzero = 0, overlaps = 0, big_tails = 0;
    std::uint32_t cell_mismatch = 0, group_mismatch = 0, order_bad = 0, shade_bad = 0, bounds_bad = 0;

    for (const MapFile& f : files) {
        auto ipd = lvl::parse_ipd(f.bytes);
        if (!ipd) {
            ++failures;
            std::fprintf(stderr, "parse failed: %s: %s\n", f.path.c_str(), ipd.error().c_str());
            continue;
        }
        const lvl::IpdFile& v = ipd.value();
        add(totals, v);
        add(per_area[std::string(lvl::area_of_ipd(f.path))], v);

        for (const lvl::Gap& g : v.layout.gaps) {
            gaps_nonzero += (!g.all_zero && g.offset + g.size != v.layout.file_size) ? 1 : 0;
        }
        overlaps += v.layout.overlap_bytes != 0;
        big_tails += v.layout.trailing_bytes > 255;

        // the cell in the header is the one in the name: four hex digits, x then z
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "%02X%02X", static_cast<std::uint8_t>(v.cell_x), static_cast<std::uint8_t>(v.cell_z));
        cell_mismatch += f.path.find(suffix) == std::string::npos;

        int sum = 0;
        for (const lvl::IpdOrderGroup& g : v.order_groups) {
            group_mismatch += g.start != sum;
            sum += g.count;
        }
        group_mismatch += sum != v.model_order_count;
        for (const std::uint8_t o : v.model_order) {
            order_bad += o >= v.model_buffers.size();
        }

        for (const auto& model : v.lm.models) {
            for (const auto& mesh : model.meshes) {
                // shade stream: normal counts sum to its length, and each corner's slot holds its own vertex
                int count_sum = 0;
                for (const auto& n : mesh.normals) {
                    count_sum += n.count;
                }
                shade_bad += count_sum != static_cast<int>(mesh.shade.size());
                for (const auto& p : mesh.primitives) {
                    for (int c = 0; c < 4; ++c) {
                        const int slot = p.shade_slot[c] - model.normal_offset;
                        const int local = p.vertex[c] - model.vertex_offset;
                        bounds_bad += slot < 0 || slot >= static_cast<int>(mesh.shade.size()) || mesh.shade[slot] != p.vertex[c];
                        bounds_bad += local < 0 || local >= static_cast<int>(mesh.vertices.size());
                    }
                }
            }
        }
    }

    SH_CHECK(failures == 0);
    SH_CHECK(gaps_nonzero == 0);  // every internal gap is zero padding
    SH_CHECK(overlaps == 0);
    SH_CHECK(big_tails == 0);
    SH_CHECK(cell_mismatch == 0);
    SH_CHECK(group_mismatch == 0);
    SH_CHECK(order_bad == 0);
    SH_CHECK(shade_bad == 0);
    SH_CHECK(bounds_bad == 0);
    SH_CHECK(per_area.size() == 15);  // 13 game areas + BTT_ and RMT of TEST/

    // Totals agree with an independent parser of the format.
    SH_CHECK(totals.files == 498);
    SH_CHECK(totals.materials == 1696);
    SH_CHECK(totals.models == 6436);
    SH_CHECK(totals.meshes == 10427);
    SH_CHECK(totals.primitives == 431185);
    SH_CHECK(totals.triangles == 0);
    SH_CHECK(totals.vertices == 595374);
    SH_CHECK(totals.normals == 165047);
    SH_CHECK(totals.model_infos == 8520);
    SH_CHECK(totals.model_buffers == 4062);
    SH_CHECK(totals.instances == 20332);

    if (const char* dir = std::getenv("SH_LEVEL_REPORT_DIR")) {
        std::ofstream tsv(fs::path(dir) / "cpp_ipd_counts.tsv");
        tsv << "path\tsize\tmeshes\tprims\tverts\tmodels\tmats\tinstances\tmodel_infos\tmodel_buffers\tsplit\tsurf\tsubcells\tp18\n";
        for (const MapFile& f : files) {
            auto ipd = lvl::parse_ipd(f.bytes);
            if (!ipd) {
                continue;
            }
            Totals t;
            add(t, ipd.value());
            tsv << f.path << '\t' << f.bytes.size() << '\t' << t.meshes << '\t' << t.primitives << '\t' << t.vertices << '\t'
                << t.models << '\t' << t.materials << '\t' << t.instances << '\t' << t.model_infos << '\t' << t.model_buffers
                << '\t' << t.split_vertices << '\t' << t.surfaces << '\t' << t.subcells << '\t' << t.elements18 << '\n';
        }
    }
}

SH_TEST(LevelParsesAllPlmOfTheImage) {
    const auto disc = shared_disc();
    const auto files = read_files(*disc, ".PLM");
    SH_CHECK(files.size() == 28);
    Totals totals;
    std::uint32_t failures = 0;
    for (const MapFile& f : files) {
        auto plm = lvl::parse_plm(f.bytes);
        if (!plm) {
            ++failures;
            std::fprintf(stderr, "parse failed: %s: %s\n", f.path.c_str(), plm.error().c_str());
            continue;
        }
        add(totals, plm.value());
        ++totals.files;
        SH_CHECK(plm.value().layout.overlap_bytes == 0);
        for (const lvl::Gap& g : plm.value().layout.gaps) {
            SH_CHECK(g.all_zero || g.offset + g.size == plm.value().layout.file_size);
        }
    }
    SH_CHECK(failures == 0);
    SH_CHECK(totals.files == 28);
    SH_CHECK(totals.materials == 55);
    SH_CHECK(totals.models == 350);
    SH_CHECK(totals.meshes == 365);
    SH_CHECK(totals.primitives == 10735);
    SH_CHECK(totals.vertices == 14530);
    SH_CHECK(totals.normals == 4948);
}

SH_TEST(LevelConverterDecodesEveryMapFileToJson) {
    const auto disc = shared_disc();
    const auto converter = lvl::make_converter();
    auto files = read_files(*disc, ".IPD");
    for (auto& f : read_files(*disc, ".PLM")) {
        files.push_back(std::move(f));
    }
    std::uint32_t accepted = 0, decoded = 0;
    std::uint64_t json_bytes = 0;
    const char* dir = std::getenv("SH_LEVEL_REPORT_DIR");
    for (const MapFile& f : files) {
        if (!converter->accepts(f.bytes, f.path)) {
            SH_CHECK(starts_with(f.path, "ITEM/"));  // held items are left to the model converter
            continue;
        }
        ++accepted;
        auto out = converter->decode(f.bytes, f.path);
        SH_CHECK(out);
        if (!out || out.value().size() != 2) {
            continue;
        }
        ++decoded;
        json_bytes += out.value()[0].bytes.size();
        SH_CHECK(!out.value()[0].bytes.empty() && out.value()[0].bytes[0] == '{');
        if (dir != nullptr) {
            const fs::path target = fs::path(dir) / "json" / out.value()[0].relative_path;
            fs::create_directories(target.parent_path());
            std::ofstream(target, std::ios::binary).write(reinterpret_cast<const char*>(out.value()[0].bytes.data()),
                                                          static_cast<std::streamsize>(out.value()[0].bytes.size()));
        }
    }
    SH_CHECK(accepted == 498 + 11);  // 498 IPD, 11 PLM under BG/
    SH_CHECK(decoded == accepted);
    std::fprintf(stderr, "level json: %u files, %llu bytes\n", decoded, static_cast<unsigned long long>(json_bytes));
}

SH_TEST(LevelFlattensEveryCell) {
    const auto disc = shared_disc();
    std::map<std::string, lvl::LmFile> globals;  // area -> GLB PLM
    for (const MapFile& f : read_files(*disc, ".PLM")) {
        if (f.path.size() > 8 && f.path.compare(f.path.size() - 8, 8, "_GLB.PLM") == 0 && starts_with(f.path, "BG/")) {
            auto plm = lvl::parse_plm(f.bytes);
            SH_CHECK(plm);
            if (plm) {
                globals.emplace(f.path, std::move(plm.value()));
            }
        }
    }
    SH_CHECK(globals.size() == 9);

    std::uint64_t instances = 0, unresolved = 0, skipped = 0, triangles = 0, billboards = 0, agree = 0, disagree = 0;
    std::uint32_t bad_indices = 0;
    for (const MapFile& f : read_files(*disc, ".IPD")) {
        auto ipd = lvl::parse_ipd(f.bytes);
        SH_CHECK(ipd);
        if (!ipd) {
            continue;
        }
        const std::string glb(lvl::global_plm_of_area(lvl::area_of_ipd(f.path)));
        const auto found = globals.find(glb);
        const lvl::FlatLevel level = lvl::flatten_ipd(ipd.value(), found != globals.end() ? &found->second : nullptr);
        instances += level.instances.size();
        unresolved += level.unresolved_instances;
        billboards += level.billboards.size();
        for (const lvl::FlatModel& m : level.models) {
            skipped += m.skipped_primitives;
            for (const lvl::FlatMesh& mesh : m.meshes) {
                const std::size_t verts = mesh.positions.size() / 3;
                bad_indices += mesh.normals.size() != mesh.positions.size() || mesh.uvs.size() != verts * 2;
                for (const std::uint32_t i : mesh.indices) {
                    bad_indices += i >= verts;
                }
                // Counter-clockwise front faces: the triangle's geometric normal against its vertex normals.
                for (std::size_t t = 0; t + 2 < mesh.indices.size() && bad_indices == 0; t += 3) {
                    const float* a = &mesh.positions[mesh.indices[t] * 3];
                    const float* b = &mesh.positions[mesh.indices[t + 1] * 3];
                    const float* c = &mesh.positions[mesh.indices[t + 2] * 3];
                    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                    const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
                    float d = 0.0f;
                    for (int k = 0; k < 3; ++k) {
                        const float* vn = &mesh.normals[mesh.indices[t + k] * 3];
                        d += n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2];
                    }
                    (d > 0.0f ? agree : disagree) += 1;
                }
                triangles += mesh.indices.size() / 3;
            }
        }
        for (const lvl::FlatInstance& inst : level.instances) {
            SH_CHECK(inst.flat_model >= -1 && inst.flat_model < static_cast<int>(level.models.size()));
        }
    }
    SH_CHECK(instances == 20332);
    SH_CHECK(bad_indices == 0);
    SH_CHECK(skipped == 0);
    SH_CHECK(disagree * 500 < agree);  // 99.87 %: the faces wind counter-clockwise around their normal
    std::fprintf(stderr, "flatten: %llu instances (%llu unresolved), %llu triangles in distinct models, %llu billboards, "
                 "winding agrees %llu, disagrees %llu\n",
                 static_cast<unsigned long long>(instances), static_cast<unsigned long long>(unresolved),
                 static_cast<unsigned long long>(triangles), static_cast<unsigned long long>(billboards),
                 static_cast<unsigned long long>(agree), static_cast<unsigned long long>(disagree));
}

SH_TEST(LevelEveryMapHasItsCells) {
    const auto disc = shared_disc();
    std::map<std::string, std::uint32_t> cells_of_area;
    for (const MapFile& f : read_files(*disc, ".IPD")) {
        ++cells_of_area[std::string(lvl::area_of_ipd(f.path))];
    }
    std::uint32_t maps_with_cells = 0;
    for (const auto& m : lvl::map_areas()) {
        maps_with_cells += cells_of_area[std::string(m.area)] > 0;
    }
    SH_CHECK(maps_with_cells == 43);
    SH_CHECK(cells_of_area["THR"] == 128);
    SH_CHECK(cells_of_area["ER"] == 58);
    SH_CHECK(cells_of_area["HU"] == 58);
}

SH_TEST(LevelSurvivesCorruptedRealFiles) {
    const auto disc = shared_disc();
    const auto files = read_files(*disc, ".IPD");
    SH_CHECK(!files.empty());
    if (files.empty()) {
        return;
    }
    // Deterministic xorshift. No parse of damaged data may read outside the buffer; the sanitizer builds check that.
    std::uint32_t state = 0x1234567u;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    std::uint32_t rejected = 0, accepted = 0;
    for (int round = 0; round < 300; ++round) {
        std::vector<std::uint8_t> bytes = files[next() % files.size()].bytes;
        if (round % 3 == 0) {
            bytes.resize(next() % bytes.size());
        } else {
            for (int k = 0; k < 8; ++k) {
                bytes[next() % bytes.size()] = static_cast<std::uint8_t>(next());
            }
        }
        auto ipd = lvl::parse_ipd(bytes);
        if (ipd) {
            ++accepted;
            (void)lvl::flatten_ipd(ipd.value(), nullptr);
            (void)lvl::ipd_to_json(ipd.value(), "BG/X.IPD");
        } else {
            ++rejected;
        }
    }
    SH_CHECK(rejected > 0);
    std::fprintf(stderr, "corruption: %u rejected, %u still parsed\n", rejected, accepted);
}

// Every map file through the converter with a reader of the image: one .glb per cell and per BG/ PLM, one per map area,
// all read back by cgltf; the counts of the instances, the global references and the textures.
SH_TEST(LevelConverterWritesGlbForEveryMapFile) {
    const auto disc = shared_disc();
    std::unordered_map<std::string, const arc::Entry*> by_name;
    for (const arc::Entry& e : disc->table) {
        by_name.emplace(e.name, &e);
    }
    lvl::Options options;
    options.sibling = [&](std::string_view p) -> std::optional<std::vector<std::uint8_t>> {
        const auto it = by_name.find(std::string(p));
        if (it == by_name.end()) {
            return std::nullopt;
        }
        return disc->silent->read(*it->second);
    };
    const auto converter = lvl::make_converter(options);
    auto files = read_files(*disc, ".IPD");
    for (auto& f : read_files(*disc, ".PLM")) {
        files.push_back(std::move(f));
    }
    std::uint32_t cell_glbs = 0, plm_glbs = 0, bad = 0;
    std::uint64_t glb_bytes = 0, area_bytes = 0, triangles = 0;
    std::set<std::string> areas;
    for (const MapFile& f : files) {
        if (!converter->accepts(f.bytes, f.path)) {
            continue;
        }
        auto out = converter->decode(f.bytes, f.path);
        SH_CHECK(out);
        if (!out) {
            continue;
        }
        for (const auto& o : out.value()) {
            if (!o.relative_path.ends_with(".glb")) {
                continue;
            }
            const auto info = sh::gltf::inspect_glb(o.bytes);
            bad += !info;
            for (const std::string& uri : info ? info.value().image_uris : std::vector<std::string>{}) {
                bad += uri.rfind("../../textures/", 0) != 0;
            }
            if (o.relative_path.rfind("levels/areas/", 0) == 0) {
                SH_CHECK(areas.insert(o.relative_path).second);
                area_bytes += o.bytes.size();
            } else {
                (f.path.ends_with(".IPD") ? cell_glbs : plm_glbs) += 1;
                glb_bytes += o.bytes.size();
                triangles += info ? info.value().triangles : 0;
            }
        }
    }
    SH_CHECK(bad == 0);
    SH_CHECK(cell_glbs == 498);
    SH_CHECK(plm_glbs == 11);
    SH_CHECK(areas.size() == 13);
    for (const auto& m : lvl::map_areas()) {
        SH_CHECK(areas.count("levels/areas/" + std::string(m.area) + ".glb") == 1);
    }

    // The same cells straight through cell_to_glb, for the counts.
    lvl::TextureCatalog catalog(options.sibling);
    std::map<std::string, lvl::LmFile> globals;
    std::uint64_t instances = 0, global_refs = 0, unresolved = 0, outside = 0;
    std::set<std::string> missing;
    for (const MapFile& f : read_files(*disc, ".IPD")) {
        auto ipd = lvl::parse_ipd(f.bytes);
        SH_CHECK(ipd);
        if (!ipd) {
            continue;
        }
        const std::string glb = starts_with(f.path, "BG/") ? std::string(lvl::global_plm_of_area(lvl::area_of_ipd(f.path))) : "";
        if (!glb.empty() && !globals.count(glb)) {
            auto plm = lvl::parse_plm(*options.sibling(glb));
            SH_CHECK(plm);
            globals.emplace(glb, std::move(plm.value()));
        }
        const lvl::LmFile* global = glb.empty() ? nullptr : &globals.at(glb);
        auto cell = lvl::cell_to_glb(ipd.value(), f.path, global, glb, &catalog);
        SH_CHECK(cell);
        if (!cell) {
            continue;
        }
        instances += cell.value().instances;
        global_refs += cell.value().global_references;
        unresolved += cell.value().unresolved;
        outside += cell.value().stats.palettes_outside_tim;
        missing.insert(cell.value().stats.missing_textures.begin(), cell.value().stats.missing_textures.end());
    }
    SH_CHECK(instances + unresolved == 20332);
    SH_CHECK(unresolved == 236);
    SH_CHECK(global_refs == 8488);
    SH_CHECK(outside == 0);
    SH_CHECK(missing == std::set<std::string>{"DWAVE"});  // the water surface, drawn into VRAM at run time
    std::fprintf(stderr, "level glb: %u cells, %u PLM, %zu areas; %llu triangles; cells+PLM %llu bytes, areas %llu bytes\n",
                 cell_glbs, plm_glbs, areas.size(), static_cast<unsigned long long>(triangles),
                 static_cast<unsigned long long>(glb_bytes), static_cast<unsigned long long>(area_bytes));
}
