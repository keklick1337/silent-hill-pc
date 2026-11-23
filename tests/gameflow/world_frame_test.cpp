#include "framework/sh_test.h"
#include "engine/gameflow/world_frame.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// The world frame (src/engine/gameflow/world_frame.*): the shipped data equals the built-in rules, and the step
// order with the soundtrack pause, the particle skip and the player draw gates.

namespace {

namespace gf = sh::gameflow;

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Steps final : gf::WorldFrameHost {
    std::string log, map = "map0_s00";
    int paused_reads = 0, pause_after = 99;
    bool visible = true;
    void add(const char* s) { log += log.empty() ? s : std::string(" ") + s; }
    void latch_chunks_ready() override { add("latch"); }
    bool music_paused() override { return ++paused_reads > pause_after; }
    void update_world_objects() override { add("objects"); }
    void cutscene_camera() override { add("cutcam"); }
    void crosshair() override { add("cross"); }
    void music_track() override { add("bgm"); }
    void restore_random() override { add("r"); }
    void collision_triggers() override { add("coll"); }
    void move_camera() override { add("cam"); }
    void alternate_cameras() override { add("alt"); }
    void map_frame_hook() override { add("hook"); }
    void update_player() override { add("player"); }
    void update_flashlight() override { add("light"); }
    std::string map_name() override { return map; }
    void update_particles() override { add("particles"); }
    bool player_visible() override { return visible; }
    void draw_player() override { add("drawplayer"); }
    void npc_spawn_pass() override { add("spawn"); }
    void update_npcs() override { add("npcs"); }
    void update_2d_effects() override { add("fx2d"); }
    void close_range_chunks() override { add("chunks"); }
    void sustain_positional_loops() override { add("loops"); }
    void draw_world() override { add("draw"); }
    void frame_world() override { add("frame"); }
    void advance_random() override { add("adv"); }
};

}  // namespace

SH_TEST(world_frame_shipped_equals_builtin) {
    const auto r = gf::WorldFrameRules::parse(read_file(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/config/world_frame.json"));
    SH_CHECK(static_cast<bool>(r));
    const gf::WorldFrameRules b = gf::WorldFrameRules::builtin();
    SH_CHECK(r.value().maps_without_particles == b.maps_without_particles && r.value().health_rumble == b.health_rumble);
    SH_CHECK(b.rumble_for(-1) == -1 && b.rumble_for(0) == 17 && b.rumble_for(6) == 12 && b.rumble_for(7) == -1);
    SH_CHECK(!static_cast<bool>(gf::WorldFrameRules::parse("{\"health_rumble\": [300]}")));
}

SH_TEST(world_frame_step_order) {
    const gf::WorldFrameRules rules = gf::WorldFrameRules::builtin();
    Steps a;
    gf::run_world_frame(a, rules);
    SH_CHECK(a.log == "latch objects cutcam cross bgm r r coll cam alt hook r player r light particles r drawplayer r "
                      "spawn npcs fx2d chunks loops draw frame adv");
    Steps b;
    b.map = "map7_s03";
    b.visible = false;
    gf::run_world_frame(b, rules);
    SH_CHECK(b.log == "latch objects cutcam cross bgm r r coll cam alt hook r player r light r r spawn npcs fx2d chunks "
                      "loops draw frame adv");
    // The soundtrack paused by its own update: the objects ran, the world stops after the music step.
    Steps c;
    c.pause_after = 1;
    gf::run_world_frame(c, rules);
    SH_CHECK(c.log == "latch objects cutcam cross bgm r r");
    Steps d;
    d.pause_after = 0;
    gf::run_world_frame(d, rules);
    SH_CHECK(d.log == "latch cutcam cross bgm r r");
}
