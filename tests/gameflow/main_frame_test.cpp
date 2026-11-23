#include "framework/golden.h"
#include "framework/sh_test.h"
#include "engine/gameflow/main_frame.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

// The main frame (src/engine/gameflow/main_frame.*): the shipped data equals the built-in rules; the frame
// skeleton with its hooks, soft-reset cuts, pacing branch and end-of-frame draw decisions gives the recorded
// calls over a mock game whose every call may change the state the later reads see; and the rules in words.

namespace {

namespace gf = sh::gameflow;

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

constexpr int kInGame = 11, kMainMenu = 7, kInventory = 14, kLoadStatus = 19, kPaperMap = 15, kSave = 16,
              kOption = 18, kLoadMap = 20;
constexpr int kStatusMenu = 2, kOptionsMenu = 1, kMapScreen = 3, kSaveMenu0 = 8, kSaveMenu1 = 9, kGameOver = 13;
const char* const kMaps[] = {"map0_s00", "map3_s01", "map3_s02", "map3_s03", "map3_s04", "map5_s01"};

// The game records the frame reads, and every call's log line.
struct World {
    std::mt19937 rng;
    std::string log;
    int game_state = kInGame, sys_state = 0;
    bool cutscene = false, border = false, menu = false, demo = false;
    int pickup = 0;
    bool puzzle = false, world_drawn = false, present_last = false, fade_opaque = false, map_screen = false;
    bool fog = true, has_save = true;
    int map = 0, room = 0;
    int hor = 0, hor_gate = 0, depth_always = 1, cut_flag = 0, item_take = 0, ui_pass = 0;
    bool dev = true, keyboard = true, key = false, free_cam = false;
    int fog_off = 0;
    bool fps_display = false;
    int post_fire = 0;
    // The end-of-frame draw calls (framing gate to scene end) are renderer calls that never touch the game
    // records the draw decisions read.
    bool frozen = false;
    // A test that wants a game no call changes sets this.
    bool still = false;

    explicit World(std::uint32_t seed) : rng(seed) {}

    int pick(int n) { return static_cast<int>(rng() % static_cast<std::uint32_t>(n)); }
    bool coin(int one_in) { return pick(one_in) == 0; }

    void scramble() {
        static const int states[] = {kInGame, kInGame, kInGame, kMainMenu, kInventory, kLoadStatus, kPaperMap, kSave,
                                     kOption, kLoadMap, 10, 0, 12};
        static const int sys[] = {0, 0, 0, kStatusMenu, kOptionsMenu, kMapScreen, kSaveMenu0, kSaveMenu1, kGameOver,
                                  7, 14, 4};
        switch (pick(20)) {
            case 0: game_state = states[pick(static_cast<int>(std::size(states)))]; break;
            case 1: sys_state = sys[pick(static_cast<int>(std::size(sys)))]; break;
            case 2: cutscene = coin(2); break;
            case 3: border = coin(3); break;
            case 4: menu = coin(3); break;
            case 5: demo = coin(4); break;
            case 6: pickup = pick(3); break;
            case 7: puzzle = coin(4); break;
            case 8: world_drawn = coin(2); break;
            case 9: present_last = coin(3); break;
            case 10: fade_opaque = coin(3); break;
            case 11: map_screen = coin(5); break;
            case 12: fog = coin(2); break;
            case 13: has_save = !coin(6); break;
            case 14: map = pick(static_cast<int>(std::size(kMaps))); break;
            case 15: room = pick(40); break;
            case 16: key = coin(2); break;
            case 17: free_cam = coin(2); break;
            case 18: dev = !coin(4); keyboard = !coin(8); break;
            case 19: hor = pick(2); depth_always = pick(2); fps_display = coin(2); break;
        }
    }

    // A call: logged, then it may change the world.
    void call(const std::string& name) {
        log += name;
        log += ';';
        if (!frozen && !still && coin(3)) scramble();
    }
    void put(const std::string& name, long v) { call(name + "=" + std::to_string(v)); }
    // A pure query or a journal line: logged, changes nothing.
    void note(const std::string& name) {
        log += name;
        log += ';';
    }
};

// The host: the mock game behind the frame's hooks and host steps. Every call is recorded under its own name.
struct NewHost final : gf::MainFrameHost {
    World& w;
    gf::PositionKey key;
    bool startup_ran = false;
    explicit NewHost(World& world) : w(world) {}

    bool between = false;  // the coming frame is presented without an update
    int owed = 0;  // updates still to run without a present
    bool resets = true;  // the soft reset may fire
    int reset_at = 0, asked = 0;  // when set: the soft reset fires at that check and at no other
    bool update_only_due() override { return owed > 0; }
    void update_only_end() override {
        --owed;
        w.call("update_only_end");
    }
    bool render_only_due() override { return between; }
    void render_only_begin() override { w.call("render_only_begin"); }
    void render_only_present() override { w.call("render_only_present"); }

    struct Hook {
        const char* name;
        void (*fn)(NewHost&);
    };
    static const std::vector<Hook>& hooks() {
        static const std::vector<Hook> h = {
            {"vcount_init", [](NewHost& n) { n.w.call("vcount_init"); }},
            {"save_init", [](NewHost& n) { n.w.call("save_init"); }},
            {"pad_init", [](NewHost& n) { n.w.call("pad_init"); }},
            {"vsync_callback", [](NewHost& n) { n.w.call("vsync_callback"); }},
            {"geometry_init", [](NewHost& n) { n.w.call("geometry_init"); }},
            {"item_screen_init", [](NewHost& n) { n.w.call("item_screen_init"); }},
            {"audio_init", [](NewHost& n) { n.w.call("audio_init"); }},
            {"warning_screen", [](NewHost& n) { n.w.call("warning_screen"); }},
            {"replay_init", [](NewHost& n) { n.w.call("replay_init"); }},
            {"debug_menu_init", [](NewHost& n) { n.w.call("debug_menu_init"); }},
            {"debug_overlay", [](NewHost& n) { n.w.call("debug_overlay"); }},
            {"console_command_file", [](NewHost& n) { n.w.call("console_command_file"); }},
            {"game_script", [](NewHost& n) { n.w.call("game_script"); }},
            {"mod_hooks", [](NewHost&) {}},  // the mod hook bus: not recorded
            {"pad_read", [](NewHost& n) { n.w.call("pad_read"); }},
            {"replay_pad", [](NewHost& n) { n.w.call("replay_pad"); }},
            {"demo_pad", [](NewHost& n) { n.w.call("demo_pad"); }},
            {"pad_build", [](NewHost& n) { n.w.call("pad_build"); }},
            {"frame_cheats", [](NewHost& n) { n.w.call("frame_cheats"); }},
            {"input_gate_after_pad", [](NewHost& n) { n.w.call("input_gate_after_pad"); }},
            {"quick_save_load", [](NewHost& n) { n.w.call("quick_save_load"); }},
            {"control_style", [](NewHost& n) { n.w.call("control_style"); }},
            {"extra_actions", [](NewHost& n) { n.w.call("extra_actions"); }},
            {"rear_look", [](NewHost& n) { n.w.call("rear_look"); }},
            {"hero_light", [](NewHost& n) { n.w.call("hero_light"); }},
            {"dpad_movement_gate", [](NewHost& n) { n.w.call("dpad_movement_gate"); }},
            {"mouse_cursor", [](NewHost& n) { n.w.call("mouse_cursor"); }},
            {"console_fmv", [](NewHost& n) { n.w.call("console_fmv"); }},
            {"console_gpu_bench", [](NewHost& n) { n.w.call("console_gpu_bench"); }},
            {"texture_upscale", [](NewHost& n) { n.w.note("texture_upscale"); }},
            {"packet_arena_check",
             [](NewHost& n) {
                 if (n.w.game_state == kInGame) n.w.call("packet_arena_check");
             }},
            {"shadow_gate", [](NewHost& n) { n.w.put("shadow_gate", n.w.world_drawn); }},
            {"freeze_frame", [](NewHost& n) { n.w.call("freeze_frame"); }},
            {"demo_update", [](NewHost& n) { n.w.call("demo_update"); }},
            {"demo_random_seed", [](NewHost& n) { n.w.call("demo_random_seed"); }},
            {"screen_fade", [](NewHost& n) { n.w.call("screen_fade"); }},
            {"heal_flash", [](NewHost& n) { n.w.call("heal_flash"); }},
            {"low_health_glow", [](NewHost& n) { n.w.call("low_health_glow"); }},
            {"log_flush", [](NewHost& n) { n.w.call("log_flush"); }},
            {"audio_tasks", [](NewHost& n) { n.w.call("audio_tasks"); }},
            {"audio_tasks_drain", [](NewHost& n) { n.w.call("audio_tasks_drain"); }},
            {"xa_player", [](NewHost& n) { n.w.call("xa_player"); }},
            {"message_voice", [](NewHost& n) { n.w.call("message_voice"); }},
            {"fog_floor_frame_end", [](NewHost& n) { n.w.call("fog_floor_frame_end"); }},
            {"fps_counter_preload",
             [](NewHost& n) {
                 if (n.w.fps_display) n.w.call("fps_counter_preload");
             }},
            {"file_queue", [](NewHost& n) { n.w.call("file_queue"); }},
            {"vibration", [](NewHost& n) { n.w.call("vibration"); }},
            {"water", [](NewHost& n) { n.w.call("water"); }},
            // Describes the frame's lights for the renderer only: not a call of the recorded loop.
            {"map_lights", [](NewHost&) {}},
            {"draw_sync", [](NewHost& n) { n.w.call("draw_sync"); }},
            {"position_key",
             [](NewHost& n) {
                 gf::PositionKeyIn in;
                 in.enabled = n.w.dev && n.w.keyboard && n.w.game_state == kInGame;
                 in.key_down = in.enabled && n.w.key;
                 in.free_camera = n.w.free_cam;
                 in.fog_disabled = n.w.fog_off != 0;
                 const gf::PositionKeyOut out = n.key.update(in);
                 if (out.log_position) n.w.note("log_position");
                 n.w.fog_off = out.fog_disabled ? 1 : 0;
                 if (out.fog_toggled) n.w.note("fog_echo=" + std::to_string(n.w.fog_off));
                 if (out.force_fog_off) n.w.fog = false;
             }},
            {"post_fire_trace",
             [](NewHost& n) {
                 if (n.w.post_fire > 0) n.w.post_fire--;
                 n.w.put("post_fire_trace", n.w.post_fire);
             }},
        };
        return h;
    }

    int hook_id(std::string_view name) override {
        const auto& h = hooks();
        for (std::size_t i = 0; i < h.size(); ++i) {
            if (name == h[i].name) return static_cast<int>(i);
        }
        return -1;
    }
    void run_hook(int id) override { hooks()[static_cast<std::size_t>(id)].fn(*this); }
    void tick() override { w.call("tick"); }
    void mark(gf::FrameMarkPoint p) override {
        static const char* const names[] = {"Top", "UpdateStart", "UpdateEnd", "FilesEnd", "SwapEnd"};
        w.call(std::string("mark:") + names[static_cast<int>(p)]);
    }
    void poll_input() override { w.call("poll_input"); }
    bool warm_reset_due() override {
        const bool reset = reset_at != 0 ? ++asked == reset_at : resets && w.coin(40);
        w.call(std::string("warm_reset_due:") + (reset ? "1" : "0"));
        return reset;
    }
    void warm_boot() override { w.call("warm_boot"); }
    void begin_buffers() override { w.call("begin_buffers"); }
    void clear_frame_flags() override {
        w.call("clear_frame_flags");
        w.pickup = 0;
        w.puzzle = false;
        w.world_drawn = false;
    }
    void replay_tick() override { w.call("replay_tick"); }
    void update_game_state() override { w.call("update_game_state"); }
    bool demo_active() override { return w.demo; }
    void pace_demo_frame() override { w.call("pace_demo_frame"); }
    void clock_demo_frame() override { w.call("clock_demo_frame"); }
    void pace_frame() override { w.call("pace_frame"); }
    void clock_credit_vblanks() override { w.call("clock_credit_vblanks"); }
    void profile_sample() override { w.call("profile_sample"); }
    void clock_line_frame() override { w.call("clock_line_frame"); }
    void clock_commit() override { w.call("clock_commit"); }
    void swap_buffers() override { w.call("swap_buffers"); }
    bool framing_gate() override {
        w.frozen = true;
        const bool held = w.coin(2);
        w.call(std::string("framing_gate:") + (held ? "1" : "0"));
        w.hor_gate = w.hor;
        return held;
    }
    gf::FrameSignals signals() override {
        gf::FrameSignals s;
        s.game_state = w.game_state;
        s.sys_state = w.sys_state;
        s.cutscene_flag = w.cutscene;
        s.border_active = w.border;
        s.menu_flag = w.menu;
        s.pickup_item = w.pickup;
        s.puzzle_depth = w.puzzle;
        s.world_drawn = w.world_drawn;
        s.present_last_frame = w.present_last;
        s.fade_opaque = w.fade_opaque;
        s.map_screen = w.map_screen;
        s.fog_enabled = w.fog;
        s.has_save = w.has_save;
        if (s.has_save) {
            s.map = kMaps[w.map];
            s.room = w.room;
        }
        return s;
    }
    void publish_cutscene(bool live) override {
        w.cut_flag = live ? 1 : 0;
        w.put("publish_cutscene", w.cut_flag);
    }
    void publish_item_take(std::int32_t v) override {
        w.item_take = v;
        w.put("publish_item_take", w.item_take);
    }
    std::int32_t world_anchor() override {
        const std::int32_t ofy = static_cast<std::int32_t>(w.pick(40)) - 10;
        w.put("world_anchor", ofy);
        return ofy;
    }
    void set_projection_offset(std::int32_t, std::int32_t y) override { w.put("set_projection_offset", y); }
    void set_dither_suppressed(bool on) override { w.put("set_dither_suppressed", on ? 1 : 0); }
    void apply_clear_color(const gf::ClearColorOut& co) override {
        w.put("apply_clear_color", static_cast<int>(co.color) * 2 + (co.set_fog_uniform ? 1 : 0));
    }
    void sort_clear() override { w.call("sort_clear"); }
    void prepare_world_list() override { w.call("prepare_world_list"); }
    void item_depth_begin() override { w.call("item_depth_begin"); }
    void draw_world_list(bool opaque) override {
        const int prev = w.depth_always;
        if (opaque) w.depth_always = 0;
        w.put("draw_world_list", w.depth_always);
        w.depth_always = prev;
    }
    void item_depth_end() override { w.call("item_depth_end"); }
    void draw_ui_list(bool menu) override {
        const int saved = w.hor;
        w.hor = menu ? 0 : w.hor_gate;
        w.ui_pass = 1;
        w.put("draw_ui_list", w.hor);
        w.ui_pass = 0;
        w.hor = saved;
    }
    void end_scene() override {
        w.call("end_scene");
        w.frozen = false;
    }
};

std::string state_of(const World& w) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%d %d %d%d%d%d %d %d%d%d%d%d%d%d%d %d %d %d %d %d %d %d %d %d", w.game_state,
                  w.sys_state, w.cutscene, w.border, w.menu, w.demo, w.pickup, w.puzzle, w.world_drawn,
                  w.present_last, w.fade_opaque, w.map_screen, w.fog, w.has_save, w.key, w.map, w.room, w.hor,
                  w.hor_gate, w.depth_always, w.cut_flag, w.item_take, w.fog_off, w.post_fire);
    return buf;
}

constexpr const char* kTable = "gameflow/main_frame.txt";

}  // namespace

SH_TEST(main_frame_shipped_equals_builtin) {
    const auto r = gf::MainFrameRules::parse(read_file(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/config/main_frame.json"));
    SH_CHECK(static_cast<bool>(r));
    const gf::MainFrameRules b = gf::MainFrameRules::builtin();
    for (int s = 0; s < static_cast<int>(gf::FrameStage::Count); ++s) SH_CHECK(r.value().hooks[s] == b.hooks[s]);
    SH_CHECK(r.value().opaque_depth_rooms.size() == b.opaque_depth_rooms.size());
    for (std::size_t i = 0; i < b.opaque_depth_rooms.size(); ++i) {
        SH_CHECK(r.value().opaque_depth_rooms[i].map == b.opaque_depth_rooms[i].map &&
                 r.value().opaque_depth_rooms[i].rooms == b.opaque_depth_rooms[i].rooms);
    }
    SH_CHECK(r.value().black_void_cutscene_maps == b.black_void_cutscene_maps);
    SH_CHECK(r.value().ui_menu_game_states == b.ui_menu_game_states);
    SH_CHECK(r.value().ui_menu_sys_states == b.ui_menu_sys_states);
    SH_CHECK(!gf::MainFrameRules::parse("{\"ui_menu_sys_states\": [\"Nope\"]}"));
    SH_CHECK(!gf::MainFrameRules::parse("{\"hooks\": {\"top\": [1]}}"));
}

SH_TEST(main_frame_unknown_hooks_are_reported) {
    World w(1);
    NewHost host(w);
    gf::MainFrameRules rules = gf::MainFrameRules::builtin();
    rules.hooks[static_cast<int>(gf::FrameStage::Top)].push_back("no_such_hook");
    gf::MainFrame frame(host, rules);
    SH_CHECK(frame.unknown_hooks().size() == 1 && frame.unknown_hooks()[0] == "no_such_hook");
}

SH_TEST(main_frame_startup_order) {
    World w(2);
    NewHost host(w);
    gf::MainFrame frame(host, gf::MainFrameRules::builtin());
    SH_CHECK(frame.unknown_hooks().empty());
    w.rng.seed(0);  // no state changes matter here; only the order
    frame.startup();
    std::string names;
    for (std::size_t i = 0, j = 0; i < w.log.size(); i = j + 1) {
        j = w.log.find(';', i);
        names += w.log.substr(i, j - i) + " ";
    }
    SH_CHECK(names == "vcount_init save_init pad_init vsync_callback geometry_init item_screen_init audio_init "
                      "vsync_callback warning_screen replay_init ");
}

namespace {

// One frame's calls as the table writes them: blanks between the calls.
std::string calls_of(const World& w) {
    std::string out = w.log;
    for (char& c : out) {
        if (c == ';') c = ' ';
    }
    if (!out.empty()) out.pop_back();
    return out;
}

std::string cells_from(const sh::test::GoldenRow& r, std::size_t first) {
    std::string out;
    for (std::size_t i = first; i < r.cells.size(); ++i) out += (i > first ? " " : "") + r.cells[i];
    return out;
}

// A quiet game for the worded tests: in game, no call changes anything, no soft reset.
struct Quiet {
    World w{9};
    NewHost host{w};
    gf::MainFrame frame{host, gf::MainFrameRules::builtin()};
    Quiet() {
        w.still = true;
        host.resets = false;
    }
    // The calls of the next frame.
    std::string run() {
        w.log.clear();
        SH_CHECK(frame.run());
        return " " + calls_of(w) + " ";
    }
};

bool has(const std::string& calls, const std::string& call) { return calls.find(" " + call + " ") != std::string::npos; }

bool in_order(const std::string& calls, std::initializer_list<const char*> names) {
    std::size_t at = 0;
    for (const char* name : names) {
        at = calls.find(std::string(" ") + name + " ", at);
        if (at == std::string::npos) return false;
        ++at;
    }
    return true;
}

}  // namespace

// The first frames of seed 1, call by call and state by state.
SH_TEST(main_frame_first_frames_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "frame");
    SH_CHECK(rows.size() == 24);
    World w(1);
    NewHost host(w);
    gf::MainFrame frame(host, gf::MainFrameRules::builtin());
    int differences = 0;
    for (const auto& r : rows) {
        w.log.clear();
        const bool ran = frame.run();
        std::string state = state_of(w);
        for (char& c : state) {
            if (c == ' ') c = ',';
        }
        if (ran != r.flag(1) || state != r.str(2) || calls_of(w) != cells_from(r, 3)) {
            if (++differences <= 2) sh::test::golden_mismatch(kTable, r, state + "  " + calls_of(w));
        }
    }
    SH_CHECK(differences == 0);
}

// 20 seeds of 20000 frames: the digest of every frame's calls and state at each recorded checkpoint.
SH_TEST(main_frame_seeded_runs_digest) {
    const auto rows = sh::test::golden_rows(kTable, "digest");
    SH_CHECK(rows.size() == 20 * 10);
    long frames = 0, resets = 0;
    int differences = 0;
    std::size_t next = 0;
    while (next < rows.size()) {
        const std::uint32_t seed = static_cast<std::uint32_t>(rows[next].num(0));
        World w(seed);
        NewHost host(w);
        gf::MainFrame frame(host, gf::MainFrameRules::builtin());
        sh::test::Digest d;
        int done = 0;
        for (; next < rows.size() && rows[next].num(0) == seed; ++next) {
            for (; done < rows[next].num(1); ++done) {
                w.log.clear();
                const bool ran = frame.run();
                std::string state = state_of(w);
                for (char& c : state) {
                    if (c == ' ') c = ',';
                }
                d.add(ran ? 1 : 0);
                d.add(calls_of(w));
                d.add(state);
                ++frames;
                if (!ran) ++resets;
            }
            if (differences == 0 && !sh::test::golden_digest_is(kTable, rows[next], 2, d)) ++differences;
        }
    }
    SH_CHECK(differences == 0);
    SH_CHECK(frames == 400000 && resets > 100);
}

// An ordinary frame: input and the pad pipeline, the update between its buffers and its marks, the late hooks,
// pacing, the swap, and the end-of-frame draw from the framing gate to the end of the scene.
SH_TEST(main_frame_ordinary_frame_order) {
    Quiet q;
    const std::string calls = q.run();
    SH_CHECK(in_order(calls, {"tick", "mark:Top", "poll_input", "pad_read", "pad_build", "frame_cheats", "warm_reset_due:0",
                              "begin_buffers", "clear_frame_flags", "replay_tick", "mark:UpdateStart", "update_game_state",
                              "mark:UpdateEnd", "packet_arena_check", "freeze_frame", "warm_reset_due:0", "screen_fade",
                              "file_queue", "mark:FilesEnd", "draw_sync", "pace_frame", "clock_credit_vblanks",
                              "profile_sample", "clock_line_frame", "clock_commit", "swap_buffers", "mark:SwapEnd",
                              "sort_clear", "prepare_world_list", "end_scene"}));
    SH_CHECK(calls.find(" framing_gate:") < calls.find(" publish_cutscene=") &&
             calls.find(" publish_cutscene=") < calls.find(" sort_clear "));
    SH_CHECK(calls.find(" draw_world_list=") < calls.find(" draw_ui_list=") &&
             calls.find(" draw_ui_list=") < calls.find(" end_scene "));
    SH_CHECK(!has(calls, "pace_demo_frame") && !has(calls, "warm_boot"));
}

// A demo paces and clocks its frame its own way; the commit and the swap follow as in any frame.
SH_TEST(main_frame_demo_pacing) {
    Quiet q;
    q.w.demo = true;
    const std::string calls = q.run();
    SH_CHECK(in_order(calls, {"draw_sync", "pace_demo_frame", "clock_demo_frame", "clock_commit", "swap_buffers"}));
    for (const char* absent : {"pace_frame", "clock_credit_vblanks", "profile_sample", "clock_line_frame"}) {
        SH_CHECK(!has(calls, absent));
    }
}

// A soft reset cuts the frame short: before the update nothing is begun, after it nothing is paced or drawn.
SH_TEST(main_frame_soft_reset_cuts_the_frame) {
    for (int at : {1, 2}) {
        World w(5);
        w.frozen = true;
        NewHost host(w);
        host.reset_at = at;
        gf::MainFrame frame(host, gf::MainFrameRules::builtin());
        SH_CHECK(!frame.run());
        const std::string calls = " " + calls_of(w) + " ";
        SH_CHECK(has(calls, "warm_boot") && has(calls, "pad_read"));
        SH_CHECK(has(calls, "begin_buffers") == (at == 2) && has(calls, "update_game_state") == (at == 2));
        for (const char* absent : {"screen_fade", "pace_frame", "swap_buffers", "sort_clear", "end_scene"}) {
            SH_CHECK(!has(calls, absent));
        }
        w.log.clear();
        SH_CHECK(frame.run());  // the frame after it is whole
        SH_CHECK(has(" " + calls_of(w) + " ", "end_scene"));
    }
}

// The world list is prepared only in game; the item depth bracket surrounds its draw on the inventory screen,
// while an item is being picked up and under a full-screen puzzle.
SH_TEST(main_frame_world_list_and_item_depth) {
    Quiet q;
    std::string calls = q.run();
    SH_CHECK(has(calls, "prepare_world_list") && !has(calls, "item_depth_begin") && !has(calls, "item_depth_end"));
    SH_CHECK(has(calls, "packet_arena_check") && has(calls, "set_dither_suppressed=0"));

    q.w.game_state = kInventory;
    calls = q.run();
    SH_CHECK(!has(calls, "prepare_world_list") && !has(calls, "packet_arena_check"));
    SH_CHECK(in_order(calls, {"sort_clear", "item_depth_begin", "draw_world_list=1", "item_depth_end"}));
    SH_CHECK(has(calls, "set_dither_suppressed=1"));
}

// The frame clears the pickup, puzzle and world-drawn flags before the update, so only what the update sets this
// frame reaches the draw decisions.
SH_TEST(main_frame_clears_the_frame_flags_before_the_update) {
    Quiet q;
    q.w.pickup = 2;
    q.w.puzzle = true;
    q.w.world_drawn = true;
    const std::string calls = q.run();
    SH_CHECK(has(calls, "shadow_gate=0") && has(calls, "publish_item_take=0") && !has(calls, "item_depth_begin"));
    SH_CHECK(q.w.pickup == 0 && !q.w.puzzle && !q.w.world_drawn);
}

// The rooms listed in the rules draw the world with the opaque depth test, in game and with a save loaded.
SH_TEST(main_frame_opaque_depth_rooms) {
    Quiet q;
    q.w.map = 1;  // map3_s01
    q.w.room = 10;
    SH_CHECK(has(q.run(), "draw_world_list=0"));
    q.w.room = 12;
    SH_CHECK(has(q.run(), "draw_world_list=1"));
    q.w.room = 10;
    q.w.has_save = false;
    SH_CHECK(has(q.run(), "draw_world_list=1"));
    q.w.has_save = true;
    q.w.game_state = kPaperMap;
    SH_CHECK(has(q.run(), "draw_world_list=1"));
}

// The UI list is drawn in the menu's 4:3 framing in a menu state once the world is off the screen and the fade is
// not opaque; otherwise in the framing the gate chose.
SH_TEST(main_frame_ui_framing) {
    Quiet q;
    q.w.hor = 1;
    SH_CHECK(has(q.run(), "draw_ui_list=1"));  // gameplay: the gate's framing
    q.w.game_state = kOption;
    SH_CHECK(has(q.run(), "draw_ui_list=0"));
    q.w.present_last = true;  // the frozen world is still behind the menu
    SH_CHECK(has(q.run(), "draw_ui_list=1"));
    q.w.present_last = false;
    q.w.fade_opaque = true;
    SH_CHECK(has(q.run(), "draw_ui_list=1"));
    q.w.fade_opaque = false;
    q.w.game_state = kInGame;
    q.w.sys_state = kStatusMenu;
    SH_CHECK(has(q.run(), "draw_ui_list=0"));
    q.w.sys_state = 0;
    q.w.menu = true;
    SH_CHECK(has(q.run(), "draw_ui_list=0"));
}

// A cutscene or the letterbox is published to the renderer as a live cutscene.
SH_TEST(main_frame_publishes_the_cutscene) {
    Quiet q;
    SH_CHECK(has(q.run(), "publish_cutscene=0"));
    q.w.cutscene = true;
    SH_CHECK(has(q.run(), "publish_cutscene=1"));
    q.w.cutscene = false;
    q.w.border = true;
    SH_CHECK(has(q.run(), "publish_cutscene=1"));
}

// The developer's position key: the press edge logs the position; with the free camera on it also toggles the
// fog, and the fog comes back as soon as the free camera is off.
SH_TEST(main_frame_position_key) {
    gf::PositionKey key;
    gf::PositionKeyIn in;
    in.enabled = in.key_down = true;
    gf::PositionKeyOut out = key.update(in);
    SH_CHECK(out.log_position && !out.fog_toggled && !out.fog_disabled && !out.force_fog_off);
    out = key.update(in);  // held: no second edge
    SH_CHECK(!out.log_position);
    in.key_down = false;
    key.update(in);
    in.key_down = in.free_camera = true;
    out = key.update(in);
    SH_CHECK(out.log_position && out.fog_toggled && out.fog_disabled && out.force_fog_off);
    in.key_down = false;
    in.fog_disabled = true;
    out = key.update(in);
    SH_CHECK(!out.fog_toggled && out.fog_disabled && out.force_fog_off);  // stays off while flying
    in.free_camera = false;
    out = key.update(in);
    SH_CHECK(!out.fog_disabled && !out.force_fog_off);
    in.enabled = false;
    in.key_down = true;
    SH_CHECK(!key.update(in).log_position);
}

// A frame before the next vertical blank: the clock is asked and the last update is presented
// again; no hook, no game state update, no buffer swap, no input read by the game.
SH_TEST(main_frame_render_only_frame_updates_nothing) {
    World w(3);
    NewHost host(w);
    gf::MainFrame frame(host, gf::MainFrameRules::builtin());
    frame.startup();
    w.game_state = kInGame;
    w.frozen = true;  // the calls change no game record: only the order is checked
    w.log.clear();
    host.between = true;
    SH_CHECK(frame.run());
    const std::string between = w.log;
    for (const char* absent : {"update_game_state", "begin_buffers", "replay_tick", "swap_buffers", "tick;",
                               "poll_input", "prepare_world_list", "warm_boot"})
        SH_CHECK(between.find(absent) == std::string::npos);
    const std::size_t begin = between.find("render_only_begin");
    const std::size_t pace = between.find("pace_frame");
    const std::size_t commit = between.find("clock_commit");
    const std::size_t again = between.find("render_only_present");
    const std::size_t clear = between.find("sort_clear");
    SH_CHECK(begin != std::string::npos && clear != std::string::npos);
    SH_CHECK(begin < pace && pace < commit && commit < again && again < clear);

    // The frame after it updates as any other.
    w.log.clear();
    host.between = false;
    SH_CHECK(frame.run());
    SH_CHECK(w.log.find("update_game_state") != std::string::npos);
    SH_CHECK(w.log.find("render_only") == std::string::npos);
}

// A frame that took several times its usual blanks: whole updates one after another, each with
// its hooks and its own pad read, and one pace and present after the last.
SH_TEST(main_frame_owed_updates_run_before_one_present) {
    World w(4);
    NewHost host(w);
    gf::MainFrame frame(host, gf::MainFrameRules::builtin());
    frame.startup();
    w.game_state = kInGame;
    w.frozen = true;
    host.owed = 2;
    host.resets = false;
    for (int i = 0; i < 2; ++i) {
        w.log.clear();
        SH_CHECK(frame.run());
        SH_CHECK(w.log.find("update_game_state") != std::string::npos);
        SH_CHECK(w.log.find("begin_buffers") != std::string::npos);
        SH_CHECK(w.log.find("update_only_end") != std::string::npos);
        for (const char* absent : {"pace_frame", "clock_commit", "swap_buffers", "sort_clear"})
            SH_CHECK(w.log.find(absent) == std::string::npos);
    }
    w.log.clear();
    SH_CHECK(frame.run());
    SH_CHECK(w.log.find("update_only_end") == std::string::npos);
    for (const char* present : {"update_game_state", "pace_frame", "clock_commit", "swap_buffers",
                                "sort_clear"})
        SH_CHECK(w.log.find(present) != std::string::npos);
}
