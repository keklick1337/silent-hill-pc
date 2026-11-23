// The rumble update and the status screen load state against their specification (the original's
// GameState_ControllerVibrationUpdate and GameState_LoadStatusScreen_Update).

#include <string>
#include <vector>

#include "engine/gameflow/status_screen.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

using Log = std::vector<std::string>;

class Host final : public gf::StatusScreenHost {
public:
    Log log;
    int step_v = 0;
    std::string map = "map1_s00";
    gf::RumbleInput input;
    bool streaming = false, loaded = false;

    int step() const override { return step_v; }
    void advance_step() override { ++step_v; log.push_back("advance"); }
    void wait_for_draw() override { log.push_back("wait"); }
    void one_vblank_per_frame() override { log.push_back("vblank"); }
    void reset_fade() override { log.push_back("fade reset"); }
    gf::RumbleInput rumble_input() const override { return input; }
    bool streaming_audio() override { return streaming; }
    void play_streaming_stop() override { log.push_back("stream stop"); }
    void prepare_items() override { log.push_back("items"); }
    void motion_blur() override { log.push_back("blur"); }
    bool files_loaded() override { return loaded; }
    void open_inventory() override { log.push_back("inventory"); }
    void rumble_reset() override { log.push_back("reset"); }
    void rumble_main(bool normal, std::int32_t a) override { log.push_back(std::string("main ") + (normal ? "n " : "d ") + std::to_string(a)); }
    void rumble_drill(std::int32_t a) override { log.push_back("drill " + std::to_string(a)); }
    std::string map_name() const override { return map; }
    void stop_sound(const std::string& n) override { log.push_back("stop " + n); }
};

gf::MapSoundStops stops() {
    return gf::MapSoundStops::parse(R"({"stops":[{"map":"map3_s03","sounds":["a","b"]}]})").value();
}

}  // namespace

SH_TEST(status_rumble_above_threshold_scales_by_whole_units) {
    Host h;
    h.input.level = 256 * 4096 + 5 * 4096 + 100;
    gf::rumble_update(h, h.input, stops());
    SH_CHECK((h.log == Log{"reset", "main n 5"}));
}

SH_TEST(status_rumble_below_threshold_is_zero_and_drill_follows) {
    Host h;
    h.input.level = 100;
    h.input.rock_drill = true;
    gf::rumble_update(h, h.input, stops());
    SH_CHECK((h.log == Log{"reset", "main d 0", "drill 0"}));
    h.log.clear();
    h.input.level = 257 * 4096;
    h.input.drill_level = 256 * 4096 + 7 * 4096;
    gf::rumble_update(h, h.input, stops());
    SH_CHECK((h.log == Log{"reset", "main d 1", "drill 7"}));
}

SH_TEST(status_rumble_stops_the_sounds_of_the_map) {
    Host h;
    h.map = "map3_s03";
    gf::rumble_update(h, h.input, stops());
    SH_CHECK((h.log == Log{"reset", "main n 0", "stop a", "stop b"}));
}

SH_TEST(status_load_prepares_once_then_waits_for_files) {
    Host h;
    h.streaming = true;
    gf::load_status_screen(h, stops());
    SH_CHECK((h.log == Log{"wait", "vblank", "fade reset", "reset", "main n 0", "stream stop", "items", "advance", "blur"}));
    h.log.clear();
    h.loaded = true;
    gf::load_status_screen(h, stops());
    SH_CHECK((h.log == Log{"blur", "inventory"}));
}

SH_TEST(status_sound_stops_data_rejects_bad_entries) {
    SH_CHECK(!gf::MapSoundStops::parse(R"({"stops":[{"map":"x"}]})"));
    SH_CHECK(stops().sounds("map0_s00").empty());
}
