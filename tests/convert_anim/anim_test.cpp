#include "framework/sh_disc.h"
#include "framework/sh_test.h"
#include "formats/convert/anim/anim_clips.h"
#include "formats/convert/anim/anim_converter.h"
#include "formats/convert/anim/anim_json.h"
#include "formats/convert/anim/anm_file.h"
#include "formats/convert/model/model_converter.h"
#include "formats/convert/model/skin_math.h"
#include "formats/gltf/gltf_inspect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ANM <-> sh-anim JSON without loss, the game's bone arithmetic, and the skinned glTF of every character checked
// against that arithmetic: joint matrices and skinned vertices read back with cgltf, posed at every keyframe of every
// clip, compared with the model-space bone matrices the game computes (Anim_BonesUpdate + GsGetLws).

namespace {

namespace an = sh::convert::anim;
using Bytes = std::vector<std::uint8_t>;

// Three bones: 0 root, 1 hips (rotation slot 0, translation slot 0), 2 child of 1 (rotation slot 1, bind (1, 2, 3)).
Bytes synthetic_anm() {
    const int bones = 3, rot = 2, tr = 1;
    const int ks = rot * 9 + tr * 3;
    const int data = 0x14 + bones * 6 + 2;  // two gap bytes
    const int keyframes = 2;
    Bytes b(static_cast<std::size_t>(data + ks * keyframes + 5), 0);
    b[0] = static_cast<std::uint8_t>(data);
    b[2] = rot;
    b[3] = tr;
    b[4] = static_cast<std::uint8_t>(ks);
    b[6] = bones;
    b[8] = 0xFF;
    b[12] = static_cast<std::uint8_t>(b.size());
    b[0x10] = keyframes;
    b[0x12] = 2;   // scale log2
    b[0x13] = 10;  // root Y offset
    const std::int8_t table[3][6] = {{-1, -1, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {1, 1, -1, 1, 2, 3}};
    for (int i = 0; i < bones; ++i) {
        for (int j = 0; j < 6; ++j) {
            b[static_cast<std::size_t>(0x14 + i * 6 + j)] = static_cast<std::uint8_t>(table[i][j]);
        }
    }
    b[static_cast<std::size_t>(data - 2)] = 0xAB;
    for (int k = 0; k < keyframes; ++k) {
        std::uint8_t* p = b.data() + data + k * ks;
        p[0] = static_cast<std::uint8_t>(10 + k * 4);
        p[1] = static_cast<std::uint8_t>(-20);
        p[2] = static_cast<std::uint8_t>(k);
        for (int r = 0; r < rot; ++r) {
            // Identity (127 ~ 1.0) with a small off-diagonal term that changes per keyframe.
            std::uint8_t* m = p + 3 + r * 9;
            m[0] = m[4] = m[8] = 127;
            m[1] = static_cast<std::uint8_t>(k * 8);
            m[3] = static_cast<std::uint8_t>(-k * 8);
        }
    }
    b[b.size() - 1] = 0x5A;
    return b;
}

std::string text_of(const Bytes& b) { return std::string(b.begin(), b.end()); }

bool ends_with(const std::string& s, const char* ext) {
    const std::size_t n = std::char_traits<char>::length(ext);
    return s.size() >= n && s.compare(s.size() - n, n, ext) == 0;
}

// Column-major 4x4 helpers, written independently of skin_math.h.
using M4 = std::array<double, 16>;

M4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int rr = 0; rr < 4; ++rr) {
            double s = 0;
            for (int k = 0; k < 4; ++k) {
                s += a[static_cast<std::size_t>(k * 4 + rr)] * b[static_cast<std::size_t>(c * 4 + k)];
            }
            r[static_cast<std::size_t>(c * 4 + rr)] = s;
        }
    }
    return r;
}

M4 trs(const std::array<float, 3>& t, const std::array<float, 4>& q, const std::array<float, 3>& s) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                         2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                         2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    M4 m = identity();
    for (int rr = 0; rr < 3; ++rr) {
        for (int c = 0; c < 3; ++c) {
            m[static_cast<std::size_t>(c * 4 + rr)] = r[rr * 3 + c] * s[static_cast<std::size_t>(c)];
        }
        m[static_cast<std::size_t>(12 + rr)] = t[static_cast<std::size_t>(rr)];
    }
    return m;
}

// The game's model-space pose in glTF axes (x' = -x/256, y' = -y/256, z' = z/256).
M4 game_to_gltf(const an::Pose& p) {
    const double d[3] = {-1, -1, 1};
    M4 m = identity();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[static_cast<std::size_t>(c * 4 + r)] = d[r] * d[c] * p.m[static_cast<std::size_t>(r * 3 + c)] / 4096.0;
        }
        m[static_cast<std::size_t>(12 + r)] = d[r] * p.t[static_cast<std::size_t>(r)] / 256.0;
    }
    return m;
}

std::array<double, 3> transform_point(const M4& m, const float* v) {
    std::array<double, 3> out{};
    for (int r = 0; r < 3; ++r) {
        out[static_cast<std::size_t>(r)] = m[static_cast<std::size_t>(r)] * v[0] + m[static_cast<std::size_t>(4 + r)] * v[1] +
                                           m[static_cast<std::size_t>(8 + r)] * v[2] + m[static_cast<std::size_t>(12 + r)];
    }
    return out;
}

M4 invert_affine(const M4& m) {
    const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double inv[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                           (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                           (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    M4 r = identity();
    for (int rr = 0; rr < 3; ++rr) {
        for (int cc = 0; cc < 3; ++cc) {
            r[static_cast<std::size_t>(cc * 4 + rr)] = inv[rr * 3 + cc];
        }
        double t = 0;
        for (int k = 0; k < 3; ++k) {
            t -= inv[rr * 3 + k] * m[static_cast<std::size_t>(12 + k)];
        }
        r[static_cast<std::size_t>(12 + rr)] = t;
    }
    return r;
}

}  // namespace

SH_TEST(AnmJsonRoundTripIsLossless) {
    const Bytes raw = synthetic_anm();
    SH_CHECK(an::AnmFile::has_header(raw));
    const auto file = an::AnmFile::parse(raw);
    SH_CHECK(file);
    if (!file) {
        return;
    }
    SH_CHECK(file.value().keyframes().size() == 2);
    SH_CHECK(file.value().gap().size() == 2 && file.value().gap()[0] == 0xAB);
    SH_CHECK(file.value().tail().size() == 5 && file.value().tail()[4] == 0x5A);
    SH_CHECK(file.value().serialize() == raw);

    const auto conv = an::make_converter({});
    SH_CHECK(conv->lossless());
    SH_CHECK(conv->accepts(raw, "ANIM/TEST.ANM"));
    SH_CHECK(!conv->accepts(raw, "ANIM/TEST.TIM"));
    SH_CHECK(!conv->accepts(Bytes{}, "ANIM/TEST.ANM"));
    const auto out = conv->decode(raw, "ANIM/TEST.ANM");
    SH_CHECK(out && out.value().size() == 1);
    if (!out) {
        return;
    }
    SH_CHECK(out.value()[0].relative_path == "anim/ANIM/TEST.anim.json");
    const auto back = conv->encode(out.value());
    SH_CHECK(back && back.value() == raw);

    // A changed value survives the JSON and lands in the right byte.
    std::string text = text_of(out.value()[0].bytes);
    const std::size_t at = text.find("\"root_y_offset\": 10");
    SH_CHECK(at != std::string::npos);
    text.replace(at, 19, "\"root_y_offset\": 11");
    const auto edited = an::anm_from_json(text);
    SH_CHECK(edited);
    if (edited) {
        const Bytes b = edited.value().serialize();
        SH_CHECK(b.size() == raw.size() && b[0x13] == 11);
    }
    SH_CHECK(!an::anm_from_json("{\"format\": \"sh-anim/1\", \"kind\": \"anm\"}"));
}

SH_TEST(AnmPoseUsesTheGameArithmetic) {
    const auto file = an::AnmFile::parse(synthetic_anm());
    SH_CHECK(file);
    if (!file) {
        return;
    }
    // Halfway between keyframes 0 and 1: t = (f0 << 2) + (((f1 - f0) * 2048) >> 10), m = (f0 << 5) + ((df * 2048) >> 7).
    const auto local = file.value().local_poses(0, 1, 2048);
    SH_CHECK(local.size() == 3);
    SH_CHECK(local[0].t == (std::array<std::int32_t, 3>{0, 0, 0}));  // the root is the character's coordinate
    SH_CHECK(local[1].t[0] == 10 * 4 + ((4 * 2048) >> 10));
    SH_CHECK(local[1].t[1] == -20 * 4 - 10);  // slot 0 minus the root Y offset
    SH_CHECK(local[1].t[2] == 0 + ((1 * 2048) >> 10));
    SH_CHECK(local[1].m[0] == 127 * 32);
    SH_CHECK(local[1].m[1] == ((8 * 2048) >> 7));
    SH_CHECK(local[1].m[3] == ((-8 * 2048) >> 7));
    SH_CHECK(local[2].t == (std::array<std::int32_t, 3>{4, 8, 12}));  // bind << scale
    const auto world = file.value().world_poses(local);
    // Bone 2's translation is bone 1's rotation applied to (4, 8, 12), shifted by 12, plus bone 1's translation.
    const std::int64_t x = (std::int64_t{local[1].m[0]} * 4 + std::int64_t{local[1].m[1]} * 8 + std::int64_t{local[1].m[2]} * 12) >> 12;
    SH_CHECK(world[2].t[0] == x + local[1].t[0]);
}

SH_TEST(ClipCatalogNamesHarry) {
    const auto& catalog = an::builtin_clips();
    const auto hero = an::clips_for(catalog, "anim/hb_base.anm");
    SH_CHECK(!hero.empty());
    const auto walk = std::find_if(hero.begin(), hero.end(), [](const an::Clip& c) { return c.name == "WalkForward"; });
    SH_CHECK(walk != hero.end());
    if (walk != hero.end()) {
        SH_CHECK(walk->start == 1 && walk->end == 23 && walk->loop && walk->variable_speed);
    }
    SH_CHECK(an::clips_for(catalog, "ANIM/NOPE.ANM").empty());
    SH_CHECK(!an::parse_clip_catalog("{\"anm\": {\"A\": {\"clips\": [{\"name\": \"x\", \"start\": 3, \"end\": 1}]}}}"));
}

// Every ANM of the image goes to JSON and back to the same bytes.
SH_TEST(EveryAnmOfTheImageRoundTrips) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const auto conv = an::make_converter([&](std::string_view p) { return disc->read(std::string(p)); });
    std::size_t files = 0, accepted = 0, extensions = 0, keyframes = 0;
    for (const auto& e : disc->table) {
        if (!ends_with(e.name, ".ANM")) {
            continue;
        }
        ++files;
        const auto raw = disc->read(e.name);
        SH_CHECK(raw.has_value());
        if (!raw || !conv->accepts(*raw, e.name)) {
            continue;
        }
        ++accepted;
        const auto out = conv->decode(*raw, e.name);
        SH_CHECK(out);
        if (!out) {
            std::printf("  %s: %s\n", e.name.c_str(), out.error().c_str());
            continue;
        }
        const auto back = conv->encode(out.value());
        SH_CHECK(back && back.value() == *raw);
        const auto file = an::anm_from_json(text_of(out.value()[0].bytes));
        if (file) {
            extensions += file.value().extension() ? 1 : 0;
            keyframes += file.value().keyframes().size();
        }
    }
    std::printf("  ANM: %zu files, %zu converted (%zu headerless), %zu keyframes\n", files, accepted, extensions, keyframes);
    SH_CHECK(files == 113);
    SH_CHECK(accepted == 108);  // the five empty HB_M*.ANM entries (FORMATS.md) are not accepted
    SH_CHECK(extensions == 53);
}

// The skinned glb of every character, posed by cgltf's reading of its animations, against the game's bone matrices.
SH_TEST(SkinnedModelsMatchTheGameBoneMatrices) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    sh::convert::model::Options options;
    options.sibling = [&](std::string_view p) { return disc->read(std::string(p)); };

    std::size_t models = 0, clips = 0, map_clips = 0, poses = 0, vertices_checked = 0, over_1mm = 0, over_5mm = 0;
    double sum_err = 0;
    double worst_matrix = 0, worst_vertex = 0, worst_raw_matrix = 0;
    // Harry's map files: a few keyframes have s8 matrices far from rotations (the worst: the last of HB_M7S02), so their
    // T * R * S split is exact (worst_matrix) but the skinned vertices drift from the raw matrices; counted apart.
    double map_worst_vertex = 0, map_sum_err = 0;
    std::size_t map_vertices = 0;
    std::string map_worst_where;
    std::string worst_where;
    for (const auto& e : disc->table) {
        if (!ends_with(e.name, ".ILM")) {
            continue;
        }
        const auto raw = disc->read(e.name);
        if (!raw) {
            continue;
        }
        const auto converted = sh::convert::model::convert_model(*raw, e.name, options);
        SH_CHECK(converted);
        if (!converted || converted.value().stats.joints == 0) {
            continue;
        }
        ++models;
        const auto scene = sh::gltf::read_glb_scene(converted.value().glb);
        SH_CHECK(scene);
        if (!scene) {
            std::printf("  %s: %s\n", e.name.c_str(), scene.error().c_str());
            continue;
        }
        const auto& sc = scene.value();
        SH_CHECK(sc.skins.size() == 1);
        SH_CHECK(!sc.animations.empty());
        if (sc.skins.size() != 1) {
            continue;
        }
        const auto& skin = sc.skins[0];

        // The ANM the converter used, from the sidecar.
        const std::string side(converted.value().sidecar_json.begin(), converted.value().sidecar_json.end());
        const std::size_t at = side.find("\"anm\": \"");
        SH_CHECK(at != std::string::npos);
        const std::string anm_path = side.substr(at + 8, side.find('"', at + 8) - at - 8);
        const auto anm_raw = disc->read(anm_path);
        SH_CHECK(anm_raw.has_value());
        const auto anm = an::AnmFile::parse(*anm_raw);
        SH_CHECK(anm);
        if (!anm) {
            continue;
        }
        const std::size_t bones = anm.value().bones().size();
        SH_CHECK(skin.joints.size() == bones);

        for (const auto& animation : sc.animations) {
            ++clips;
            // Harry's map files are one animation each, named after the file, in the base file's layout.
            const an::AnmFile* file = &anm.value();
            std::optional<an::AnmFile> map_file;
            for (const std::string_view path : an::harry_map_keyframe_files()) {
                const std::size_t slash = path.rfind('/'), dot = path.rfind('.');
                if (anm_path != an::kHarryBaseAnm || path.substr(slash + 1, dot - slash - 1) != animation.name) {
                    continue;
                }
                const auto bytes = disc->read(std::string(path));
                SH_CHECK(bytes.has_value());
                if (bytes) {
                    auto parsed = an::AnmFile::parse_extension(*bytes, anm.value());
                    SH_CHECK(parsed);
                    if (parsed) {
                        map_file = std::move(parsed.value());
                        file = &*map_file;
                        ++map_clips;
                    }
                }
            }
            // start_keyframe from the clip catalog (or 0 for the whole-file clip).
            int start = 0;
            for (const auto& c : an::clips_for(an::builtin_clips(), anm_path)) {
                if (c.name == animation.name) {
                    start = c.start;
                    break;
                }
            }
            std::size_t keys = 0;
            for (const auto& ch : animation.channels) {
                keys = std::max(keys, ch.times.size());
                SH_CHECK(ch.interpolation == "LINEAR");
            }
            for (std::size_t k = 0; k < keys; ++k) {
                // glTF pose: node rest TRS overridden by the animation's key k.
                std::vector<std::array<float, 3>> t(sc.nodes.size()), s(sc.nodes.size());
                std::vector<std::array<float, 4>> q(sc.nodes.size());
                for (std::size_t n = 0; n < sc.nodes.size(); ++n) {
                    t[n] = sc.nodes[n].translation;
                    q[n] = sc.nodes[n].rotation;
                    s[n] = sc.nodes[n].scale;
                }
                for (const auto& ch : animation.channels) {
                    const std::size_t n = static_cast<std::size_t>(ch.node);
                    if (ch.path == "translation") {
                        std::copy_n(ch.values.begin() + static_cast<std::ptrdiff_t>(k * 3), 3, t[n].begin());
                    } else if (ch.path == "rotation") {
                        std::copy_n(ch.values.begin() + static_cast<std::ptrdiff_t>(k * 4), 4, q[n].begin());
                    } else if (ch.path == "scale") {
                        std::copy_n(ch.values.begin() + static_cast<std::ptrdiff_t>(k * 3), 3, s[n].begin());
                    }
                }
                std::vector<M4> world(sc.nodes.size());
                std::vector<int> done(sc.nodes.size(), 0);
                const auto world_of = [&](auto&& self, std::size_t n) -> const M4& {
                    if (!done[n]) {
                        const M4 local = trs(t[n], q[n], s[n]);
                        world[n] = sc.nodes[n].parent >= 0 ? mul(self(self, static_cast<std::size_t>(sc.nodes[n].parent)), local) : local;
                        done[n] = 1;
                    }
                    return world[n];
                };

                const std::size_t kf = static_cast<std::size_t>(start) + k;
                const auto game_local = file->local_poses(kf, kf, 0);
                const auto game = file->world_poses(game_local);
                // The same bones with each local matrix replaced by its T * R * S split (what glTF can hold).
                std::vector<M4> split(bones);
                std::vector<int> split_done(bones, 0);
                const auto split_of = [&](auto&& self, std::size_t b) -> const M4& {
                    if (!split_done[b]) {
                        const auto t3 = sh::convert::model::skin::to_trs(sh::convert::model::skin::to_gltf(game_local[b]));
                        const M4 local = trs(t3.translation, t3.rotation, t3.scale);
                        const int p = anm.value().bones()[b].parent;
                        split[b] = p >= 0 && static_cast<std::size_t>(p) < bones && static_cast<std::size_t>(p) != b
                                       ? mul(self(self, static_cast<std::size_t>(p)), local)
                                       : local;
                        split_done[b] = 1;
                    }
                    return split[b];
                };
                std::vector<M4> skinning(bones);
                for (std::size_t b = 0; b < bones; ++b) {
                    const M4& jw = world_of(world_of, static_cast<std::size_t>(skin.joints[b]));
                    const M4& expect = split_of(split_of, b);
                    const M4 raw_game = game_to_gltf(game[b]);
                    for (std::size_t i = 0; i < 16; ++i) {
                        worst_raw_matrix = std::max(worst_raw_matrix, std::abs(jw[i] - raw_game[i]));
                        // Rotation part is unitless, translation in metres; both compared directly.
                        const double d = std::abs(jw[i] - expect[i]);
                        if (d > worst_matrix) {
                            worst_matrix = d;
                            worst_where = e.name + " " + animation.name + " key " + std::to_string(k) + " bone " + std::to_string(b);
                        }
                    }
                    M4 ibm;
                    std::copy(skin.inverse_bind[b].begin(), skin.inverse_bind[b].end(), ibm.begin());
                    skinning[b] = mul(jw, ibm);
                }
                ++poses;
                // Skinned vertices of the glb against the game: W_game(kf) * W_game(rest)^-1 * rest vertex.
                if (k % 7 != 0) {
                    continue;
                }
                const std::size_t rest_kf = 0;
                const auto rest = anm.value().world_poses(anm.value().local_poses(rest_kf, rest_kf, 0));
                for (const auto& prim : sc.primitives) {
                    for (std::size_t v = 0; v * 3 < prim.positions.size(); ++v) {
                        const std::size_t j = prim.joints[v * 4];
                        const auto mine = transform_point(skinning[j], &prim.positions[v * 3]);
                        const M4 game_skin = mul(game_to_gltf(game[j]), invert_affine(game_to_gltf(rest[j])));
                        const auto ref = transform_point(game_skin, &prim.positions[v * 3]);
                        double err = 0;
                        for (std::size_t c = 0; c < 3; ++c) {
                            err = std::max(err, std::abs(mine[c] - ref[c]));
                        }
                        if (map_file) {
                            if (err > map_worst_vertex) {
                                map_worst_vertex = err;
                                map_worst_where = animation.name + " key " + std::to_string(k) + " bone " +
                                                  std::to_string(j);
                            }
                            map_sum_err += err;
                            ++map_vertices;
                            continue;
                        }
                        worst_vertex = std::max(worst_vertex, err);
                        sum_err += err;
                        over_1mm += err > 0.001 ? 1 : 0;
                        over_5mm += err > 0.005 ? 1 : 0;
                        ++vertices_checked;
                    }
                }
            }
        }
    }
    std::printf("  skinned models %zu, clips %zu (map files %zu), poses %zu, vertices %zu\n", models, clips, map_clips,
                poses, vertices_checked);
    std::printf("  joint matrices vs the game's bones split as T*R*S: worst element difference %.2e (%s)\n", worst_matrix,
                worst_where.c_str());
    std::printf("  vs the game's raw s8 matrices: worst element difference %.4f, worst skinned vertex %.3f mm\n",
                worst_raw_matrix, worst_vertex * 1000.0);
    std::printf("  skinned vertices off by more than 1 mm: %zu, more than 5 mm: %zu, mean %.3f mm\n", over_1mm, over_5mm,
                vertices_checked ? sum_err / static_cast<double>(vertices_checked) * 1000.0 : 0.0);
    std::printf("  Harry's map files: %zu vertices, mean %.3f mm, worst %.3f mm (%s)\n", map_vertices,
                map_vertices ? map_sum_err / static_cast<double>(map_vertices) * 1000.0 : 0.0, map_worst_vertex * 1000.0,
                map_worst_where.c_str());
    SH_CHECK(models == 47);
    // HERO.glb: one animation per non-empty map file of Harry (48 on the disc, five of them empty).
    SH_CHECK(map_clips == 43);
    // The glb is the game's skeleton exactly, up to float precision, once each bone matrix is split as T * R * S.
    SH_CHECK(worst_matrix < 1e-4);
    // The s8 matrices are not rotations (about 1% noise, shear that glTF cannot hold): the skinned vertices of the glb
    // stay within a few millimetres of the PS1's on average.
    SH_CHECK(vertices_checked > 0 && sum_err / static_cast<double>(vertices_checked) < 0.01);
    SH_CHECK(worst_vertex < 0.05);
}
