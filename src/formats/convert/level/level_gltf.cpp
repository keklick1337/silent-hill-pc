#include "formats/convert/level/level_gltf.h"

#include "formats/common/json.h"
#include "formats/gltf/ps1_blend.h"

#include <cstdio>
#include <utility>

namespace sh::convert::level {
namespace {

std::string stem_of(std::string_view game_path) {
    const auto slash = game_path.rfind('/');
    if (slash != std::string_view::npos) {
        game_path.remove_prefix(slash + 1);
    }
    return std::string(game_path.substr(0, game_path.rfind('.')));
}

// "BG/THR_GLB.PLM" -> "levels/BG/THR_GLB.glb": where the level converter writes a PLM's .glb.
std::string plm_glb_path(std::string_view game_path) {
    return "levels/" + std::string(game_path.substr(0, game_path.rfind('.'))) + ".glb";
}

std::string source_extras(std::string_view game_path) {
    json::Writer e;
    e.begin_object().member("source", std::string(game_path)).end_object();
    return e.take();
}

// A model of a library (a PLM, or the LM of a cell) at the origin, found by name. ot_group: the model's OT bucket
// group (+0xB bits 1-3), which places its prims in the frame.
gltf::Node library_node(const LmModel& model, std::size_t index, int mesh) {
    gltf::Node node;
    node.name = model.name;
    node.mesh = mesh;
    json::Writer e;
    e.begin_object().member("model_index", index).member("ot_group", model.order_bucket()).end_object();
    node.extras = e.take();
    return node;
}

gltf::Node instance_node(const FlatInstance& inst) {
    gltf::Node node;
    node.name = inst.model_name;
    node.has_matrix = true;
    node.matrix = inst.matrix;
    return node;
}

}  // namespace

GltfBuilder::GltfBuilder(std::string glb_path, const TextureCatalog* catalog)
    : glb_path_(std::move(glb_path)), catalog_(catalog) {}

int GltfBuilder::material_for(const FlatMesh& mesh, const TimInfo** tim_out) {
    const TimInfo* tim = catalog_ != nullptr ? catalog_->by_name(mesh.material_name) : nullptr;
    *tim_out = tim;
    int palette = tim != nullptr ? palette_index(*tim, mesh.clut, mesh.clut_base) : -1;
    const bool inside = palette >= 0;
    if (!inside) {
        palette = 0;
    }
    const auto key = std::make_tuple(tim != nullptr ? tim->game_path : "?" + mesh.material_name,
                                     tim != nullptr && !inside ? -1 - mesh.clut : palette, mesh.transparent);
    const auto found = material_index_.find(key);
    if (found != material_index_.end()) {
        return found->second;
    }

    gltf::Material mat;
    char name[64];
    std::snprintf(name, sizeof name, "%s_p%02d%s", mesh.material_name.empty() ? "none" : mesh.material_name.c_str(),
                  palette, mesh.transparent ? "_alpha" : "");
    mat.name = name;
    json::Writer extras;
    extras.begin_object();
    extras.member("material", mesh.material_name);
    extras.member("clut", mesh.clut);
    extras.member("clut_base", mesh.clut_base);
    if (tim != nullptr) {
        const std::string png = palette_png(*tim, palette);
        extras.member("tim", tim->game_path);
        extras.member("palette", palette);
        extras.member("palette_in_tim", inside);
        extras.member("png", png);
        const std::string uri = relative_uri(glb_path_, texture_root_ + png);
        auto it = texture_index_.find(uri);
        if (it == texture_index_.end()) {
            doc_.textures.push_back({uri, true});
            it = texture_index_.emplace(uri, static_cast<int>(doc_.textures.size() - 1)).first;
        }
        mat.texture = it->second;
        mat.alpha_mode = gltf::AlphaMode::Mask;
        ++stats_.textured_materials;
        if (!inside) {
            ++stats_.palettes_outside_tim;
        }
    } else if (mesh.material_name.empty()) {
        // Index 127 is NO_VALUE: Model_MaterialFlagsApply gives such primitives the GPU word 32 and no texture.
        extras.member("material_index", mesh.material_index);
        extras.member("texture", "no material");
    } else {
        extras.member("tim", "");
        extras.member("texture", "no TIM of this name on the disc (drawn into VRAM at run time)");
        stats_.missing_textures.insert(mesh.material_name);
    }
    extras.member("semi_transparent", mesh.transparent);
    if (mesh.transparent) {
        // The disc leaves the LM material's page byte zero: the game's loaders give every map, PLM and held-item
        // material the additive mode (Lm_MaterialsLoadWithFilter, Lm_MaterialFsImageApply1 with mode 1).
        extras.member("abr", gltf::kMapBlendAbr);
        extras.member("blend_mode", std::string(gltf::blend_mode_name(gltf::kMapBlendAbr)));
        extras.member("blend", "PS1 semi-transparency on texels with the STP bit; shown as BLEND at alpha 0.5");
        mat.alpha_mode = gltf::AlphaMode::Blend;
        mat.base_color[3] = 0.5f;
    }
    extras.end_object();
    mat.extras = extras.take();
    doc_.materials.push_back(std::move(mat));
    ++stats_.materials;
    const int index = static_cast<int>(doc_.materials.size() - 1);
    material_index_.emplace(key, index);
    return index;
}

int GltfBuilder::add_mesh(const FlatModel& model) {
    gltf::Mesh mesh;
    mesh.name = model.name;
    for (const FlatMesh& flat : model.meshes) {
        if (flat.indices.empty()) {
            continue;
        }
        const TimInfo* tim = nullptr;
        gltf::Primitive prim;
        prim.material = material_for(flat, &tim);
        prim.positions = flat.positions;
        prim.normals = flat.normals;
        const float w = tim != nullptr ? static_cast<float>(tim->width) : 256.0f;
        const float h = tim != nullptr ? static_cast<float>(tim->height) : 256.0f;
        prim.texcoords.reserve(flat.uvs.size());
        for (std::size_t i = 0; i + 1 < flat.uvs.size(); i += 2) {
            prim.texcoords.push_back(flat.uvs[i] / w);
            prim.texcoords.push_back(flat.uvs[i + 1] / h);
        }
        prim.indices = flat.indices;
        stats_.vertices += prim.vertex_count();
        stats_.triangles += prim.indices.size() / 3;
        mesh.primitives.push_back(std::move(prim));
    }
    if (mesh.primitives.empty()) {
        return -1;
    }
    doc_.meshes.push_back(std::move(mesh));
    ++stats_.meshes;
    return static_cast<int>(doc_.meshes.size() - 1);
}

int GltfBuilder::add_node(gltf::Node node, int parent) {
    doc_.nodes.push_back(std::move(node));
    const int index = static_cast<int>(doc_.nodes.size() - 1);
    if (parent < 0) {
        doc_.scene_roots.push_back(index);
    } else {
        doc_.nodes[static_cast<std::size_t>(parent)].children.push_back(index);
    }
    return index;
}

Result<std::vector<std::uint8_t>> GltfBuilder::write() const { return gltf::write_glb(doc_); }

Result<CellGlb> cell_to_glb(const IpdFile& ipd, std::string_view game_path, const LmFile* global,
                            std::string_view global_path, const TextureCatalog* catalog) {
    using Out = Result<CellGlb>;
    const std::string glb_path = plm_glb_path(game_path);
    GltfBuilder builder(glb_path, catalog);
    const FlatLevel level = flatten_ipd(ipd, global);
    CellGlb out;

    gltf::Node root;
    root.name = stem_of(game_path);
    {
        json::Writer e;
        e.begin_object();
        e.member("source", std::string(game_path));
        e.member("cell_x", static_cast<int>(ipd.cell_x));
        e.member("cell_z", static_cast<int>(ipd.cell_z));
        e.end_object();
        root.extras = e.take();
    }
    const int root_index = builder.add_node(std::move(root));

    std::vector<int> mesh_of(level.models.size(), -2);
    json::Writer unresolved;
    unresolved.begin_array();
    for (const FlatInstance& inst : level.instances) {
        if (inst.flat_model < 0) {
            ++out.unresolved;
            unresolved.begin_object();
            unresolved.member("model_info", inst.model_info_index);
            unresolved.member("model", inst.model_name);
            unresolved.member("global", inst.is_global_plm);
            unresolved.end_object();
            continue;
        }
        gltf::Node node = instance_node(inst);
        json::Writer e;
        e.begin_object();
        e.member("model_info", inst.model_info_index);
        if (inst.is_global_plm) {
            e.member("global_plm", std::string(global_path));
            e.member("glb", relative_uri(glb_path, plm_glb_path(global_path)));
            e.member("model", inst.model_name);
            ++out.global_references;
        } else {
            int& mesh = mesh_of[static_cast<std::size_t>(inst.flat_model)];
            if (mesh == -2) {
                mesh = builder.add_mesh(level.models[static_cast<std::size_t>(inst.flat_model)]);
            }
            node.mesh = mesh;
        }
        e.end_object();
        node.extras = e.take();
        builder.add_node(std::move(node), root_index);
        ++out.instances;
    }
    unresolved.end_array();
    for (const FlatBillboard& b : level.billboards) {
        gltf::Node node;
        node.name = "billboard";
        node.translation = b.position;
        json::Writer e;
        e.begin_object().member("billboard_kind", b.kind).end_object();
        node.extras = e.take();
        builder.add_node(std::move(node), root_index);
    }

    builder.document().extras = source_extras(game_path);
    builder.document().scene_name = stem_of(game_path);
    auto glb = builder.write();
    if (!glb) {
        return Out::fail(glb.error());
    }
    out.glb = std::move(glb.value());
    out.stats = builder.stats();

    json::Writer s;
    s.begin_object();
    s.member("file", glb_path);
    s.member("instances", out.instances);
    s.member("global_references", out.global_references);
    s.member("global_plm", std::string(global_path));
    s.member("billboards", level.billboards.size());
    s.member("meshes", out.stats.meshes);
    s.member("triangles", out.stats.triangles);
    s.member("materials", out.stats.materials);
    s.member("palettes_outside_tim", out.stats.palettes_outside_tim);
    s.key("missing_textures").begin_array();
    for (const std::string& name : out.stats.missing_textures) {
        s.value(name);
    }
    s.end_array();
    s.member("unresolved_note", "instances the game skips too: the model is in neither this cell's LM nor the global PLM");
    s.key("unresolved").raw(unresolved.str());
    s.end_object();
    out.json_section = s.take();
    return Out::ok(std::move(out));
}

int add_library_nodes(GltfBuilder& builder, const LmFile& plm, std::string_view game_path,
                      std::map<std::string, int>* built) {
    gltf::Node root;
    root.name = stem_of(game_path);
    {
        // The library's texture names in file order: the engine hands out the map's texture pool from them.
        json::Writer e;
        e.begin_object().member("source", std::string(game_path));
        e.key("materials").begin_array();
        for (const LmMaterial& m : plm.materials) {
            e.value(m.name);
        }
        e.end_array().end_object();
        root.extras = e.take();
    }
    const int root_index = builder.add_node(std::move(root));
    for (std::size_t i = 0; i < plm.models.size(); ++i) {
        int mesh = -2;
        if (built != nullptr) {
            if (auto it = built->find(plm.models[i].name); it != built->end()) mesh = it->second;
        }
        if (mesh == -2) {
            mesh = builder.add_mesh(flatten_model(plm.models[i], plm.materials));
            if (built != nullptr) built->emplace(plm.models[i].name, mesh);
        }
        builder.add_node(library_node(plm.models[i], i, mesh), root_index);
    }
    return root_index;
}

Result<std::vector<std::uint8_t>> plm_to_glb(const LmFile& plm, std::string_view game_path, std::string_view glb_path,
                                             const TextureCatalog* catalog, GltfStats* stats) {
    GltfBuilder builder(std::string(glb_path), catalog);
    add_library_nodes(builder, plm, game_path);
    builder.document().extras = source_extras(game_path);
    builder.document().scene_name = stem_of(game_path);
    if (stats != nullptr) {
        *stats = builder.stats();
    }
    return builder.write();
}

std::size_t add_area_nodes(GltfBuilder& builder, const std::vector<AreaCell>& cells, const LmFile* global,
                           std::vector<int>* cell_roots, std::map<std::string, int>* global_meshes) {
    std::map<std::string, int> global_mesh;
    std::size_t unresolved = 0;
    for (const AreaCell& cell : cells) {
        const FlatLevel level = flatten_ipd(cell.ipd, global);
        const std::string root_name = stem_of(cell.game_path);
        gltf::Node root;
        root.name = root_name;
        root.extras = source_extras(cell.game_path);
        const int root_index = builder.add_node(std::move(root));
        if (cell_roots != nullptr) {
            cell_roots->push_back(root_index);
        }
        std::vector<int> mesh_of(level.models.size(), -2);
        std::map<std::string, int> local_mesh;
        for (const FlatInstance& inst : level.instances) {
            if (inst.flat_model < 0) {
                ++unresolved;
                continue;
            }
            const FlatModel& model = level.models[static_cast<std::size_t>(inst.flat_model)];
            int* mesh = &mesh_of[static_cast<std::size_t>(inst.flat_model)];
            if (inst.is_global_plm) {
                mesh = &global_mesh.emplace(inst.model_name, -2).first->second;
            }
            if (*mesh == -2) {
                *mesh = builder.add_mesh(model);
            }
            if (!inst.is_global_plm) {
                local_mesh.emplace(inst.model_name, *mesh);
            }
            gltf::Node node = instance_node(inst);
            node.mesh = *mesh;
            builder.add_node(std::move(node), root_index);
        }
        // Every model of the cell's LM by name, under a group the cell geometry leaves out; instanced ones share
        // their mesh.
        gltf::Node group;
        group.name = root_name + "/models";
        const int group_index = builder.add_node(std::move(group), root_index);
        for (std::size_t i = 0; i < cell.ipd.lm.models.size(); ++i) {
            const LmModel& lm_model = cell.ipd.lm.models[i];
            auto [it, added] = local_mesh.try_emplace(lm_model.name, -2);
            if (added) {
                it->second = builder.add_mesh(flatten_model(lm_model, cell.ipd.lm.materials));
            }
            builder.add_node(library_node(lm_model, i, it->second), group_index);
        }
    }
    if (global_meshes != nullptr) {
        *global_meshes = std::move(global_mesh);
    }
    return unresolved;
}

Result<std::vector<std::uint8_t>> area_to_glb(std::string_view area, const std::vector<AreaCell>& cells,
                                              const LmFile* global, std::string_view global_path,
                                              const std::vector<std::string>& maps, const TextureCatalog* catalog,
                                              GltfStats* stats) {
    const std::string glb_path = "levels/areas/" + std::string(area) + ".glb";
    GltfBuilder builder(glb_path, catalog);
    const std::size_t unresolved = add_area_nodes(builder, cells, global, nullptr);
    json::Writer e;
    e.begin_object();
    e.member("area", std::string(area));
    e.key("maps").begin_array();
    for (const std::string& m : maps) {
        e.value(m);
    }
    e.end_array();
    e.member("cells", cells.size());
    e.member("global_plm", std::string(global_path));
    e.member("unresolved_instances", unresolved);
    e.end_object();
    builder.document().extras = e.take();
    builder.document().scene_name = std::string(area);
    if (stats != nullptr) {
        *stats = builder.stats();
    }
    return builder.write();
}

}  // namespace sh::convert::level
