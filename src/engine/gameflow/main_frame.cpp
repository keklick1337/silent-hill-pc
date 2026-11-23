#include "engine/gameflow/main_frame.h"

#include <algorithm>
#include <utility>

#include "engine/game_state/game_state.h"
#include "engine/gameflow/state_names.h"
#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

namespace gs = sh::game_state;

constexpr const char* kStageKeys[] = {"startup", "top", "input", "after_update", "late", "sync", "after_swap",
                                      "after_present"};
static_assert(std::size(kStageKeys) == static_cast<std::size_t>(FrameStage::Count));

constexpr std::int32_t kInGame = static_cast<int>(gs::GameState::InGame);
constexpr std::int32_t kInventory = static_cast<int>(gs::GameState::InventoryScreen);
constexpr std::int32_t kGameOver = static_cast<int>(gs::SysState::GameOver);

using RulesResult = Result<MainFrameRules>;

Result<std::vector<std::int32_t>> state_list(const json::Value& v, const char* key, std::int32_t (*by_name)(std::string_view)) {
    using R = Result<std::vector<std::int32_t>>;
    if (!v.is_array()) return R::fail(std::string("main_frame.json: \"") + key + "\" must be an array of state names");
    std::vector<std::int32_t> out;
    for (const json::Value& e : v.array()) {
        const std::int32_t state = e.is_string() ? by_name(e.string()) : -1;
        if (state < 0) return R::fail(std::string("main_frame.json: unknown state in \"") + key + "\"");
        out.push_back(state);
    }
    return R::ok(std::move(out));
}

Result<std::vector<std::string>> name_list(const json::Value& v, const std::string& key) {
    using R = Result<std::vector<std::string>>;
    if (!v.is_array()) return R::fail("main_frame.json: \"" + key + "\" must be an array of names");
    std::vector<std::string> out;
    for (const json::Value& e : v.array()) {
        if (!e.is_string() || e.string().empty()) return R::fail("main_frame.json: \"" + key + "\" holds a non-name");
        out.push_back(e.string());
    }
    return R::ok(std::move(out));
}

bool has(const std::vector<std::int32_t>& v, std::int32_t x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

}  // namespace

MainFrameRules MainFrameRules::builtin() {
    using S = gs::SysState;
    using G = gs::GameState;
    MainFrameRules r;
    auto& h = r.hooks;
    h[static_cast<int>(FrameStage::Startup)] = {"vcount_init", "save_init", "pad_init", "vsync_callback",
                                                "geometry_init", "item_screen_init", "audio_init",
                                                "vsync_callback", "warning_screen", "replay_init"};
    h[static_cast<int>(FrameStage::Top)] = {"debug_menu_init", "debug_overlay", "console_command_file",
                                            "game_script", "mod_hooks"};
    h[static_cast<int>(FrameStage::Input)] = {"pad_read", "replay_pad", "demo_pad", "pad_build",
                                              "frame_cheats", "input_gate_after_pad",
                                              "quick_save_load", "control_style", "extra_actions", "rear_look",
                                              "hero_light", "dpad_movement_gate", "mouse_cursor", "console_fmv",
                                              "console_gpu_bench", "texture_upscale"};
    h[static_cast<int>(FrameStage::AfterUpdate)] = {"packet_arena_check", "shadow_gate", "freeze_frame",
                                                    "demo_update", "demo_random_seed"};
    h[static_cast<int>(FrameStage::Late)] = {"screen_fade", "heal_flash", "low_health_glow",
                                             "log_flush", "audio_tasks", "audio_tasks_drain", "xa_player",
                                             "message_voice", "fog_floor_frame_end", "fps_counter_preload",
                                             "file_queue"};
    h[static_cast<int>(FrameStage::Sync)] = {"vibration", "water", "map_lights", "draw_sync"};
    h[static_cast<int>(FrameStage::AfterSwap)] = {"position_key"};
    h[static_cast<int>(FrameStage::AfterPresent)] = {"post_fire_trace"};
    r.opaque_depth_rooms = {{"map3_s01", {10, 11}}, {"map3_s04", {10}}, {"map3_s03", {29, 35}}};
    r.black_void_cutscene_maps = {"map3_s02"};
    for (G g : {G::InventoryScreen, G::LoadStatusScreen, G::PaperMapScreen, G::SaveScreen, G::OptionScreen,
                G::LoadMapScreen}) {
        r.ui_menu_game_states.push_back(static_cast<int>(g));
    }
    for (S s : {S::StatusMenu, S::OptionsMenu, S::MapScreen, S::SaveMenu0, S::SaveMenu1}) {
        r.ui_menu_sys_states.push_back(static_cast<int>(s));
    }
    return r;
}

RulesResult MainFrameRules::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) return RulesResult::fail("main_frame.json: " + doc.error());
    const json::Value& root = doc.value();
    if (!root.is_object()) return RulesResult::fail("main_frame.json: the root must be an object");
    MainFrameRules out = builtin();

    if (const json::Value* hooks = root.find("hooks")) {
        if (!hooks->is_object()) return RulesResult::fail("main_frame.json: \"hooks\" must be an object");
        for (std::size_t i = 0; i < std::size(kStageKeys); ++i) {
            if (const json::Value* list = hooks->find(kStageKeys[i])) {
                auto names = name_list(*list, std::string("hooks.") + kStageKeys[i]);
                if (!names) return RulesResult::fail(names.error());
                out.hooks[i] = std::move(names.value());
            }
        }
    }
    if (const json::Value* rooms = root.find("opaque_depth_rooms")) {
        if (!rooms->is_array()) return RulesResult::fail("main_frame.json: \"opaque_depth_rooms\" must be an array");
        out.opaque_depth_rooms.clear();
        for (const json::Value& e : rooms->array()) {
            const json::Value* map = e.is_object() ? e.find("map") : nullptr;
            const json::Value* list = e.is_object() ? e.find("rooms") : nullptr;
            if (map == nullptr || !map->is_string() || list == nullptr || !list->is_array()) {
                return RulesResult::fail("main_frame.json: \"opaque_depth_rooms\" entries are {\"map\", \"rooms\"}");
            }
            OpaqueDepthRoom room{map->string(), {}};
            for (const json::Value& n : list->array()) {
                if (!n.is_integer() || n.integer() < 0 || n.integer() > 255) {
                    return RulesResult::fail("main_frame.json: rooms are integers from 0 to 255");
                }
                room.rooms.push_back(static_cast<std::int32_t>(n.integer()));
            }
            out.opaque_depth_rooms.push_back(std::move(room));
        }
    }
    if (const json::Value* maps = root.find("black_void_cutscene_maps")) {
        auto names = name_list(*maps, "black_void_cutscene_maps");
        if (!names) return RulesResult::fail(names.error());
        out.black_void_cutscene_maps = std::move(names.value());
    }
    if (const json::Value* v = root.find("ui_menu_game_states")) {
        auto states = state_list(*v, "ui_menu_game_states", game_state_by_name);
        if (!states) return RulesResult::fail(states.error());
        out.ui_menu_game_states = std::move(states.value());
    }
    if (const json::Value* v = root.find("ui_menu_sys_states")) {
        auto states = state_list(*v, "ui_menu_sys_states", sys_state_by_name);
        if (!states) return RulesResult::fail(states.error());
        out.ui_menu_sys_states = std::move(states.value());
    }
    return RulesResult::ok(std::move(out));
}

bool MainFrameRules::opaque_depth(std::string_view map, std::int32_t room) const {
    for (const OpaqueDepthRoom& r : opaque_depth_rooms) {
        if (r.map == map && has(r.rooms, room)) return true;
    }
    return false;
}

bool MainFrameRules::black_void(std::string_view map) const {
    return std::find(black_void_cutscene_maps.begin(), black_void_cutscene_maps.end(), map) !=
           black_void_cutscene_maps.end();
}

MainFrame::MainFrame(MainFrameHost& host, MainFrameRules rules) : host_(host), rules_(std::move(rules)) {
    for (int s = 0; s < static_cast<int>(FrameStage::Count); ++s) {
        for (const std::string& name : rules_.hooks[s]) {
            const int id = host_.hook_id(name);
            if (id < 0) {
                unknown_.push_back(name);
                continue;
            }
            ids_[s].push_back(id);
        }
    }
}

void MainFrame::stage(FrameStage s) {
    for (int id : ids_[static_cast<int>(s)]) host_.run_hook(id);
}

void MainFrame::startup() {
    stage(FrameStage::Startup);
}

// A frame between two game updates: no hook, no game state and no random roll runs. The clock is asked again (it
// tells when the next update is due) and the last update is presented again.
void MainFrame::run_render_only() {
    host_.mark(FrameMarkPoint::Top);
    host_.render_only_begin();
    host_.mark(FrameMarkPoint::UpdateStart);
    host_.mark(FrameMarkPoint::UpdateEnd);
    host_.mark(FrameMarkPoint::FilesEnd);
    host_.pace_frame();
    host_.clock_credit_vblanks();
    host_.profile_sample();
    host_.clock_line_frame();
    host_.clock_commit();
    host_.mark(FrameMarkPoint::SwapEnd);
    host_.render_only_present();
    present(true);
}

bool MainFrame::run() {
    if (host_.render_only_due()) {
        run_render_only();
        return true;
    }
    host_.tick();
    host_.mark(FrameMarkPoint::Top);
    host_.poll_input();
    stage(FrameStage::Top);
    stage(FrameStage::Input);
    if (host_.warm_reset_due()) {
        host_.warm_boot();
        return false;
    }

    host_.begin_buffers();
    host_.clear_frame_flags();
    host_.replay_tick();
    host_.mark(FrameMarkPoint::UpdateStart);
    host_.update_game_state();
    host_.mark(FrameMarkPoint::UpdateEnd);
    stage(FrameStage::AfterUpdate);
    if (host_.warm_reset_due()) {
        host_.warm_boot();
        return false;
    }

    stage(FrameStage::Late);
    host_.mark(FrameMarkPoint::FilesEnd);
    stage(FrameStage::Sync);

    // A frame that took several times its usual blanks: the updates it owes run one after another, each a whole
    // update of the usual step, and only the last is paced and presented.
    if (host_.update_only_due()) {
        host_.update_only_end();
        return true;
    }

    if (host_.demo_active()) {
        host_.pace_demo_frame();
        host_.clock_demo_frame();
    } else {
        host_.pace_frame();
        host_.clock_credit_vblanks();
        host_.profile_sample();
        host_.clock_line_frame();
    }
    host_.clock_commit();
    host_.swap_buffers();
    host_.mark(FrameMarkPoint::SwapEnd);
    stage(FrameStage::AfterSwap);
    host_.updated_present();
    present(false);
    stage(FrameStage::AfterPresent);
    return true;
}

void MainFrame::present(bool again) {
    const bool bg2d_held = host_.framing_gate();
    const FrameSignals s = host_.signals();
    const bool in_game = s.game_state == kInGame;

    // Cutscenes frame vertically with their letterbox bars, and the item-take screen stages its own
    // camera, so the renderer exempts both from the gameplay vertical crop.
    const bool cutscene_live = s.cutscene_flag || s.border_active;
    host_.publish_cutscene(cutscene_live);
    host_.publish_item_take(s.pickup_item);

    // The anchor is always asked for: it latches the value held across non-gameplay frames.
    std::int32_t ofy = host_.world_anchor();
    if (pickup_zero_anchor(in_game, s.pickup_item != 0, s.world_drawn, cutscene_live, s.border_active)) ofy = 0;
    host_.set_projection_offset(0, ofy);

    host_.set_dither_suppressed(dither_suppressed(in_game));

    ClearColorIn cc;
    cc.map_screen_active = s.map_screen;
    cc.present_last_frame = s.present_last_frame;
    cc.in_game = in_game;
    cc.inventory_screen = s.game_state == kInventory;
    cc.cutscene_active = s.cutscene_flag;
    cc.alessa_shop_scene = s.has_save && rules_.black_void(s.map);
    cc.game_over_state = s.sys_state == kGameOver;
    cc.world_drawn = s.world_drawn;
    cc.fog_enabled = s.fog_enabled;
    cc.bg2d_held = bg2d_held;
    host_.apply_clear_color(clear_color_choose(cc));
    host_.sort_clear();

    // A list presented again was prepared by its first present.
    if (in_game && !again) host_.prepare_world_list();
    // The item alone in the world list (inventory, pickup, puzzle) needs real per-pixel depth so its
    // front faces hide its back faces.
    const bool item_depth = s.game_state == kInventory || s.pickup_item != 0 || s.puzzle_depth;
    if (item_depth) host_.item_depth_begin();
    host_.draw_world_list(in_game && s.has_save && rules_.opaque_depth(s.map, s.room));
    if (item_depth) host_.item_depth_end();

    // Full-screen menus are authored 4:3, but only once the world is gone and the fade no longer
    // covers the frame: a full-frame fade drawn 4:3 over a wide world leaves bright side bars.
    const bool menu_state = s.menu_flag || has(rules_.ui_menu_game_states, s.game_state) ||
                            has(rules_.ui_menu_sys_states, s.sys_state);
    const bool world_on_screen = s.world_drawn || s.present_last_frame;
    host_.draw_ui_list(menu_state && !world_on_screen && !s.fade_opaque);
    host_.end_scene();
}

PositionKeyOut PositionKey::update(const PositionKeyIn& in) {
    PositionKeyOut out;
    out.fog_disabled = in.fog_disabled;
    if (!in.enabled) return out;
    if (in.key_down && !prev_) {
        out.log_position = true;
        if (in.free_camera) {
            out.fog_disabled = !out.fog_disabled;
            out.fog_toggled = true;
        }
    }
    prev_ = in.key_down;
    if (!in.free_camera) out.fog_disabled = false;
    out.force_fog_off = out.fog_disabled;
    return out;
}

}  // namespace sh::gameflow
