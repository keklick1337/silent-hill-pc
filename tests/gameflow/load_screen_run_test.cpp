// The loading screen frame's call order: the one-time setup runs only in the gameplay state and ends by
// advancing it; the per-frame part always follows. The original's order is written out here.

#include <string>

#include "engine/gameflow/load_screen_run.h"
#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {
struct Rec final : gf::LoadScreenHost {
    int gameplay = 0, overlay = 0;
    std::string log;
    void add(const char* s) { log += s; log += ' '; }
    int state_is_gameplay() const override { return gameplay; }
    int overlay_transition() const override { return overlay; }
    void area_player_position_update() override { add("area"); }
    void camera_init() override { add("cinit"); }
    void collision_triggers_set() override { add("coll"); }
    void player_placement(std::int32_t& x, std::int32_t& z, std::int32_t& yaw) const override { x = 4096; z = 0; yaw = 0; }
    void camera_watch_target(const gf::LoadScreenTarget& t) override { add("watch"); log += std::to_string(t.x) + "," + std::to_string(t.y) + "," + std::to_string(t.z) + " "; }
    void camera_position_target(const gf::LoadScreenTarget& t) override { add("cam"); log += std::to_string(t.y) + " "; }
    void spotlight_attribs_fix() override { add("spot"); }
    void map_effects_update() override { add("fx"); }
    void player_pose(std::int32_t t, std::int32_t h) override { add("pose"); log += std::to_string(t) + "," + std::to_string(h) + " "; }
    void player_status_save() override { add("save"); }
    void player_matrix_set() override { add("mat"); }
    void state_advance() override { add("adv"); }
    void player_anim_flags_set() override { add("flags"); }
    void bones_reset() override { add("bones"); }
    void neutral_light_set(std::int32_t t) override { add("light"); log += std::to_string(t) + " "; }
    void fog_off() override { add("fog"); }
    void flashlight_pass_off() override { add("fpass"); }
    std::int32_t screen_dt() const override { return 77; }
    void anim_run(std::int32_t dt) override { add("anim"); log += std::to_string(dt) + " "; }
    void camera_move() override { add("cmove"); }
    void flashlight_update() override { add("flash"); }
    void player_draw() override { add("draw"); }
};
}  // namespace

SH_TEST(load_screen_run_per_frame_only) {
    Rec r;
    gf::load_screen_player_frame(r, gf::LoadScreenParams::builtin());
    SH_CHECK(r.log == "flags light 4096 fog fpass bones anim 77 cmove flash draw ");
}

SH_TEST(load_screen_run_setup_frame) {
    Rec r;
    r.gameplay = 1;
    r.overlay = 1;
    gf::load_screen_player_frame(r, gf::LoadScreenParams::builtin());
    SH_CHECK(r.log.rfind("area cinit coll watch 4096,-2457,0 cam -4096 spot fx pose 106496,819 save mat adv flags ", 0) == 0);
}
