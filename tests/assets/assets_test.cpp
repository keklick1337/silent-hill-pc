#include "framework/sh_disc.h"
#include "framework/sh_test.h"
#include "engine/assets/anim_layer.h"
#include "engine/assets/glb_anims.h"
#include "engine/assets/skin_model.h"
#include "formats/convert/anim/anim_converter.h"
#include "formats/convert/anim/anm_file.h"
#include "formats/convert/model/ilm.h"
#include "formats/convert/model/model_converter.h"
#include "formats/gltf/gltf_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace an = sh::convert::anim;
namespace md = sh::convert::model;

bool ends_with(const std::string& s, const char* tail) {
    const std::string t(tail);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

fs::path fresh_dir(const char* name) {
    // Per process: test runs started side by side must not clear each other's folders.
    static const unsigned tag = std::random_device{}();
    const fs::path dir = fs::temp_directory_path() / (std::string(name) + "_" + std::to_string(tag));
    // Removed when the test process ends.
    struct Cleanup {
        std::vector<fs::path> dirs;
        ~Cleanup() {
            for (const fs::path& d : dirs) {
                std::error_code e;
                fs::remove_all(d, e);
            }
        }
    };
    static Cleanup cleanup;
    cleanup.dirs.push_back(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    os.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// raw/ of a gamedata: the disc bytes, and one sector's worth of tail behind each file.
class DiscLayer final : public sh::vfs::Layer {
public:
    explicit DiscLayer(const sh::test::GameDisc& disc) : disc_(disc) {}
    std::string name() const override { return "disc"; }
    bool exists(std::string_view path) const override { return disc_.read(std::string(path)).has_value(); }
    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override {
        return disc_.read(std::string(path));
    }
    std::unique_ptr<sh::ByteSource> open_stored(std::string_view path) const override;
    static std::vector<std::uint8_t> tail_of(std::size_t size) {
        std::vector<std::uint8_t> t((2048 - size % 2048) % 2048);
        for (std::size_t i = 0; i < t.size(); ++i) {
            t[i] = static_cast<std::uint8_t>(i * 7 + 1);
        }
        return t;
    }

private:
    const sh::test::GameDisc& disc_;
};

class VecSource final : public sh::ByteSource {
public:
    explicit VecSource(std::vector<std::uint8_t> d) : d_(std::move(d)) {}
    std::uint64_t size() const override { return d_.size(); }
    bool read_at(std::uint64_t off, void* dst, std::size_t n) const override {
        if (off > d_.size() || n > d_.size() - off) {
            return false;
        }
        std::copy(d_.begin() + static_cast<std::ptrdiff_t>(off), d_.begin() + static_cast<std::ptrdiff_t>(off + n),
                  static_cast<std::uint8_t*>(dst));
        return true;
    }

private:
    std::vector<std::uint8_t> d_;
};

std::unique_ptr<sh::ByteSource> DiscLayer::open_stored(std::string_view path) const {
    auto bytes = read(path);
    if (!bytes) {
        return nullptr;
    }
    const auto tail = tail_of(bytes->size());
    bytes->insert(bytes->end(), tail.begin(), tail.end());
    return std::make_unique<VecSource>(std::move(*bytes));
}

}  // namespace

SH_TEST(AnimGamePathOfConvertedFiles) {
    SH_CHECK(sh::assets::anim_game_path("ANIM/HB_BASE.anim.json") == "ANIM/HB_BASE.ANM");
    SH_CHECK(sh::assets::anim_game_path("anim/hero.ANIM.JSON") == "ANIM/HERO.ANM");
    SH_CHECK(sh::assets::anim_game_path("ANIM/HERO.json").empty());
    SH_CHECK(sh::assets::bone_of_joint_name("bone07") == 7);
    SH_CHECK(sh::assets::bone_of_joint_name("Bone_12") == 12);
    SH_CHECK(sh::assets::bone_of_joint_name("bone") == -1);
    SH_CHECK(sh::assets::bone_of_joint_name("mixamorig:Hips") == -1);
    SH_CHECK(sh::assets::below_convert("/x/gamedata/convert/models/CHARA/../../textures/CHARA/HERO.png") ==
             fs::path("textures/CHARA/HERO.png"));
    SH_CHECK(sh::assets::below_convert("/x/mods/hd/HERO.png").empty());
}

// Every ANM of the image -> anim.json (the importer's converter) -> the layer the engine reads: the disc bytes, and
// the sector tail of raw/ behind them.
SH_TEST(AnimJsonLayerGivesTheDiscBytesOfEveryAnm) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const fs::path root = fresh_dir("sh_assets_anim_layer");
    const auto conv = an::make_converter(disc->reader());
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
    for (const auto& e : disc->table) {
        if (!ends_with(e.name, ".ANM")) {
            continue;
        }
        const auto raw = disc->read(e.name);
        if (!raw || !conv->accepts(*raw, e.name)) {
            continue;
        }
        const auto out = conv->decode(*raw, e.name);
        SH_CHECK(out);
        if (!out) {
            continue;
        }
        for (const auto& o : out.value()) {
            write_file(root / "convert" / o.relative_path, o.bytes);
        }
        files.emplace_back(e.name, *raw);
    }
    SH_CHECK(files.size() >= 100);

    DiscLayer below(*disc);
    std::vector<std::string> warnings;
    const auto layer = sh::assets::make_anim_json_layer(root, "anims", &below,
                                                        [&](int level, const std::string& m) {
                                                            if (level) {
                                                                warnings.push_back(m);
                                                            }
                                                        });
    SH_CHECK(layer != nullptr);
    if (!layer) {
        return;
    }
    std::size_t same = 0;
    for (const auto& [path, raw] : files) {
        SH_CHECK(layer->exists(path));
        const auto bytes = layer->read(path);
        SH_CHECK(bytes.has_value() && *bytes == raw);
        const auto stored = layer->open_stored(path);
        SH_CHECK(stored != nullptr);
        if (!bytes || !stored) {
            continue;
        }
        std::vector<std::uint8_t> sectors(static_cast<std::size_t>(stored->size()));
        SH_CHECK(stored->read_at(0, sectors.data(), sectors.size()));
        std::vector<std::uint8_t> expect = raw;
        const auto tail = DiscLayer::tail_of(raw.size());
        expect.insert(expect.end(), tail.begin(), tail.end());
        SH_CHECK(sectors == expect);
        same += sectors == expect ? 1 : 0;
    }
    SH_CHECK(warnings.empty());
    std::printf("  %zu ANM files: anim.json gives the disc bytes and sectors of %zu\n", files.size(), same);

    // A broken file answers as missing, so the layers below give it.
    write_file(root / "convert" / "anim" / "ANIM" / "BROKEN.anim.json", {'{', '}'});
    const auto again = sh::assets::make_anim_json_layer(root, "anims", nullptr, nullptr);
    SH_CHECK(again && !again->exists("ANIM/BROKEN.ANM") && again->exists(files.front().first));
    std::error_code ec;
    fs::remove_all(root, ec);
}

// HERO.glb packs Harry's base file and every non-empty map file byte for byte; the engine reads them back from the
// glb exactly as the disc has them. The parts of every character name their ILM part by index, and the
// semi-transparent materials leave the blend mode to the character.
SH_TEST(CharacterGlbPacksAnimationsAndNamesItsParts) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const fs::path root = fresh_dir("sh_assets_glb_anims");
    md::Options options;
    options.sibling = disc->reader();
    for (const char* name : {"CHARA/HERO.ILM", "CHARA/DOC.ILM"}) {
        const auto raw = disc->read(name);
        SH_CHECK(raw.has_value());
        if (!raw) {
            return;
        }
        const auto converted = md::convert_model(*raw, name, options);
        SH_CHECK(converted);
        if (!converted) {
            return;
        }
        const std::string stem = std::string(name).substr(0, std::string(name).size() - 4);
        write_file(root / "convert" / "models" / (stem + ".glb"), converted.value().glb);
    }

    const auto index = sh::assets::packed_anim_index(root / "convert");
    std::size_t packed = 0;
    for (const auto& [path, glb] : index) {
        SH_CHECK(glb == "models/CHARA/HERO.glb");
        const auto file = sh::assets::packed_anm(root / "convert" / glb, path);
        SH_CHECK(file);
        const auto bytes = disc->read(path);
        SH_CHECK(bytes.has_value());
        if (!file || !bytes) {
            continue;
        }
        SH_CHECK(file.value().file.serialize() == *bytes);
        SH_CHECK(file.value().base == (path == an::kHarryBaseAnm ? "" : std::string(an::kHarryBaseAnm)));
        ++packed;
    }
    // The base file and 43 map files (five of the 48 are empty on the disc).
    SH_CHECK(packed == 44);
    SH_CHECK(index.count(std::string(an::kHarryBaseAnm)) == 1 && index.count("ANIM/HB_M1S00.ANM") == 1);
    SH_CHECK(!sh::assets::packed_anm(root / "convert" / "models" / "CHARA" / "DOC.glb", "ANIM/DOC.ANM"));

    std::vector<std::string> warnings;
    const auto hero = sh::assets::load_skin_model(root / "convert" / "models" / "CHARA" / "HERO.glb", nullptr, warnings);
    const auto doc = sh::assets::load_skin_model(root / "convert" / "models" / "CHARA" / "DOC.glb", nullptr, warnings);
    SH_CHECK(hero && doc);
    if (!hero || !doc) {
        return;
    }
    for (const auto* model : {&hero.value(), &doc.value()}) {
        for (const sh::assets::SkinGroup& g : model->groups) {
            SH_CHECK(g.part >= 0);
        }
    }
    for (const sh::assets::SkinMaterial& m : hero.value().materials) {
        SH_CHECK(!m.semi_transparent);
    }
    for (const sh::assets::SkinMaterial& m : doc.value().materials) {
        SH_CHECK(m.semi_transparent && m.abr == -1);
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

// The imported glb of every ILM character, loaded by the engine's skin loader: each influence is a vertex of the
// ILM part that owns that bone, in the bone's own space -- the numbers the game transforms -- so posing it with the
// game's bone matrices is the game's arithmetic.
SH_TEST(SkinModelOfEveryCharacterIsTheIlmInBoneSpace) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const fs::path root = fresh_dir("sh_assets_skin_model");
    md::Options options;
    options.sibling = disc->reader();
    std::size_t models = 0, vertices = 0, posed = 0;
    double worst_local = 0, worst_pose = 0;
    std::string worst_where;
    for (const auto& e : disc->table) {
        if (!ends_with(e.name, ".ILM")) {
            continue;
        }
        const auto raw = disc->read(e.name);
        if (!raw) {
            continue;
        }
        const auto converted = md::convert_model(*raw, e.name, options);
        SH_CHECK(converted);
        if (!converted || converted.value().stats.joints == 0) {
            continue;
        }
        const fs::path glb = root / "convert" / "models" / (e.name.substr(0, e.name.size() - 4) + ".glb");
        write_file(glb, converted.value().glb);
        std::vector<std::string> warnings;  // the PNGs are not written here
        const auto model = sh::assets::load_skin_model(glb, nullptr, warnings);
        SH_CHECK(model);
        if (!model) {
            std::printf("  %s: %s\n", e.name.c_str(), model.error().c_str());
            continue;
        }
        ++models;
        const auto ilm = md::parse_ilm(*raw);
        SH_CHECK(ilm);
        if (!ilm) {
            continue;
        }
        // ILM vertices by the bone of their part.
        std::map<int, std::set<std::tuple<int, int, int>>> by_bone;
        for (const md::IlmPart& part : ilm.value().parts) {
            for (const md::IlmMesh& mesh : part.meshes) {
                for (const auto& v : mesh.vertices) {
                    by_bone[part.bone()].insert({v[0], v[1], v[2]});
                }
            }
        }
        for (const sh::assets::SkinVertex& v : model.value().vertices) {
            SH_CHECK(v.count == 1);
            const sh::assets::SkinInfluence& in = v.influence[0];
            const std::tuple<int, int, int> key{static_cast<int>(std::lround(in.local[0])),
                                                static_cast<int>(std::lround(in.local[1])),
                                                static_cast<int>(std::lround(in.local[2]))};
            const double off = std::max({std::abs(static_cast<double>(in.local[0]) - std::get<0>(key)),
                                         std::abs(static_cast<double>(in.local[1]) - std::get<1>(key)),
                                         std::abs(static_cast<double>(in.local[2]) - std::get<2>(key))});
            const bool found = by_bone[in.bone].count(key) != 0;
            SH_CHECK(found);
            if (!found || off > worst_local) {
                worst_local = std::max(worst_local, off);
                worst_where = e.name;
            }
            ++vertices;
        }

        // Posed with the game's own bone matrices of a keyframe in the middle of the file.
        const std::string side = converted.value().sidecar_json;
        const std::size_t at = side.find("\"anm\": \"");
        SH_CHECK(at != std::string::npos);
        const auto anm_raw = disc->read(side.substr(at + 8, side.find('"', at + 8) - at - 8));
        const auto anm = anm_raw ? an::AnmFile::parse(*anm_raw) : sh::Result<an::AnmFile>::fail("no ANM");
        SH_CHECK(anm);
        if (!anm) {
            continue;
        }
        const std::size_t k = anm.value().keyframes().size() / 2;
        const auto world = anm.value().world_poses(anm.value().local_poses(k, k, 0));
        std::vector<sh::assets::BoneTransform> bones(world.size());
        std::vector<std::uint8_t> have(world.size(), 1);
        for (std::size_t b = 0; b < world.size(); ++b) {
            for (int i = 0; i < 9; ++i) {
                bones[b].m[static_cast<std::size_t>(i)] = static_cast<float>(world[b].m[static_cast<std::size_t>(i)] / 4096.0);
            }
            for (int i = 0; i < 3; ++i) {
                bones[b].t[static_cast<std::size_t>(i)] = static_cast<float>(world[b].t[static_cast<std::size_t>(i)]);
            }
        }
        std::vector<float> xyz;
        std::vector<std::uint8_t> ok;
        model.value().pose(bones, have, xyz, ok);
        for (std::size_t vi = 0; vi < model.value().vertices.size(); ++vi) {
            const sh::assets::SkinInfluence& in = model.value().vertices[vi].influence[0];
            const auto& w = world[static_cast<std::size_t>(in.bone)];
            SH_CHECK(ok[vi] == 1);
            for (int r = 0; r < 3; ++r) {
                double ref = w.t[static_cast<std::size_t>(r)];
                for (int c = 0; c < 3; ++c) {
                    ref += w.m[static_cast<std::size_t>(r * 3 + c)] / 4096.0 * std::lround(in.local[static_cast<std::size_t>(c)]);
                }
                worst_pose = std::max(worst_pose, std::abs(ref - xyz[vi * 3 + static_cast<std::size_t>(r)]));
            }
            ++posed;
        }
    }
    SH_CHECK(models >= 45);
    SH_CHECK(worst_local < 0.05);
    SH_CHECK(worst_pose < 0.1);
    std::printf("  %zu models, %zu vertices in ILM bone space (worst %.4f units, %s); posed %zu, worst %.4f units\n",
                models, vertices, worst_local, worst_where.c_str(), posed, worst_pose);
    std::error_code ec;
    fs::remove_all(root, ec);
}

// An HD rig of a mod: a bind pose of its own (translated inverse binds), a helper joint without a game bone name, two
// weights per vertex. Posing is glTF skinning with the game's bone transforms in place of the joints'.
SH_TEST(SkinModelOfAnHdRigFollowsTheGameBones) {
    namespace gl = sh::gltf;
    gl::Document doc;
    // Joints: bone00 (root) -> Bone_01 -> forearm_twist (no game bone: moves with bone 1).
    gl::Node root, b1, helper, body;
    root.name = "bone00";
    root.children = {1};
    b1.name = "Bone_01";
    b1.translation = {0, 1, 0};
    b1.children = {2};
    helper.name = "forearm_twist";
    helper.translation = {0, 0.5f, 0};
    body.name = "hd_body";
    body.mesh = 0;
    body.skin = 0;
    doc.nodes = {root, b1, helper, body};
    gl::Skin skin;
    skin.joints = {0, 1, 2};
    // The joints bind at y = 0, 1 and 1.5 metres.
    skin.inverse_bind = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                         {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1},
                         {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1.5f, 0, 1}};
    doc.skins = {skin};
    gl::Primitive prim;
    prim.positions = {0.25f, 0.5f, 0, 0.25f, 1.25f, 0, 0, 1.75f, 0.5f};
    prim.normals = {1, 0, 0, 1, 0, 0, 0, 0, 1};
    prim.texcoords = {0, 0, 1, 0, 0, 1};
    prim.joints = {0, 1, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};
    prim.weights = {0.5f, 0.5f, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    prim.indices = {0, 1, 2};
    gl::Mesh mesh;
    mesh.name = "hd_body";
    mesh.primitives = {prim};
    doc.meshes = {mesh};
    const auto glb = gl::write_glb(doc);
    SH_CHECK(glb);
    if (!glb) {
        return;
    }
    const fs::path dir = fresh_dir("sh_assets_hd_rig");
    write_file(dir / "HD.glb", glb.value());
    std::vector<std::string> warnings;
    const auto model = sh::assets::load_skin_model(dir / "HD.glb", nullptr, warnings);
    SH_CHECK(model);
    if (!model) {
        std::printf("  %s\n", model.error().c_str());
        return;
    }
    const auto& m = model.value();
    SH_CHECK(m.vertices.size() == 3 && m.bone_count == 2 && m.groups.size() == 1 && m.groups[0].node == "hd_body");
    // Game axes: x = -256 x', y = -256 y'. Vertex 1 in bone 1's space: (0.25, 0.25) m -> (-64, -64).
    SH_CHECK(m.vertices[1].count == 1 && m.vertices[1].influence[0].bone == 1);
    SH_CHECK(std::abs(m.vertices[1].influence[0].local[0] + 64) < 1e-3f &&
             std::abs(m.vertices[1].influence[0].local[1] + 64) < 1e-3f);
    // The helper's vertex goes to bone 1 through bone 1's inverse bind.
    SH_CHECK(m.vertices[2].influence[0].bone == 1 && std::abs(m.vertices[2].influence[0].local[1] + 192) < 1e-3f);
    SH_CHECK(m.vertices[2].normal[2] > 0.99f);

    // Bone 1 turned 90 degrees about Z and one metre up (y = -256 in game axes).
    std::vector<sh::assets::BoneTransform> bones(2);
    bones[1].m = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    bones[1].t = {0, -256, 0};
    std::vector<float> xyz;
    std::vector<std::uint8_t> ok;
    m.pose(bones, std::vector<std::uint8_t>{1, 1}, xyz, ok);
    SH_CHECK(ok[0] && ok[1] && ok[2]);
    // Vertex 1: R * (-64, -64, 0) + t = (64, -320, 0).
    SH_CHECK(std::abs(xyz[3] - 64) < 1e-3f && std::abs(xyz[4] + 320) < 1e-3f);
    // Vertex 0: half bone 0 (-64, -128), half bone 1 (local (-64, 128) -> (-128, -320)).
    SH_CHECK(std::abs(xyz[0] + 96) < 1e-3f && std::abs(xyz[1] + 224) < 1e-3f);
    // Without bone 1, vertex 1 has no bone left and vertex 0 keeps bone 0 alone.
    m.pose(bones, std::vector<std::uint8_t>{1, 0}, xyz, ok);
    SH_CHECK(ok[0] && !ok[1] && std::abs(xyz[0] + 64) < 1e-3f && std::abs(xyz[1] + 128) < 1e-3f);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

SH_TEST(SkinModelWithItsOwnProportionsMovesTheJointsOfRestBones) {
    namespace gl = sh::gltf;
    const auto build = [](bool own) {
        gl::Document doc;
        if (own) {
            doc.extras = R"({"proportions": "model"})";
        }
        // bone00 (the character) -> bone01 (a root bone the clips move) -> bone02 -> bone03.
        gl::Node n0, n1, n2, n3, body;
        n0.name = "bone00";
        n0.children = {1};
        n1.name = "bone01";
        n1.children = {2};
        n2.name = "bone02";
        n2.children = {3};
        n3.name = "bone03";
        body.name = "body";
        body.mesh = 0;
        body.skin = 0;
        doc.nodes = {n0, n1, n2, n3, body};
        gl::Skin skin;
        skin.joints = {0, 1, 2, 3};
        // Bind pose: bone01 at (0, 1, 0) turned 90 degrees about Z, bone02 at (0.5, 1, 0), bone03 at (0.5, 1.25, 0).
        skin.inverse_bind = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                             {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, -1, 0, 0, 1},
                             {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -0.5f, -1, 0, 1},
                             {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -0.5f, -1.25f, 0, 1}};
        doc.skins = {skin};
        gl::Primitive prim;
        prim.positions = {0, 1, 0, 0.5f, 1, 0, 0.5f, 1.25f, 0};
        prim.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
        prim.texcoords = {0, 0, 1, 0, 0, 1};
        prim.joints = {1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0};
        prim.weights = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        prim.indices = {0, 1, 2};
        gl::Mesh mesh;
        mesh.name = "body";
        mesh.primitives = {prim};
        doc.meshes = {mesh};
        return gl::write_glb(doc);
    };
    const fs::path dir = fresh_dir("sh_assets_own_proportions");
    // The animation's skeleton: bone 2 rests 100 units along its parent's Y, bone 3 64 units above bone 2.
    std::vector<sh::assets::BoneRest> rest(4);
    rest[1].parent = 0;
    rest[2] = {1, false, {0, 100, 0}};
    rest[3] = {2, false, {0, -64, 0}};
    const auto turned = [](int bone, double out[9]) {
        // Bone 1 turned 90 degrees about Z in the pose, the others not at all (Q12).
        const double z[9] = {0, -4096, 0, 4096, 0, 0, 0, 0, 4096}, one[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
        std::copy_n(bone == 1 ? z : one, 9, out);
    };
    for (const bool own : {true, false}) {
        const auto glb = build(own);
        SH_CHECK(glb);
        if (!glb) {
            return;
        }
        const fs::path file = dir / (own ? "OWN.glb" : "PLAIN.glb");
        write_file(file, glb.value());
        std::vector<std::string> warnings;
        const auto model = sh::assets::load_skin_model(file, nullptr, warnings);
        SH_CHECK(model);
        if (!model) {
            std::printf("  %s\n", model.error().c_str());
            return;
        }
        const auto& m = model.value();
        SH_CHECK(m.own_proportions == own);
        std::vector<std::array<double, 3>> shift(4, {9.0, 9.0, 9.0});
        m.joint_shifts(rest.data(), 4, 4096.0, turned, shift.data());
        if (!own) {
            SH_CHECK(m.rest.empty());
            for (const auto& s : shift) {
                SH_CHECK(s[0] == 0.0 && s[1] == 0.0 && s[2] == 0.0);
            }
            continue;
        }
        // The file's joints seen from their parent bones, in game units (x = -256 x', y = -256 y'): bone 2 is half a
        // metre along bone 1's turned -Y, bone 3 a quarter of a metre above bone 2.
        SH_CHECK(m.has_rest.size() == 4 && m.has_rest[1] && m.has_rest[2] && m.has_rest[3] && !m.has_rest[0]);
        SH_CHECK(m.rest_parent[2] == 1 && m.rest_parent[3] == 2);
        SH_CHECK(std::abs(m.rest[2][0]) < 1e-3f && std::abs(m.rest[2][1] - 128) < 1e-3f);
        SH_CHECK(std::abs(m.rest[3][0]) < 1e-3f && std::abs(m.rest[3][1] + 64) < 1e-3f);
        // The root bone stays where the clips put it; bone 2 is 28 units further along its parent's Y, which the
        // pose turned onto -X; bone 3 has the animation's own offset and only follows its parent.
        SH_CHECK(shift[1][0] == 0.0 && shift[1][1] == 0.0);
        SH_CHECK(std::abs(shift[2][0] + 28) < 1e-3 && std::abs(shift[2][1]) < 1e-3);
        SH_CHECK(std::abs(shift[3][0] + 28) < 1e-3 && std::abs(shift[3][1]) < 1e-3);
        // A bone the clips move keeps the clips' translation whatever the file says.
        std::vector<sh::assets::BoneRest> moved = rest;
        moved[2].animated = true;
        m.joint_shifts(moved.data(), 4, 4096.0, turned, shift.data());
        SH_CHECK(shift[2][0] == 0.0 && shift[3][0] == 0.0);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
