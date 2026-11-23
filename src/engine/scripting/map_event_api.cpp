// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/scripting/map_event_api.h"

#include "engine/camera/game_camera.h"
#include "engine/game_state/session.h"
#include "engine/inventory/item_view.h"
#include "engine/screens/paper_map_view.h"
#include "engine/screens/screens_view.h"
#include "engine/text/game_text.h"
#include "engine/world/fixed_math.h"
#include "input/mouse_pointer.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace sh::scripting {

namespace {

MapEventHost* s_host = nullptr;
decltype(ShScriptGameApi::named_call) s_next_call = nullptr;
decltype(ShScriptGameApi::named_list) s_next_list = nullptr;

// ---- the running save (sh::game::Session::game.save) ----

game::SaveRecord& running_save() { return game::session().game.save; }

// ---- flat functions (sh.<name>) ----

Point point_of(const int xyz[3]) { return Point{xyz[0], xyz[1], xyz[2]}; }

int fs_read(int file, int buffer) { return s_host->file_read(file, buffer); }
void fs_wait() { s_host->file_wait(); }
int fs_seek(int file) { return s_host->file_seek(file); }
int fs_entry_loaded(int queue_index) { return s_host->file_loaded(queue_index) ? 1 : 0; }

void dms_fix(int buffer) { s_host->cutscene_start(buffer); }
void dms_chara_apply(int slot, const char* track, int time, int buffer) {
    s_host->cutscene_chara_apply(slot, track, time, buffer);
}
void dms_camera_apply(int time, int buffer) { s_host->cutscene_camera_apply(time, buffer); }

void vc_cam_target(const int xyz[3], int warp) { s_host->camera_target(point_of(xyz), warp != 0); }
void vc_watch_target(const int xyz[3], int warp) { s_host->camera_watch(point_of(xyz), warp != 0); }
void vc_view_position(int out[3]) {
    const Point p = s_host->view_position();
    out[0] = p.x, out[1] = p.y, out[2] = p.z;
}
int vc_stereo_balance(const int xyz[3]) { return s_host->stereo_balance(point_of(xyz)); }

void camera_default_speeds(int out[4]) {
    const camera::GameCamera& cam = camera::game_camera();
    out[0] = cam.user_camera.max_speed_xz;
    out[1] = cam.user_camera.max_speed_y;
    out[2] = cam.default_watch.max_speed_x;
    out[3] = cam.default_watch.max_speed_y;
}

// The light rig a cutscene drives (session system block).
int light_intensity_get() { return game::session().system.light_intensity; }
void light_intensity_set(int value) {
    game::session().system.light_intensity = static_cast<std::int16_t>(value);
}
void light_bones_clear() {
    game::SystemBlock& sys = game::session().system;
    sys.light_bone = nullptr;
    sys.lens_flare_bone = nullptr;
}
void light_pos_set(const int xyz[3]) { game::session().system.light_position = {xyz[0], xyz[1], xyz[2]}; }
void light_rot_set(const int xyz[3]) {
    game::Vec3s& r = game::session().system.light_rotation;
    r.x = static_cast<std::int16_t>(xyz[0]);
    r.y = static_cast<std::int16_t>(xyz[1]);
    r.z = static_cast<std::int16_t>(xyz[2]);
}
void flashlight_attrs_fix() { s_host->flashlight_fix(); }

int sys_flags_get() { return static_cast<int>(game::session().system.process.sys_flags); }
void sys_flags_set(int value) { game::session().system.process.sys_flags = static_cast<std::uint32_t>(value); }

int math_vec2_mag_q6(int x, int z) { return planar_length_q6(x, z); }
void math_point_from_angle(const int xyz[3], int angle, int a, int b) {
    s_host->math_point_from_angle(point_of(xyz), angle, a, b);
}

void chara_model_clear(int slot) { s_host->chara_model_clear(slot); }
void chara_props_reset(int slot) { s_host->chara_motion_reset(slot); }
int chara_get(int slot, int field, int* ok) {
    std::optional<std::int32_t> v;
    if (field >= 0 && field < static_cast<int>(CharaField::Count)) {
        v = s_host->chara_field(slot, static_cast<CharaField>(field));
    }
    *ok = v.has_value() ? 1 : 0;
    return v.value_or(0);
}
int chara_set(int slot, int field, int value) {
    if (field < 0 || field >= static_cast<int>(CharaField::Count)) {
        return 0;
    }
    return s_host->set_chara_field(slot, static_cast<CharaField>(field), value) ? 1 : 0;
}
int chara_anim_start_keyframe_get(int slot) { return s_host->chara_anim_start_keyframe(slot); }
void chara_anim_unlock_play_to_end(int slot) { s_host->chara_anim_unlock_play_to_end(slot); }
void chara_bones_init(int index) { s_host->chara_bones_init(index); }

void player_weapon_stash() { s_host->weapon_stash(); }
void player_weapon_restore() { s_host->weapon_restore(); }
int item_remove(int item, int count) {
    return s_host->item_remove(item, count) ? 1 : 0;
}

void map_effects_init(int a, int b) { s_host->map_effects_init(a, b); }
int map_effects_update(int a, int b, int primitive, const char* data_name, int c, int d) {
    void* data = s_host->map_state(data_name);
    if (data == nullptr) {
        return 0;
    }
    s_host->map_effects_update(a, b, primitive, data, c, d);
    return 1;
}

// variant 0: the event message (Event_DisplayMapMsgWithAudio), 1: the map scripts' copy, which differs in its
// re-display guard. The audio index is a u8 map variable, or the script's own number when no name is given.
int event_msg_audio(int variant, int message, const char* index_name, int index_value, const char* cmds_name) {
    std::uint8_t local = static_cast<std::uint8_t>(index_value);
    std::uint8_t* index = index_name != nullptr ? static_cast<std::uint8_t*>(s_host->map_state(index_name)) : &local;
    const auto* cmds = static_cast<const std::uint16_t*>(s_host->map_state(cmds_name));
    if (index == nullptr || cmds == nullptr) {
        return index_value;
    }
    s_host->message_with_audio(variant != 0, message, index, cmds);
    return *index;
}
void event_msg_dimmed_bg(int file, int fade0, int fade1, int message0, int message1) {
    s_host->message_dimmed(file, fade0, fade1, message0, message1);
}

void paper_map_cmd(int cmd, int paper_map) { s_host->paper_map_cmd(cmd, paper_map); }
void map_screen_vertex_draw(int left, int top, int scale) { screens::draw_paper_map_page(left, top, scale); }
int map_screen_marking_draw(const int a[7]) {
    return screens::draw_paper_map_marks(a[0], a[1], a[2], a[3], a[4], a[5], a[6]) ? 1 : 0;
}
int map_screen_player_mark_draw(int page, int left, int top, int scale) {
    return screens::draw_paper_map_player(page, left, top, scale);
}
void map_box_outline_draw(const int a[9]) {
    const auto s16 = [](int v) { return static_cast<std::int16_t>(v); };
    screens::draw_outline_box(s16(a[0]), {s16(a[1]), s16(a[2]), s16(a[3]), s16(a[4])},
                              {s16(a[5]), s16(a[6]), s16(a[7]), s16(a[8])});
}
// The twelve words of the game's cursor call; the half height, texture rectangle and palette row are 16-bit there.
void cursor_draw(const int a[12]) {
    const auto s16 = [](int v) { return static_cast<std::int32_t>(static_cast<std::int16_t>(v)); };
    screens::draw_puzzle_cursor({a[0], s16(a[1]), a[2], s16(a[3]), s16(a[4]), s16(a[5]), s16(a[6]), a[7], a[8],
                                 static_cast<std::uint32_t>(a[9]), s16(a[10]), a[11]});
}
int mouse_cursor_puzzle_override(const char* x_name, const char* y_name) {
    int* x = static_cast<int*>(s_host->map_state(x_name));
    int* y = static_cast<int*>(s_host->map_state(y_name));
    if (x == nullptr || y == nullptr) {
        return 0;
    }
    sh::input::mouse_pointer().puzzle_override(x, y);
    return 1;
}

int savegame_get(int field) {
    const game::SaveRecord& r = running_save();
    switch (field) {
        case 0: return r.map;
        case 1: return r.paper_map;
        default: return static_cast<int>(r.item_toggles);
    }
}
void savegame_set(int field, int value) {
    game::SaveRecord& r = running_save();
    switch (field) {
        case 0: r.map = static_cast<std::int32_t>(value); break;
        case 1: r.paper_map = static_cast<std::int32_t>(value); break;
        default: r.item_toggles = static_cast<std::uint32_t>(value); break;
    }
}

// ---- named functions (sh.<group>.<name>) ----

constexpr const char* kNames[] = {
    // characters and the player
    "npc.path_walk", "npc.anim_playback_state", "npc.control_request", "npc.model_buffers_assign", "npc.slot_count",
    "npc.remove_saved", "npc.effect_spawn", "player.extra_anim_flags_set", "map.chara_anim_reset",
    "map.player_weapon_hide", "map.player_weapon_unequip",
    // loading, world, map
    "load.chunks_init_done", "load.flame_texture", "load.queue_length", "world.chunk_set", "map.particles_update",
    // screens and pad
    "screen.bg_transition", "screen.debug_text_position", "screen.credits_roll_start",
    "screen.credits_roll_music_started", "screen.credits_roll_finish", "pad.button",
    // item view close-ups
    "item_view.read", "item_view.camera_init", "item_view.models_clear", "item_view.models_map",
    "item_view.model_link", "item_view.model_linked", "item_view.get", "item_view.set", "item_view.model_rotate",
    "item_view.begin", "item_view.finish", "item_view.model_draw", "item_view.precise_depth",
    "render.flat_light", "render.ambient_light", "render.light_mode", "render.ui_gouraud", "render.ui_draw_mode",
    "render.ui_frame",
    // effect pools and music
    "effects.heat_field_start", "effects.heat_field_spawn", "effects.heat_field_update",
    "effects.heat_field_start_between", "effects.fire_pool_attached_reset", "effects.fire_pool_attach",
    "effects.fire_pool_draw_attached", "effects.bolt_pool_attach", "effects.bolt_pool_update",
    "sound.bgm_play_new",
};

// Map variables of sh.map.get / sh.map.set served here: map, name, data symbol, element count (s16 elements).
struct MapVar {
    const char* map;
    const char* name;
    const char* symbol;
    int count;
};
// map2_s00: the lock picture of the Door of Eclipse for each set of opened locks (index = Lion 4 + Woodman 2 +
// Scarecrow 1), game file numbers.
constexpr MapVar kMapVars[] = {{"map2_s00", "lock_tim_files", "g_Gfx_LockTimFileIdxs", 8}};

// Controller 1 buttons by pad name (the bits of the pad words).
struct Button {
    const char* name;
    std::uint32_t bit;
};
constexpr Button kButtons[] = {
    {"Select", 1u << 0},       {"L3", 1u << 1},           {"R3", 1u << 2},          {"Start", 1u << 3},
    {"DpadUp", 1u << 4},       {"DpadRight", 1u << 5},    {"DpadDown", 1u << 6},    {"DpadLeft", 1u << 7},
    {"L2", 1u << 8},           {"R2", 1u << 9},           {"L1", 1u << 10},         {"R1", 1u << 11},
    {"Triangle", 1u << 12},    {"Circle", 1u << 13},      {"Cross", 1u << 14},      {"Square", 1u << 15},
    {"RStickUp", 1u << 20},    {"RStickRight", 1u << 21}, {"RStickDown", 1u << 22}, {"RStickLeft", 1u << 23},
    {"LStickUp", 1u << 24},    {"LStickRight", 1u << 25}, {"LStickDown", 1u << 26}, {"LStickLeft", 1u << 27},
};

constexpr int kItemViewSlots = 10;
constexpr int kUiDepths = 16;

// Arguments of one named call; the first failure is kept in `err`.
class Args {
public:
    Args(const char* fn, int argc, const ShScriptArg* argv, char* err, int err_cap)
        : fn_(fn), argc_(argc), argv_(argv), err_(err), err_cap_(err_cap) {}

    int count() const { return argc_; }
    bool bad() const { return bad_; }
    bool present(int i) const { return i < argc_ && argv_[i].kind != 0; }

    int fail(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (!bad_ && err_ != nullptr && err_cap_ > 0) {
            va_list ap;
            va_start(ap, fmt);
            std::vsnprintf(err_, static_cast<std::size_t>(err_cap_), fmt, ap);
            va_end(ap);
        }
        bad_ = true;
        return -1;
    }

    // A number (a boolean reads 0/1); missing or nil reads 0.
    long long num(int i) {
        if (i >= argc_ || argv_[i].kind == 0) {
            return 0;
        }
        if (argv_[i].kind == 1 || argv_[i].kind == 3) {
            return argv_[i].i;
        }
        fail("argument %d: a number is expected", i + 1);
        return 0;
    }
    std::int32_t i32(int i) { return static_cast<std::int32_t>(num(i)); }

    // A number that must be given (no nil), in the 32-bit range.
    std::int32_t strict(int i) {
        if (i >= argc_ || (argv_[i].kind != 1 && argv_[i].kind != 3)) {
            fail("%s: argument %d must be a number", fn_, i + 1);
            return 0;
        }
        if (argv_[i].i < INT32_MIN || argv_[i].i > INT32_MAX) {
            fail("%s: argument %d is outside the 32-bit range", fn_, i + 1);
            return 0;
        }
        return static_cast<std::int32_t>(argv_[i].i);
    }

    // A non-empty name.
    const char* name(int i) {
        if (i < argc_ && argv_[i].kind == 2 && argv_[i].s != nullptr && argv_[i].s[0] != '\0') {
            return argv_[i].s;
        }
        fail("%s: argument %d must be a name", fn_, i + 1);
        return "";
    }
    // Any string (an empty one too).
    const char* text(int i) {
        if (i < argc_ && argv_[i].kind == 2 && argv_[i].s != nullptr) {
            return argv_[i].s;
        }
        fail("argument %d: a name is expected", i + 1);
        return "";
    }

    // A character slot number (0 the player, 1.. NPC slots).
    int slot(MapEventHost& host, int i) {
        if (i >= argc_ || argv_[i].kind != 1) {
            fail("argument %d: a character slot number is expected", i + 1);
            return -1;
        }
        const long long s = argv_[i].i;
        if (s < 0 || s > host.npc_slot_count()) {
            fail("bad character slot %lld (0..%d expected)", s, host.npc_slot_count());
            return -1;
        }
        return static_cast<int>(s);
    }

    bool exactly(int n) {
        if (argc_ != n) {
            fail("%s: %d arguments expected, got %d", fn_, n, argc_);
        }
        return !bad_;
    }

    const ShScriptArg& raw(int i) const { return argv_[i]; }

private:
    const char* fn_;
    int argc_;
    const ShScriptArg* argv_;
    char* err_;
    int err_cap_;
    bool bad_ = false;
};

const char* current_map() {
    const char* m = ShScript_MapName();
    return m != nullptr ? m : "";
}

// sh.map.get(name[, i]) / sh.map.set(name, value[, i]) for kMapVars; kNotMapEventCall for any other name.
int map_var(MapEventHost& host, bool set, Args& a, long long* out) {
    if (a.count() < 1 || a.raw(0).kind != 2 || a.raw(0).s == nullptr) {
        return kNotMapEventCall;
    }
    const MapVar* var = nullptr;
    for (const MapVar& v : kMapVars) {
        if (std::strcmp(v.name, a.raw(0).s) == 0 && std::strcmp(v.map, current_map()) == 0) {
            var = &v;
        }
    }
    if (var == nullptr) {
        return kNotMapEventCall;
    }
    const long long value = set ? a.num(1) : 0;
    const long long index = a.num(set ? 2 : 1);
    if (a.bad()) {
        return -1;
    }
    if (index < 0 || index >= var->count) {
        return a.fail("map variable '%s': index out of range", var->name);
    }
    auto* base = static_cast<std::int16_t*>(host.map_data(var->symbol));
    if (base == nullptr) {
        return a.fail("map variable '%s' is not defined on this map", var->name);
    }
    if (set) {
        base[index] = static_cast<std::int16_t>(value);
        return 0;
    }
    out[0] = base[index];
    return 1;
}

int character_call(MapEventHost& host, std::string_view fn, Args& a, long long* out) {
    if (fn == "npc.path_walk") {
        // path_walk(slot, anim, count, advance_step): one step along the event waypoints of slot 1.
        const int slot = a.slot(host, 0);
        const std::int32_t anim = a.i32(1);
        const std::int32_t count = a.i32(2);
        const bool advance = a.num(3) != 0;
        if (a.bad()) return -1;
        host.chara_path_walk(slot, anim, count, advance);
        return 0;
    }
    if (fn == "npc.anim_playback_state") {
        const int slot = a.slot(host, 0);
        if (a.bad()) return -1;
        out[0] = host.chara_anim_playback_state(slot);
        return 1;
    }
    if (fn == "npc.control_request") {
        // control_request(slot, state): a running behaviour takes the state on its next tick; one not started yet
        // (control state 0) reads it from its state step on its first.
        const int slot = a.slot(host, 0);
        const long long state = a.num(1);
        if (!a.bad() && (state < 1 || state > 255)) a.fail("control state out of range (1..255)");
        if (a.bad()) return -1;
        if (host.chara_field(slot, CharaField::ControlState).value_or(0) != 0) {
            host.set_chara_field(slot, CharaField::ControlState, static_cast<std::int32_t>(state));
            host.set_chara_field(slot, CharaField::StateStep, 0);
        } else {
            host.set_chara_field(slot, CharaField::StateStep, static_cast<std::int32_t>(state));
        }
        return 0;
    }
    if (fn == "npc.model_buffers_assign") {
        const long long mask = a.num(0);
        if (!a.bad() && (mask < 0 || mask > 255)) a.fail("group mask out of range (0..255)");
        if (a.bad()) return -1;
        host.chara_model_buffers_assign(static_cast<int>(mask));
        return 0;
    }
    if (fn == "npc.slot_count") {
        out[0] = host.npc_slot_count();
        return 1;
    }
    if (fn == "npc.remove_saved") {
        const int slot = a.slot(host, 0);
        if (a.bad()) return -1;
        host.chara_remove_saved(slot);
        return 0;
    }
    if (fn == "npc.effect_spawn") {
        // effect_spawn(slot, x, y, z, kind, variant)
        if (a.count() != 6) return a.fail("%s: %d arguments expected", "npc.effect_spawn", 6);
        std::int32_t v[6];
        for (int i = 0; i < 6; ++i) {
            if (a.raw(i).kind != 1) return a.fail("%s: argument %d must be a number", "npc.effect_spawn", i + 1);
            v[i] = a.strict(i);
        }
        if (a.bad()) return -1;
        if (v[0] < 0 || v[0] > host.npc_slot_count()) {
            return a.fail("%s: no character slot %d", "npc.effect_spawn", v[0]);
        }
        host.chara_effect_spawn(v[0], Point{v[1], v[2], v[3]}, v[4], v[5]);
        return 0;
    }
    if (fn == "player.extra_anim_flags_set") {
        const long long flags = a.num(0);
        if (a.bad()) return -1;
        host.extra_anim_flags_set(static_cast<std::int32_t>(flags));
        return 0;
    }
    if (fn == "map.chara_anim_reset") {
        const int slot = a.slot(host, 0);
        const bool to_still = a.num(1) != 0;
        if (a.bad()) return -1;
        if (!host.chara_anim_reset(slot, to_still)) {
            return a.fail("'%s' is not defined on this map", "Chara_AnimReset");
        }
        return 0;
    }
    if (fn == "map.player_weapon_hide") {
        if (!host.weapon_hide()) return a.fail("'%s' is not defined on this map", "Player_CutsceneWeaponHide");
        return 0;
    }
    if (fn == "map.player_weapon_unequip") {
        if (!host.weapon_unequip()) {
            return a.fail("'%s' is not defined on this map", "Player_CutsceneWeaponUnequip");
        }
        return 0;
    }
    return kNotMapEventCall;
}

int world_call(MapEventHost& host, std::string_view fn, Args& a, long long* out) {
    if (fn == "load.chunks_init_done") {
        out[0] = host.chunks_ready() ? 1 : 0;
        return 1;
    }
    if (fn == "load.flame_texture") {
        host.load_flame_texture();
        return 0;
    }
    if (fn == "load.queue_length") {
        out[0] = host.file_queue_length();
        return 1;
    }
    if (fn == "world.chunk_set") {
        // chunk_set(file, cell_x, cell_z): file is an IPD game file number (sh.file_number("BG/X.IPD")).
        const long long file = a.num(0);
        const long long cx = a.num(1);
        const long long cz = a.num(2);
        if (a.bad()) return -1;
        host.world_chunk_set(static_cast<std::int16_t>(file), static_cast<int>(cx), static_cast<int>(cz));
        return 0;
    }
    if (fn == "map.particles_update") {
        const std::int32_t a0 = a.i32(0);
        const std::int32_t a1 = a.i32(1);
        const std::int32_t a2 = a.i32(2);
        if (a.bad()) return -1;
        host.weather_update(a0, a1, a2);
        return 0;
    }
    return kNotMapEventCall;
}

int screen_call(MapEventHost& host, std::string_view fn, Args& a, long long* out) {
    if (fn == "screen.bg_transition") {
        // bg_transition(from_image, to_image, alpha_q12)
        void* images[2] = {};
        for (int i = 0; i < 2 && !a.bad(); ++i) {
            const char* name = a.text(i);
            if (a.bad()) break;
            images[i] = host.image(name);
            if (images[i] == nullptr) {
                a.fail("unknown image '%s' (item_inspection_img, bg_texture_img or an image map variable)", name);
            }
        }
        const long long alpha = a.num(2);
        if (a.bad()) return -1;
        host.bg_transition(images[0], images[1], static_cast<std::int16_t>(alpha));
        return 0;
    }
    if (fn == "screen.debug_text_position") {
        const long long x = a.num(0);
        const long long y = a.num(1);
        if (a.bad()) return -1;
        text::game_text().set_debug_position(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y));
        return 0;
    }
    if (fn == "screen.credits_roll_start") {
        // credits_roll_start(ending): the staff roll of an ending (row 0..5 of the credits music table).
        const long long ending = a.num(0);
        if (!a.bad() && (ending < 0 || ending > 5)) a.fail("ending out of range (0..5)");
        if (a.bad()) return -1;
        host.credits_roll_start(static_cast<int>(ending));
        return 0;
    }
    if (fn == "screen.credits_roll_music_started") {
        out[0] = host.credits_roll_music_started() ? 1 : 0;
        return 1;
    }
    if (fn == "screen.credits_roll_finish") {
        out[0] = host.credits_roll_finish() ? 1 : 0;
        return 1;
    }
    if (fn == "pad.button") {
        // button(name[, mode]) -> 1/0 for controller 1, by pad name; mode 0 held, 1 pressed this tick (default),
        // 2 released, 3 pressed or repeating.
        const char* name = a.text(0);
        const long long mode = a.count() > 1 ? a.num(1) : 1;
        if (a.bad()) return -1;
        const Button* button = nullptr;
        for (const Button& b : kButtons) {
            if (std::strcmp(b.name, name) == 0) {
                button = &b;
                break;
            }
        }
        if (button == nullptr) return a.fail("unknown pad button '%s'", name);
        const game::PadState& pad = game::session().game.pads[0];
        std::uint32_t bits;
        switch (mode) {
            case 0: bits = pad.held; break;
            case 2: bits = pad.released; break;
            case 3: bits = pad.pulsed; break;
            default: bits = pad.clicked; break;
        }
        out[0] = (bits & button->bit) != 0 ? 1 : 0;
        return 1;
    }
    return kNotMapEventCall;
}

bool item_slot_ok(Args& a, const char* fn, long long slot) {
    if (slot < 0 || slot >= kItemViewSlots) {
        a.fail("%s: slot %lld is outside 0..%d", fn, slot, kItemViewSlots - 1);
        return false;
    }
    return true;
}

// Integer arguments 0..n-1, exactly n of them.
bool ints(Args& a, const char* fn, int n, long long* v) {
    if (a.count() != n) {
        a.fail("%s: %d arguments expected, got %d", fn, n, a.count());
        return false;
    }
    for (int i = 0; i < n; ++i) {
        if (a.raw(i).kind != 1 && a.raw(i).kind != 3) {
            a.fail("%s: argument %d must be a number", fn, i + 1);
            return false;
        }
        v[i] = a.raw(i).i;
    }
    return true;
}

int item_view_call(MapEventHost& host, const char* name, Args& a, long long* out, int out_cap) {
    const std::string_view fn(name);
    long long v[16] = {};
    if (fn == "item_view.read") {
        if (!ints(a, name, 2, v)) return -1;
        host.item_view_read(static_cast<int>(v[0]), static_cast<int>(v[1]));
        return 0;
    }
    if (fn == "item_view.camera_init") {
        if (!ints(a, name, 0, v)) return -1;
        host.item_view_camera_init();
        return 0;
    }
    if (fn == "item_view.models_clear") {
        if (!ints(a, name, 0, v)) return -1;
        inventory::item_view().linked = 0;
        return 0;
    }
    if (fn == "item_view.models_map") {
        if (!ints(a, name, 0, v)) return -1;
        host.item_view_models_map();
        return 0;
    }
    if (fn == "item_view.model_link") {
        if (!ints(a, name, 2, v) || !item_slot_ok(a, name, v[0])) return -1;
        if (v[1] < 0 || v[1] > 255) return a.fail("%s: object must be 0..255", name);
        host.item_view_model_link(static_cast<int>(v[0]), static_cast<int>(v[1]));
        return 0;
    }
    if (fn == "item_view.model_linked") {
        if (!ints(a, name, 1, v) || !item_slot_ok(a, name, v[0]) || out_cap < 1) return -1;
        out[0] = host.item_view_model_linked(static_cast<int>(v[0])) ? 1 : 0;
        return 1;
    }
    if (fn == "item_view.get" || fn == "item_view.set") {
        const bool get = fn == "item_view.get";
        if (a.count() != (get ? 2 : 5) || (a.raw(0).kind != 1 && a.raw(0).kind != 3)) {
            return a.fail(get ? "%s takes (slot, field)" : "%s takes (slot, field, x, y, z)", name);
        }
        const long long slot = a.raw(0).i;
        if (!item_slot_ok(a, name, slot)) return -1;
        const ShScriptArg& f = a.raw(1);
        ItemViewField field;
        if (f.kind != 2 || f.s == nullptr) {
            return a.fail("%s: field must be rotation, scale, translation or position", name);
        }
        const std::string_view fs(f.s);
        if (fs == "rotation") {
            field = ItemViewField::Rotation;
        } else if (fs == "scale") {
            field = ItemViewField::Scale;
        } else if (fs == "translation") {
            field = ItemViewField::Translation;
        } else if (fs == "position") {
            field = ItemViewField::Position;
        } else {
            return a.fail("%s: field must be rotation, scale, translation or position", name);
        }
        if (get) {
            if (out_cap < 3) return a.fail("%s: output too small", name);
            const Point p = host.item_view_get(static_cast<int>(slot), field);
            out[0] = p.x, out[1] = p.y, out[2] = p.z;
            return 3;
        }
        std::int32_t xyz[3] = {};
        int mask = 0;
        for (int k = 0; k < 3; ++k) {
            const ShScriptArg& c = a.raw(2 + k);
            if (c.kind == 0) continue;
            if (c.kind != 1 && c.kind != 3) return a.fail("%s: x, y, z must be numbers or nil", name);
            xyz[k] = static_cast<std::int32_t>(c.i);
            mask |= 1 << k;
        }
        host.item_view_set(static_cast<int>(slot), field, Point{xyz[0], xyz[1], xyz[2]}, mask);
        return 0;
    }
    if (fn == "item_view.model_rotate") {
        if (!ints(a, name, 1, v) || !item_slot_ok(a, name, v[0])) return -1;
        if (!host.item_view_model_rotate(static_cast<int>(v[0]))) {
            return a.fail("%s: the slot has no linked frame", name);
        }
        return 0;
    }
    if (fn == "item_view.begin") {
        if (!ints(a, name, 0, v)) return -1;
        host.item_view_begin();
        return 0;
    }
    if (fn == "item_view.finish") {
        if (!ints(a, name, 0, v)) return -1;
        host.item_view_finish();
        return 0;
    }
    if (fn == "item_view.model_draw") {
        if (!ints(a, name, a.count() == 1 ? 1 : 2, v) || !item_slot_ok(a, name, v[0])) return -1;
        host.item_view_model_draw(static_cast<int>(v[0]), static_cast<int>(v[1]));
        return 0;
    }
    if (fn == "item_view.precise_depth") {
        if (!ints(a, name, 0, v)) return -1;
        host.item_view_precise_depth();
        return 0;
    }
    return a.fail("unknown function '%s'", name);
}

int render_call(MapEventHost& host, const char* name, Args& a) {
    const std::string_view fn(name);
    long long v[16] = {};
    if (fn == "render.flat_light") {
        if (!ints(a, name, 7, v)) return -1;
        if (v[0] < 0 || v[0] > 2) return a.fail("%s: light index must be 0..2", name);
        host.flat_light(static_cast<int>(v[0]),
                        Point{static_cast<std::int32_t>(v[1]), static_cast<std::int32_t>(v[2]),
                              static_cast<std::int32_t>(v[3])},
                        static_cast<std::uint8_t>(v[4]), static_cast<std::uint8_t>(v[5]),
                        static_cast<std::uint8_t>(v[6]));
        return 0;
    }
    if (fn == "render.ambient_light") {
        if (!ints(a, name, 3, v)) return -1;
        host.ambient_light(static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]));
        return 0;
    }
    if (fn == "render.light_mode") {
        if (!ints(a, name, 1, v)) return -1;
        if (v[0] < 0 || v[0] > 3) return a.fail("%s: mode must be 0..3", name);
        host.light_mode(static_cast<int>(v[0]));
        return 0;
    }
    if (fn == "render.ui_gouraud") {
        if (a.count() != 11 && a.count() != 14) {
            return a.fail("%s takes (depth, semi, then x, y, rgb for 3 or 4 corners)", name);
        }
        if (!ints(a, name, a.count(), v)) return -1;
        if (v[0] < 0 || v[0] >= kUiDepths) return a.fail("%s: depth must be 0..15", name);
        const int corners = a.count() == 11 ? 3 : 4;
        int xy_rgb[4][3] = {};
        for (int i = 0; i < corners; ++i) {
            for (int k = 0; k < 3; ++k) {
                xy_rgb[i][k] = static_cast<int>(v[2 + i * 3 + k]);
            }
        }
        host.ui_gouraud(static_cast<int>(v[0]), v[1] != 0, corners, xy_rgb);
        return 0;
    }
    if (fn == "render.ui_draw_mode") {
        if (a.count() != 2 && a.count() != 4) return a.fail("%s takes (depth, blend[, page_x, page_y])", name);
        if (!ints(a, name, a.count(), v)) return -1;
        if (v[0] < 0 || v[0] >= kUiDepths || v[1] < 0 || v[1] > 3 || v[2] < 0 || v[2] > 1023 || v[3] < 0 ||
            v[3] > 511) {
            return a.fail("%s: depth 0..15, blend 0..3, page x 0..1023, y 0..511", name);
        }
        host.ui_draw_mode(static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]),
                          static_cast<int>(v[3]));
        return 0;
    }
    if (fn == "render.ui_frame") {
        if (!ints(a, name, 0, v)) return -1;
        host.ui_frame();
        return 0;
    }
    return kNotMapEventCall;
}

// `names` leading names, then `count` numbers in the 32-bit range (`optional` more may follow).
bool pool_args(Args& a, const char* fn, int names, int count, int optional, std::int32_t* v) {
    if (a.count() < names + count || a.count() > names + count + optional) {
        a.fail("%s: %d arguments expected", fn, names + count);
        return false;
    }
    for (int i = 0; i < names; ++i) {
        a.name(i);
    }
    for (int i = names; i < a.count() && !a.bad(); ++i) {
        // A trailing optional flag may be a boolean; a required number may not.
        if (a.raw(i).kind != 1 && !(i >= names + count && a.raw(i).kind == 3)) {
            a.fail("%s: argument %d must be a number", fn, i + 1);
            break;
        }
        v[i - names] = a.strict(i);
    }
    return !a.bad();
}

int effects_call(MapEventHost& host, const char* name, Args& a) {
    const std::string_view fn(name);
    std::int32_t v[6] = {};
    if (fn == "effects.heat_field_start") {
        if (!pool_args(a, name, 1, 3, 0, v)) return -1;
        host.heat_field_start(a.raw(0).s, Point{v[0], v[1], v[2]});
        return 0;
    }
    if (fn == "effects.heat_field_spawn") {
        if (!pool_args(a, name, 1, 4, 0, v)) return -1;
        if (v[3] < 0 || v[3] > 4096) return a.fail("%s: count %d is outside 0..4096", name, v[3]);
        host.heat_field_spawn(a.raw(0).s, Point{v[0], v[1], v[2]}, v[3]);
        return 0;
    }
    if (fn == "effects.heat_field_update") {
        v[0] = 1;
        if (!pool_args(a, name, 1, 0, 1, v)) return -1;
        host.heat_field_update(a.raw(0).s, v[0] != 0);
        return 0;
    }
    if (fn == "effects.heat_field_start_between") {
        if (!pool_args(a, name, 1, 6, 0, v)) return -1;
        host.heat_field_start_between(a.raw(0).s, Point{v[0], v[1], v[2]}, Point{v[3], v[4], v[5]});
        return 0;
    }
    if (fn == "effects.fire_pool_attached_reset") {
        if (!pool_args(a, name, 1, 0, 0, v)) return -1;
        host.fire_pool_attached_reset(a.raw(0).s);
        return 0;
    }
    if (fn == "effects.fire_pool_attach") {
        if (!pool_args(a, name, 1, 0, 0, v)) return -1;
        host.fire_pool_attach(a.raw(0).s);
        return 0;
    }
    if (fn == "effects.fire_pool_draw_attached") {
        if (!pool_args(a, name, 1, 1, 0, v)) return -1;
        if (v[0] < 0 || v[0] > host.npc_slot_count()) return a.fail("%s: no character slot %d", name, v[0]);
        host.fire_pool_draw_attached(a.raw(0).s, v[0]);
        return 0;
    }
    if (fn == "effects.bolt_pool_attach") {
        if (!pool_args(a, name, 2, 2, 0, v)) return -1;
        host.bolt_pool_attach(a.raw(0).s, a.raw(1).s, v[0], v[1]);
        return 0;
    }
    if (fn == "effects.bolt_pool_update") {
        if (!pool_args(a, name, 1, 0, 0, v)) return -1;
        host.bolt_pool_update(a.raw(0).s);
        return 0;
    }
    if (fn == "sound.bgm_play_new") {
        if (!pool_args(a, name, 0, 1, 0, v)) return -1;
        host.bgm_play_new(v[0]);
        return 0;
    }
    return kNotMapEventCall;
}

bool is_ours(std::string_view fn) {
    for (const char* n : kNames) {
        if (fn == n) {
            return true;
        }
    }
    return false;
}

int chained_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err,
                 int err_cap) {
    if (s_host != nullptr) {
        const int n = map_event_call(*s_host, name, argc, argv, out, out_cap, err, err_cap);
        if (n != kNotMapEventCall) {
            return n;
        }
    }
    if (s_next_call == nullptr) {
        std::snprintf(err, static_cast<std::size_t>(err_cap), "unknown function '%s'", name);
        return -1;
    }
    return s_next_call(name, argc, argv, out, out_cap, err, err_cap);
}

const char* const* chained_list() {
    static std::vector<std::string> names;
    static std::vector<const char*> ptrs;
    if (!ptrs.empty()) {
        return ptrs.data();
    }
    if (s_next_list != nullptr) {
        for (const char* const* n = s_next_list(); n != nullptr && *n != nullptr; ++n) {
            names.emplace_back(*n);
        }
    }
    for (const char* n : kNames) {
        bool seen = false;
        for (const std::string& s : names) {
            seen = seen || s == n;
        }
        if (!seen) {
            names.emplace_back(n);
        }
    }
    for (const std::string& s : names) {
        ptrs.push_back(s.c_str());
    }
    ptrs.push_back(nullptr);
    return ptrs.data();
}

}  // namespace

std::int32_t planar_length_q6(std::int32_t x, std::int32_t z) {
    const std::int32_t qx = x >> 6;
    const std::int32_t qz = z >> 6;
    const std::int32_t sum = world::wrap_add(world::wrap_mul(qx, qx), world::wrap_mul(qz, qz));
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(world::game_sqrt(sum)) << 6);
}

int map_event_call(MapEventHost& host, const char* name, int argc, const ShScriptArg* argv, long long* out,
                   int out_cap, char* err, int err_cap) {
    if (name == nullptr) {
        return kNotMapEventCall;
    }
    const std::string_view fn(name);
    const bool map_get = fn == "map.get";
    if (!map_get && fn != "map.set" && !is_ours(fn)) {
        return kNotMapEventCall;
    }
    if (out_cap < 8) {
        std::snprintf(err, static_cast<std::size_t>(err_cap), "%s", "result buffer too small");
        return -1;
    }
    Args a(name, argc, argv, err, err_cap);
    int n = kNotMapEventCall;
    if (map_get || fn == "map.set") {
        n = map_var(host, !map_get, a, out);
    } else if (fn.starts_with("item_view.")) {
        n = item_view_call(host, name, a, out, out_cap);
    } else if (fn.starts_with("render.")) {
        n = render_call(host, name, a);
    } else if (fn.starts_with("effects.") || fn.starts_with("sound.")) {
        n = effects_call(host, name, a);
    } else if (fn.starts_with("screen.") || fn.starts_with("pad.")) {
        n = screen_call(host, fn, a, out);
    } else if (fn.starts_with("load.") || fn.starts_with("world.") || fn == "map.particles_update") {
        n = world_call(host, fn, a, out);
    } else {
        n = character_call(host, fn, a, out);
    }
    if (n == kNotMapEventCall) {
        return n;
    }
    return a.bad() ? -1 : n;
}

void bind_map_event_api(ShScriptGameApi& api, MapEventHost& host) {
    s_host = &host;
    api.fs_read = fs_read;
    api.fs_wait = fs_wait;
    api.fs_seek = fs_seek;
    api.fs_entry_loaded = fs_entry_loaded;
    api.dms_fix = dms_fix;
    api.dms_chara_apply = dms_chara_apply;
    api.dms_camera_apply = dms_camera_apply;
    api.vc_cam_target = vc_cam_target;
    api.vc_watch_target = vc_watch_target;
    api.vc_view_position = vc_view_position;
    api.vc_stereo_balance = vc_stereo_balance;
    api.camera_default_speeds = camera_default_speeds;
    api.light_intensity_get = light_intensity_get;
    api.light_intensity_set = light_intensity_set;
    api.light_bones_clear = light_bones_clear;
    api.light_pos_set = light_pos_set;
    api.light_rot_set = light_rot_set;
    api.flashlight_attrs_fix = flashlight_attrs_fix;
    api.sys_flags_get = sys_flags_get;
    api.sys_flags_set = sys_flags_set;
    api.math_vec2_mag_q6 = math_vec2_mag_q6;
    api.math_point_from_angle = math_point_from_angle;
    api.chara_model_clear = chara_model_clear;
    api.chara_props_reset = chara_props_reset;
    api.chara_get = chara_get;
    api.chara_set = chara_set;
    api.chara_anim_start_keyframe_get = chara_anim_start_keyframe_get;
    api.chara_anim_unlock_play_to_end = chara_anim_unlock_play_to_end;
    api.chara_bones_init = chara_bones_init;
    api.player_weapon_stash = player_weapon_stash;
    api.player_weapon_restore = player_weapon_restore;
    api.item_remove = item_remove;
    api.map_effects_init = map_effects_init;
    api.map_effects_update = map_effects_update;
    api.event_msg_audio = event_msg_audio;
    api.event_msg_dimmed_bg = event_msg_dimmed_bg;
    api.paper_map_state = paper_map_cmd;
    api.paper_map_cmd = paper_map_cmd;
    api.map_screen_vertex_draw = map_screen_vertex_draw;
    api.map_screen_marking_draw = map_screen_marking_draw;
    api.map_screen_player_mark_draw = map_screen_player_mark_draw;
    api.map_box_outline_draw = map_box_outline_draw;
    api.cursor_draw = cursor_draw;
    api.mouse_cursor_puzzle_override = mouse_cursor_puzzle_override;
    api.savegame_get = savegame_get;
    api.savegame_set = savegame_set;
    s_next_call = api.named_call;
    s_next_list = api.named_list;
    api.named_call = chained_call;
    api.named_list = chained_list;
}

}  // namespace sh::scripting
