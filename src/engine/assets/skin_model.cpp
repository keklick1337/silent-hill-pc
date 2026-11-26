#include "engine/assets/skin_model.h"
#include "engine/assets/texture_upscale.h"
#include "formats/png/palette_variant.h"

#include "formats/common/json.h"
#include "formats/gltf/gltf_skin_reader.h"
#include "formats/png/stb_png.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>
#include <iterator>
#include <map>
#include <optional>

namespace sh::assets {
namespace {

namespace fs = std::filesystem;

std::optional<std::int64_t> extras_int(const std::string& extras, std::string_view key) {
    if (extras.empty()) {
        return std::nullopt;
    }
    auto doc = sh::json::parse(extras);
    if (!doc) {
        return std::nullopt;
    }
    const sh::json::Value* v = doc.value().find(key);
    if (!v || !v->is_integer()) {
        return std::nullopt;
    }
    return v->integer();
}

bool extras_is(const std::string& extras, std::string_view key, std::string_view wanted) {
    if (extras.empty()) {
        return false;
    }
    auto doc = sh::json::parse(extras);
    if (!doc) {
        return false;
    }
    const sh::json::Value* v = doc.value().find(key);
    return v && v->is_string() && v->string() == wanted;
}

// The translation of the inverse of an affine column-major 4x4: where a joint sits in its bind pose, from its inverse
// bind matrix. False when the matrix has no inverse.
bool bind_position(const std::array<float, 16>& m, float out[3]) {
    const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12) {
        return false;
    }
    const double r[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                         (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (int k = 0; k < 3; ++k) {
        out[k] = static_cast<float>(-(r[k * 3] * m[12] + r[k * 3 + 1] * m[13] + r[k * 3 + 2] * m[14]));
    }
    return true;
}

bool extras_flag(const std::string& extras, std::string_view key) {
    if (extras.empty()) {
        return false;
    }
    auto doc = sh::json::parse(extras);
    if (!doc) {
        return false;
    }
    const sh::json::Value* v = doc.value().find(key);
    return v && v->is_bool() && v->boolean();
}

std::optional<std::vector<std::uint8_t>> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return out;
}

bool decode_rgba(const std::vector<std::uint8_t>& bytes, SkinTexture& out) {
    if (bytes.empty() || bytes.size() > 0x7FFFFFFF) {
        return false;
    }
    int w = 0, h = 0;
    unsigned char* px = sh_png_stb_load_rgba(bytes.data(), static_cast<int>(bytes.size()), &w, &h);
    if (!px) {
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    sh_png_stb_free(px);
    return true;
}

// The picture of a texture file: a palette variant is not a file but the base PNG with a CLUT row of the .tim.json
// (palette_variant.h); the upscaled picture when texture_upscale has one.
std::optional<std::vector<std::uint8_t>> texture_bytes(const fs::path& file) {
    auto bytes = sh::png::read_texture_or_variant(file.generic_string(),
                                                  [](const std::string& p) { return read_file(fs::path(p)); });
    if (bytes) {
        if (auto upscaled = sh::upscale::find(file.generic_string(), *bytes)) bytes = std::move(upscaled);
    }
    return bytes;
}

// Column-major 4x4 times a point / a direction.
std::array<double, 3> mul_point(const std::array<float, 16>& m, const float* p) {
    std::array<double, 3> o{};
    for (int r = 0; r < 3; ++r) {
        o[static_cast<std::size_t>(r)] = static_cast<double>(m[static_cast<std::size_t>(r)]) * p[0] +
                                         static_cast<double>(m[static_cast<std::size_t>(4 + r)]) * p[1] +
                                         static_cast<double>(m[static_cast<std::size_t>(8 + r)]) * p[2] +
                                         static_cast<double>(m[static_cast<std::size_t>(12 + r)]);
    }
    return o;
}

std::array<double, 3> mul_dir(const std::array<float, 16>& m, const float* p) {
    std::array<double, 3> o{};
    for (int r = 0; r < 3; ++r) {
        o[static_cast<std::size_t>(r)] = static_cast<double>(m[static_cast<std::size_t>(r)]) * p[0] +
                                         static_cast<double>(m[static_cast<std::size_t>(4 + r)]) * p[1] +
                                         static_cast<double>(m[static_cast<std::size_t>(8 + r)]) * p[2];
    }
    return o;
}

// glTF axes and metres -> game axes and units (the importer's x' = -x/256, y' = -y/256, z' = z/256 undone).
std::array<float, 3> to_game(const std::array<double, 3>& v, double scale) {
    return {static_cast<float>(-v[0] * scale), static_cast<float>(-v[1] * scale), static_cast<float>(v[2] * scale)};
}

}  // namespace

fs::path below_convert(const fs::path& path) {
    const fs::path norm = path.lexically_normal();
    std::vector<fs::path> parts(norm.begin(), norm.end());
    for (std::size_t i = parts.size(); i-- > 0;) {
        if (parts[i] == "convert") {
            fs::path rel;
            for (std::size_t k = i + 1; k < parts.size(); ++k) {
                rel /= parts[k];
            }
            return rel;
        }
    }
    return {};
}

int bone_of_joint_name(std::string_view name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.rfind("bone", 0) != 0) {
        return -1;
    }
    std::size_t i = 4;
    if (i < lower.size() && lower[i] == '_') {
        ++i;
    }
    if (i == lower.size() || lower.size() - i > 3) {
        return -1;
    }
    int value = 0;
    for (; i < lower.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(lower[i]))) {
            return -1;
        }
        value = value * 10 + (lower[i] - '0');
    }
    return value < 256 ? value : -1;
}

void SkinModel::pose(const std::vector<BoneTransform>& bones, const std::vector<std::uint8_t>& have,
                     std::vector<float>& xyz, std::vector<std::uint8_t>& ok) const {
    xyz.assign(vertices.size() * 3, 0.0f);
    ok.assign(vertices.size(), 0);
    pose(bones.data(), std::min(bones.size(), have.size()), have.data(), xyz.data(), ok.data());
}

void SkinModel::pose(const BoneTransform* bones, std::size_t bone_count, const std::uint8_t* have, float* xyz,
                     std::uint8_t* ok) const {
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        const SkinVertex& sv = vertices[v];
        double p[3] = {0, 0, 0};
        double ws = 0;
        for (int q = 0; q < sv.count; ++q) {
            const SkinInfluence& in = sv.influence[static_cast<std::size_t>(q)];
            const auto b = static_cast<std::size_t>(in.bone);
            if (b >= bone_count || !have[b]) {
                continue;
            }
            const BoneTransform& t = bones[b];
            for (int r = 0; r < 3; ++r) {
                const auto r3 = static_cast<std::size_t>(r * 3);
                p[r] += in.weight * (static_cast<double>(t.m[r3]) * in.local[0] + static_cast<double>(t.m[r3 + 1]) * in.local[1] +
                                     static_cast<double>(t.m[r3 + 2]) * in.local[2] + t.t[static_cast<std::size_t>(r)]);
            }
            ws += in.weight;
        }
        ok[v] = 0;
        if (ws < 1e-4) {
            continue;
        }
        for (int r = 0; r < 3; ++r) {
            xyz[v * 3 + static_cast<std::size_t>(r)] = static_cast<float>(p[r] / ws);
        }
        ok[v] = 1;
    }
}

void SkinModel::joint_shifts(const BoneRest* rest, int count, double unit,
                             const std::function<void(int bone, double out[9])>& rotation,
                             std::array<double, 3>* out) const {
    std::vector<std::uint8_t> done(static_cast<std::size_t>(count > 0 ? count : 0), 0);
    std::vector<int> chain;
    for (int b = 0; b < count; ++b) {
        // Parents first: up to the nearest finished bone, then back down.
        chain.clear();
        for (int k = b; k >= 0 && k < count && !done[static_cast<std::size_t>(k)] &&
                        static_cast<int>(chain.size()) < count;
             k = rest[k].parent) {
            chain.push_back(k);
        }
        while (!chain.empty()) {
            const int k = chain.back();
            chain.pop_back();
            const auto ki = static_cast<std::size_t>(k);
            const BoneRest& r = rest[k];
            const bool under = r.parent >= 0 && r.parent < count && done[static_cast<std::size_t>(r.parent)];
            done[ki] = 1;
            out[k] = under ? out[r.parent] : std::array<double, 3>{0.0, 0.0, 0.0};
            if (!own_proportions || r.animated || !under || ki >= has_rest.size() || !has_rest[ki]) {
                continue;
            }
            double rot[9];
            rotation(r.parent, rot);
            double delta[3];
            for (std::size_t c = 0; c < 3; ++c) {
                delta[c] = static_cast<double>(this->rest[ki][c]) - r.offset[c];
            }
            for (std::size_t row = 0; row < 3; ++row) {
                out[k][row] += (rot[row * 3] * delta[0] + rot[row * 3 + 1] * delta[1] + rot[row * 3 + 2] * delta[2]) / unit;
            }
        }
    }
}

bool read_skin_derive(const fs::path& manifest, SkinDerive& out, std::string& error) {
    std::ifstream in(manifest, std::ios::binary);
    if (!in) {
        error = "cannot be read";
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto doc = sh::json::parse(text);
    if (!doc) {
        error = doc.error();
        return false;
    }
    const sh::json::Value* derive = doc.value().is_object() ? doc.value().find("derive") : nullptr;
    if (derive == nullptr || !derive->is_object()) {
        error = "has no \"derive\" object";
        return false;
    }
    const sh::json::Value* from = derive->find("from");
    if (from == nullptr || !from->is_string() || from->string().empty() ||
        from->string().find("..") != std::string::npos) {
        error = "\"derive\" needs \"from\", a glb below convert/models/";
        return false;
    }
    out = SkinDerive{};
    out.from = from->string();
    if (out.from.starts_with("models/")) {
        out.from.erase(0, 7);
    }
    if (const sh::json::Value* p = derive->find("proportions"); p != nullptr && p->is_string()) {
        out.own_proportions = p->string() == "model";
    }
    if (const sh::json::Value* parts = derive->find("parts"); parts != nullptr && parts->is_object()) {
        for (const auto& [mesh, names] : parts->members()) {
            std::vector<std::string> list;
            if (names.is_string()) {
                list.push_back(names.string());
            } else if (names.is_array()) {
                for (const sh::json::Value& n : names.array()) {
                    if (n.is_string()) {
                        list.push_back(n.string());
                    }
                }
            } else if (!names.is_null()) {
                error = "\"derive\" \"parts\": \"" + mesh + "\" is a part name, a list of them or null";
                return false;
            }
            out.parts.emplace_back(mesh, std::move(list));
        }
    }
    return true;
}

Result<SkinModel> load_skin_model(const fs::path& file, const TextureResolver& resolve,
                                  std::vector<std::string>& warnings, const SkinDerive* derive) {
    using Out = Result<SkinModel>;
    auto gltf = sh::gltf::read_skinned_gltf(file);
    if (!gltf) {
        return Out::fail(gltf.error());
    }
    const sh::gltf::SkinnedGltf& g = gltf.value();

    // Joint -> game bone, and the inverse bind matrix the vertex is taken through.
    const std::size_t nj = g.joints.size();
    std::vector<int> own(nj, -1);
    for (std::size_t j = 0; j < nj; ++j) {
        own[j] = bone_of_joint_name(g.joints[j].name);
        if (own[j] < 0) {
            if (auto b = extras_int(g.joints[j].extras, "bone"); b && *b >= 0 && *b < 256) {
                own[j] = static_cast<int>(*b);
            }
        }
    }
    std::vector<int> bone(nj, -1);
    std::vector<int> through(nj, -1);  // the joint whose inverse bind applies (the joint itself or its ancestor)
    bool any = false;
    for (std::size_t j = 0; j < nj; ++j) {
        int k = static_cast<int>(j);
        for (int guard = 0; k >= 0 && own[static_cast<std::size_t>(k)] < 0 && guard < 1024; ++guard) {
            k = g.joints[static_cast<std::size_t>(k)].parent;
        }
        if (k >= 0 && own[static_cast<std::size_t>(k)] >= 0) {
            bone[j] = own[static_cast<std::size_t>(k)];
            through[j] = k;
            any = true;
        }
    }
    if (!any) {
        return Out::fail("no joint is a game bone (joint names boneNN, as the importer writes them)");
    }

    SkinModel model;
    if (extras_is(g.asset_extras, "proportions", "model") || (derive != nullptr && derive->own_proportions)) {
        model.own_proportions = true;
        for (std::size_t j = 0; j < nj; ++j) {
            int p = g.joints[j].parent;
            for (int guard = 0; p >= 0 && own[static_cast<std::size_t>(p)] < 0 && guard < 1024; ++guard) {
                p = g.joints[static_cast<std::size_t>(p)].parent;
            }
            float at[3];
            if (own[j] < 0 || p < 0 || !bind_position(g.joints[j].inverse_bind, at)) {
                continue;
            }
            const auto b = static_cast<std::size_t>(own[j]);
            if (model.rest.size() <= b) {
                model.rest.resize(b + 1);
                model.rest_parent.resize(b + 1, -1);
                model.has_rest.resize(b + 1, 0);
            }
            model.rest[b] = to_game(mul_point(g.joints[static_cast<std::size_t>(p)].inverse_bind, at), 256.0);
            model.rest_parent[b] = own[static_cast<std::size_t>(p)];
            model.has_rest[b] = 1;
        }
    }
    // Textures: one per distinct image, decoded once.
    std::map<std::string, int> texture_of;
    for (const sh::gltf::SkinMaterialData& m : g.materials) {
        SkinMaterial out;
        out.name = m.name;
        out.base_color = m.base_color;
        out.alpha = m.alpha == sh::gltf::SkinAlpha::Mask    ? SkinAlphaMode::Mask
                    : m.alpha == sh::gltf::SkinAlpha::Blend ? SkinAlphaMode::Blend
                                                            : SkinAlphaMode::Opaque;
        out.alpha_cutoff = m.alpha_cutoff;
        out.double_sided = m.double_sided;
        out.semi_transparent = extras_flag(m.extras, "semi_transparent");
        if (auto abr = extras_int(m.extras, "abr"); abr && *abr >= 0 && *abr <= 3) {
            out.abr = static_cast<int>(*abr);
        }
        if (!m.image_uri.empty()) {
            const fs::path resolved = (file.parent_path() / fs::path(m.image_uri)).lexically_normal();
            const fs::path chosen = resolve ? resolve(resolved) : resolved;
            const std::string key = chosen.generic_string();
            if (auto it = texture_of.find(key); it != texture_of.end()) {
                out.texture = it->second;
            } else {
                SkinTexture tex;
                tex.file = chosen;
                const auto bytes = texture_bytes(chosen);
                if (bytes && decode_rgba(*bytes, tex)) {
                    out.texture = static_cast<int>(model.textures.size());
                    model.textures.push_back(std::move(tex));
                } else {
                    warnings.push_back("material " + m.name + ": image " + chosen.generic_string() + " cannot be read");
                }
                texture_of.emplace(key, out.texture);
            }
        } else if (!m.image_bytes.empty()) {
            SkinTexture tex;
            if (decode_rgba(m.image_bytes, tex)) {
                out.texture = static_cast<int>(model.textures.size());
                model.textures.push_back(std::move(tex));
            } else {
                warnings.push_back("material " + m.name + ": the stored image cannot be decoded");
            }
        }
        model.materials.push_back(std::move(out));
    }

    constexpr double kUnits = 256.0;  // game units per metre
    for (const sh::gltf::SkinPrimitiveData& p : g.primitives) {
        const std::size_t n = p.positions.size() / 3;
        const auto base = static_cast<std::uint32_t>(model.vertices.size());
        for (std::size_t v = 0; v < n; ++v) {
            SkinVertex sv;
            // Weights of joints on the same bone add up; the bind-local position is the same for them only when
            // they share the inverse bind, so each (bone, joint) keeps its own influence.
            float best = -1.0f;
            for (int k = 0; k < 4; ++k) {
                const float w = p.weights[v * 4 + static_cast<std::size_t>(k)];
                const std::uint16_t j = p.joints[v * 4 + static_cast<std::size_t>(k)];
                if (!(w > 0.0f) || bone[j] < 0) {
                    continue;
                }
                const auto& ibm = g.joints[static_cast<std::size_t>(through[j])].inverse_bind;
                SkinInfluence in;
                in.bone = bone[j];
                in.weight = w;
                in.local = to_game(mul_point(ibm, &p.positions[v * 3]), kUnits);
                sv.influence[static_cast<std::size_t>(sv.count++)] = in;
                model.bone_count = std::max(model.bone_count, in.bone + 1);
                if (w > best) {
                    best = w;
                    sv.dominant = in.bone;
                    if (!p.normals.empty()) {
                        const auto d = mul_dir(ibm, &p.normals[v * 3]);
                        const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                        // The importer's normals are the ILM's s8 / 128, not unit: kept as they are, the lighting
                        // gets the game's own magnitudes. Only an odd length (an HD file's scaled bind) is fixed.
                        if (len > 1e-9) {
                            const double k = len > 1.05 || len < 0.5 ? 1.0 / len : 1.0;
                            sv.normal = to_game({d[0] * k, d[1] * k, d[2] * k}, 1.0);
                        }
                    }
                }
            }
            if (!p.uvs.empty()) {
                sv.uv = {p.uvs[v * 2], p.uvs[v * 2 + 1]};
            }
            model.vertices.push_back(sv);
        }
        SkinGroup group;
        group.node = p.node;
        if (auto part = extras_int(p.node_extras, "part_index"); part && *part >= 0 && *part < 256) {
            group.part = static_cast<int>(*part);
        }
        group.material = p.material;
        group.indices.reserve(p.indices.size());
        for (const std::uint32_t i : p.indices) {
            group.indices.push_back(base + i);
        }
        model.groups.push_back(std::move(group));
    }
    if (derive != nullptr) {
        // The part numbers are the ones of the model the file was made for: the wearer's parts go by name.
        std::vector<SkinGroup> groups;
        for (SkinGroup& group : model.groups) {
            group.part = -1;
            const auto named = std::find_if(derive->parts.begin(), derive->parts.end(),
                                            [&](const auto& entry) { return entry.first == group.node; });
            if (named == derive->parts.end()) {
                groups.push_back(std::move(group));
                continue;
            }
            for (const std::string& part : named->second) {
                SkinGroup copy = group;
                copy.node = part;
                groups.push_back(std::move(copy));
            }
        }
        model.groups = std::move(groups);
    }
    return Out::ok(std::move(model));
}

bool reload_skin_texture(SkinTexture& tex) {
    if (tex.file.empty()) return false;
    const auto bytes = texture_bytes(tex.file);
    SkinTexture fresh;
    if (!bytes || !decode_rgba(*bytes, fresh)) return false;
    tex.width = fresh.width;
    tex.height = fresh.height;
    tex.rgba = std::move(fresh.rgba);
    return true;
}

}  // namespace sh::assets
