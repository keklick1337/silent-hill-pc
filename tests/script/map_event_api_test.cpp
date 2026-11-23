// The map event binds (src/engine/scripting/map_event_api.h): names, argument checks, the parts the engine does on
// its own records (light rig, system flags, running save, controller 1) and the hand-off to the host.

#include "framework/sh_test.h"

#include "engine/game_state/session.h"
#include "engine/scripting/map_event_api.h"
#include "engine/screens/cursor_shapes.h"
#include "engine/text/game_text.h"
#include "input/mouse_pointer.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

// The paper map, cursor and mouse entry points live in the game; the module only forwards to them.
namespace sh::screens {
void draw_paper_map_page(int, int, int) {}
bool draw_paper_map_marks(int, int, int, int, int, int, int) { return false; }
std::int32_t draw_paper_map_player(int, int, int, int) { return 0; }
void draw_puzzle_cursor(const PuzzleCursor&) {}
void draw_outline_box(std::int16_t, OutlineBox, OutlineBox) {}
}  // namespace sh::screens
namespace sh::input {
MousePointer& mouse_pointer() {
    static MousePointer pointer;
    return pointer;
}
void MousePointer::puzzle_override(int*, int*) {}
}  // namespace sh::input

// The game's text drawer is bound to the game's records by the game too: a drawer over a host that draws nothing.
namespace sh::text {
namespace {
struct NoChars final : CharSource {
    int map_char(std::uint32_t, GlyphEmit*) override { return 0; }
};
struct NoTextHost final : GameTextHost {
    NoChars no_chars;
    FontAtlas font() override { return {}; }
    CharSource& chars() override { return no_chars; }
    const char* menu_text(const char* t) override { return t; }
    bool hi_res() override { return false; }
    void set_hi_res(bool) override {}
    const char* map_message(std::int32_t) override { return ""; }
    std::int32_t message_line_cap() override { return 0; }
    std::int32_t message_v_shift() override { return 0; }
    float text_size_percent() override { return 100.0f; }
    bool cutscene_active() override { return false; }
    bool message_timer_running() override { return false; }
    void set_message_timer(std::int32_t) override {}
    void set_audio_load_block(std::int32_t) override {}
    void release_text_layer_exemption() override {}
    void begin_run(bool) override {}
    void submit(std::int32_t, const draw2d::Command&, TextStream) override {}
    void reserve(const draw2d::Command&, TextStream) override {}
    void end_run(bool) override {}
};
}  // namespace
GameText& game_text() {
    static NoTextHost host;
    static GameText text(host);
    return text;
}
}  // namespace sh::text

namespace {

namespace ss = sh::scripting;

class FakeHost final : public ss::MapEventHost {
public:
    std::map<int, std::map<int, std::int32_t>> fields;  // slot -> field -> value (slots 0..3 exist)
    std::vector<std::string> calls;
    std::uint8_t audio_index = 7;
    std::uint16_t audio_cmds[2] = {1, 2};
    ss::Point item_point;
    int item_mask = -1;

    void* map_state(const char* name) override {
        if (std::strcmp(name, "msg_audio_idx") == 0) return &audio_index;
        if (std::strcmp(name, "msg_audio_cmds") == 0) return audio_cmds;
        return nullptr;
    }
    void* map_data(const char*) override { return nullptr; }
    void* image(const char* name) override { return std::strcmp(name, "item_inspection_img") == 0 ? this : nullptr; }
    int file_read(int, int buffer) override { return buffer == 23 ? -1 : 5; }
    void file_wait() override {}
    int file_seek(int) override { return 0; }
    bool file_loaded(int) override { return true; }
    int file_queue_length() override { return 3; }
    bool chunks_ready() override { return true; }
    void load_flame_texture() override {}
    void world_chunk_set(int, int, int) override {}
    void cutscene_start(int) override {}
    void cutscene_chara_apply(int, const char*, std::int32_t, int) override {}
    void cutscene_camera_apply(std::int32_t, int) override {}
    void camera_target(const ss::Point&, bool) override {}
    void camera_watch(const ss::Point&, bool) override {}
    ss::Point view_position() override { return {1, 2, 3}; }
    int stereo_balance(const ss::Point&) override { return 0; }
    void flashlight_fix() override {}
    std::optional<std::int32_t> chara_field(int slot, ss::CharaField f) override {
        if (slot < 0 || slot > 3) return std::nullopt;
        return fields[slot][static_cast<int>(f)];
    }
    bool set_chara_field(int slot, ss::CharaField f, std::int32_t v) override {
        if (slot < 0 || slot > 3) return false;
        fields[slot][static_cast<int>(f)] = v;
        return true;
    }
    void chara_model_clear(int) override {}
    void chara_motion_reset(int) override {}
    int chara_anim_start_keyframe(int) override { return 0; }
    void chara_anim_unlock_play_to_end(int) override {}
    void chara_bones_init(int) override {}
    void chara_path_walk(int, int, int, bool) override {}
    int chara_anim_playback_state(int slot) override { return slot + 10; }
    bool chara_anim_reset(int, bool) override { return false; }
    void chara_remove_saved(int) override {}
    void chara_effect_spawn(int, const ss::Point&, int, int) override { calls.push_back("effect_spawn"); }
    void chara_model_buffers_assign(int) override {}
    int npc_slot_count() override { return 3; }
    void weapon_stash() override {}
    void weapon_restore() override {}
    bool weapon_hide() override { return true; }
    bool weapon_unequip() override { return true; }
    void extra_anim_flags_set(std::int32_t) override {}
    bool item_remove(int item, int count) override { return item == 0x112 && count == 0x234; }
    void map_effects_init(int, int) override {}
    void map_effects_update(int, int, int, void*, int, int) override {}
    void weather_update(int, int, int) override {}
    void message_with_audio(bool, int, std::uint8_t* index, const std::uint16_t*) override { ++*index; }
    void message_dimmed(int, int, int, int, int) override {}
    void paper_map_cmd(int, int) override {}
    void math_point_from_angle(const ss::Point&, std::int32_t, int, int) override {}
    void bg_transition(void*, void*, std::int32_t) override { calls.push_back("bg_transition"); }
    void credits_roll_start(int) override {}
    bool credits_roll_music_started() override { return true; }
    bool credits_roll_finish() override { return false; }
    void item_view_read(int, int) override {}
    void item_view_camera_init() override {}
    void item_view_models_map() override {}
    void item_view_model_link(int, int) override {}
    bool item_view_model_linked(int) override { return false; }
    ss::Point item_view_get(int, ss::ItemViewField) override { return {4, 5, 6}; }
    void item_view_set(int, ss::ItemViewField, const ss::Point& v, int mask) override {
        item_point = v;
        item_mask = mask;
    }
    bool item_view_model_rotate(int) override { return false; }
    void item_view_begin() override {}
    void item_view_finish() override {}
    void item_view_model_draw(int, int) override {}
    void item_view_precise_depth() override {}
    void flat_light(int, const ss::Point&, int, int, int) override {}
    void ambient_light(int, int, int) override {}
    void light_mode(int) override {}
    void ui_gouraud(int, bool, int corners, const int (*p)[3]) override {
        calls.push_back("gouraud" + std::to_string(corners) + ":" + std::to_string(p[corners - 1][2]));
    }
    void ui_draw_mode(int, int, int, int) override {}
    void ui_frame() override {}
    void heat_field_start(const char*, const ss::Point&) override {}
    void heat_field_start_between(const char*, const ss::Point&, const ss::Point&) override {}
    void heat_field_spawn(const char*, const ss::Point&, int) override {}
    void heat_field_update(const char* name, bool advance) override {
        calls.push_back(std::string("heat:") + name + (advance ? ":1" : ":0"));
    }
    void fire_pool_attached_reset(const char*) override {}
    void fire_pool_attach(const char*) override {}
    void fire_pool_draw_attached(const char*, int) override {}
    void bolt_pool_attach(const char*, const char*, std::int32_t, std::int32_t) override {}
    void bolt_pool_update(const char*) override {}
    void bgm_play_new(int) override {}
};

ShScriptArg num(long long v) { return ShScriptArg{1, v, nullptr}; }
ShScriptArg name(const char* s) { return ShScriptArg{2, 0, s}; }
ShScriptArg nil() { return ShScriptArg{0, 0, nullptr}; }

struct Call {
    int n = 0;
    long long out[8] = {};
    char err[128] = {};
};

Call call(FakeHost& h, const char* fn, std::vector<ShScriptArg> args) {
    Call c;
    c.n = ss::map_event_call(h, fn, static_cast<int>(args.size()), args.data(), c.out, 8, c.err, sizeof(c.err));
    return c;
}

const char* const kNext[] = {"other.fn", nullptr};
int next_call(const char* name, int, const ShScriptArg*, long long* out, int, char*, int) {
    out[0] = 99;
    return std::strcmp(name, "other.fn") == 0 ? 1 : -1;
}
const char* const* next_list() { return kNext; }

}  // namespace

SH_TEST(map_event_api_flat_functions_use_the_session) {
    FakeHost host;
    ShScriptGameApi api{};
    api.named_call = next_call;
    api.named_list = next_list;
    ss::bind_map_event_api(api, host);
    auto& sys = sh::game::session().system;

    api.light_intensity_set(0x12345);
    SH_CHECK(sys.light_intensity == 0x2345);
    SH_CHECK(api.light_intensity_get() == 0x2345);
    const int pos[3] = {1, -2, 3};
    api.light_pos_set(pos);
    SH_CHECK(sys.light_position.x == 1 && sys.light_position.y == -2 && sys.light_position.z == 3);
    const int rot[3] = {0x10001, -1, 4096};
    api.light_rot_set(rot);
    SH_CHECK(sys.light_rotation.x == 1 && sys.light_rotation.y == -1 && sys.light_rotation.z == 4096);
    sys.light_bone = &host;
    api.light_bones_clear();
    SH_CHECK(sys.light_bone == nullptr && sys.lens_flare_bone == nullptr);

    api.sys_flags_set(-1);
    SH_CHECK(sys.process.sys_flags == 0xFFFFFFFFu && api.sys_flags_get() == -1);
    api.sys_flags_set(0);

    api.savegame_set(0, 200);  // map and paper map numbers are int (a mod's maps go past a byte)
    SH_CHECK(api.savegame_get(0) == 200);
    api.savegame_set(1, 300);
    SH_CHECK(api.savegame_get(1) == 300);
    api.savegame_set(2, static_cast<int>(0x80000001u));
    SH_CHECK(api.savegame_get(2) == static_cast<int>(0x80000001u));

    SH_CHECK(api.fs_read(1, 23) == -1 && api.fs_read(1, 16) == 5);
    SH_CHECK(api.item_remove(0x112, 0x234) == 1);  // an item number and a count past a byte reach the host whole

    // No index variable: the script's own number is the index, returned updated.
    SH_CHECK(api.event_msg_audio(0, 3, nullptr, 9, "msg_audio_cmds") == 10);
    SH_CHECK(api.event_msg_audio(1, 3, "msg_audio_idx", 0, "msg_audio_cmds") == 8 && host.audio_index == 8);
    SH_CHECK(api.event_msg_audio(0, 3, "msg_audio_idx", 4, "missing") == 4);

    int ok = 1;
    host.fields[2][0] = 77;
    SH_CHECK(api.chara_get(2, 0, &ok) == 77 && ok == 1);
    api.chara_get(9, 0, &ok);
    SH_CHECK(ok == 0);
    api.chara_get(2, 99, &ok);
    SH_CHECK(ok == 0);
    SH_CHECK(api.chara_set(1, 3, 5) == 1 && host.fields[1][3] == 5);

    SH_CHECK(api.math_vec2_mag_q6(3 * 4096, 4 * 4096) == 5 * 4096);
    SH_CHECK(ss::planar_length_q6(0, 0) == 0);
}

SH_TEST(map_event_api_named_functions) {
    FakeHost host;
    ShScriptGameApi api{};
    api.named_call = next_call;
    api.named_list = next_list;
    ss::bind_map_event_api(api, host);

    // The chain: other names go on, the list has both.
    long long out[8] = {};
    char err[64] = {};
    SH_CHECK(api.named_call("other.fn", 0, nullptr, out, 8, err, 64) == 1 && out[0] == 99);
    bool ours = false, theirs = false;
    for (const char* const* n = api.named_list(); *n != nullptr; ++n) {
        ours |= std::strcmp(*n, "pad.button") == 0;
        theirs |= std::strcmp(*n, "other.fn") == 0;
    }
    SH_CHECK(ours && theirs);
    SH_CHECK(call(host, "map.get", {name("unknown_var")}).n == ss::kNotMapEventCall);

    // Controller 1 by pad name.
    auto& pad = sh::game::session().game.pads[0];
    pad.held = 1u << 13;
    pad.clicked = 0;
    SH_CHECK(call(host, "pad.button", {name("Circle"), num(0)}).out[0] == 1);
    SH_CHECK(call(host, "pad.button", {name("Circle")}).out[0] == 0);
    SH_CHECK(call(host, "pad.button", {name("Nope")}).n == -1);

    // A behaviour already running takes the state now; one not started reads it from its step.
    host.fields[1][static_cast<int>(ss::CharaField::ControlState)] = 0;
    SH_CHECK(call(host, "npc.control_request", {num(1), num(4)}).n == 0);
    SH_CHECK(host.fields[1][static_cast<int>(ss::CharaField::StateStep)] == 4);
    host.fields[1][static_cast<int>(ss::CharaField::ControlState)] = 2;
    SH_CHECK(call(host, "npc.control_request", {num(1), num(6)}).n == 0);
    SH_CHECK(host.fields[1][static_cast<int>(ss::CharaField::ControlState)] == 6);
    SH_CHECK(host.fields[1][static_cast<int>(ss::CharaField::StateStep)] == 0);
    SH_CHECK(call(host, "npc.control_request", {num(1), num(0)}).n == -1);
    SH_CHECK(call(host, "npc.control_request", {num(9), num(1)}).n == -1);

    SH_CHECK(call(host, "npc.anim_playback_state", {num(2)}).out[0] == 12);
    SH_CHECK(call(host, "npc.effect_spawn", {num(4), num(0), num(0), num(0), num(1), num(2)}).n == -1);
    SH_CHECK(call(host, "npc.effect_spawn", {num(1), num(0), num(0), num(0), num(1), num(2)}).n == 0);
    SH_CHECK(call(host, "map.chara_anim_reset", {num(1), num(1)}).n == -1);
    SH_CHECK(call(host, "load.queue_length", {}).out[0] == 3);
    SH_CHECK(call(host, "screen.credits_roll_start", {num(6)}).n == -1);
    SH_CHECK(call(host, "screen.bg_transition", {name("item_inspection_img"), name("x"), num(0)}).n == -1);
    SH_CHECK(call(host, "screen.bg_transition", {name("item_inspection_img"), name("item_inspection_img"), num(1)}).n == 0);

    // Item view: nil keeps a component.
    SH_CHECK(call(host, "item_view.set", {num(2), name("scale"), num(7), nil(), num(9)}).n == 0);
    SH_CHECK(host.item_mask == 5 && host.item_point.x == 7 && host.item_point.z == 9);
    SH_CHECK(call(host, "item_view.set", {num(10), name("scale"), num(7), nil(), num(9)}).n == -1);
    SH_CHECK(call(host, "item_view.set", {num(1), name("size"), num(7), nil(), num(9)}).n == -1);
    const Call g = call(host, "item_view.get", {num(1), name("rotation")});
    SH_CHECK(g.n == 3 && g.out[0] == 4 && g.out[2] == 6);
    SH_CHECK(call(host, "item_view.model_draw", {num(1)}).n == 0);
    SH_CHECK(call(host, "item_view.model_rotate", {num(1)}).n == -1);

    // 2D shapes: three or four corners.
    SH_CHECK(call(host, "render.ui_gouraud", {num(1), num(0), num(0), num(0), num(0x10), num(1), num(1), num(0x20),
                                              num(2), num(2), num(0x30)})
                 .n == 0);
    SH_CHECK(host.calls.back() == "gouraud3:48");
    SH_CHECK(call(host, "render.ui_gouraud", {num(16), num(0), num(0), num(0), num(0), num(1), num(1), num(0),
                                              num(2), num(2), num(0)})
                 .n == -1);
    SH_CHECK(call(host, "render.ui_draw_mode", {num(0), num(4)}).n == -1);

    // Effect pools by name; the update's advance flag may be a boolean and defaults to on.
    SH_CHECK(call(host, "effects.heat_field_update", {name("flame")}).n == 0);
    SH_CHECK(host.calls.back() == "heat:flame:1");
    SH_CHECK(call(host, "effects.heat_field_update", {name("flame"), ShScriptArg{3, 0, nullptr}}).n == 0);
    SH_CHECK(host.calls.back() == "heat:flame:0");
    SH_CHECK(call(host, "effects.heat_field_spawn", {name("flame"), num(0), num(0), num(0), num(5000)}).n == -1);
    SH_CHECK(call(host, "effects.heat_field_start", {num(1), num(0), num(0), num(0)}).n == -1);
    SH_CHECK(call(host, "effects.fire_pool_draw_attached", {name("f"), num(5)}).n == -1);
}
