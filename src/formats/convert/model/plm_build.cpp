#include "formats/common/crc32.h"
#include "formats/convert/level/level_gltf.h"
#include "formats/convert/level/level_parse.h"
#include "formats/convert/model/model_build.h"
#include "formats/common/json.h"

#include <cstdio>

namespace sh::convert::model {

bool looks_like_plm(std::span<const std::uint8_t> raw) { return raw.size() >= 20 && raw[0] == 0x30 && raw[1] == 6; }

// Held items (ITEM/*.PLM) are LM model libraries, the map format: the level module parses and flattens them, and the
// texture rules are the game's (level_textures.h). The texture is named by the material: HERO (Harry's own sheet,
// CHARA/HERO.TIM) for the knife, guns, hammer and axe, the item's own ITEM/<NAME>.TIM for the others
// (WorldGfx_PlayerHeldItemSet).
Result<ConvertedModel> build_plm(std::span<const std::uint8_t> raw, std::string_view game_path, const Options& options) {
    using Out = Result<ConvertedModel>;
    auto plm = level::parse_plm(raw);
    if (!plm) {
        return Out::fail(plm.error());
    }
    std::optional<level::TextureCatalog> catalog;
    if (options.sibling) {
        catalog.emplace(options.sibling);
    }
    const std::string glb_path = "models/" + path_without_extension(game_path) + ".glb";
    level::GltfStats gstats;
    auto glb = level::plm_to_glb(plm.value(), game_path, glb_path, catalog ? &*catalog : nullptr, &gstats);
    if (!glb) {
        return Out::fail(glb.error());
    }

    ModelStats stats;
    stats.kind = "plm";
    stats.parts = plm.value().models.size();
    stats.rest_pose_source = "not applicable (the item is attached to a hand bone by the game)";
    stats.glb_vertices = gstats.vertices;
    stats.glb_triangles = gstats.triangles;

    json::Writer side(true);
    side.begin_object();
    side.member("format", "sh-model/1");
    side.key("source").begin_object();
    side.member("path", std::string(game_path));
    side.member("kind", "plm");
    side.member("size", raw.size());
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", crc32(raw.data(), raw.size()));
    side.member("crc32", crc);
    side.member("lossless", false);
    side.end_object();
    side.key("coordinates").begin_object();
    side.member("units_per_metre", kUnitsPerMetre);
    side.member("transform", "x' = -x/256, y' = -y/256, z' = z/256 (rotation by 180 degrees about Z)");
    side.member("quad", "GPU strip (0,1,2) and (1,3,2), each reversed for counter-clockwise front faces");
    side.member("uv", "(u + 0.5) / tim_width, (v + 0.5) / tim_height; V down, no flip");
    side.member("normals", "source normals negated (they point inward), turned like positions");
    side.end_object();
    side.key("materials").begin_array();
    for (const level::LmMaterial& m : plm.value().materials) {
        side.begin_object();
        side.member("name", m.name);
        side.member("offset", m.offset);
        const level::TimInfo* tim = catalog ? catalog->by_name(m.name) : nullptr;
        side.member("tim", tim != nullptr ? tim->game_path : std::string());
        if (tim != nullptr) {
            side.member("tim_width", tim->width);
            side.member("tim_height", tim->height);
            side.member("palettes", tim->palette_count);
            if (stats.texture.empty()) {
                stats.texture = tim->game_path;
            }
        }
        side.member("clut", m.clut);
        side.member("clut_base", m.clut_base);
        side.member("uv", m.uv);
        side.member("uv_base", m.uv_base);
        side.end_object();
    }
    side.end_array();
    side.key("models").begin_array();
    for (const level::LmModel& m : plm.value().models) {
        side.begin_object();
        side.member("name", m.name);
        side.member("offset", m.offset);
        side.member("meshes", m.meshes.size());
        std::size_t prims = 0;
        std::size_t verts = 0;
        for (const level::LmMesh& mesh : m.meshes) {
            prims += mesh.primitives.size();
            verts += mesh.vertices.size();
            for (const level::LmPrimitive& p : mesh.primitives) {
                stats.source_triangles += p.is_triangle() ? 1 : 2;
            }
        }
        side.member("primitives", prims);
        side.member("vertices", verts);
        stats.source_primitives += prims;
        stats.source_vertices += verts;
        side.end_object();
    }
    side.end_array();
    side.key("totals").begin_object();
    side.member("models", stats.parts);
    side.member("source_vertices", stats.source_vertices);
    side.member("source_primitives", stats.source_primitives);
    side.member("source_triangles", stats.source_triangles);
    side.member("glb_vertices", stats.glb_vertices);
    side.member("glb_triangles", stats.glb_triangles);
    side.member("palettes_outside_tim", gstats.palettes_outside_tim);
    side.end_object();
    side.end_object();

    ConvertedModel result;
    result.glb = std::move(glb.value());
    result.sidecar_json = side.take() + "\n";
    result.stats = std::move(stats);
    return Out::ok(std::move(result));
}

}  // namespace sh::convert::model
