#include "engine/maps/sh_maps.h"

#include "engine/maps/map_runtime.h"
#include "engine/maps/map_store.h"
#include "engine/maps/spawn_overrides.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {

namespace md = sh::convert::mapdata;

std::unique_ptr<sh::maps::MapStore>& store() {
    static std::unique_ptr<sh::maps::MapStore> instance;
    return instance;
}

void log_stderr(int level, const char* message) {
    std::fprintf(stderr, "%s%s\n", level == SH_MAPS_LOG_WARN ? "warning: " : "", message);
}

static_assert(md::kAllKinds.size() == SH_MAP_KIND_COUNT);
static_assert(static_cast<int>(md::Kind::Spawns) == SH_MAP_KIND_SPAWNS &&
              static_cast<int>(md::Kind::Collision) == SH_MAP_KIND_COLLISION);

/* The spawn_overrides of the last ShMaps_Load, kept for ShMaps_SpawnOverridesSelect: the
 * selector needs the full condition trees, which the C ShMapData does not carry. */
std::vector<md::SpawnOverride>& last_spawn_overrides() {
    static std::vector<md::SpawnOverride> instance;
    return instance;
}

/* The spawn_variants of the last ShMaps_Load, rows already resolved. */
md::SpawnVariants& last_spawn_variants() {
    static md::SpawnVariants instance;
    return instance;
}

void (*&log_fn())(int, const char*) {
    static void (*instance)(int, const char*) = log_stderr;
    return instance;
}

/* A variant without inline rows takes the map's imported table of its name. Without one the variants
 * are dropped with a warning: a half-filled table would spawn monsters at the origin. */
void resolve_variants(const std::string& map, const md::MapData& data, md::SpawnVariants& variants) {
    const sh::Result<void> resolved = md::resolve_spawn_variant_rows(data.spawn_variant_rows, variants);
    if (!resolved) {
        log_fn()(SH_MAPS_LOG_WARN, (map + ": spawn variants dropped: " + resolved.error()).c_str());
        variants = md::SpawnVariants{};
    }
}

}  // namespace

extern "C" void ShMaps_Configure(const ShMapsConfig* c) {
    sh::maps::StoreConfig config;
    if (c->gamedataDir) {
        config.gamedata_dir = c->gamedataDir;
    }
    if (c->modsDir && *c->modsDir) {
        config.mods_dir = c->modsDir;
    }
    void (*log)(int, const char*) = c->log ? c->log : log_stderr;
    log_fn() = log;
    config.log = [log](int level, const std::string& message) { log(level, message.c_str()); };
    store() = std::make_unique<sh::maps::MapStore>(std::move(config));
}

extern "C" int ShMaps_Load(const char* map, ShMapData* out) {
    if (!store()) {
        const ShMapsConfig defaults{};
        ShMaps_Configure(&defaults);
    }
    std::memset(out, 0, sizeof *out);
    const sh::maps::MapLoad loaded = store()->load(map ? map : "");
    int found = 0;
    for (int k = 0; k < SH_MAP_KIND_COUNT; ++k) {
        out->source[k] = static_cast<std::uint8_t>(loaded.kinds[k].source);
        std::snprintf(out->path[k], sizeof out->path[k], "%s", loaded.kinds[k].path.c_str());
        found += loaded.kinds[k].source != sh::maps::Source::None;
    }
    static_assert(static_cast<int>(sh::maps::Source::Mod) == SH_MAP_SOURCE_MOD);
    const md::MapData& d = loaded.data;
    out->header = d.header;
    out->spawns = d.spawns;
    out->pointCount = static_cast<std::uint16_t>(std::min<std::size_t>(d.points.size(), SH_MAP_POINTS_MAX));
    if (out->pointCount != 0) {  // an empty vector's data() may be null, which memcpy must not get
        std::memcpy(out->points, d.points.data(), out->pointCount * sizeof(ShMapPoint));
    }
    out->eventCount = static_cast<std::uint16_t>(std::min<std::size_t>(d.events.size(), SH_MAP_EVENTS_MAX));
    if (out->eventCount != 0) {
        std::memcpy(out->events, d.events.data(), out->eventCount * sizeof(ShMapEvent));
    }
    std::memcpy(out->cameras, d.cameras.data(), sizeof out->cameras);
    std::memcpy(out->collision, d.collision.data(), sizeof out->collision);
    sh::maps::MapRuntime::Kinds kinds;
    kinds.header = out->source[SH_MAP_KIND_HEADER] != SH_MAP_SOURCE_NONE;
    kinds.spawns = out->source[SH_MAP_KIND_SPAWNS] != SH_MAP_SOURCE_NONE;
    kinds.points = out->source[SH_MAP_KIND_POINTS] != SH_MAP_SOURCE_NONE;
    kinds.events = out->source[SH_MAP_KIND_EVENTS] != SH_MAP_SOURCE_NONE;
    kinds.cameras = out->source[SH_MAP_KIND_CAMERAS] != SH_MAP_SOURCE_NONE;
    kinds.collision = out->source[SH_MAP_KIND_COLLISION] != SH_MAP_SOURCE_NONE;
    sh::maps::map_runtime().load_data(d, kinds);
    last_spawn_overrides() = d.spawn_overrides;
    last_spawn_variants() = d.spawn_variants;
    resolve_variants(map ? map : "", d, last_spawn_variants());
    return found;
}

extern "C" int ShMaps_AmbientAudioOf(const char* map) {
    if (!store()) {
        const ShMapsConfig defaults{};
        ShMaps_Configure(&defaults);
    }
    const sh::maps::MapLoad loaded = store()->load(map ? map : "");
    if (loaded.kinds[SH_MAP_KIND_HEADER].source == sh::maps::Source::None) {
        return -1;
    }
    return loaded.data.header.ambientAudioIdx;
}

extern "C" int ShMaps_SpawnOverridesSelect(int difficulty, ShMapsSpawnFlagGet flag_get, void* user,
                                           ShMapSpawnWrite* out, int capacity) {
    if (last_spawn_overrides().empty()) {
        return 0;
    }
    const sh::maps::SpawnFlagGet get = [flag_get, user](const std::string& name) {
        return flag_get != nullptr && flag_get(user, name.c_str()) != 0;
    };
    const std::vector<const md::SpawnFlagSet*> selected =
        sh::maps::select_spawn_overrides(last_spawn_overrides(), difficulty, get);
    int filled = 0;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        if (out != nullptr && (int)i < capacity) {
            ShMapSpawnWrite& w = out[i];
            const md::SpawnFlagSet& s = *selected[i];
            w.chara = s.chara.c_str();
            w.slot = s.slot;
            w.flagCount = (int)std::min<std::size_t>(s.flags.size(), 8);
            for (int f = 0; f < w.flagCount; ++f) {
                w.flags[f] = s.flags[f].c_str();
            }
        }
        ++filled;
    }
    return filled;
}

extern "C" int ShMaps_SpawnVariantSelect(int difficulty, ShMapsSpawnFlagGet flag_get, void* user,
                                         ShMapSpawnVariantPick* out) {
    const md::SpawnVariants& variants = last_spawn_variants();
    if (!variants.present() || out == nullptr) {
        return 0;
    }
    const sh::maps::SpawnFlagGet get = [flag_get, user](const std::string& name) {
        return flag_get != nullptr && flag_get(user, name.c_str()) != 0;
    };
    const md::SpawnVariantChoice* choice = sh::maps::select_spawn_variant(variants, difficulty, get);
    const md::SpawnVariant* variant = choice ? sh::maps::find_spawn_variant(variants, choice->variant) : nullptr;
    if (variant == nullptr || !variant->rows_resolved) {
        return 0;
    }
    out->variant = variant->name.c_str();
    out->rows = variant->rows.data();
    out->enemyStateActive = variants.enemy_state_active;
    out->enemyStateLoad = variant->enemy_state_load;
    out->enemyStateSave = variant->enemy_state_save;
    out->setFlagCount = (int)std::min<std::size_t>(choice->set_flags.size(), 8);
    for (int f = 0; f < out->setFlagCount; ++f) {
        out->setFlags[f] = choice->set_flags[f].c_str();
    }
    return 1;
}

extern "C" int ShMaps_CharaNumberOf(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    return md::chara_number_of(name);
}

extern "C" const char* ShMaps_KindName(int kind) {
    if (kind < 0 || kind >= SH_MAP_KIND_COUNT) {
        return "?";
    }
    return md::kind_name(md::kAllKinds[kind]).data();
}
