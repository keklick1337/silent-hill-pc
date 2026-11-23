#include "framework/sh_test.h"

#include "formats/common/json.h"
#include "formats/convert/materials/texture_bake.h"
#include "formats/gltf/gltf_inspect.h"
#include "formats/gltf/gltf_writer.h"
#include "formats/png/sh_png.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace mat = sh::convert::materials;

namespace {

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "texture_bake" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& relative, std::string_view bytes) const {
        fs::create_directories((root / relative).parent_path());
        std::ofstream out(root / relative, std::ios::binary);
        out << bytes;
    }
    std::string read(const fs::path& relative) const {
        std::ifstream in(root / relative, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    // Every file under the root, for idempotence checks.
    std::map<std::string, std::string> snapshot() const {
        std::map<std::string, std::string> files;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) {
                files[entry.path().lexically_relative(root).generic_string()] = read(entry.path().lexically_relative(root));
            }
        }
        return files;
    }
};

constexpr int kW = 8;
constexpr int kH = 8;

std::string hex(std::uint16_t word) {
    char text[8];
    std::snprintf(text, sizeof text, "%04x", word);
    return text;
}

// A 4-bit TIM of kW x kH whose pixel index is its x, with the given CLUT rows (16 words each).
void write_tim(const Scratch& dir, const std::string& stem, const std::vector<std::vector<std::uint16_t>>& rows) {
    std::string json = "{\"format\": \"sh-tim\", \"version\": 1, \"bpp\": 4, \"width\": " + std::to_string(kW) +
                       ", \"height\": " + std::to_string(kH) + ", \"palettes\": [";
    for (std::size_t r = 0; r < rows.size(); ++r) {
        json += r ? ", [" : "[";
        for (std::size_t i = 0; i < rows[r].size(); ++i) {
            json += (i ? ", \"" : "\"") + hex(rows[r][i]) + "\"";
        }
        json += "]";
    }
    json += "]}";
    dir.write("convert/textures/" + stem + ".tim.json", json);

    sh::png::Image image;
    image.width = kW;
    image.height = kH;
    image.color_type = sh::png::ColorType::Indexed;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            image.pixels.push_back(static_cast<std::uint8_t>(x));
        }
    }
    image.palette.assign(16 * 3, 0);
    auto png = sh::png::encode(image);
    SH_CHECK(png);
    dir.write("convert/indexed/" + stem + ".png",
              std::string(reinterpret_cast<const char*>(png.value().data()), png.value().size()));
    dir.write("convert/textures/" + stem + ".png", "row0 picture of the converter");
}

struct QuadUse {
    std::string tim;
    int row;
    int u0, u1;  // texel columns of the quad's edges; it spans every row of the picture
    bool semi = false;
    bool in_tim = true;
    std::string uri = {};  // the image of the material when not "<TIM stem>.pNN.png"
};

// A glb with one material and one quad per use. `to_textures` is the glb's way to convert/textures/.
std::string make_glb(const std::vector<QuadUse>& uses, const std::string& to_textures) {
    sh::gltf::Document doc;
    sh::gltf::Mesh mesh;
    mesh.name = "m";
    for (const QuadUse& use : uses) {
        const std::string stem = use.tim.substr(0, use.tim.size() - 4);
        const std::string variant = stem + ".p" + (use.row < 10 ? "0" : "") + std::to_string(use.row) + ".png";
        const std::string uri = to_textures + (use.uri.empty() ? variant : use.uri);
        int texture = 0;
        while (texture < static_cast<int>(doc.textures.size()) && doc.textures[static_cast<std::size_t>(texture)].uri != uri) {
            ++texture;
        }
        if (texture == static_cast<int>(doc.textures.size())) {
            doc.textures.push_back({uri, true});
        }
        sh::gltf::Material material;
        material.name = "row" + std::to_string(use.row);
        material.texture = texture;
        material.extras = "{\"tim\":\"" + use.tim + "\",\"palette\":" + std::to_string(use.row) +
                          ",\"palette_in_tim\":" + (use.in_tim ? "true" : "false") + ",\"png\":\"textures/" + variant +
                          "\",\"semi_transparent\":" + (use.semi ? "true" : "false") + "}";
        doc.materials.push_back(std::move(material));
        sh::gltf::Primitive prim;
        prim.positions = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
        const float a = static_cast<float>(use.u0) / kW;
        const float b = static_cast<float>(use.u1) / kW;
        prim.texcoords = {a, 0, b, 0, b, 1, a, 1};
        prim.indices = {0, 1, 2, 0, 2, 3};
        prim.material = static_cast<int>(doc.materials.size()) - 1;
        mesh.primitives.push_back(std::move(prim));
    }
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

std::uint32_t le32(const std::string& bytes, std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

// The bytes after the JSON chunk (the BIN chunk).
std::string bin_of(const std::string& glb) { return glb.substr(20 + le32(glb, 12)); }

sh::json::Value json_of(const std::string& glb) {
    auto doc = sh::json::parse(std::string_view(glb).substr(20, le32(glb, 12)));
    SH_CHECK(doc);
    return doc.value();
}

std::string image_uri(const sh::json::Value& doc, std::size_t material) {
    const sh::json::Value& m = doc.find("materials")->array()[material];
    const std::int64_t texture = m.find("pbrMetallicRoughness")->find("baseColorTexture")->find("index")->integer();
    const std::int64_t image = doc.find("textures")->array()[static_cast<std::size_t>(texture)].find("source")->integer();
    return doc.find("images")->array()[static_cast<std::size_t>(image)].find("uri")->string();
}

std::string extras_png(const sh::json::Value& doc, std::size_t material) {
    return doc.find("materials")->array()[material].find("extras")->find("png")->string();
}

sh::png::Rgba8 picture(const Scratch& dir, const std::string& relative) {
    const std::string bytes = dir.read(relative);
    auto image = sh::png::decode_rgba8({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
    SH_CHECK(image);
    return image.value();
}

std::array<std::uint8_t, 4> texel(const sh::png::Rgba8& image, int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
}

std::array<std::uint8_t, 4> rgba(std::uint16_t word, std::uint8_t alpha) {
    auto e = [](unsigned v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); };
    return {e(word & 31), e((word >> 5) & 31), e((word >> 10) & 31), alpha};
}

std::vector<std::uint16_t> row_words(std::uint16_t base) {
    std::vector<std::uint16_t> row;
    for (std::uint16_t i = 0; i < 16; ++i) {
        row.push_back(static_cast<std::uint16_t>(base + i));
    }
    return row;
}

}  // namespace

SH_TEST(texture_bake_coverage_rule) {
    // A quad over texel edges 4..8: the bilinear tap on its left (3) is drawn, the column past its right edge is not.
    const std::vector<float> quad = {4 / 16.f, 4 / 16.f, 8 / 16.f, 4 / 16.f, 8 / 16.f, 8 / 16.f,
                                     4 / 16.f, 4 / 16.f, 8 / 16.f, 8 / 16.f, 4 / 16.f, 8 / 16.f};
    const auto box = mat::coverage_mask(16, 16, quad);
    int drawn = 0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool inside = x >= 3 && x <= 7 && y >= 3 && y <= 7;
            SH_CHECK(box[static_cast<std::size_t>(y * 16 + x)] == (inside ? 1 : 0));
            drawn += box[static_cast<std::size_t>(y * 16 + x)];
        }
    }
    SH_CHECK(drawn == 25);

    // One triangle (0,0) (4,0) (0,4): texels whose dilated square reaches the open triangle, x + y <= 3.
    const std::vector<float> tri = {0, 0, 4 / 16.f, 0, 0, 4 / 16.f};
    const auto half = mat::coverage_mask(16, 16, tri);
    SH_CHECK(half[0 * 16 + 3] == 1);
    SH_CHECK(half[2 * 16 + 1] == 1);
    SH_CHECK(half[2 * 16 + 2] == 0);
    SH_CHECK(half[1 * 16 + 3] == 0);
    SH_CHECK(half[0 * 16 + 4] == 0);

    // Texel-centre UVs as the importer writes them, (t + 0.5) / size: BG/APUG2F of APU.glb, texels (96,255) (127,255)
    // (99,224). The left edge passes x = 97.4 .. 97.6 between y = 238.5 and 240.5, so texel 96 is read by the
    // bilinear taps and texel 95 is not.
    const std::vector<float> centres = {96.5f / 256, 255.5f / 256, 127.5f / 256, 255.5f / 256, 99.5f / 256, 224.5f / 256};
    const auto apu = mat::coverage_mask(256, 256, centres);
    SH_CHECK(apu[239 * 256 + 96] == 1);
    SH_CHECK(apu[239 * 256 + 95] == 0);
    SH_CHECK(apu[224 * 256 + 99] == 1);
    SH_CHECK(apu[255 * 256 + 127] == 1);

    // A zero-area triangle (a line along v = 2): its whole box, x 1..5 on row 1.
    const std::vector<float> line = {2 / 16.f, 2 / 16.f, 6 / 16.f, 2 / 16.f, 4 / 16.f, 2 / 16.f};
    const auto thin = mat::coverage_mask(16, 16, line);
    int count = 0;
    for (std::size_t i = 0; i < thin.size(); ++i) {
        count += thin[i];
    }
    SH_CHECK(count == 5);
    for (int x = 1; x <= 5; ++x) {
        SH_CHECK(thin[static_cast<std::size_t>(16 + x)] == 1);
    }
}

SH_TEST(texture_bake_base_variants_alpha_and_rewrite) {
    Scratch dir("bake");
    // Pixel index = x. Row 0: entry 0 is transparent. Row 2 = row 0 except entry 6 and entry 7 (STP).
    std::vector<std::uint16_t> row0 = row_words(0x0400);
    row0[0] = 0x0000;
    std::vector<std::uint16_t> row2 = row0;
    row2[6] = 0x0C06;
    row2[7] = 0x8C07;
    std::vector<std::uint16_t> row3 = row_words(0x0800);
    row3[4] = 0x1004;
    write_tim(dir, "T/A", {row0, row_words(0x0800), row2, row3});
    write_tim(dir, "T/LONE", {row_words(0x0400), row_words(0x0800)});  // no consumer: left alone

    // Row 2 (semi, scene) covers columns 2..7 (48 texels), row 1 columns 3..7 (40), row 0 columns 0..3 (32),
    // row 3 columns 4..5 (16).
    dir.write("maps/areas/MR.glb", make_glb({{"T/A.TIM", 2, 3, 8, true}}, "../../convert/textures/"));
    dir.write("maps/areas/ER.glb", make_glb({{"T/A.TIM", 1, 4, 8}}, "../../convert/textures/"));
    dir.write("convert/models/T/PRS2.glb",
              make_glb({{"T/A.TIM", 0, 0, 4}, {"T/A.TIM", 3, 5, 6}, {"T/A.TIM", 5, 0, 8, false, false}},
                       "../../textures/"));
    const std::string prs2_before = dir.read("convert/models/T/PRS2.glb");

    const auto report = mat::bake_textures_with_usage(dir.root, 3, "{\"version\":1,\"usages\":[]}");
    SH_CHECK(report);
    SH_CHECK(report.value().warnings.empty());
    for (const std::string& w : report.value().warnings) {
        std::fprintf(stderr, "  warning: %s\n", w.c_str());
    }
    SH_CHECK(report.value().tims == 1);
    SH_CHECK(report.value().variants == 2);
    SH_CHECK(report.value().consumers == 3);
    SH_CHECK(report.value().glbs_rewritten == 3);
    SH_CHECK(dir.read("convert/textures/T/LONE.png") == "row0 picture of the converter");

    // Base: row 0 on columns 0..1 (0 transparent), row 2 on 2..7 (7 has STP under a semi-transparent scene material).
    const auto base = picture(dir, "convert/textures/T/A.png");
    SH_CHECK(texel(base, 0, 0) == rgba(0x0000, 0));
    SH_CHECK(texel(base, 1, 5) == rgba(0x0401, 255));
    SH_CHECK(texel(base, 3, 2) == rgba(0x0403, 255));
    SH_CHECK(texel(base, 6, 7) == rgba(0x0C06, 255));
    SH_CHECK(texel(base, 7, 3) == rgba(0x8C07, 128));
    // Row 1 disagrees on 3..7: variant "er" (its only consumer is the area ER). Row 3 contradicts that layer on
    // column 4, so it gets its own, named after the model.
    const auto er = picture(dir, "convert/textures/T/A.er.png");
    SH_CHECK(texel(er, 1, 0) == rgba(0x0401, 255));
    SH_CHECK(texel(er, 3, 0) == rgba(0x0803, 255));
    SH_CHECK(texel(er, 7, 0) == rgba(0x0807, 255));
    const auto prs2 = picture(dir, "convert/textures/T/A.prs2.png");
    SH_CHECK(texel(prs2, 4, 4) == rgba(0x1004, 255));
    SH_CHECK(texel(prs2, 5, 4) == rgba(0x0805, 255));
    SH_CHECK(texel(prs2, 6, 4) == rgba(0x0C06, 255));
    SH_CHECK(texel(prs2, 7, 4) == rgba(0x8C07, 128));

    // The glbs name the baked pictures; the BIN chunks are untouched; cgltf accepts them.
    const std::string prs2_glb = dir.read("convert/models/T/PRS2.glb");
    SH_CHECK(bin_of(prs2_glb) == bin_of(prs2_before));
    SH_CHECK(le32(prs2_glb, 8) == prs2_glb.size());
    SH_CHECK(le32(prs2_glb, 12) % 4 == 0);
    SH_CHECK(sh::gltf::inspect_glb({reinterpret_cast<const std::uint8_t*>(prs2_glb.data()), prs2_glb.size()}));
    const sh::json::Value doc = json_of(prs2_glb);
    SH_CHECK(image_uri(doc, 0) == "../../textures/T/A.png");
    SH_CHECK(extras_png(doc, 0) == "textures/T/A.png");
    SH_CHECK(image_uri(doc, 1) == "../../textures/T/A.prs2.png");
    SH_CHECK(extras_png(doc, 1) == "textures/T/A.prs2.png");
    SH_CHECK(image_uri(doc, 2) == "../../textures/T/A.p05.png");  // palette outside the TIM: not the bake's
    const sh::json::Value er_doc = json_of(dir.read("maps/areas/ER.glb"));
    SH_CHECK(image_uri(er_doc, 0) == "../../convert/textures/T/A.er.png");
    SH_CHECK(extras_png(er_doc, 0) == "textures/T/A.er.png");
    SH_CHECK(image_uri(json_of(dir.read("maps/areas/MR.glb")), 0) == "../../convert/textures/T/A.png");

    // The record of the bake.
    auto record = sh::json::parse(dir.read("convert/texture_bake.json"));
    SH_CHECK(record);
    const sh::json::Value* textures = record.value().find("textures");
    SH_CHECK(textures != nullptr && textures->array().size() == 1);
    const sh::json::Value* variants = textures->array()[0].find("variants");
    SH_CHECK(variants != nullptr && variants->find("textures/T/A.er.png") != nullptr);
    SH_CHECK(variants->find("textures/T/A.prs2.png")->find("label")->string() == "prs2");

    // Again: nothing changes.
    const auto before = dir.snapshot();
    const auto again = mat::bake_textures_with_usage(dir.root, 1, "{\"version\":1,\"usages\":[]}");
    SH_CHECK(again);
    SH_CHECK(again.value().glbs_rewritten == 0);
    SH_CHECK(again.value().pictures_written == 0);
    SH_CHECK(dir.snapshot() == before);
}

// Two rows that disagree with the base on different texels must not share a variant when one of them reads, from
// that picture, a texel the other one repaints.
SH_TEST(texture_bake_layer_pins_every_texel_its_rows_draw) {
    Scratch dir("bake_pins");
    const std::vector<std::uint16_t> row0 = row_words(0x0400);
    std::vector<std::uint16_t> row1 = row0;  // differs from row 0 on columns 0 and 1 only
    row1[0] = 0x1000;
    row1[1] = 0x1001;
    std::vector<std::uint16_t> row2 = row0;  // differs on columns 2 and 3 only
    row2[2] = 0x2002;
    row2[3] = 0x2003;
    write_tim(dir, "T/B", {row0, row1, row2});
    // Row 0 draws columns 0..7 (the base), row 1 columns 0..3, row 2 columns 2..5.
    dir.write("convert/models/T/M1.glb", make_glb({{"T/B.TIM", 0, 0, 8}, {"T/B.TIM", 1, 0, 4}}, "../../textures/"));
    dir.write("convert/models/T/M2.glb", make_glb({{"T/B.TIM", 2, 3, 6}}, "../../textures/"));

    const auto report = mat::bake_textures_with_usage(dir.root, 2, "{\"version\":1,\"usages\":[]}");
    SH_CHECK(report);
    SH_CHECK(report.value().variants == 2);
    const auto m1 = picture(dir, "convert/textures/T/B.m1.png");
    SH_CHECK(texel(m1, 0, 0) == rgba(0x1000, 255));
    SH_CHECK(texel(m1, 2, 0) == rgba(0x0402, 255));  // row 1 reads column 2 in its own colour, not row 2's
    SH_CHECK(texel(m1, 3, 0) == rgba(0x0403, 255));
    const auto m2 = picture(dir, "convert/textures/T/B.m2.png");
    SH_CHECK(texel(m2, 2, 0) == rgba(0x2002, 255));
    SH_CHECK(texel(m2, 1, 0) == rgba(0x0401, 255));
}

SH_TEST(texture_bake_draw_usage_and_stale_variants) {
    Scratch dir("usage");
    std::vector<std::uint16_t> row1 = row_words(0x0800);
    row1[0] = 0x8801;  // STP
    write_tim(dir, "U/B", {row_words(0x0400), row1});
    const char* usage = R"({"version":1,"usages":[
        {"tim":"U/B.TIM","row":0,"rects":[[0,0,8,8]],"semi":false,"label":"background"},
        {"tim":"U/B.TIM","row":1,"rects":[[0,0,2,2]],"semi":true,"label":"flame"}]})";
    const auto report = mat::bake_textures_with_usage(dir.root, 2, usage);
    SH_CHECK(report);
    SH_CHECK(report.value().usages == 2);
    SH_CHECK(report.value().tims == 1);
    SH_CHECK(report.value().variants == 1);
    const auto flame = picture(dir, "convert/textures/U/B.flame.png");
    SH_CHECK(texel(flame, 0, 0) == rgba(0x8801, 128));
    SH_CHECK(texel(flame, 1, 1) == rgba(0x0801, 255));
    SH_CHECK(texel(flame, 2, 1) == rgba(0x0402, 255));
    SH_CHECK(texel(picture(dir, "convert/textures/U/B.png"), 0, 0) == rgba(0x0400, 255));

    // Without the usage the variant is the bake's own leftover and goes; a picture a mod added stays.
    dir.write("convert/textures/U/B.mod.png", "a mod's picture");
    const auto later = mat::bake_textures_with_usage(dir.root, 2, R"({"version":1,"usages":[
        {"tim":"U/B.TIM","row":0,"rects":[[0,0,8,8]],"semi":false,"label":"background"}]})");
    SH_CHECK(later);
    SH_CHECK(later.value().stale_removed == 1);
    SH_CHECK(!fs::exists(dir.root / "convert/textures/U/B.flame.png"));
    SH_CHECK(fs::exists(dir.root / "convert/textures/U/B.mod.png"));
}

SH_TEST(texture_bake_splits_a_shared_image) {
    Scratch dir("shared");
    write_tim(dir, "S/C", {row_words(0x0400), row_words(0x0800)});
    // One image for both rows, as the inventory models have it.
    dir.write("convert/models/S/SHARED.glb", make_glb({{"S/C.TIM", 0, 0, 8, false, true, "S/C.png"},
                                                       {"S/C.TIM", 1, 0, 4, false, true, "S/C.png"}},
                                                      "../../textures/"));
    const std::string before = dir.read("convert/models/S/SHARED.glb");
    SH_CHECK(json_of(before).find("images")->array().size() == 1);

    const auto report = mat::bake_textures_with_usage(dir.root, 2, "{\"version\":1,\"usages\":[]}");
    SH_CHECK(report);
    SH_CHECK(report.value().warnings.empty());
    SH_CHECK(report.value().variants == 1);
    const std::string glb = dir.read("convert/models/S/SHARED.glb");
    SH_CHECK(bin_of(glb) == bin_of(before));
    SH_CHECK(sh::gltf::inspect_glb({reinterpret_cast<const std::uint8_t*>(glb.data()), glb.size()}));
    const sh::json::Value doc = json_of(glb);
    SH_CHECK(doc.find("images")->array().size() == 2);
    SH_CHECK(doc.find("textures")->array().size() == 2);
    SH_CHECK(image_uri(doc, 0) == "../../textures/S/C.png");
    SH_CHECK(image_uri(doc, 1) == "../../textures/S/C.shared.png");
    SH_CHECK(extras_png(doc, 1) == "textures/S/C.shared.png");
    SH_CHECK(doc.find("textures")->array()[1].find("sampler") != nullptr);

    const auto again = mat::bake_textures_with_usage(dir.root, 2, "{\"version\":1,\"usages\":[]}");
    SH_CHECK(again && again.value().glbs_rewritten == 0);
    SH_CHECK(dir.read("convert/models/S/SHARED.glb") == glb);
}

SH_TEST(texture_bake_builtin_usage_parses) {
    auto doc = sh::json::parse(mat::builtin_draw_usage_json());
    SH_CHECK(doc);
    SH_CHECK(doc.value().find("usages") != nullptr && doc.value().find("usages")->array().size() >= 69);
}

SH_TEST(texture_bake_needs_textures) {
    Scratch dir("none");
    SH_CHECK(!mat::bake_textures(dir.root, 1));
}
