// The composite event helpers against their specification (the original's event helpers): the order of their effects,
// what they do on each sub-step, the ammo multiplier and the voice guard, and the data files.

#include <string>
#include <vector>

#include "engine/gameflow/composite_data.h"
#include "engine/gameflow/event_composites.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

using Log = std::vector<std::string>;

class Host final : public gf::CompositeHost {
public:
    std::int32_t s[3] = {0, 0, 0};
    std::int32_t hold_v = 0, timer_v = 0;
    Log log;
    bool idle = true, done = true, loaded = true, animate = true, ended = false, redisplay = false, stopped = true;
    gf::MessageResult msg = gf::MessageResult::Showing;
    int mult = 1;

    std::int32_t step(int l) const override { return s[l]; }
    void store_step(int l, std::int32_t v) override { s[l] = v; }
    std::int32_t hold() const override { return hold_v; }
    void store_hold(std::int32_t v) override { hold_v = v; }
    std::int32_t timer() const override { return timer_v; }
    void store_timer(std::int32_t v) override { timer_v = v; }
    std::int32_t raw_delta() const override { return 136; }
    bool player_stopped() const override { return stopped; }
    bool flag(std::int16_t) const override { return false; }
    gf::MessageResult show_message(int m) override { log.push_back("message " + std::to_string(m)); return msg; }

    void set_fade_rate(std::int32_t r) override { log.push_back("rate " + std::to_string(r)); }
    void start_fade(bool in, bool) override { log.push_back(in ? "fade in" : "fade out"); }
    void show_borders() override {}
    void hide_borders() override {}
    void mark_cutscene() override {}
    bool fade_idle() const override { return idle; }
    bool fade_done() const override { return done; }
    bool borders_hidden() const override { return true; }
    bool borders_shown() const override { return true; }
    void load_background(int f, bool) override { log.push_back("load " + std::to_string(f)); }
    bool files_loaded() override { return loaded; }
    void draw_background(bool) override { log.push_back("draw"); }
    void store_vram() override {}
    void restore_vram() override {}
    void load_item_model(int i) override { log.push_back("model " + std::to_string(i)); }
    void show_item_pickup(int i) override { log.push_back("pickup " + std::to_string(i)); }
    void add_item(int i, int c) override { log.push_back("add " + std::to_string(i) + " x" + std::to_string(c)); }

    void freeze_player() override { log.push_back("freeze"); }
    void unfreeze_player() override { log.push_back("unfreeze"); }
    void resume_gameplay() override { log.push_back("gameplay"); }
    bool world_frozen() const override { return false; }
    bool hostile_npc_alive() const override { return false; }
    void stop_frame_time() override { log.push_back("stop time"); }
    bool confirm_pressed() override { return false; }
    void set_background_gamma(std::int32_t g) override { log.push_back("gamma " + std::to_string(g)); }
    void play_sound(int s_, const gf::Waypoint*) override { log.push_back("sound " + std::to_string(s_)); }
    void set_flag(std::int16_t f, bool on) override { log.push_back(std::string(on ? "flag " : "unflag ") + std::to_string(f)); }

    bool is_player(gf::CharaHandle c) const override { return c == &player; }
    void anim_set_state(gf::CharaHandle, std::int32_t st) override { log.push_back("anim " + std::to_string(st)); }
    bool anim_ended(gf::CharaHandle) override { return ended; }
    void anim_lock(gf::CharaHandle) override { log.push_back("lock"); }
    void anim_unlock(gf::CharaHandle) override { log.push_back("unlock"); }
    void anim_reset(gf::CharaHandle) override { log.push_back("reset"); }

    gf::MessageResult show_voiced_message(int) override { return msg; }
    bool voice_redisplay_consume() override { return redisplay; }
    void voice_dropped(bool d, int, int, int) override { log.push_back(d ? "dropped" : "audible"); }
    void play_voice(int c) override { log.push_back("voice " + std::to_string(c)); }

    void pickup_begin() override {}
    bool refuse = false;
    int count_to = -1;
    bool pickup_starting(int item, int count, int flag) override {
        log.push_back("starting " + std::to_string(item) + " x" + std::to_string(count) + " " + std::to_string(flag));
        return !refuse;
    }
    void pickup_count(int, int, int& count) override {
        if (count_to >= 0) count = count_to;
    }
    void pickup_added(int item, int count, int) override {
        log.push_back("added " + std::to_string(item) + " x" + std::to_string(count));
    }
    void pickup_hold_world() override {}
    bool pickup_animate(int) override { return animate; }
    void pickup_end() override { log.push_back("pose back"); }
    int ammo_pickup_count(int) override { return 15; }
    int ammo_multiplier() override { return mult; }

    void map_seek(int p) override { log.push_back("seek " + std::to_string(p)); }
    void map_store_vram(gf::VramArea) override { log.push_back("store"); }
    void map_restore_vram(gf::VramArea) override { log.push_back("restore"); }
    void map_read_page(int p, bool m) override { log.push_back("read " + std::to_string(p) + (m ? " marks" : "")); }
    void map_enter_screen(bool) override { log.push_back("enter"); }
    void map_swap_buffers() override { log.push_back("swap"); }
    void map_wait_files() override { log.push_back("wait files"); }
    void map_leave_screen() override { log.push_back("leave"); }
    void map_draw() override { log.push_back("map"); }
    void map_mark_found(int p) override { log.push_back("found " + std::to_string(p)); }
    void map_mark_found_mask(std::uint32_t m) override { log.push_back("found mask " + std::to_string(m)); }

    gf::CameraParams camera_position_defaults() const override { return {1, 2, 3, 4}; }
    gf::CameraParams camera_watch_defaults() const override { return {5, 6, 7, 8}; }
    void camera_move(const gf::Waypoint& t, const gf::CameraParams& p, bool w) override {
        log.push_back("move " + std::to_string(t.x) + "," + std::to_string(t.y) + "," + std::to_string(t.z) + " " +
                      std::to_string(p.accel_a) + std::to_string(p.accel_b) + std::to_string(p.max_a) + std::to_string(p.max_b) +
                      (w ? " warp" : ""));
    }
    void camera_watch(const gf::Waypoint& t, const gf::CameraParams& p, bool) override {
        log.push_back("watch " + std::to_string(t.x) + " " + std::to_string(p.accel_a) + std::to_string(p.max_b));
    }

    int player = 0;
};

}  // namespace

SH_TEST(composites_camera_uses_defaults_for_zero_and_offsets_from_base) {
    Host h;
    const gf::Waypoint base{100, 200, 300};
    gf::composites::camera_position(h, &base, {1, 2, 3}, {0, 9, 0, 0}, true);
    SH_CHECK((h.log == Log{"move 101,202,303 1934 warp"}));
    h.log.clear();
    gf::composites::camera_look_at(h, nullptr, {7, 0, 0}, {0, 0, 0, 5}, false);
    SH_CHECK((h.log == Log{"watch 7 55"}));
}

SH_TEST(composites_item_take_yes_adds_and_keeps_flag) {
    Host h;
    gf::composites::item_take(h, 3, 2, 77, 5);  // step 0: freeze, load, then wait for the load (the fade-through)
    SH_CHECK(h.log.size() >= 3 && h.log[0] == "starting 3 x2 77" && h.log[1] == "freeze" && h.log[2] == "model 3");
    SH_CHECK(h.s[1] >= 1);
    h.log.clear();
    h.s[1] = 2;
    gf::composites::item_take(h, 3, 2, 77, 5);
    SH_CHECK((h.log == Log{"message 5", "flag 77"}));
    h.log.clear();
    h.s[1] = 3;
    gf::composites::item_take(h, 3, 2, 77, 5);
    SH_CHECK((h.log == Log{"add 3 x2", "added 3 x2", "unfreeze", "pose back", "gameplay"}));
}

SH_TEST(composites_item_take_hooks_cancel_and_count) {
    Host h;
    h.refuse = true;
    gf::composites::item_take(h, 3, 2, 77, 5);  // cancelled at the start: nothing frozen, loaded or flagged
    SH_CHECK((h.log == Log{"starting 3 x2 77", "gameplay"}));
    SH_CHECK(h.s[1] == 0);
    h.log.clear();
    h.refuse = false;
    h.count_to = 9;
    h.s[1] = 3;
    gf::composites::item_take(h, 3, 2, 77, 5);  // the count a hook set is what is added
    SH_CHECK((h.log == Log{"add 3 x9", "added 3 x9", "unfreeze", "pose back", "gameplay"}));
}

SH_TEST(composites_item_take_no_clears_the_flag) {
    Host h;
    h.s[1] = -1;
    gf::composites::item_take(h, 3, 1, 77, 5);
    SH_CHECK((h.log == Log{"unflag 77", "unfreeze", "pose back", "gameplay"}));
}

SH_TEST(composites_common_pickup_ammo_scales_by_the_multiplier) {
    const gf::CommonPickup table[] = {{10, 5, false}, {11, 8, true}};
    Host h;
    h.mult = 2;
    h.s[1] = 3;
    gf::composites::common_item_take(h, table, 2, 1, 4);
    SH_CHECK(h.log[0] == "add 11 x30");
    h.log.clear();
    h.s[1] = 3;
    gf::composites::common_item_take(h, table, 2, 0, 4);
    SH_CHECK(h.log[0] == "add 10 x1");
    h.log.clear();
    gf::composites::common_item_take(h, table, 2, 9, 4);  // not a pickup: nothing happens
    SH_CHECK(h.log.empty());
}

SH_TEST(composites_paper_map_marks_the_page_and_its_reveals) {
    const gf::PaperMapReveal reveals[] = {{6, 0xAA}, {2, 0x8}};
    Host h;
    h.s[1] = 4;
    gf::composites::paper_map_take(h, reveals, 2, 6, 9, 12);
    SH_CHECK(h.log.size() >= 6);
    SH_CHECK((Log(h.log.begin(), h.log.begin() + 6) == Log{"stop time", "found 6", "found mask 170", "flag 9", "gamma 88", "map"}));
    h.log.clear();
    h.s[1] = 4;
    gf::composites::paper_map_take(h, reveals, 2, 7, 9, 12);  // no reveal entry
    SH_CHECK(h.log[1] == "found 7" && h.log[2] == "flag 9");
}

SH_TEST(composites_voice_guard_drops_commands_out_of_range) {
    const std::uint16_t cmds[] = {0x1005, 0x0200, 0x2000};
    Host h;
    h.msg = gf::MessageResult::Finished;
    std::uint8_t idx = 0;
    gf::composites::map_msg_with_audio(h, 1, idx, cmds);
    gf::composites::map_msg_with_audio(h, 1, idx, cmds);
    gf::composites::map_msg_with_audio(h, 1, idx, cmds);
    SH_CHECK((h.log == Log{"audible", "voice 4101", "dropped", "dropped"}));
    SH_CHECK(idx == 3);
    h.log.clear();
    h.redisplay = true;  // a re-display plays nothing and keeps the index
    gf::composites::map_msg_with_audio(h, 1, idx, cmds);
    SH_CHECK(h.log.empty() && idx == 3);
}

SH_TEST(composites_anim_play_to_end_waits_for_the_end) {
    Host h;
    gf::composites::chara_anim_play_to_end(h, &h.player, 4);
    SH_CHECK((h.log == Log{"anim 4"}) && h.s[1] == 1);
    gf::composites::chara_anim_play_to_end(h, &h.player, 4);
    SH_CHECK(h.s[1] == 1);
    h.ended = true;
    gf::composites::chara_anim_play_to_end(h, &h.player, 4);
    SH_CHECK(h.s[1] == 2);
    gf::composites::chara_anim_play_to_end(h, &h.player, 4);
    SH_CHECK(h.s[0] == 1 && h.s[1] == 0);
}

SH_TEST(composites_reset_unlocks_only_the_player) {
    Host h;
    int other = 0;
    gf::composites::chara_anim(h, gf::CharaAnimCommand::Reset, &h.player, 0, false);
    gf::composites::chara_anim(h, gf::CharaAnimCommand::Reset, &other, 0, false);
    SH_CHECK((h.log == Log{"unlock", "reset", "reset"}));
}

SH_TEST(composites_data_files_parse) {
    const auto pickups = gf::PickupTable::parse(
        R"({"pickups":[{"item":"A","message":"M"},{"item":"B","message":"N","ammo":true}]})");
    SH_CHECK(static_cast<bool>(pickups));
    SH_CHECK(pickups.value().entries().size() == 2 && pickups.value().entries()[1].ammo);
    SH_CHECK(!gf::PickupTable::parse(R"({"pickups":[{"item":"A"}]})"));
    const auto reveals = gf::PaperMapReveals::parse(R"({"reveals":[{"page":"P","reveals":["Q","R"]}]})");
    SH_CHECK(static_cast<bool>(reveals) && reveals.value().entries()[0].revealed.size() == 2);
    SH_CHECK(!gf::PaperMapReveals::parse(R"({"reveals":[{"page":"P","reveals":[1]}]})"));
}
