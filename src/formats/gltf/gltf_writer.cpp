#include "formats/gltf/gltf_writer.h"

#include "formats/common/json.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

// The binary chunk is little-endian by definition; floats and integers are copied from memory as they are.
static_assert(std::endian::native == std::endian::little, "gltf_writer assumes a little-endian host");

namespace sh::gltf {
namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67;  // "glTF"
constexpr std::uint32_t kChunkJson = 0x4E4F534A;
constexpr std::uint32_t kChunkBin = 0x004E4942;

constexpr int kArrayBuffer = 34962;
constexpr int kElementArrayBuffer = 34963;
constexpr int kUnsignedByte = 5121;
constexpr int kUnsignedShort = 5123;
constexpr int kUnsignedInt = 5125;
constexpr int kFloat = 5126;

constexpr int kNearest = 9728;
constexpr int kLinear = 9729;
constexpr int kLinearMipmapLinear = 9987;

struct AccessorDesc {
    int buffer_view;
    int component_type;
    std::size_t count;
    const char* type;
    bool normalized = false;
    bool has_min_max = false;
    std::array<float, 3> min{};
    std::array<float, 3> max{};
    std::size_t min_max_components = 3;  // 1 for the SCALAR key times of an animation
};

class BinaryBuilder {
public:
    // Appends `bytes` at a 4-byte boundary and returns the new bufferView index.
    int add_view(const void* data, std::size_t bytes, int target) {
        while (bin_.size() % 4 != 0) {
            bin_.push_back(0);
        }
        views_.push_back({bin_.size(), bytes, target});
        const auto* p = static_cast<const std::uint8_t*>(data);
        bin_.insert(bin_.end(), p, p + bytes);
        return static_cast<int>(views_.size() - 1);
    }

    int add_accessor(const AccessorDesc& a) {
        accessors_.push_back(a);
        return static_cast<int>(accessors_.size() - 1);
    }

    void finish() {
        while (bin_.size() % 4 != 0) {
            bin_.push_back(0);
        }
    }

    struct View {
        std::size_t offset;
        std::size_t length;
        int target;
    };

    std::vector<std::uint8_t> bin_;
    std::vector<View> views_;
    std::vector<AccessorDesc> accessors_;
};

Result<void> check_primitive(const Primitive& p, std::size_t mesh_index, std::size_t prim_index,
                             std::size_t material_count) {
    const std::string where = "mesh " + std::to_string(mesh_index) + " primitive " + std::to_string(prim_index) + ": ";
    if (p.positions.empty() || p.positions.size() % 3 != 0) {
        return Result<void>::fail(where + "positions must be a non-empty multiple of 3 floats");
    }
    const std::size_t n = p.vertex_count();
    if (!p.normals.empty() && p.normals.size() != n * 3) {
        return Result<void>::fail(where + "normals count differs from positions");
    }
    if (!p.texcoords.empty() && p.texcoords.size() != n * 2) {
        return Result<void>::fail(where + "texcoords count differs from positions");
    }
    if (!p.colors.empty() && p.colors.size() != n * 4) {
        return Result<void>::fail(where + "colors count differs from positions");
    }
    if (p.joints.empty() != p.weights.empty()) {
        return Result<void>::fail(where + "joints and weights must be given together");
    }
    if (!p.joints.empty() && (p.joints.size() != n * 4 || p.weights.size() != n * 4)) {
        return Result<void>::fail(where + "joints/weights count differs from positions");
    }
    if (p.indices.empty() || p.indices.size() % 3 != 0) {
        return Result<void>::fail(where + "indices must be a non-empty multiple of 3");
    }
    for (const std::uint32_t index : p.indices) {
        if (index >= n) {
            return Result<void>::fail(where + "index " + std::to_string(index) + " out of range");
        }
    }
    for (const float f : p.positions) {
        if (!std::isfinite(f)) {
            return Result<void>::fail(where + "non-finite position");
        }
    }
    if (p.material < -1 || (p.material >= 0 && static_cast<std::size_t>(p.material) >= material_count)) {
        return Result<void>::fail(where + "material index out of range");
    }
    return Result<void>::ok();
}

Result<void> check_document(const Document& doc) {
    for (std::size_t i = 0; i < doc.materials.size(); ++i) {
        const int t = doc.materials[i].texture;
        if (t < -1 || (t >= 0 && static_cast<std::size_t>(t) >= doc.textures.size())) {
            return Result<void>::fail("material " + std::to_string(i) + ": texture index out of range");
        }
    }
    for (std::size_t m = 0; m < doc.meshes.size(); ++m) {
        if (doc.meshes[m].primitives.empty()) {
            return Result<void>::fail("mesh " + std::to_string(m) + " has no primitives");
        }
        for (std::size_t p = 0; p < doc.meshes[m].primitives.size(); ++p) {
            auto ok = check_primitive(doc.meshes[m].primitives[p], m, p, doc.materials.size());
            if (!ok) {
                return ok;
            }
        }
    }

    const std::size_t node_count = doc.nodes.size();
    std::vector<int> parent(node_count, -1);
    for (std::size_t i = 0; i < node_count; ++i) {
        const Node& node = doc.nodes[i];
        const std::string where = "node " + std::to_string(i) + ": ";
        if (node.mesh < -1 || (node.mesh >= 0 && static_cast<std::size_t>(node.mesh) >= doc.meshes.size())) {
            return Result<void>::fail(where + "mesh index out of range");
        }
        if (node.skin < -1 || (node.skin >= 0 && static_cast<std::size_t>(node.skin) >= doc.skins.size())) {
            return Result<void>::fail(where + "skin index out of range");
        }
        for (const int child : node.children) {
            if (child < 0 || static_cast<std::size_t>(child) >= node_count) {
                return Result<void>::fail(where + "child index out of range");
            }
            if (parent[static_cast<std::size_t>(child)] != -1) {
                return Result<void>::fail(where + "child " + std::to_string(child) + " already has a parent");
            }
            parent[static_cast<std::size_t>(child)] = static_cast<int>(i);
        }
    }
    // A node whose parent chain never ends is part of a cycle.
    for (std::size_t i = 0; i < node_count; ++i) {
        int cur = static_cast<int>(i);
        std::size_t steps = 0;
        while (cur != -1) {
            cur = parent[static_cast<std::size_t>(cur)];
            if (++steps > node_count) {
                return Result<void>::fail("node " + std::to_string(i) + " is part of a cycle");
            }
        }
    }
    for (const int root : doc.scene_roots) {
        if (root < 0 || static_cast<std::size_t>(root) >= node_count || parent[static_cast<std::size_t>(root)] != -1) {
            return Result<void>::fail("scene root " + std::to_string(root) + " is not a parentless node");
        }
    }
    for (std::size_t s = 0; s < doc.scenes.size(); ++s) {
        for (const int root : doc.scenes[s].roots) {
            if (root < 0 || static_cast<std::size_t>(root) >= node_count || parent[static_cast<std::size_t>(root)] != -1) {
                return Result<void>::fail("scene " + std::to_string(s + 1) + ": root " + std::to_string(root) +
                                          " is not a parentless node");
            }
        }
    }
    for (std::size_t b = 0; b < doc.blobs.size(); ++b) {
        if (doc.blobs[b].empty()) {
            return Result<void>::fail("blob " + std::to_string(b) + " is empty");
        }
    }
    for (std::size_t s = 0; s < doc.skins.size(); ++s) {
        const Skin& skin = doc.skins[s];
        const std::string where = "skin " + std::to_string(s) + ": ";
        if (skin.joints.empty()) {
            return Result<void>::fail(where + "no joints");
        }
        for (const int j : skin.joints) {
            if (j < 0 || static_cast<std::size_t>(j) >= node_count) {
                return Result<void>::fail(where + "joint index out of range");
            }
        }
        if (!skin.inverse_bind.empty() && skin.inverse_bind.size() != skin.joints.size()) {
            return Result<void>::fail(where + "inverse_bind must be empty or one matrix per joint");
        }
        if (skin.skeleton < -1 || (skin.skeleton >= 0 && static_cast<std::size_t>(skin.skeleton) >= node_count)) {
            return Result<void>::fail(where + "skeleton index out of range");
        }
    }
    for (std::size_t a = 0; a < doc.animations.size(); ++a) {
        const Animation& anim = doc.animations[a];
        const std::string where = "animation " + std::to_string(a) + ": ";
        if (anim.channels.empty()) {
            return Result<void>::fail(where + "no channels");
        }
        std::vector<std::size_t> sampler_components(anim.samplers.size(), 0);
        std::vector<std::pair<int, int>> targets;
        for (const AnimationChannel& c : anim.channels) {
            if (c.sampler < 0 || static_cast<std::size_t>(c.sampler) >= anim.samplers.size()) {
                return Result<void>::fail(where + "channel sampler index out of range");
            }
            if (c.node < 0 || static_cast<std::size_t>(c.node) >= node_count) {
                return Result<void>::fail(where + "channel node index out of range");
            }
            const std::pair<int, int> target{c.node, static_cast<int>(c.path)};
            if (std::find(targets.begin(), targets.end(), target) != targets.end()) {
                return Result<void>::fail(where + "two channels target the same node and path");
            }
            targets.push_back(target);
            const std::size_t comps = c.path == AnimationPath::Rotation ? 4 : 3;
            std::size_t& known = sampler_components[static_cast<std::size_t>(c.sampler)];
            if (known != 0 && known != comps) {
                return Result<void>::fail(where + "a sampler is used for rotation and for a vector path");
            }
            known = comps;
        }
        for (std::size_t si = 0; si < anim.samplers.size(); ++si) {
            const AnimationSampler& sm = anim.samplers[si];
            const std::string sw = where + "sampler " + std::to_string(si) + ": ";
            if (sampler_components[si] == 0) {
                return Result<void>::fail(sw + "not used by any channel");
            }
            if (sm.input.empty()) {
                return Result<void>::fail(sw + "no keys");
            }
            for (std::size_t k = 0; k < sm.input.size(); ++k) {
                if (!std::isfinite(sm.input[k]) || sm.input[k] < 0.0f || (k > 0 && !(sm.input[k] > sm.input[k - 1]))) {
                    return Result<void>::fail(sw + "key times must be finite, non-negative and strictly increasing");
                }
            }
            if (sm.output.size() != sm.input.size() * sampler_components[si]) {
                return Result<void>::fail(sw + "output count does not match the keys");
            }
            for (const float f : sm.output) {
                if (!std::isfinite(f)) {
                    return Result<void>::fail(sw + "non-finite output value");
                }
            }
        }
    }
    return Result<void>::ok();
}

void write_floats(json::Writer& w, const float* v, std::size_t n) {
    w.begin_array();
    for (std::size_t i = 0; i < n; ++i) {
        w.value_float(v[i]);
    }
    w.end_array();
}

// glTF requires the extras to be a JSON object; an empty string means "no extras".
void write_extras(json::Writer& w, const std::string& extras) {
    if (!extras.empty()) {
        w.key("extras").raw(extras);
    }
}

void write_accessor(json::Writer& w, const AccessorDesc& a) {
    w.begin_object();
    w.member("bufferView", a.buffer_view);
    w.member("componentType", a.component_type);
    if (a.normalized) {
        w.member("normalized", true);
    }
    w.member("count", static_cast<std::uint64_t>(a.count));
    w.member("type", a.type);
    if (a.has_min_max) {
        w.key("max");
        write_floats(w, a.max.data(), a.min_max_components);
        w.key("min");
        write_floats(w, a.min.data(), a.min_max_components);
    }
    w.end_object();
}

}  // namespace

Result<std::vector<std::uint8_t>> write_glb(const Document& doc) {
    using Out = Result<std::vector<std::uint8_t>>;
    if (auto ok = check_document(doc); !ok) {
        return Out::fail("glTF document: " + ok.error());
    }

    BinaryBuilder bin;
    for (const std::vector<std::uint8_t>& blob : doc.blobs) {
        bin.add_view(blob.data(), blob.size(), 0);
    }

    // Per mesh, per primitive: the accessor index of each attribute, -1 when absent.
    struct PrimAccessors {
        int position = -1, normal = -1, texcoord = -1, color = -1, joints = -1, weights = -1, indices = -1;
    };
    std::vector<std::vector<PrimAccessors>> prim_accessors(doc.meshes.size());

    for (std::size_t m = 0; m < doc.meshes.size(); ++m) {
        for (const Primitive& p : doc.meshes[m].primitives) {
            PrimAccessors acc;
            const std::size_t n = p.vertex_count();

            AccessorDesc pos{};
            pos.buffer_view = bin.add_view(p.positions.data(), p.positions.size() * sizeof(float), kArrayBuffer);
            pos.component_type = kFloat;
            pos.count = n;
            pos.type = "VEC3";
            pos.has_min_max = true;
            pos.min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()};
            pos.max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest()};
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t c = 0; c < 3; ++c) {
                    pos.min[c] = std::min(pos.min[c], p.positions[i * 3 + c]);
                    pos.max[c] = std::max(pos.max[c], p.positions[i * 3 + c]);
                }
            }
            acc.position = bin.add_accessor(pos);

            if (!p.normals.empty()) {
                AccessorDesc a{};
                a.buffer_view = bin.add_view(p.normals.data(), p.normals.size() * sizeof(float), kArrayBuffer);
                a.component_type = kFloat;
                a.count = n;
                a.type = "VEC3";
                acc.normal = bin.add_accessor(a);
            }
            if (!p.texcoords.empty()) {
                AccessorDesc a{};
                a.buffer_view = bin.add_view(p.texcoords.data(), p.texcoords.size() * sizeof(float), kArrayBuffer);
                a.component_type = kFloat;
                a.count = n;
                a.type = "VEC2";
                acc.texcoord = bin.add_accessor(a);
            }
            if (!p.colors.empty()) {
                AccessorDesc a{};
                a.buffer_view = bin.add_view(p.colors.data(), p.colors.size(), kArrayBuffer);
                a.component_type = kUnsignedByte;
                a.count = n;
                a.type = "VEC4";
                a.normalized = true;
                acc.color = bin.add_accessor(a);
            }
            if (!p.joints.empty()) {
                AccessorDesc j{};
                j.buffer_view = bin.add_view(p.joints.data(), p.joints.size() * sizeof(std::uint16_t), kArrayBuffer);
                j.component_type = kUnsignedShort;
                j.count = n;
                j.type = "VEC4";
                acc.joints = bin.add_accessor(j);
                AccessorDesc wt{};
                wt.buffer_view = bin.add_view(p.weights.data(), p.weights.size() * sizeof(float), kArrayBuffer);
                wt.component_type = kFloat;
                wt.count = n;
                wt.type = "VEC4";
                acc.weights = bin.add_accessor(wt);
            }

            AccessorDesc idx{};
            idx.count = p.indices.size();
            idx.type = "SCALAR";
            if (n < 65535) {
                std::vector<std::uint16_t> narrow(p.indices.begin(), p.indices.end());
                idx.buffer_view = bin.add_view(narrow.data(), narrow.size() * sizeof(std::uint16_t), kElementArrayBuffer);
                idx.component_type = kUnsignedShort;
            } else {
                idx.buffer_view =
                    bin.add_view(p.indices.data(), p.indices.size() * sizeof(std::uint32_t), kElementArrayBuffer);
                idx.component_type = kUnsignedInt;
            }
            acc.indices = bin.add_accessor(idx);
            prim_accessors[m].push_back(acc);
        }
    }

    std::vector<int> skin_ibm(doc.skins.size(), -1);
    for (std::size_t s = 0; s < doc.skins.size(); ++s) {
        const Skin& skin = doc.skins[s];
        if (skin.inverse_bind.empty()) {
            continue;
        }
        AccessorDesc a{};
        a.buffer_view =
            bin.add_view(skin.inverse_bind.data(), skin.inverse_bind.size() * 16 * sizeof(float), 0);
        a.component_type = kFloat;
        a.count = skin.inverse_bind.size();
        a.type = "MAT4";
        skin_ibm[s] = bin.add_accessor(a);
    }

    // Per animation, per sampler: input and output accessors. Equal key times share one input accessor.
    std::vector<std::vector<std::pair<int, int>>> sampler_accessors(doc.animations.size());
    for (std::size_t ai = 0; ai < doc.animations.size(); ++ai) {
        const Animation& anim = doc.animations[ai];
        std::vector<std::size_t> comps(anim.samplers.size(), 3);
        for (const AnimationChannel& c : anim.channels) {
            comps[static_cast<std::size_t>(c.sampler)] = c.path == AnimationPath::Rotation ? 4 : 3;
        }
        for (std::size_t si = 0; si < anim.samplers.size(); ++si) {
            const AnimationSampler& sm = anim.samplers[si];
            int input = -1;
            for (std::size_t prev = 0; prev < si; ++prev) {
                if (anim.samplers[prev].input == sm.input) {
                    input = sampler_accessors[ai][prev].first;
                    break;
                }
            }
            if (input < 0) {
                AccessorDesc in{};
                in.buffer_view = bin.add_view(sm.input.data(), sm.input.size() * sizeof(float), 0);
                in.component_type = kFloat;
                in.count = sm.input.size();
                in.type = "SCALAR";
                in.has_min_max = true;
                in.min_max_components = 1;
                in.min[0] = sm.input.front();
                in.max[0] = sm.input.back();
                input = bin.add_accessor(in);
            }
            AccessorDesc out{};
            out.buffer_view = bin.add_view(sm.output.data(), sm.output.size() * sizeof(float), 0);
            out.component_type = kFloat;
            out.count = sm.input.size();
            out.type = comps[si] == 4 ? "VEC4" : "VEC3";
            sampler_accessors[ai].push_back({input, bin.add_accessor(out)});
        }
    }
    bin.finish();

    json::Writer w;
    w.begin_object();

    w.key("asset").begin_object();
    w.member("version", "2.0");
    if (!doc.generator.empty()) {
        w.member("generator", doc.generator);
    }
    write_extras(w, doc.extras);
    w.end_object();
    if (!doc.extensions_used.empty()) {
        w.key("extensionsUsed").begin_array();
        for (const std::string& name : doc.extensions_used) {
            w.value(name);
        }
        w.end_array();
    }
    if (!doc.extensions.empty()) {
        w.key("extensions").raw(doc.extensions);
    }

    // Roots of the default scene: the caller's list, or every node that has no parent.
    std::vector<int> roots = doc.scene_roots;
    if (roots.empty()) {
        std::vector<bool> has_parent(doc.nodes.size(), false);
        for (const Node& node : doc.nodes) {
            for (const int child : node.children) {
                has_parent[static_cast<std::size_t>(child)] = true;
            }
        }
        for (std::size_t i = 0; i < doc.nodes.size(); ++i) {
            if (!has_parent[i]) {
                roots.push_back(static_cast<int>(i));
            }
        }
    }
    if (!doc.nodes.empty()) {
        w.member("scene", 0);
        w.key("scenes").begin_array().begin_object();
        if (!doc.scene_name.empty()) {
            w.member("name", doc.scene_name);
        }
        w.key("nodes").begin_array();
        for (const int r : roots) {
            w.value(r);
        }
        w.end_array();
        w.end_object();
        for (const Scene& scene : doc.scenes) {
            w.begin_object();
            if (!scene.name.empty()) {
                w.member("name", scene.name);
            }
            w.key("nodes").begin_array();
            for (const int r : scene.roots) {
                w.value(r);
            }
            w.end_array();
            write_extras(w, scene.extras);
            w.end_object();
        }
        w.end_array();
    }

    if (!doc.nodes.empty()) {
        w.key("nodes").begin_array();
        for (const Node& node : doc.nodes) {
            w.begin_object();
            if (!node.name.empty()) {
                w.member("name", node.name);
            }
            if (!node.children.empty()) {
                w.key("children").begin_array();
                for (const int c : node.children) {
                    w.value(c);
                }
                w.end_array();
            }
            if (node.mesh >= 0) {
                w.member("mesh", node.mesh);
            }
            if (node.skin >= 0) {
                w.member("skin", node.skin);
            }
            if (node.has_matrix) {
                w.key("matrix");
                write_floats(w, node.matrix.data(), 16);
            } else {
                if (node.translation != std::array<float, 3>{0, 0, 0}) {
                    w.key("translation");
                    write_floats(w, node.translation.data(), 3);
                }
                if (node.rotation != std::array<float, 4>{0, 0, 0, 1}) {
                    w.key("rotation");
                    write_floats(w, node.rotation.data(), 4);
                }
                if (node.scale != std::array<float, 3>{1, 1, 1}) {
                    w.key("scale");
                    write_floats(w, node.scale.data(), 3);
                }
            }
            write_extras(w, node.extras);
            if (!node.extensions.empty()) {
                w.key("extensions").raw(node.extensions);
            }
            w.end_object();
        }
        w.end_array();
    }

    if (!doc.meshes.empty()) {
        w.key("meshes").begin_array();
        for (std::size_t m = 0; m < doc.meshes.size(); ++m) {
            const Mesh& mesh = doc.meshes[m];
            w.begin_object();
            if (!mesh.name.empty()) {
                w.member("name", mesh.name);
            }
            w.key("primitives").begin_array();
            for (std::size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
                const Primitive& p = mesh.primitives[pi];
                const PrimAccessors& a = prim_accessors[m][pi];
                w.begin_object();
                w.key("attributes").begin_object();
                w.member("POSITION", a.position);
                if (a.normal >= 0) {
                    w.member("NORMAL", a.normal);
                }
                if (a.texcoord >= 0) {
                    w.member("TEXCOORD_0", a.texcoord);
                }
                if (a.color >= 0) {
                    w.member("COLOR_0", a.color);
                }
                if (a.joints >= 0) {
                    w.member("JOINTS_0", a.joints);
                    w.member("WEIGHTS_0", a.weights);
                }
                w.end_object();
                w.member("indices", a.indices);
                if (p.material >= 0) {
                    w.member("material", p.material);
                }
                w.member("mode", 4);
                write_extras(w, p.extras);
                w.end_object();
            }
            w.end_array();
            write_extras(w, mesh.extras);
            w.end_object();
        }
        w.end_array();
    }

    if (!doc.materials.empty()) {
        w.key("materials").begin_array();
        for (const Material& mat : doc.materials) {
            w.begin_object();
            if (!mat.name.empty()) {
                w.member("name", mat.name);
            }
            w.key("pbrMetallicRoughness").begin_object();
            w.key("baseColorFactor");
            write_floats(w, mat.base_color.data(), 4);
            if (mat.texture >= 0) {
                w.key("baseColorTexture").begin_object().member("index", mat.texture).end_object();
            }
            w.member_float("metallicFactor", mat.metallic);
            w.member_float("roughnessFactor", mat.roughness);
            w.end_object();
            if (mat.alpha_mode == AlphaMode::Mask) {
                w.member("alphaMode", "MASK");
                w.member_float("alphaCutoff", mat.alpha_cutoff);
            } else if (mat.alpha_mode == AlphaMode::Blend) {
                w.member("alphaMode", "BLEND");
            }
            if (mat.double_sided) {
                w.member("doubleSided", true);
            }
            write_extras(w, mat.extras);
            w.end_object();
        }
        w.end_array();
    }

    if (!doc.textures.empty()) {
        // Sampler 0 is NEAREST, sampler 1 is LINEAR with mipmaps; only the ones in use are written.
        const bool any_nearest =
            std::any_of(doc.textures.begin(), doc.textures.end(), [](const Texture& t) { return t.nearest; });
        const bool any_linear =
            std::any_of(doc.textures.begin(), doc.textures.end(), [](const Texture& t) { return !t.nearest; });
        const int linear_sampler = any_nearest ? 1 : 0;

        w.key("samplers").begin_array();
        if (any_nearest) {
            w.begin_object().member("magFilter", kNearest).member("minFilter", kNearest).end_object();
        }
        if (any_linear) {
            w.begin_object().member("magFilter", kLinear).member("minFilter", kLinearMipmapLinear).end_object();
        }
        w.end_array();

        w.key("images").begin_array();
        for (const Texture& t : doc.textures) {
            w.begin_object().member("uri", t.uri).end_object();
        }
        w.end_array();

        w.key("textures").begin_array();
        for (std::size_t i = 0; i < doc.textures.size(); ++i) {
            w.begin_object()
                .member("sampler", doc.textures[i].nearest ? 0 : linear_sampler)
                .member("source", static_cast<int>(i))
                .end_object();
        }
        w.end_array();
    }

    if (!doc.skins.empty()) {
        w.key("skins").begin_array();
        for (std::size_t s = 0; s < doc.skins.size(); ++s) {
            const Skin& skin = doc.skins[s];
            w.begin_object();
            if (!skin.name.empty()) {
                w.member("name", skin.name);
            }
            if (skin_ibm[s] >= 0) {
                w.member("inverseBindMatrices", skin_ibm[s]);
            }
            if (skin.skeleton >= 0) {
                w.member("skeleton", skin.skeleton);
            }
            w.key("joints").begin_array();
            for (const int j : skin.joints) {
                w.value(j);
            }
            w.end_array();
            w.end_object();
        }
        w.end_array();
    }

    if (!doc.animations.empty()) {
        w.key("animations").begin_array();
        for (std::size_t ai = 0; ai < doc.animations.size(); ++ai) {
            const Animation& anim = doc.animations[ai];
            w.begin_object();
            if (!anim.name.empty()) {
                w.member("name", anim.name);
            }
            w.key("channels").begin_array();
            for (const AnimationChannel& c : anim.channels) {
                const char* path = c.path == AnimationPath::Translation ? "translation"
                                   : c.path == AnimationPath::Rotation  ? "rotation"
                                                                        : "scale";
                w.begin_object();
                w.member("sampler", c.sampler);
                w.key("target").begin_object().member("node", c.node).member("path", path).end_object();
                w.end_object();
            }
            w.end_array();
            w.key("samplers").begin_array();
            for (std::size_t si = 0; si < anim.samplers.size(); ++si) {
                w.begin_object();
                w.member("input", sampler_accessors[ai][si].first);
                w.member("interpolation",
                         anim.samplers[si].interpolation == Interpolation::Step ? "STEP" : "LINEAR");
                w.member("output", sampler_accessors[ai][si].second);
                w.end_object();
            }
            w.end_array();
            write_extras(w, anim.extras);
            w.end_object();
        }
        w.end_array();
    }

    if (!bin.accessors_.empty()) {
        w.key("accessors").begin_array();
        for (const AccessorDesc& a : bin.accessors_) {
            write_accessor(w, a);
        }
        w.end_array();
    }
    if (!bin.views_.empty()) {
        w.key("bufferViews").begin_array();
        for (const BinaryBuilder::View& v : bin.views_) {
            w.begin_object();
            w.member("buffer", 0);
            if (v.offset != 0) {
                w.member("byteOffset", static_cast<std::uint64_t>(v.offset));
            }
            w.member("byteLength", static_cast<std::uint64_t>(v.length));
            if (v.target != 0) {
                w.member("target", v.target);
            }
            w.end_object();
        }
        w.end_array();

        w.key("buffers").begin_array().begin_object().member("byteLength", static_cast<std::uint64_t>(bin.bin_.size()));
        w.end_object().end_array();
    }
    w.end_object();

    std::string json_text = w.take();
    while (json_text.size() % 4 != 0) {
        json_text += ' ';
    }

    const std::size_t json_chunk = 8 + json_text.size();
    const std::size_t bin_chunk = bin.bin_.empty() ? 0 : 8 + bin.bin_.size();
    const std::size_t total = 12 + json_chunk + bin_chunk;
    if (total > std::numeric_limits<std::uint32_t>::max()) {
        return Out::fail("glTF document: larger than 4 GiB");
    }

    std::vector<std::uint8_t> out;
    out.reserve(total);
    const auto put32 = [&out](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    put32(kGlbMagic);
    put32(2);
    put32(static_cast<std::uint32_t>(total));
    put32(static_cast<std::uint32_t>(json_text.size()));
    put32(kChunkJson);
    out.insert(out.end(), json_text.begin(), json_text.end());
    if (!bin.bin_.empty()) {
        put32(static_cast<std::uint32_t>(bin.bin_.size()));
        put32(kChunkBin);
        out.insert(out.end(), bin.bin_.begin(), bin.bin_.end());
    }
    return Out::ok(std::move(out));
}

}  // namespace sh::gltf
