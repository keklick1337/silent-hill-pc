#include "formats/convert/level/map_unified.h"

#include "formats/common/crc32.h"
#include "formats/common/json.h"
#include "formats/convert/level/level_json.h"
#include "formats/convert/level/level_parse.h"
#include "formats/convert/level/map_areas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace sh::convert::level {
namespace {

namespace md = sh::convert::mapdata;

constexpr std::uint32_t kGlbMagic = 0x46546C67;  // "glTF"
constexpr std::uint32_t kChunkJson = 0x4E4F534A;
constexpr std::uint32_t kChunkBin = 0x004E4942;

std::string stem_of(std::string_view game_path) {
    const auto slash = game_path.rfind('/');
    if (slash != std::string_view::npos) {
        game_path.remove_prefix(slash + 1);
    }
    return std::string(game_path.substr(0, game_path.rfind('.')));
}

bool ends_with(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

// PS1 world (Q12 metres, +Y down) to the glTF space of level_flat.h: turned 180 degrees about Z.
std::array<float, 3> world_q12(std::int32_t x, std::int32_t z) {
    return {-static_cast<float>(x) / 4096.0f, 0.0f, static_cast<float>(z) / 4096.0f};
}

std::string kind_of_event(int sys_state) {
    switch (sys_state) {
        case 0: return "flag_gate";
        case 4: return "fmv";
        case 5: return "door";
        case 6: return "room_warp";
        case 7: return "message";
        case 10: return "callback";
        case 11: return "set_flag";
        case 12: return "sound";
        default: return "other";
    }
}

bool spawn_empty(const ShMapSpawn& s) {
    static const ShMapSpawn zero{};
    return std::memcmp(&s, &zero, sizeof s) == 0;
}

int add_child(GltfBuilder& b, int parent, std::string name, std::array<float, 3> at, std::string extras) {
    gltf::Node n;
    n.name = std::move(name);
    n.translation = at;
    n.extras = std::move(extras);
    return b.add_node(std::move(n), parent);
}

int add_group(GltfBuilder& b, int parent, const char* name) {
    gltf::Node n;
    n.name = name;
    return b.add_node(std::move(n), parent);
}

// The placement copy of a map's overlay data (see the header): one root node, a group per kind.
int add_map_nodes(GltfBuilder& b, const MapScene& scene) {
    const md::MapData& d = scene.data;
    gltf::Node root;
    root.name = scene.map;
    {
        json::Writer e;
        e.begin_object();
        e.member("map", scene.map);
        e.member("data", "../" + scene.map + "/map.json");
        e.member("events", "../" + scene.map + "/events.lua");
        e.member("authority", "map.json; these nodes are a copy for editors");
        e.member("bgm", static_cast<int>(d.header.bgmIdx));
        e.member("ambient_audio", static_cast<int>(d.header.ambientAudioIdx));
        e.member("ambient_tint", static_cast<int>(d.header.field16));
        e.member("weather", static_cast<int>(d.header.field17));
        e.end_object();
        root.extras = e.take();
    }
    const int root_index = b.add_node(std::move(root));

    const int points = add_group(b, root_index, "points");
    for (std::size_t i = 0; i < d.points.size(); ++i) {
        const ShMapPoint& p = d.points[i];
        json::Writer e;
        e.begin_object();
        e.member("index", i);
        e.member("x_q12", p.positionX);
        e.member("z_q12", p.positionZ);
        e.member("paper_map", static_cast<int>(p.paperMapIdx));
        e.member("loading_screen", static_cast<int>(p.loadingScreenId));
        // Named as in points.json: a box trigger's point holds half extents, any other a heading and a half width.
        const bool box = std::any_of(d.events.begin(), d.events.end(),
                                     [&](const ShMapEvent& ev) { return ev.triggerType == 1 && ev.mapPointIdx == i; });
        e.member(box ? "half_x_q2" : "heading_q8", static_cast<int>(p.triggerParam0));
        e.member(box ? "half_z_q2" : "half_width_q4", static_cast<int>(p.triggerParam1));
        e.end_object();
        add_child(b, points, "point " + std::to_string(i), world_q12(p.positionX, p.positionZ), e.take());
    }

    const int events = add_group(b, root_index, "events");
    for (std::size_t i = 0; i < d.events.size(); ++i) {
        const ShMapEvent& ev = d.events[i];
        json::Writer e;
        e.begin_object();
        e.member("index", i);
        e.member("kind", kind_of_event(ev.sysState));
        e.member("sys_state", static_cast<int>(ev.sysState));
        e.member("param", static_cast<int>(ev.eventParam));
        e.member("trigger_type", static_cast<int>(ev.triggerType));
        e.member("activation_type", static_cast<int>(ev.activationType));
        e.member("map_point", static_cast<int>(ev.mapPointIdx));
        e.member("required_flag", static_cast<int>(ev.requiredFlag));
        e.member("complete_flag", static_cast<int>(ev.completeFlag));
        e.member("required_item", static_cast<int>(ev.requiredItemId));
        e.member("map_idx", static_cast<int>(ev.mapIdx));
        e.end_object();
        std::array<float, 3> at{0, 0, 0};
        if (ev.mapPointIdx < d.points.size()) {
            at = world_q12(d.points[ev.mapPointIdx].positionX, d.points[ev.mapPointIdx].positionZ);
        }
        add_child(b, events, "event " + std::to_string(i) + " " + kind_of_event(ev.sysState), at, e.take());
    }

    const int cameras = add_group(b, root_index, "cameras");
    std::size_t camera_count = 0;
    for (std::size_t i = 0; i < d.cameras.size(); ++i) {
        static const ShMapCamera zero{};
        if (std::memcmp(&d.cameras[i], &zero, sizeof zero) != 0) {
            camera_count = i + 1;
        }
    }
    for (std::size_t i = 0; i < camera_count; ++i) {
        const ShMapCamera& c = d.cameras[i];
        json::Writer e;
        e.begin_object();
        e.member("index", i);
        // Q4 metres: [min_x, max_x, min_z, max_z].
        e.key("switch_area_q4").begin_array();
        for (const std::int16_t v : c.switchArea) {
            e.value(static_cast<int>(v));
        }
        e.end_array();
        e.key("road_area_q4").begin_array();
        for (const std::int16_t v : c.roadArea) {
            e.value(static_cast<int>(v));
        }
        e.end_array();
        e.member("flags", static_cast<int>(c.flags));
        e.member("road_type", static_cast<int>(c.roadType));
        e.member("camera_move", static_cast<int>(c.cameraMoveType));
        e.end_object();
        const std::array<float, 3> at = {-(c.switchArea[0] + c.switchArea[1]) / 32.0f, 0.0f,
                                         (c.switchArea[2] + c.switchArea[3]) / 32.0f};
        add_child(b, cameras, "camera " + std::to_string(i), at, e.take());
    }

    const int triggers = add_group(b, root_index, "collision_triggers");
    for (std::size_t i = 0; i < d.collision.size() && !d.collision[i].isEndOfArray; ++i) {
        const ShMapCollision& c = d.collision[i];
        json::Writer e;
        e.begin_object();
        e.member("index", i);
        e.member("x", static_cast<int>(c.positionX));
        e.member("z", static_cast<int>(c.positionZ));
        e.member("size_x", static_cast<int>(c.sizeX));
        e.member("size_z", static_cast<int>(c.sizeZ));
        e.member("height_half_m", static_cast<int>(c.height));
        e.end_object();
        add_child(b, triggers, "trigger " + std::to_string(i),
                  {-static_cast<float>(c.positionX), 0.0f, static_cast<float>(c.positionZ)}, e.take());
    }

    const int spawns = add_group(b, root_index, "spawns");
    for (int g = 0; g < SH_MAP_SPAWN_GROUPS; ++g) {
        for (int s = 0; s < SH_MAP_SPAWN_SLOTS; ++s) {
            const ShMapSpawn& sp = d.spawns.slots[g][s];
            if (spawn_empty(sp)) {
                continue;
            }
            json::Writer e;
            e.begin_object();
            e.member("group", g);
            e.member("slot", s);
            // The character by name; a number is an id the name table does not know.
            if (const char* group_chara = md::chara_name_of(d.spawns.charaGroupIds[g])) {
                e.member("group_chara", group_chara);
            } else {
                e.member("group_chara", static_cast<int>(d.spawns.charaGroupIds[g]));
            }
            if (const char* chara = md::chara_name_of(sp.charaId)) {
                e.member("chara", chara);
            } else {
                e.member("chara", static_cast<int>(sp.charaId));
            }
            e.member("rotation_q8", static_cast<int>(sp.rotationY));
            e.member("flags", static_cast<int>(sp.flags));
            e.member("difficulty_min", static_cast<int>(sp.gameDifficultyMin));
            e.end_object();
            gltf::Node n;
            n.name = "spawn " + std::to_string(g) + "." + std::to_string(s);
            n.translation = world_q12(sp.positionX, sp.positionZ);
            // A turn about PS1 +Y is the opposite turn about glTF +Y after the 180 degree turn about Z.
            const double half = -static_cast<double>(sp.rotationY) / 256.0 * 3.14159265358979323846;
            n.rotation = {0.0f, static_cast<float>(std::sin(half)), 0.0f, static_cast<float>(std::cos(half))};
            n.extras = e.take();
            b.add_node(std::move(n), spawns);
        }
    }
    return root_index;
}

struct Packed {
    const SourceFile* file;
    const char* kind;
    int node;
};

std::uint32_t get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

}  // namespace

std::string unified_relative_path(std::string_view group) { return "maps/areas/" + std::string(group) + ".glb"; }

std::string unified_group_of(std::string_view game_path) {
    if (game_path.size() < 4 || game_path.substr(0, 3) != "BG/" || game_path.find('/', 3) != std::string_view::npos) {
        return {};
    }
    if (ends_with(game_path, ".IPD")) {
        return std::string(area_of_ipd(game_path));
    }
    if (ends_with(game_path, ".PLM")) {
        std::string stem = stem_of(game_path);
        if (ends_with(stem, "_GLB") && stem.size() > 4) {
            stem.resize(stem.size() - 4);
        }
        return stem;
    }
    return {};
}

Result<std::vector<std::uint8_t>> write_unified(const UnifiedInput& in, UnifiedStats* stats) {
    using Out = Result<std::vector<std::uint8_t>>;
    UnifiedStats local;
    UnifiedStats& st = stats ? *stats : local;
    const std::string glb_path = unified_relative_path(in.group);
    GltfBuilder builder(glb_path, in.catalog);
    builder.set_texture_root("convert/");

    std::vector<Packed> packed;
    auto check = [&](const SourceFile& f) -> Result<void> {
        if (f.stored.size() < f.size || f.size == 0) {
            return Result<void>::fail(f.game_path + ": " + std::to_string(f.stored.size()) +
                                      " stored bytes for a file of " + std::to_string(f.size));
        }
        return Result<void>::ok();
    };

    std::optional<LmFile> global;
    if (in.global) {
        if (auto ok = check(*in.global); !ok) {
            return Out::fail(ok.error());
        }
        auto plm = parse_plm(std::span<const std::uint8_t>(in.global->stored.data(), in.global->size));
        if (plm) {
            global = std::move(plm.value());
        } else {
            st.warnings.push_back(in.global->game_path + ": " + plm.error() + " (packed without geometry)");
        }
    }

    // Cells: geometry of those that parse, every one packed.
    std::vector<AreaCell> cells;
    std::vector<const SourceFile*> cell_files;
    for (const SourceFile& f : in.cells) {
        if (auto ok = check(f); !ok) {
            return Out::fail(ok.error());
        }
        auto ipd = parse_ipd(std::span<const std::uint8_t>(f.stored.data(), f.size));
        if (!ipd) {
            st.warnings.push_back(f.game_path + ": " + ipd.error() + " (packed without geometry)");
            packed.push_back({&f, "ipd", -1});
            continue;
        }
        cells.push_back({f.game_path, std::move(ipd.value())});
        cell_files.push_back(&f);
    }
    std::vector<int> cell_roots;
    std::map<std::string, int> global_meshes;
    st.unresolved_instances = add_area_nodes(builder, cells, global ? &*global : nullptr, &cell_roots, &global_meshes);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        gltf::Node& root = builder.document().nodes[static_cast<std::size_t>(cell_roots[i])];
        json::Writer e;
        e.begin_object();
        e.member("source", cells[i].game_path);
        e.member("cell_x", static_cast<int>(cells[i].ipd.cell_x));
        e.member("cell_z", static_cast<int>(cells[i].ipd.cell_z));
        // The lamp glows of the cell in cell space (Q8) with their effect kind, in file order: the engine draws them
        // from here, not from the cell file.
        e.key("glows").begin_array();
        for (const IpdModelBuffer& buffer : cells[i].ipd.model_buffers) {
            for (const IpdVec4& b : buffer.billboards) {
                e.begin_array().value(b.x).value(b.y).value(b.z).value(b.w).end_array();
            }
        }
        e.end_array();
        // The texture names of the cell's LM in file order: the engine hands out the map's texture pool from them
        // instead of reading the cell file.
        e.key("materials").begin_array();
        for (const LmMaterial& m : cells[i].ipd.lm.materials) {
            e.value(m.name);
        }
        e.end_array();
        e.end_object();
        root.extras = e.take();

        gltf::Node coll;
        coll.name = stem_of(cells[i].game_path) + "/collision";
        {
            const IpdCollision& c = cells[i].ipd.collision;
            json::Writer e2;
            e2.begin_object();
            e2.member("subcells", static_cast<int>(c.subcell_count_x) * static_cast<int>(c.subcell_count_z));
            e2.member("surfaces", c.surfaces.size());
            e2.member("split_lines", c.subcells.size());
            e2.end_object();
            coll.extras = e2.take();
        }
        coll.extensions = "{\"" + std::string(kCollisionExtension) + "\":" + collision_to_json(cells[i].ipd.collision) + "}";
        // The collision grid sits at the cell origin, which the instances already carry; the node stays at 0.
        builder.add_node(std::move(coll), cell_roots[i]);
        packed.push_back({cell_files[i], "ipd", cell_roots[i]});
    }

    // Libraries: the area's global PLM (its models are shared by the cells; every one is listed by name, the meshes
    // the cells instance are reused) and stand-alone ones.
    if (in.global) {
        int node = -1;
        if (global) {
            node = add_library_nodes(builder, *global, in.global->game_path, &global_meshes);
        }
        packed.push_back({&*in.global, "plm", node});
    }
    for (const SourceFile& f : in.libraries) {
        if (auto ok = check(f); !ok) {
            return Out::fail(ok.error());
        }
        int node = -1;
        auto plm = parse_plm(std::span<const std::uint8_t>(f.stored.data(), f.size));
        if (plm) {
            node = add_library_nodes(builder, plm.value(), f.game_path);
        } else {
            st.warnings.push_back(f.game_path + ": " + plm.error() + " (packed without geometry)");
        }
        packed.push_back({&f, "plm", node});
    }
    if (packed.empty()) {
        return Out::fail(in.group + ": no files to pack");
    }

    gltf::Document& doc = builder.document();
    const std::vector<int> area_roots = doc.scene_roots;
    if (area_roots.empty()) {
        // A document without nodes writes no scene; give the bytes a home that editors still open.
        gltf::Node empty;
        empty.name = in.group;
        builder.add_node(std::move(empty));
    }
    doc.scene_name = in.group;
    for (const MapScene& m : in.maps) {
        // add_node(-1) appends to the default scene's roots; the map node belongs to its own scene only.
        const int map_root = add_map_nodes(builder, m);
        doc.scene_roots.pop_back();
        gltf::Scene scene;
        scene.name = m.map;
        scene.roots = area_roots;
        scene.roots.push_back(map_root);
        json::Writer e;
        e.begin_object().member("map", m.map).member("data", "../" + m.map + "/map.json").end_object();
        scene.extras = e.take();
        doc.scenes.push_back(std::move(scene));
    }

    json::Writer ext;
    ext.begin_object();
    ext.key(kFilesExtension).begin_object();
    ext.member("version", kUnifiedVersion);
    ext.member("group", in.group);
    ext.key("files").begin_array();
    for (std::size_t i = 0; i < packed.size(); ++i) {
        const SourceFile& f = *packed[i].file;
        doc.blobs.push_back(f.stored);
        ext.begin_object();
        ext.member("path", f.game_path);
        ext.member("kind", packed[i].kind);
        ext.member("size", f.size);
        ext.member("stored_size", static_cast<std::uint64_t>(f.stored.size()));
        ext.member("crc32", crc32(f.stored.data(), static_cast<std::size_t>(f.size)));
        ext.member("bufferView", i);
        if (packed[i].node >= 0) {
            ext.member("node", packed[i].node);
        }
        ext.end_object();
        ++st.files;
        st.stored_bytes += f.stored.size();
    }
    ext.end_array();
    ext.end_object();
    ext.end_object();
    doc.extensions = ext.take();
    doc.extensions_used = {std::string(kFilesExtension)};
    if (!cells.empty()) {
        doc.extensions_used.push_back(std::string(kCollisionExtension));
    }

    json::Writer asset;
    asset.begin_object();
    asset.member("format", "sh-map-area");
    asset.member("version", kUnifiedVersion);
    asset.member("area", in.group);
    asset.key("maps").begin_array();
    for (const MapScene& m : in.maps) {
        asset.value(m.map);
    }
    asset.end_array();
    asset.member("cells", in.cells.size());
    asset.member("global_plm", in.global ? in.global->game_path : std::string());
    asset.member("unresolved_instances", st.unresolved_instances);
    asset.end_object();
    doc.extras = asset.take();

    st.gltf = builder.stats();
    return builder.write();
}

Result<UnifiedIndex> read_unified_index(const ByteSource& glb) {
    using Out = Result<UnifiedIndex>;
    std::uint8_t head[20];
    if (glb.size() < sizeof head || !glb.read_at(0, head, sizeof head)) {
        return Out::fail("not a .glb: shorter than its header");
    }
    if (get32(head) != kGlbMagic || get32(head + 4) != 2) {
        return Out::fail("not a glTF 2.0 binary");
    }
    const std::uint64_t total = get32(head + 8);
    const std::uint32_t json_size = get32(head + 12);
    if (total > glb.size() || get32(head + 16) != kChunkJson || 20ull + json_size + 8 > total) {
        return Out::fail("broken GLB header");
    }
    std::string text(json_size, '\0');
    if (!glb.read_at(20, text.data(), json_size)) {
        return Out::fail("cannot read the JSON chunk");
    }
    std::uint8_t bin_head[8];
    const std::uint64_t bin_at = 20ull + json_size;
    if (!glb.read_at(bin_at, bin_head, sizeof bin_head) || get32(bin_head + 4) != kChunkBin) {
        return Out::fail("no BIN chunk");
    }
    const std::uint64_t bin_start = bin_at + 8;
    const std::uint64_t bin_size = get32(bin_head);
    if (bin_start + bin_size > total) {
        return Out::fail("BIN chunk past the end of the file");
    }

    auto parsed = json::parse(text);
    if (!parsed) {
        return Out::fail("JSON chunk: " + parsed.error());
    }
    const json::Value& root = parsed.value();
    const json::Value* exts = root.find("extensions");
    const json::Value* ext = exts ? exts->find(kFilesExtension) : nullptr;
    if (!ext || !ext->is_object()) {
        return Out::fail("no " + std::string(kFilesExtension) + " extension: not a unified map file");
    }
    const json::Value* version = ext->find("version");
    if (!version || !version->is_integer() || version->integer() != kUnifiedVersion) {
        return Out::fail(std::string(kFilesExtension) + ": version " +
                         (version && version->is_integer() ? std::to_string(version->integer()) : "?") +
                         " is not supported (this build reads " + std::to_string(kUnifiedVersion) + ")");
    }
    const json::Value* views = root.find("bufferViews");
    const json::Value* files = ext->find("files");
    if (!views || !views->is_array() || !files || !files->is_array()) {
        return Out::fail(std::string(kFilesExtension) + ": no files or bufferViews");
    }
    auto u64 = [](const json::Value* v, std::uint64_t& out) {
        if (!v || !v->is_integer() || v->integer() < 0) {
            return false;
        }
        out = static_cast<std::uint64_t>(v->integer());
        return true;
    };

    UnifiedIndex index;
    if (const json::Value* g = ext->find("group"); g && g->is_string()) {
        index.group = g->string();
    }
    for (std::size_t i = 0; i < files->array().size(); ++i) {
        const json::Value& f = files->array()[i];
        const std::string where = std::string(kFilesExtension) + ".files[" + std::to_string(i) + "]: ";
        UnifiedEntry e;
        const json::Value* path = f.find("path");
        std::uint64_t view = 0, crc = 0;
        if (!path || !path->is_string() || path->string().empty() || !u64(f.find("size"), e.size) ||
            !u64(f.find("stored_size"), e.stored_size) || !u64(f.find("crc32"), crc) ||
            !u64(f.find("bufferView"), view) || crc > std::numeric_limits<std::uint32_t>::max()) {
            return Out::fail(where + "needs path, size, stored_size, crc32 and bufferView");
        }
        e.game_path = path->string();
        e.crc32 = static_cast<std::uint32_t>(crc);
        if (const json::Value* k = f.find("kind"); k && k->is_string()) {
            e.kind = k->string();
        }
        if (view >= views->array().size()) {
            return Out::fail(where + "bufferView " + std::to_string(view) + " does not exist");
        }
        const json::Value& bv = views->array()[view];
        std::uint64_t offset = 0, length = 0, buffer = 0;
        if (const json::Value* o = bv.find("byteOffset"); o && !u64(o, offset)) {
            return Out::fail(where + "bad byteOffset");
        }
        if (const json::Value* b = bv.find("buffer"); !u64(b, buffer) || buffer != 0 || !u64(bv.find("byteLength"), length)) {
            return Out::fail(where + "bad bufferView");
        }
        if (length != e.stored_size || e.size > e.stored_size || offset + length > bin_size) {
            return Out::fail(where + "bufferView does not hold the " + std::to_string(e.stored_size) + " stored bytes");
        }
        e.glb_offset = bin_start + offset;
        index.files.push_back(std::move(e));
    }
    return Out::ok(std::move(index));
}

Result<std::vector<std::uint8_t>> read_unified_entry(const ByteSource& glb, const UnifiedEntry& entry) {
    using Out = Result<std::vector<std::uint8_t>>;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(entry.stored_size));
    if (!glb.read_at(entry.glb_offset, bytes.data(), bytes.size())) {
        return Out::fail(entry.game_path + ": cannot read its bytes");
    }
    if (crc32(bytes.data(), static_cast<std::size_t>(entry.size)) != entry.crc32) {
        return Out::fail(entry.game_path + ": CRC-32 differs from the one recorded (the .glb is damaged)");
    }
    return Out::ok(std::move(bytes));
}

}  // namespace sh::convert::level
