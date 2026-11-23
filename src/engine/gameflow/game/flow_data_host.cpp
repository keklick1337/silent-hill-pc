// The game flow data on the engine (src/engine/gameflow/flow_data.h):
// gamedata/config/door_sounds.json, game_over_tips.json and title_fog.json, loaded on first use, with the names
// turned into the numbers the sound and file code take.

#include "engine/ids/game_numbers.h"
#include <cstdint>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/log/sh_log.h"
#include "engine/loop/game_random.h"
#include "engine/mods/data_reload.h"

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;

constexpr std::int32_t kNoSound = sh::game_state::kSfxBase;  // the original's "no sound" entry

struct FlowData {
    gf::DoorSounds doors;
    std::vector<std::int32_t> door_open;  // sound numbers per set
    std::vector<std::int32_t> door_close;
    gf::GameOverTips tips;
    std::vector<std::int32_t> tip_files;  // file table index per tip
    gf::TitleFogPalette fog;
};

template <typename T>
T load_or_fail(const char* name, const std::string& gamedata, const std::string& mods) {
    std::vector<std::string> warnings;
    std::string source;
    const sh::Result<std::string> text = gf::read_config_file(gamedata, mods, name, &source, warnings);
    for (const std::string& w : warnings) {
        SH_WARN("[GAMEFLOW] %s", w.c_str());
    }
    if (!text) {
        SH_ERR("[GAMEFLOW] config/%s: %s", name, text.error().c_str());
        return T{};
    }
    sh::Result<T> parsed = T::parse(text.value());
    if (!parsed) {
        SH_ERR("[GAMEFLOW] config/%s: %s", name, parsed.error().c_str());
        return T{};
    }
    SH_LOG("[GAMEFLOW] config/%s loaded%s%s", name, source.empty() ? "" : " from mod ", source.c_str());
    return parsed.value();
}

std::int32_t sound_number(const std::string& name) {
    if (name.empty()) {
        return kNoSound;
    }
    const int number = link::sound_number(name.c_str());
    if (number < 0) {
        SH_ERR("[GAMEFLOW] door_sounds.json: unknown sound \"%s\"", name.c_str());
        return kNoSound;
    }
    return number;
}

const FlowData& data() {
    static FlowData loaded;
    static std::uint32_t loaded_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(loaded_epoch)) {
        loaded = [] {
            FlowData d;
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            const std::string gamedata = link::gamedata_path();
            d.doors = load_or_fail<gf::DoorSounds>("door_sounds.json", gamedata, mods);
            for (std::size_t i = 0; i < d.doors.count(); ++i) {
                d.door_open.push_back(sound_number(d.doors.set(i).open));
                d.door_close.push_back(sound_number(d.doors.set(i).close));
            }
            d.tips = load_or_fail<gf::GameOverTips>("game_over_tips.json", gamedata, mods);
            for (std::size_t i = 0; i < d.tips.count(); ++i) {
                const std::int32_t file = link::find_image_file(d.tips.tip(i).image.c_str());
                if (file < 0) {
                    SH_ERR("[GAMEFLOW] game_over_tips.json: no image file \"%s\"", d.tips.tip(i).image.c_str());
                }
                d.tip_files.push_back(file);
            }
            d.fog = load_or_fail<gf::TitleFogPalette>("title_fog.json", gamedata, mods);
            return d;
        }();
    }
    return loaded;
}

}  // namespace

extern "C" {

std::int32_t GameFlow_DoorOpenSound(std::int32_t set) {
    const FlowData& d = data();
    return set >= 0 && static_cast<std::size_t>(set) < d.door_open.size() ? d.door_open[set] : kNoSound;
}

std::int32_t GameFlow_DoorCloseSound(std::int32_t set) {
    const FlowData& d = data();
    return set >= 0 && static_cast<std::size_t>(set) < d.door_close.size() ? d.door_close[set] : kNoSound;
}

unsigned int GameFlow_DoorStartsLoading(std::int32_t set) {
    return set >= 0 && data().doors.set(static_cast<std::size_t>(set)).starts_loading;
}

std::int32_t GameFlow_GameOverTipPick(unsigned int darkScene) {
    const gf::GameOverTips& tips = data().tips;
    std::uint16_t seen = sh::game::session().game.settings.seen_game_over_tips;
    tips.restart_if_all_seen(seen);
    sh::game::session().game.settings.seen_game_over_tips = seen;

    const std::int32_t total = tips.total_weight(seen, darkScene != 0);
    if (total <= 0) {
        return 0;
    }
    const std::int32_t roll = sh::loop::game_random().next_int(0, total - 1);
    return static_cast<std::int32_t>(tips.pick(seen, darkScene != 0, roll));
}

std::int32_t GameFlow_GameOverTipFile(std::int32_t tip) {
    const FlowData& d = data();
    return tip >= 0 && static_cast<std::size_t>(tip) < d.tip_files.size() ? d.tip_files[tip] : -1;
}

void GameFlow_GameOverTipSeen(std::int32_t tip) {
    std::uint16_t seen = sh::game::session().game.settings.seen_game_over_tips;
    gf::GameOverTips::mark_seen(seen, static_cast<std::size_t>(tip));
    sh::game::session().game.settings.seen_game_over_tips = seen;
}

std::uint32_t GameFlow_TitleFogColor(std::int32_t heat) {
    const gf::TitleFogPalette::Rgb c = data().fog.color(static_cast<std::uint8_t>(heat));
    return static_cast<std::uint32_t>(c.r) | (static_cast<std::uint32_t>(c.g) << 8) |
           (static_cast<std::uint32_t>(c.b) << 16);
}

}  // extern "C"
