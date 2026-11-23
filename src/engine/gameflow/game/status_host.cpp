// The rumble update and the game state that loads the inventory screen on the engine
// (src/engine/gameflow/status_screen.h). The sounds a map stops come from
// gamedata/config/map_sound_stops.json.

#include "engine/ids/game_numbers.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/status_screen.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;
namespace gs = sh::game_state;

constexpr int kStreamStopSound = 19;
constexpr std::int8_t kRockDrillTap = 2;  // the rock drill's tap attack (weapon 2, tap input)

const gf::MapSoundStops& stops() {
    static gf::MapSoundStops loaded;
    static std::uint32_t loaded_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(loaded_epoch)) {
        loaded = [] {
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            const std::string gamedata = link::gamedata_path();
            std::vector<std::string> warnings;
            std::string source;
            const sh::Result<std::string> text =
                gf::read_config_file(gamedata, mods, "map_sound_stops.json", &source, warnings);
            for (const std::string& w : warnings) {
                SH_WARN("[GAMEFLOW] %s", w.c_str());
            }
            if (!text) {
                SH_ERR("[GAMEFLOW] config/map_sound_stops.json: %s", text.error().c_str());
                return gf::MapSoundStops{};
            }
            sh::Result<gf::MapSoundStops> parsed = gf::MapSoundStops::parse(text.value());
            if (!parsed) {
                SH_ERR("[GAMEFLOW] config/map_sound_stops.json: %s", parsed.error().c_str());
                return gf::MapSoundStops{};
            }
            SH_LOG("[GAMEFLOW] config/map_sound_stops.json loaded%s%s", source.empty() ? "" : " from mod ",
                   source.c_str());
            return parsed.value();
        }();
    }
    return loaded;
}

// The map's name from its number: the maps of an area are numbered one after the other.
std::string map_name_of(int index) {
    static const int kPerArea[] = {3, 7, 5, 7, 7, 4, 6, 4};
    for (int area = 0; area < 8; ++area) {
        if (index < kPerArea[area]) {
            char name[16];
            std::snprintf(name, sizeof(name), "map%d_s%02d", area, index);
            return name;
        }
        index -= kPerArea[area];
    }
    return "";
}

class StatusHost final : public gf::StatusScreenHost {
public:
    void wait_for_draw() override { link::wait_draw(); }
    void one_vblank_per_frame() override { link::set_interval_vblanks(1); }
    void reset_fade() override { link::screen_fade_reset(); }
    gf::RumbleInput rumble_input() const override {
        const auto& sys = sh::game::session().system;
        gf::RumbleInput in;
        in.level = sys.audio.gas_weapon_engine_level;
        in.drill_level = sys.audio.rock_drill_run_level;
        in.rock_drill = sys.combat.weapon_attack == kRockDrillTap;
        return in;
    }
    bool streaming_audio() override { return link::audio_streaming(); }
    void play_streaming_stop() override { gs::sound_command(kStreamStopSound); }
    void prepare_items() override {
        const int map = link::saved_map();
        link::inventory_map_set(map);
        link::load_map_item_textures(map);
    }
    void motion_blur() override { link::background_motion_blur_wait(); }
    bool files_loaded() override { return link::file_chunks_loaded(); }
    void open_inventory() override {
        sh::game::session().set_game_state_next(static_cast<int>(gs::GameState::InventoryScreen));
    }

    void rumble_reset() override { link::gas_sound_contact(0); }
    void rumble_main(bool normal, std::int32_t amount) override { link::gas_sound_motor(normal, amount); }
    void rumble_drill(std::int32_t amount) override { link::gas_sound_drill_run(amount); }
    std::string map_name() const override { return map_name_of(link::saved_map()); }
    void stop_sound(const std::string& name) override {
        const int number = link::sound_number(name.c_str());
        if (number < 0) {
            SH_ERR("[GAMEFLOW] map_sound_stops.json: unknown sound \"%s\"", name.c_str());
            return;
        }
        link::stop_sound(number);
    }
};

}  // namespace

extern "C" void GameState_ControllerVibrationUpdate(void) {
    StatusHost h;
    gf::rumble_update(h, h.rumble_input(), stops());
}

extern "C" void GameState_LoadStatusScreen_Update(void) {
    StatusHost h;
    gf::load_status_screen(h, stops());
}
