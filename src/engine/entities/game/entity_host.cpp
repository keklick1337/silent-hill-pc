// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/game/entity_host.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "engine/assets/glb_anims.h"
#include "engine/chara/behavior_state.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/game/clip_info.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/chara/runtime/chara_spawn.h"
#include "engine/entities/anim_bank.h"
#include "engine/entities/anim_player.h"
#include "engine/entities/character_record.h"
#include "engine/entities/entity_script.h"
#include "engine/entities/entity_world.h"
#include "engine/game_state/game_ids.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/script/sh_script.h"
#include "engine/vfs/vfs_c.h"
#include "render/precision/pc_smooth_xform.h"
#include "engine/chara/game/game_records.h"
#include "engine/world/game/character_world.h"
#include "engine/mods/data_reload.h"

extern "C" {
// The clip playback (src/engine/entities/game/model_runtime.cpp).
std::int32_t Anim_DurationGet(void* model, void* clip);
}  // extern "C"

extern "C" void Chara_ModelCharaIdClear(void* chara, std::int32_t unused0, std::int32_t unused1);

namespace ent = sh::entities;
namespace cg = sh::chara::game;
namespace wg = sh::world::game;
namespace ids = sh::game_state::chara;

namespace sh::entities::game {
namespace {

// The engine's own slots start past the room spawner's 32; the NPC masks cover only those.
constexpr int kLegacySlots = 32;
constexpr std::uint16_t kAnimVisible = 1u << 1;

// The layouts of every type (gamedata/chara/<Name>/state.json; a type without one uses its "state_layout" or
// "behavior" folder's), loaded on first use and again when the character library is replaced.
const sh::chara::BehaviorLayouts& layouts() {
    static sh::chara::BehaviorLayouts s_layouts;
    static const sh::chara::CharaLibrary* s_library = nullptr;
    const sh::chara::CharaLibrary* lib = sh::chara::game_library();
    // A library read again by `mods reload` may sit at the old address: the epoch tells.
    static std::uint32_t s_epoch = sh::mods::kNeverRead;
    const bool stale = sh::mods::data_stale(s_epoch);
    if (lib != nullptr && (stale || lib != s_library || !s_layouts.loaded())) {
        s_library = lib;
        std::vector<std::string> warnings;
        s_layouts.load(*lib, warnings);
        for (const std::string& w : warnings) {
            std::fprintf(stderr, "[BEHAVIOR-STATE] %s\n", w.c_str());
        }
    }
    return s_layouts;
}

// The bank loads the import's copy of a file, warns through the game's log and smooths through the renderer.
AnimBank& bank() {
    static bool installed = false;
    AnimBank& b = anim_bank();
    if (!installed) {
        installed = true;
        b.set_loader([](const std::string& game_path) -> std::shared_ptr<const AnimationSet> {
            // A file a character glb packs (Harry's base and map files in HERO.glb) comes from that glb, a mod's
            // copy of it first; a glb that no longer packs it leaves the file to convert/anim.
            static const auto packed = [] {
                const char* dir = ShVfs_GameDataDir();
                auto index = sh::assets::packed_anim_index(std::filesystem::path(dir != nullptr ? dir : "") / "convert");
                SH_LOG_INFO(ANIM, "[ENTITY] %zu animation files packed in character glbs", index.size());
                return index;
            }();
            char host[1024];
            if (const auto it = packed.find(game_path);
                it != packed.end() && ShVfs_FindConverted(it->second.c_str(), host, sizeof(host))) {
                auto file = sh::assets::packed_anm(host, game_path);
                auto set = file ? AnimationSet::from_anm(file.value().file, game_path, file.value().base)
                                : sh::Result<AnimationSet>::fail(file.error());
                if (set) {
                    return std::make_shared<const AnimationSet>(std::move(set.value()));
                }
                SH_LOG_WARN(ANIM, "[ENTITY] %s; read from convert/anim instead", set.error().c_str());
            }
            // "ANIM/HB_BASE.ANM" -> convert/anim/ANIM/HB_BASE.anim.json (the import's name for it).
            std::string relative = "anim/" + game_path;
            if (relative.size() <= 4 || relative.compare(relative.size() - 4, 4, ".ANM") != 0) {
                return nullptr;
            }
            relative.replace(relative.size() - 4, 4, ".anim.json");
            if (!ShVfs_FindConverted(relative.c_str(), host, sizeof(host))) {
                return nullptr;
            }
            auto loaded = AnimationSet::load_json(host);
            if (!loaded) {
                SH_LOG_WARN(ANIM, "[ENTITY] %s", loaded.error().c_str());
                return nullptr;
            }
            return std::make_shared<const AnimationSet>(std::move(loaded.value()));
        });
        b.set_unresolved([](const void* hdr, int k0, int k1) {
            static unsigned long long count = 0;
            if (++count <= 8) {
                SH_LOG_WARN(ANIM, "[ENTITY] no animation data for %p keyframes %d/%d; the pose holds", hdr, k0, k1);
            }
        });
        AnimBank::Smoothing smoothing;
        smoothing.active = [] { return Pc_Smooth_Active() != 0; };
        smoothing.bone_local = [](BoneCoord* bone, const double* rotation, const double* translation) {
            Pc_Smooth_BoneLocal(bone, rotation, translation);
        };
        smoothing.copy_translation = [](BoneCoord* dst, const BoneCoord* src) { Pc_Smooth_BoneCopyTrans(dst, src); };
        b.set_smoothing(std::move(smoothing));
        const void* base = nullptr;
        const void* map = nullptr;
        cg::link::harry_anim_buffers(&base, &map);
        b.set_harry_buffers(base, map);
    }
    return b;
}

class GameEntityHost final : public EntityWorld::Host {
public:
    const sh::chara::CharacterDef* def(int chara_id) override {
        const sh::chara::CharaLibrary* lib = sh::chara::game_library();
        return lib != nullptr ? lib->find(chara_id) : nullptr;
    }

    const sh::chara::BehaviorLayout* var_layout(int chara_id) override {
        return layouts().of(chara_id);
    }

    const sh::chara::BehaviorLayout* var_layout_naming(std::string_view name) override {
        return layouts().naming(name);
    }

    // The bones the game poses for a slot. NPCs of one type share one set (each NPC poses it in its own
    // update), so an NPC's bones are its own right after its update.
    EntityWorld::BoneBinding bones(int slot) override {
        EntityWorld::BoneBinding b;
        if (slot != 0) {
            const int id = cg::slots().npcs[slot - 1].model.chara_id;
            if (id <= ids::kNone || id >= ids::kMaxTypes) {
                return b;
            }
        }
        const cg::link::Skeleton sk = cg::link::skeleton(slot);
        if (sk.bones != nullptr && sk.animation != nullptr) {
            b.bones = sk.bones;
            b.animation = bank().file_at(sk.animation);
        }
        return b;
    }

    int create(int chara_id, const Vec3& position, const Angles& rotation, int spawn_state) override {
        if (chara_id <= ids::kNone || chara_id >= ids::kMaxTypes) {
            return -1;
        }
        // The engine's own slots past the room spawner's 32 first: there the spawn has no spawn row of the
        // map (behaviours read slot >= 32 as "no row": home = where it stands, no reinforcements) and it
        // leaves the map's rows and radio ring alone; the room spawner's slots only when those are full.
        CharacterSlots& s = cg::slots();
        int npc = -1;
        for (int n = 0; n < kNpcSlotCount; ++n) {
            const int i = (n + kLegacySlots) % kNpcSlotCount;
            if (s.npcs[i].model.chara_id == ids::kNone) {
                npc = i;
                break;
            }
        }
        if (npc < 0) {
            return -1;
        }
        // The fill of the map's spawn without its per-map cap and dedup (a script asked for it), like the
        // console's SPAWN; no savegame identity.
        CharacterRecord& r = s.npcs[npc];
        std::memset(static_cast<void*>(&r), 0, sizeof(r));
        r.model.chara_id = chara_id;
        r.model.control_state = 0;
        r.model.control_step = static_cast<std::uint8_t>(spawn_state);
        r.partner = npc;
        r.position.x = position.x;
        r.position.z = position.z;
        cg::GroundSurface surface{};
        cg::world_record(surface) = wg::surface_at(position.x, position.z);
        r.position.y = surface.height;
        r.rotation.y = rotation.y;
        r.model.anim.flags |= kAnimVisible;
        if (npc < kLegacySlots) {
            sh::game::SessionScene& scene = sh::game::session().system.scene;
            scene.npc_flags |= static_cast<std::int32_t>(1u << npc);
            scene.spawned_groups |= static_cast<std::int32_t>(1u << npc);
        }
        g_PcNpcDebugSpawned[npc] = 1;
        g_PcNpcSpawnAi[npc] = 0;
        return npc + 1;
    }

    bool remove(int slot) override {
        if (slot <= 0 || slot > kNpcSlotCount || cg::slots().npcs[slot - 1].model.chara_id == ids::kNone) {
            return false;
        }
        Chara_ModelCharaIdClear(&cg::slots().npcs[slot - 1], 0, 0);
        return true;
    }
};

class GameScriptHost final : public EntityScriptHost {
public:
    int chara_id(const char* name) override { return Pc_CharaIdFind(name); }
    int ai_mode(const char* name) override { return name != nullptr ? Pc_Spawn_AiFind(name) : PcSpawnAi_Auto; }
    int spawn(int chara_id, int state, int x, int z, int rot_y, int ai_mode) override {
        return Pc_Spawn_CharacterAi(chara_id, state, x, z, rot_y, ai_mode);
    }
    bool has_behavior(int slot) override { return Pc_CharaBehavior_ForSlot(slot - 1) != nullptr; }
    int spawn_choice(int slot) override { return g_PcNpcDebugSpawned[slot - 1] != 0 ? g_PcNpcSpawnAi[slot - 1] : 0; }
    bool engine_spawned(int slot) override { return g_PcNpcDebugSpawned[slot - 1] != 0; }
    std::string_view spawn_kind(int chara_id) override { return Pc_Chara_AiKind(chara_id); }
};

}  // namespace

EntityWorld& world() {
    EntityWorld& w = entity_world();
    if (w.host() == nullptr) {
        w.set_host(std::make_unique<GameEntityHost>());
    }
    return w;
}

std::optional<long long> var_get(int slot, std::string_view name) { return world().var(slot, name); }

bool var_set(int slot, std::string_view name, long long value) { return world().set_var(slot, name, value); }

void anim_file_loaded(std::int32_t file, void* data) {
    if (file == -1 || data == nullptr) {
        return;
    }
    sh::loader::DiscFiles& files = sh::loader::disc_files();
    bank().file_loaded(data, static_cast<std::size_t>(files.sector_bytes(file)), files.path(file).c_str());
}

void anim_file_rebind(void* data, std::size_t size, const std::string& game_path) {
    if (data == nullptr || size == 0) {
        return;
    }
    if (bank().load(game_path) == nullptr) {
        SH_LOG_WARN(ANIM, "[ENTITY] %s: no converted animation file; the carrier's keyframes stay", game_path.c_str());
        return;
    }
    bank().file_loaded(data, size, game_path);
}

}  // namespace sh::entities::game

namespace eg = sh::entities::game;

extern "C" {

// The script API binds its groups from its C ABI region.
void PcNpcView_BindScript(ShScriptGameApi* api) {
    static eg::GameScriptHost host;
    eg::world();
    ent::bind_entity_script(*api, host);
}

void PcEntityAnim_MemoryCopied(void* dest, const void* src, unsigned int size) {
    eg::bank().memory_copied(dest, src, size);
}

int PcEntityAnim_HarryMapKeyframeFirst(void) { return eg::bank().harry_map_keyframe_first(); }

void PcEntityAnim_HarryBankClear(void) { eg::bank().harry_bank_clear(); }

int PcEntityAnim_FileKeyframes(const char* game_path) {
    return game_path != nullptr ? eg::bank().file_keyframes(game_path) : -1;
}

int PcEntityAnim_HarryBankNext(void) { return eg::bank().harry_bank_next(); }

int PcEntityAnim_HarryBankAppend(const char* game_path, int local_first, int count) {
    return game_path != nullptr ? eg::bank().harry_bank_append(game_path, local_first, count) : -1;
}

void PcEntityAnim_BoneUpdate(void* hdr, ent::BoneCoord* coords, int k0, int k1, int alpha) {
    eg::bank().sample(hdr, coords, k0, k1, alpha);
}

void PcEntityAnim_Playback(int kind, ent::ModelRecord* model, void* hdr, ent::BoneCoord* coords, cg::ClipInfo* info) {
    if (model == nullptr || info == nullptr) {
        return;
    }
    ent::ClipEntry entry;
    entry.kind = static_cast<ent::ClipKind>(kind);
    entry.next_status = info->link;
    entry.start_keyframe = info->start_keyframe;
    entry.end_keyframe = info->end_keyframe;
    eg::bank().play(*model, hdr, coords, entry, [&] {
        return ent::anim_timestep(Anim_DurationGet(model, info), sh::loop::frame_delta(), ent::kAnimAdvances);
    });
}

}  // extern "C"

// Registration order: sh_script.h.
static ShScriptGameBinderEntry s_ScriptBinder(70, PcNpcView_BindScript);
