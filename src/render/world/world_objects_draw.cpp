#include "render/world/world_objects_draw.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "engine/app/loader/game_files.h"
#include "engine/app/pc_paths.h"
#include "engine/camera/view_system.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/item_catalog.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/math/fixed_geometry.h"
#include "engine/runtime/cell_stream_runtime.h"
#include "engine/world/held_item.h"
#include "engine/world/cell_grid.h"
#include "engine/world/held_items.h"
#include "engine/world/skeleton.h"
#include "render/skinning/gltf_chara.h"
#include "render/world/world_scene.h"
#include "render/world/world_scene_source.h"
#include "engine/mods/data_reload.h"

extern "C" const char* PcPort_GetGameDataPath(void);
// The console's prop picker: may scale the object's frame (src/input/object_pick_host.cpp).
#include "input/object_pick.h"

namespace sh::world_scene {

namespace {

namespace cam = sh::camera;

// The player's hand bones (the skeleton order of the player model).
constexpr int kLeftHandBone = 6;
constexpr int kRightHandBone = 10;

Pose scene_pose(const cam::Matrix& m) {
    Pose p;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) p.m[r][c] = m.m[r][c];
        p.t[r] = m.t[r];
    }
    return p;
}

// The library an object's model is in, searched from where the player stands (the cell stream: the map's global
// library, then the player's cell indoors or the 3x3 cells around it nearest first outdoors), else the item library.
int find_library(const world::ObjectModel& model) {
    const std::string_view name = model.name_view();
    if (world::object_item_model(name) != nullptr) return world::kItemModelLibrary;
    const std::string name8(name);
    const entities::Vec3& at = game::session().system.characters.player.position;
    if (const int library = world::stream::object_library(name8.c_str(), at.x, at.z); library != 0) return library;
    return has_object(1, name) ? 1 : 0;
}

// Whether the library a model was found in still has it. A model keeps its library while it is not queued, and the
// stream meanwhile can fill that cell slot with another cell (map3_s01: the elevator doors were found in HP0100's
// slot, which held HP0001 by the time the player came back), so the next add looks the model up again instead of
// drawing from the wrong cell. The original makes the same check at draw time by comparing the model's name.
bool library_holds(const world::ObjectModel& model) {
    return model.library < 2 || has_object(model.library, model.name_view());
}

void draw_one(const world::QueuedObject& o) {
    cam::FrameRecord frame{};
    frame.local.t[0] = o.x;
    frame.local.t[1] = o.y;
    frame.local.t[2] = o.z;
    const math::Matrix3 rot = math::rot_matrix_zxy_neg({o.angle_x, o.angle_y, o.angle_z});
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) frame.local.m[r][c] = rot.m[r][c];
    sh::input::pick_prop_drawn(o.x, o.y, o.z);
    const cam::Matrix world = cam::frame_world(&frame);

    world::ObjectModel& model = *o.model;
    const int library = model.library;
    if (library == 0) return;
    if (library == world::kItemModelLibrary) {
        if (const world::ObjectItemModel* item = world::object_item_model(model.name_view()); item != nullptr)
            draw_item_model(item->file, scene_pose(world), item->scale, 1);
        else
            model.library = 0;
        return;
    }
    // A cell slot that holds no loaded cell: looked up again next frame (this draw still tries).
    if (library >= 3 && library - 3 < world::stream::slot_count() && !world::stream::slot_loaded(library - 3))
        model.library = 0;
    if (!draw_object(library, model.name_view(), scene_pose(world), 1) && library >= 2 &&
        !has_object(library, model.name_view()))
        model.library = 0;
}

// ---- held item table (gamedata/config/held_items.json) ----

const world::HeldItems& held_table() {
    static world::HeldItems loaded;
    static std::uint32_t loaded_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(loaded_epoch)) {
        loaded = [] {
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::vector<std::string> warnings;
            std::string source;
            const Result<std::string> text =
                gameflow::read_config_file(PcPort_GetGameDataPath(), mods, "held_items.json", &source, warnings);
            for (const std::string& w : warnings) SH_WARN("[HELD] %s", w.c_str());
            if (!text) {
                SH_ERR("[HELD] config/held_items.json: %s", text.error().c_str());
                return world::HeldItems{};
            }
            Result<world::HeldItems> parsed = world::HeldItems::parse(text.value());
            if (!parsed) {
                SH_ERR("[HELD] config/held_items.json: %s", parsed.error().c_str());
                return world::HeldItems{};
            }
            return std::move(parsed.value());
        }();
    }
    return loaded;
}

// The table entry of a run-time item number (-1: "none"), once the item data is loaded.
const world::HeldItemDef* held_entry(std::int32_t item) {
    static std::map<std::int32_t, const world::HeldItemDef*> by_number;
    static bool built = false;
    // The entries point into the table and the item numbers come from the library: both are read again by
    // `mods reload`.
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        by_number.clear();
        built = false;
    }
    if (!built) {
        const items::ItemLibrary* lib = inventory::item_catalog().library();
        if (lib == nullptr) return nullptr;
        built = true;
        for (const world::HeldItemDef& d : held_table().items) {
            if (d.item == "none") {
                by_number[world::HeldItemState::kNone] = &d;
                continue;
            }
            const items::ItemDef* def = lib->find_item(d.item, "config/held_items.json");
            if (def == nullptr || def->number == 0) {
                SH_ERR("[HELD] config/held_items.json: no item %s", d.item.c_str());
                continue;
            }
            by_number[def->number] = &d;
        }
    }
    const auto it = by_number.find(item);
    return it != by_number.end() ? it->second : nullptr;
}

int file_of(const std::string& path) {
    if (path.empty()) return loader::DiscFiles::kNone;
    const int file = loader::disc_files().find(path);
    if (file < 0) SH_ERR("[HELD] no game file %s", path.c_str());
    return file;
}

// The model bytes are only read so that waits on the ticket see the same reads; a read may still be landing when
// the next item is taken, so a buffer is never freed or moved, only a larger one added.
void* model_buffer(int file) {
    static std::deque<std::vector<std::uint8_t>> buffers;
    const std::size_t need = loader::disc_files().sector_bytes(file);
    if (buffers.empty() || buffers.back().size() < need) buffers.emplace_back(std::max<std::size_t>(need, 64 * 1024));
    return buffers.back().data();
}

}  // namespace

void object_add(world::ObjectModel& model, const world::ObjectPosition& pos, const world::ObjectRotation& rot) {
    if (!library_holds(model)) model.library = 0;
    world::object_queue().add(model, pos, rot, find_library);
}

void draw_objects() { world::object_queue().drain(draw_one); }

std::int32_t held_item_set(std::int32_t item) {
    world::HeldItemState& held = world::held_item();
    if (!held.change(item)) return 0;

    const world::HeldItemDef* def = held_entry(item);
    const int tex_file = def != nullptr ? file_of(def->texture) : loader::DiscFiles::kNone;
    const world::HeldItemImage& image = tex_file >= 0 ? held_table().image_own : held_table().image_shared;
    // The queue reads the place when the read lands.
    static loader::ImagePlace place;
    place = {{image.tpage[0], image.tpage[1]}, image.u, image.v, image.clut_x, image.clut_y};
    if (tex_file >= 0) held.set_ticket(loader::queue_read_image(tex_file, sh::memory::work_buffers().data(sh::memory::WorkBuffer::HeldItemImage), &place), false);

    const int model_file = def != nullptr ? file_of(def->model) : loader::DiscFiles::kNone;
    if (model_file >= 0) {
        held.set_ticket(loader::queue_read(model_file, model_buffer(model_file)), true);
        return held.ticket();
    }
    return 0;
}

std::int32_t held_item_weapon_set(std::int32_t weapon_attack) {
    // The item the weapon numbered `weapon_attack` is carried as (weapons/<Name>.json "item"); none for no weapon.
    std::int32_t item = world::HeldItemState::kNone;
    const items::ItemLibrary* library = inventory::item_catalog().library();
    const items::WeaponDef* weapon = library != nullptr && weapon_attack != -1 ? library->find_weapon(weapon_attack)
                                                                              : nullptr;
    if (weapon != nullptr && !weapon->item.empty()) {
        const int number = ids::item_number(weapon->item.c_str());
        item = number > 0 ? number : world::HeldItemState::kNone;
    }
    return held_item_set(item);
}

void draw_held_item_in_hand() {
    world::HeldItemState& held = world::held_item();
    if (held.item() == world::HeldItemState::kNone) return;
    if (!loader::queue_loaded(held.ticket())) return;
    if (!held.settle_loaded() || held.hidden()) return;

    const world::HeldItemDef* def = held_entry(held.item());
    const int bone = def != nullptr && def->left_hand ? kLeftHandBone : kRightHandBone;
    cam::FrameRecord* bones = game::session().system.characters.player_bones;
    cam::Matrix world = cam::frame_world(&bones[bone]);
    // A model with its own proportions has its hand elsewhere than the game's skeleton.
    constexpr int kPlayerChara = 1;
    int shift[3];
    if (Pc_GltfChara_BoneShift(source::chara_skeleton(kPlayerChara), bones, bone, shift) != 0) {
        for (int c = 0; c < 3; ++c) world.t[c] += shift[c];
    }
    if (def != nullptr && !def->model.empty()) draw_held_item(def->model, scene_pose(world), 1);
}

bool common_pickup_in_range(std::int32_t x, std::int32_t y, std::int32_t z) {
    // Interiors only: rooms are short and first person sees across a cell edge within a few steps. Outdoors keeps
    // the original per-cell items.
    if (world::stream::exterior()) return false;
    const entities::Vec3& p = game::session().system.characters.player.position;
    // The floor test keeps an item on a storey above or below from showing through a stairwell.
    const std::int32_t dy = y - p.y;
    if (dy > 3 * 4096 || dy < -3 * 4096) return false;
    constexpr std::int32_t kCell = 40 * 4096;
    const auto cell = [](std::int32_t v) { return v >= 0 ? v / kCell : (v - (kCell - 1)) / kCell; };
    const std::int32_t dx = cell(x) - cell(p.x);
    const std::int32_t dz = cell(z) - cell(p.z);
    return dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1;
}

}  // namespace sh::world_scene

// The held item's entry points under the names the game code and the script binds call.
namespace {

// A world object as the map records that place it carry it: the part record of a skeleton (its flags word cleared
// with a new name) followed by the engine's model slot.
struct WorldObjectRecord {
    sh::world::SkeletonPartModel part;
    sh::world::ObjectModel model;
};

}  // namespace

extern "C" {

void WorldGfx_ObjectAdd(WorldObjectRecord* object, const sh::world::ObjectPosition* pos,
                        const sh::world::ObjectRotation* rot) {
    sh::world_scene::object_add(object->model, *pos, *rot);
}

void WorldObject_ModelNameSet(WorldObjectRecord* object, char* name) {
    object->part.flags = 0;
    object->model.set_name(name);
}

std::int32_t WorldGfx_PlayerPrevHeldItem(sh::game::PlayerCombat* combat) {
    return sh::world_scene::held_item_weapon_set(combat->weapon_attack);
}

std::int32_t WorldGfx_PlayerHeldItemSet(std::int32_t item) { return sh::world_scene::held_item_set(item); }
void WorldGfx_HeldItemShow(void) { sh::world::held_item().show(); }
void WorldGfx_HeldItemHide(void) { sh::world::held_item().hide(); }
void Item_HeldItemModelFree(void) { sh::world::held_item().free(); }

// The item model library is looked up in the scene (BG_ITEM.glb); its disc file is read at boot all the same because the
// boot's waits count the reads.
void GameFs_BgItemLoad(void) {
    static std::uint8_t buffer[4096];
    sh::loader::queue_read(sh::loader::disc_files().find("BG/BG_ITEM.PLM"), buffer);
}

}  // extern "C"
