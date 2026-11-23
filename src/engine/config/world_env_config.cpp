// The game's atmosphere (src/engine/world/world_atmosphere.h) over the engine's storage, with the numbers of
// gamedata/config/world_env.json (a mod replaces the file whole), loaded on first use. Defaults when the file is
// missing or broken (logged).

#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/gameflow/flow_data.h"
#include "engine/log/sh_log.h"
#include "engine/world/world_atmosphere.h"
#include "engine/mods/data_reload.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::world {

namespace {

const WorldEnvParams& world_env_params() {
    static WorldEnvParams loaded;
    static std::uint32_t loaded_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(loaded_epoch)) {
        loaded = [] {
            WorldEnvParams p;
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::vector<std::string> warnings;
            std::string source;
            const Result<std::string> text =
                gameflow::read_config_file(PcPort_GetGameDataPath(), mods, "world_env.json", &source, warnings);
            for (const std::string& w : warnings) SH_WARN("[WORLDENV] %s", w.c_str());
            if (!text) {
                SH_ERR("[WORLDENV] config/world_env.json: %s", text.error().c_str());
                return p;
            }
            Result<WorldEnvParams> parsed = WorldEnvParams::parse(text.value());
            if (!parsed) {
                SH_ERR("[WORLDENV] config/world_env.json: %s", parsed.error().c_str());
                return p;
            }
            SH_LOG("[WORLDENV] config/world_env.json loaded%s%s", source.empty() ? "" : " from mod ", source.c_str());
            return parsed.value();
        }();
    }
    return loaded;
}

}  // namespace

WorldAtmosphere& atmosphere() {
    static WorldAtmosphere a(detail::g_atmosphere_state, world_env_params());
    return a;
}

}  // namespace sh::world
