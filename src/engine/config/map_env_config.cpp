#include "engine/config/map_env_config.h"

#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/gameflow/flow_data.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::world {

const MapEnvParams& map_env_params() {
    static MapEnvParams loaded;
    static std::uint32_t loaded_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(loaded_epoch)) {
        loaded = [] {
            MapEnvParams p;
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::vector<std::string> warnings;
            std::string source;
            const Result<std::string> text =
                gameflow::read_config_file(PcPort_GetGameDataPath(), mods, "map_effects.json", &source, warnings);
            for (const std::string& w : warnings) SH_WARN("[MAPENV] %s", w.c_str());
            if (!text) {
                SH_ERR("[MAPENV] config/map_effects.json: %s", text.error().c_str());
                return p;
            }
            Result<MapEnvParams> parsed = MapEnvParams::parse(text.value());
            if (!parsed) {
                SH_ERR("[MAPENV] config/map_effects.json: %s", parsed.error().c_str());
                return p;
            }
            SH_LOG("[MAPENV] config/map_effects.json loaded%s%s", source.empty() ? "" : " from mod ", source.c_str());
            return parsed.value();
        }();
    }
    return loaded;
}

}  // namespace sh::world
