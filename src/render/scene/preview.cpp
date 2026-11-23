// Model previews for the UI (preview.h).

#include "render/scene/preview.h"

#include "formats/gltf/gltf_skin_reader.h"
#include "formats/png/stb_png.h"
#include "render/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sh::scene {
namespace {

constexpr float kUnitsPerMetre = 256.0f;
// Posed-mesh texture keys of previews: a tag in the top byte keeps them apart from the item keys (TIM page and
// CLUT) and the world characters' keys (a pointer shifted by 8 bits, below 2^56).
constexpr std::uint64_t kKeyTag = 0xF7ull << 56;

std::uint64_t path_hash(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace

Result<std::shared_ptr<const PreviewModel>> load_preview_model(const std::filesystem::path& glb,
                                                               const PreviewImageReader& read_image,
                                                               const std::array<float, 3>& rotate_deg) {
    using Out = Result<std::shared_ptr<const PreviewModel>>;
    auto gltf = sh::gltf::read_skinned_gltf(glb);
    if (!gltf) return Out::fail(gltf.error());
    const sh::gltf::SkinnedGltf& g = gltf.value();

    auto model = std::make_shared<PreviewModel>();
    const std::uint64_t key_base = kKeyTag | ((path_hash(glb.generic_string()) & 0xFFFFFFFFFFull) << 8);

    // One texture key per distinct image, registered with the scene once.
    struct Material {
        std::uint64_t key = 0;
        bool blended = false;
        std::array<float, 3> color{1, 1, 1};
    };
    std::vector<Material> materials;
    std::vector<std::string> images;
    auto register_image = [&](const std::string& id, const std::vector<std::uint8_t>& bytes) -> std::uint64_t {
        for (std::size_t i = 0; i < images.size(); ++i)
            if (images[i] == id) return key_base | (i + 1);
        if (images.size() >= 255 || bytes.empty() || bytes.size() > 0x7FFFFFFF) return 0;
        int w = 0, h = 0;
        unsigned char* px = sh_png_stb_load_rgba(bytes.data(), static_cast<int>(bytes.size()), &w, &h);
        if (px == nullptr) return 0;
        images.push_back(id);
        const std::uint64_t key = key_base | images.size();
        add_prelit_texture(key, w, h, px);
        sh_png_stb_free(px);
        return key;
    };
    for (std::size_t mi = 0; mi < g.materials.size(); ++mi) {
        const sh::gltf::SkinMaterialData& m = g.materials[mi];
        Material out;
        out.blended = m.alpha == sh::gltf::SkinAlpha::Blend;
        out.color = {m.base_color[0], m.base_color[1], m.base_color[2]};
        if (!m.image_uri.empty()) {
            const std::filesystem::path resolved = (glb.parent_path() / std::filesystem::path(m.image_uri)).lexically_normal();
            if (read_image) {
                if (auto bytes = read_image(resolved)) out.key = register_image(resolved.generic_string(), *bytes);
            }
        } else if (!m.image_bytes.empty()) {
            out.key = register_image("#" + std::to_string(mi), m.image_bytes);
        }
        materials.push_back(out);
    }

    // The data's turn, x then y then z: one matrix for the positions and the normals.
    std::array<std::array<float, 3>, 3> turn{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    {
        constexpr float kRadPerDeg = 3.14159265f / 180.0f;
        const float cx = std::cos(rotate_deg[0] * kRadPerDeg), sx = std::sin(rotate_deg[0] * kRadPerDeg);
        const float cy = std::cos(rotate_deg[1] * kRadPerDeg), sy = std::sin(rotate_deg[1] * kRadPerDeg);
        const float cz = std::cos(rotate_deg[2] * kRadPerDeg), sz = std::sin(rotate_deg[2] * kRadPerDeg);
        const std::array<std::array<float, 3>, 3> rx{{{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}}};
        const std::array<std::array<float, 3>, 3> ry{{{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}};
        const std::array<std::array<float, 3>, 3> rz{{{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}}};
        auto mul = [](const std::array<std::array<float, 3>, 3>& a, const std::array<std::array<float, 3>, 3>& b) {
            std::array<std::array<float, 3>, 3> m{};
            for (std::size_t i = 0; i < 3; ++i)
                for (std::size_t j = 0; j < 3; ++j)
                    for (std::size_t k = 0; k < 3; ++k) m[i][j] += a[i][k] * b[k][j];
            return m;
        };
        turn = mul(rz, mul(ry, rx));
    }
    auto turned = [&turn](const std::array<float, 3>& v) {
        return std::array<float, 3>{turn[0][0] * v[0] + turn[0][1] * v[1] + turn[0][2] * v[2],
                                    turn[1][0] * v[0] + turn[1][1] * v[1] + turn[1][2] * v[2],
                                    turn[2][0] * v[0] + turn[2][1] * v[1] + turn[2][2] * v[2]};
    };

    std::array<float, 3> lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
    std::array<float, 3> hi{-lo[0], -lo[1], -lo[2]};
    for (const sh::gltf::SkinPrimitiveData& p : g.primitives) {
        const std::size_t n = p.positions.size() / 3;
        const auto base = static_cast<std::uint32_t>(model->positions.size());
        for (std::size_t v = 0; v < n; ++v) {
            // glTF (metres, rotated 180 degrees about Z by the importer) to the game's axes.
            const std::array<float, 3> q = turned({-p.positions[v * 3] * kUnitsPerMetre,
                                                   -p.positions[v * 3 + 1] * kUnitsPerMetre,
                                                   p.positions[v * 3 + 2] * kUnitsPerMetre});
            for (int k = 0; k < 3; ++k) {
                lo[static_cast<std::size_t>(k)] = std::min(lo[static_cast<std::size_t>(k)], q[static_cast<std::size_t>(k)]);
                hi[static_cast<std::size_t>(k)] = std::max(hi[static_cast<std::size_t>(k)], q[static_cast<std::size_t>(k)]);
            }
            model->positions.push_back(q);
            std::array<float, 3> nrm{0, 0, -1};
            if (p.normals.size() >= (v + 1) * 3) {
                const float x = -p.normals[v * 3], y = -p.normals[v * 3 + 1], z = p.normals[v * 3 + 2];
                const float len = std::sqrt(x * x + y * y + z * z);
                if (len > 1e-6f) nrm = turned({x / len, y / len, z / len});
            }
            model->normals.push_back(nrm);
            model->uvs.push_back(p.uvs.size() >= (v + 1) * 2 ? std::array<float, 2>{p.uvs[v * 2], p.uvs[v * 2 + 1]}
                                                             : std::array<float, 2>{0, 0});
        }
        PreviewModel::Group group;
        if (p.material >= 0 && static_cast<std::size_t>(p.material) < materials.size()) {
            const Material& m = materials[static_cast<std::size_t>(p.material)];
            group.texture_key = m.key;
            group.blended = m.blended;
            group.color = m.color;
        }
        group.indices.reserve(p.indices.size());
        for (std::uint32_t i : p.indices)
            if (i < n) group.indices.push_back(base + i);
        if (group.indices.size() >= 3) model->groups.push_back(std::move(group));
    }
    if (model->positions.empty() || model->groups.empty()) return Out::fail("no triangles");

    const std::array<float, 3> centre{(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    float rxz = 0;
    for (auto& q : model->positions) {
        for (int k = 0; k < 3; ++k) q[static_cast<std::size_t>(k)] -= centre[static_cast<std::size_t>(k)];
        rxz = std::max(rxz, std::sqrt(q[0] * q[0] + q[2] * q[2]));
    }
    model->radius_xz = std::max(rxz, 1.0f);
    model->half_height = std::max((hi[1] - lo[1]) * 0.5f, 1.0f);
    // Blended surfaces after the opaque ones, as their pipeline does not write depth.
    std::stable_sort(model->groups.begin(), model->groups.end(),
                     [](const PreviewModel::Group& a, const PreviewModel::Group& b) { return !a.blended && b.blended; });
    return Out::ok(std::move(model));
}

std::vector<int> add_preview(const PreviewModel& model, const PreviewPlacement& where, float yaw, float zoom,
                             float lift) {
    std::vector<int> out;
    if (where.w <= 1 || where.h <= 1 || model.groups.empty()) return out;
    const float ys = where.y_scale > 0 ? where.y_scale : 1.0f;
    const float radius = std::sqrt(model.radius_xz * model.radius_xz + model.half_height * model.half_height);
    // A long lens: little perspective, and the nearest point stays well past the scene's near plane.
    const float dist = radius * 4.0f + 64.0f;
    const float nearest = dist - model.radius_xz;
    constexpr float kFill = 0.46f;  // of the rectangle, per side of its centre
    const float h = std::min(kFill * where.w * nearest / model.radius_xz,
                             kFill * where.h * nearest / (model.half_height * ys)) *
                    std::clamp(zoom, 0.05f, 20.0f);
    const float cx = where.x + where.w * 0.5f;
    const float cy = where.y + where.h * (0.5f - lift);

    Frame& frame = current_frame();
    auto isolated = [&](PrelitMesh& m) {
        m.isolated = true;
        m.casts_shadow = false;
        m.fade = 1.0f;
        m.h = h;
        m.geom_ofs_x = cx;
        m.geom_ofs_y = cy;
    };

    {
        // The depth reset: the rectangle at a depth behind the model, written whatever was there.
        PrelitMesh quad;
        isolated(quad);
        quad.depth_reset = true;
        const float z = dist + radius + 16.0f;
        const float x0 = (where.x - cx) * z / h, x1 = (where.x + where.w - cx) * z / h;
        const float y0 = (where.y - cy) * z / h, y1 = (where.y + where.h - cy) * z / h;
        for (const auto& c : {std::array<float, 2>{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}) {
            PrelitVertex v;
            v.x = c[0];
            v.y = c[1];
            v.z = z;
            quad.vertices.push_back(v);
        }
        quad.indices = {0, 1, 2, 1, 3, 2};
        frame.prelit.push_back(std::move(quad));
        out.push_back(static_cast<int>(frame.prelit.size()) - 1);
    }

    // The model's own front is +z, away from a camera that looks down +z: half a turn faces the viewer.
    const float a = yaw + 3.14159265f;
    const float c = std::cos(a), s = std::sin(a);
    // Towards the light: above, left of and in front of the model (y is down, the camera at -z).
    constexpr float kLx = -0.38f, kLy = -0.72f, kLz = -0.58f;
    constexpr float kAmbient = 0.42f, kDiffuse = 0.72f;
    std::vector<std::int32_t> remap(model.positions.size(), -1);
    for (const PreviewModel::Group& g : model.groups) {
        PrelitMesh m;
        isolated(m);
        m.texture_key = g.texture_key;
        m.blend = g.blended ? Blend::Average : Blend::Opaque;
        std::fill(remap.begin(), remap.end(), -1);
        for (std::uint32_t i : g.indices) {
            std::int32_t& r = remap[i];
            if (r < 0) {
                r = static_cast<std::int32_t>(m.vertices.size());
                const auto& p = model.positions[i];
                const auto& n = model.normals[i];
                PrelitVertex v;
                v.x = c * p[0] + s * p[2];
                v.y = p[1] * ys;
                v.z = -s * p[0] + c * p[2] + dist;
                const float nx = c * n[0] + s * n[2], nz = -s * n[0] + c * n[2];
                const float lit = kAmbient + kDiffuse * std::max(0.0f, nx * kLx + n[1] * kLy + nz * kLz);
                auto channel = [&](float k) {
                    return static_cast<std::uint8_t>(std::clamp(128.0f * lit * k + 0.5f, 0.0f, 255.0f));
                };
                v.r = channel(g.color[0]);
                v.g = channel(g.color[1]);
                v.b = channel(g.color[2]);
                v.u = model.uvs[i][0];
                v.v = model.uvs[i][1];
                m.vertices.push_back(v);
            }
            m.indices.push_back(static_cast<std::uint32_t>(r));
        }
        frame.prelit.push_back(std::move(m));
        out.push_back(static_cast<int>(frame.prelit.size()) - 1);
    }
    return out;
}

}  // namespace sh::scene
