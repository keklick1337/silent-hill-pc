// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/ids/game_numbers.h"
#include "engine/script/named_api.h"

#include "engine/camera/game_camera.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/game_random.h"
#include "engine/maps/cutscene_timer.h"
#include "engine/player/player.h"
#include "engine/screens/screens_view.h"
#include "engine/script/named_api_calls.h"
#include "engine/app/game_view.h"
#include "engine/app/loader/game_files.h"
#include "engine/scripting/cutscene_calls.h"
#include "engine/script/named_api_state.h"
#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"
#include "input/mouse_pointer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace sh::script::named {

namespace {

namespace st = sh::script::named_state;
using named_calls::CBool;

// --- Arguments ---------------------------------------------------------------------------------------------------

struct Call {
    const ShScriptArg* argv;
    int argc;
    char* err;
    int err_cap;
    bool bad = false;

    // The first failure of a call is the one reported.
    void fail(const char* fmt, const char* what) {
        if (!bad) {
            std::snprintf(err, static_cast<std::size_t>(err_cap), fmt, what);
        }
        bad = true;
    }
    const ShScriptArg& at(int i) const {
        static const ShScriptArg kNil = {0, 0, nullptr};
        return i < argc ? argv[i] : kNil;
    }
    long long integer(int i) {
        const ShScriptArg& a = at(i);
        if (a.kind == 2) {
            fail("argument is a string where an integer is expected (%s)", a.s);
            return 0;
        }
        return a.i;
    }
    const char* text(int i) {
        const ShScriptArg& a = at(i);
        if (a.kind != 2) {
            fail("%s", "a string argument is expected");
            return "";
        }
        return a.s;
    }
};

// Script scratch vectors (vec:0..15 as 3 x 32-bit, svec:0..15 as 4 x 16-bit), shared by every script.
constexpr int kScratch = 16;
std::int32_t s_vec[kScratch][3];
std::int16_t s_svec[kScratch][4];

void* resolve_ref(Call& c, const char* s);

// A reference argument: a name, or nil / 0 for none.
void* ref_arg(Call& c, int i) {
    const ShScriptArg& a = c.at(i);
    if (a.kind == 2) {
        return resolve_ref(c, a.s);
    }
    if (a.kind == 0 || ((a.kind == 1 || a.kind == 3) && a.i == 0)) {
        return nullptr;
    }
    c.fail("%s", "a pointer argument takes a handle string or nil");
    return nullptr;
}

// A character argument: its slot number (nil: none).
void* chara_arg(Call& c, int i) {
    const ShScriptArg& a = c.at(i);
    if (a.kind == 0) {
        return nullptr;
    }
    if (a.kind == 2) {
        c.fail("a character slot number is expected, not '%s'", a.s);
        return nullptr;
    }
    void* record = st::character_record(static_cast<int>(a.i));
    if (record == nullptr) {
        c.fail("%s", "bad character slot");
    }
    return record;
}

// An item: its name ("KitchenKnife", "mymod:Crowbar") or its number.
int item_arg(Call& c, int i) {
    const ShScriptArg& a = c.at(i);
    if (a.kind != 2) {
        return static_cast<int>(a.i);
    }
    const int number = sh::ids::item_number(a.s);
    if (number < 0) {
        c.fail("unknown item '%s'", a.s);
    }
    return number;
}

// File buffer n (cutscene and scene loads).
void* buffer_arg(Call& c, int i, const char* what) {
    const long long n = c.integer(i);
    void* buffer = c.bad ? nullptr : st::file_buffer(static_cast<int>(n));
    if (!c.bad && buffer == nullptr) {
        c.fail("%s", what);
    }
    return buffer;
}

// Parameter kinds of the typed binds: each converts argument i.
template <typename T>
struct Num {
    using type = T;
    static T get(Call& c, int i) { return static_cast<T>(c.integer(i)); }
};
struct Ref {
    using type = void*;
    static void* get(Call& c, int i) { return ref_arg(c, i); }
};
struct Chara {
    using type = void*;
    static void* get(Call& c, int i) { return chara_arg(c, i); }
};
struct Item {
    using type = int;
    static int get(Call& c, int i) { return item_arg(c, i); }
};
struct Text {
    using type = const char*;
    static const char* get(Call& c, int i) { return c.text(i); }
};
using I8 = Num<std::int8_t>;
using U8 = Num<std::uint8_t>;
using I16 = Num<std::int16_t>;
using U16 = Num<std::uint16_t>;
using I32 = Num<std::int32_t>;
using U32 = Num<std::uint32_t>;
using B = Num<CBool>;  // passed on as written: the C code's boolean keeps any value

template <typename... P, typename F, std::size_t... I>
int invoke_with(Call& c, long long* out, F& f, std::index_sequence<I...>) {
    // Braced initialisation converts left to right, so the first bad argument is the one reported.
    std::tuple<typename P::type...> args{P::get(c, static_cast<int>(I))...};
    if (c.bad) {
        return -1;
    }
    using R = decltype(std::apply(f, args));
    if constexpr (std::is_void_v<R>) {
        std::apply(f, args);
        return 0;
    } else {
        out[0] = static_cast<long long>(std::apply(f, args));
        return 1;
    }
}

template <typename... P, typename F>
int invoke(Call& c, long long* out, F f) {
    if (c.argc > static_cast<int>(sizeof...(P))) {
        c.fail("%s", "too many arguments");
        return -1;
    }
    return invoke_with<P...>(c, out, f, std::index_sequence_for<P...>{});
}

// --- References ----------------------------------------------------------------------------------------------------

// The script scratches a call may pass (no raw addresses).
void* scratch_ref(Call& c, const char* s) {
    const std::string_view r(s);
    game::SystemBlock& sys = game::session().system;
    int n;
    if ((n = handle_index(r, "pos:")) >= 0) {
        if (void* record = st::character_record(n)) {
            return st::character_position(record);
        }
    } else if ((n = handle_index(r, "rot:")) >= 0) {
        if (void* record = st::character_record(n)) {
            return st::character_rotation(record);
        }
    } else if ((n = handle_index(r, "vec:")) >= 0) {
        if (n < kScratch) {
            return s_vec[n];
        }
    } else if ((n = handle_index(r, "svec:")) >= 0) {
        if (n < kScratch) {
            return s_svec[n];
        }
    } else if ((n = handle_index(r, "img:")) >= 0) {
        if (n < st::kScratchImages) {
            return st::scratch_image(n);
        }
    } else if ((n = handle_index(r, "player_bone:")) >= 0) {
        if (void* bone = st::player_bone(n)) {
            return bone;
        }
    } else if ((n = handle_index(r, "npc_bone:")) >= 0) {
        if (void* bone = st::npc_bone(n)) {
            return bone;
        }
    } else if (r == "lm:2") {
        return st::model_buffer_2();
    } else if (r == "light_pos") {
        return &sys.light_position;
    } else if (r == "light_rot") {
        return &sys.light_rotation;
    } else {
        c.fail("unknown reference '%s' (a map variable name or vec:/svec:/img:/npc_bone:/player_bone:)", s);
        return nullptr;
    }
    c.fail("unknown handle '%s'", s);
    return nullptr;
}

void* resolve_ref(Call& c, const char* s) {
    std::string_view name;
    long long index = 0;
    if (parse_ref(s, &name, &index)) {
        void* address = nullptr;
        st::VarType type = st::VarType::Ref;
        char why[256] = {};
        switch (st::map_variable(name, index, &address, &type, why, static_cast<int>(sizeof(why)))) {
            case st::VarLookup::Found:
                return address;
            case st::VarLookup::Error:
                c.fail("%s", why);
                return nullptr;
            case st::VarLookup::Unknown:
                break;
        }
    }
    return scratch_ref(c, s);
}

// --- Binds ---------------------------------------------------------------------------------------------------------

using Bind = int (*)(Call&, long long*);

struct Entry {
    const char* name;
    Bind fn;
    const char* map = nullptr;  // only on this map (its scenes are the only users of the effect)
};

#define BIND(name, ...) {name, [](Call& c, long long* out) -> int { __VA_ARGS__ }}
#define BIND_ON(map, name, ...) {name, [](Call& c, long long* out) -> int { __VA_ARGS__ }, map}

game::SystemBlock& sys() {
    return game::session().system;
}

long long game_value(st::GameField f) {
    long long v = 0;
    st::game_field(f, false, v);
    return v;
}

// The cutscene light: its position on the light track, aimed at the target track.
int cutscene_light(Call& c) {
    const std::int32_t t = static_cast<std::int32_t>(c.integer(0));
    void* header = buffer_arg(c, 1, "bad FS buffer index");
    const char* light = c.at(2).kind == 2 ? c.text(2) : "LIGHT";
    const char* target_name = c.at(3).kind == 2 ? c.text(3) : "L_INT";
    if (c.bad) {
        return -1;
    }
    game::SystemBlock& s = sys();
    std::int32_t target[3] = {};
    std::int16_t unused[4] = {};
    sh::scripting::cutscene_track_pose(&s.light_position, unused, light, t, header);
    sh::scripting::cutscene_track_pose(target, unused, target_name, t, header);
    const std::int32_t dx = target[0] - s.light_position.x;
    const std::int32_t dy = target[1] - s.light_position.y;
    const std::int32_t dz = target[2] - s.light_position.z;
    s.light_rotation.x = static_cast<std::int16_t>(-world::fixed_atan2(dy, planar_length_q6(dx, dz)));
    s.light_rotation.y = static_cast<std::int16_t>(world::fixed_atan2(dx, dz));
    s.light_rotation.z = 0;
    return 0;
}

int scratch_vector(Call& c, long long* out, bool set, bool short_vector) {
    const long long i = c.integer(0);
    if (c.bad) {
        return -1;
    }
    if (i < 0 || i >= kScratch) {
        c.fail("%s", "scratch index out of range");
        return -1;
    }
    if (set) {
        const long long x = c.integer(1), y = c.integer(2), z = c.integer(3);
        if (c.bad) {
            return -1;
        }
        if (short_vector) {
            s_svec[i][0] = static_cast<std::int16_t>(x);
            s_svec[i][1] = static_cast<std::int16_t>(y);
            s_svec[i][2] = static_cast<std::int16_t>(z);
        } else {
            s_vec[i][0] = static_cast<std::int32_t>(x);
            s_vec[i][1] = static_cast<std::int32_t>(y);
            s_vec[i][2] = static_cast<std::int32_t>(z);
        }
        return 0;
    }
    for (int k = 0; k < 3; ++k) {
        out[k] = short_vector ? s_svec[i][k] : s_vec[i][k];
    }
    return 3;
}

template <typename T>
void read_or_write(T* p, bool set, long long& value) {
    if (set) {
        *p = static_cast<T>(value);
    } else {
        value = *p;
    }
}

int map_value(Call& c, long long* out, bool set) {
    const char* name = c.text(0);
    const long long value = set ? c.integer(1) : 0;
    const long long index = c.integer(set ? 2 : 1);
    if (c.bad) {
        return -1;
    }
    void* address = nullptr;
    st::VarType type = st::VarType::Ref;
    char why[256] = {};
    const st::VarLookup found = st::map_variable(name, index, &address, &type, why, static_cast<int>(sizeof(why)));
    if (found == st::VarLookup::Unknown) {
        c.fail("unknown map variable '%s'", name);
        return -1;
    }
    if (type == st::VarType::Ref) {
        c.fail("map variable '%s' is a reference, not a number", name);
        return -1;
    }
    if (found == st::VarLookup::Error) {
        c.fail("%s", why);
        return -1;
    }
    long long v = value;
    switch (type) {
        case st::VarType::S8: read_or_write(static_cast<std::int8_t*>(address), set, v); break;
        case st::VarType::U8: read_or_write(static_cast<std::uint8_t*>(address), set, v); break;
        case st::VarType::S16: read_or_write(static_cast<std::int16_t*>(address), set, v); break;
        case st::VarType::U16: read_or_write(static_cast<std::uint16_t*>(address), set, v); break;
        case st::VarType::S32: read_or_write(static_cast<std::int32_t*>(address), set, v); break;
        case st::VarType::U32: read_or_write(static_cast<std::uint32_t*>(address), set, v); break;
        case st::VarType::Ref: break;
    }
    if (set) {
        return 0;
    }
    out[0] = v;
    return 1;
}

const Entry kBinds[] = {
    // Event state machine and the per-tick event steppers.
    BIND("event.step_set", return invoke<I32, I32>(c, out, [](std::int32_t level, std::int32_t step) {
        game::session().set_sys_step(level, step);
        return step;
    });),
    BIND("event.step_increment",
         return invoke<I32>(c, out, [](std::int32_t level) { game::session().advance_sys_step(level); });),
    BIND("event.step_reset", return invoke<>(c, out, [] { game::session().set_sys_step(0, -1); });),
    BIND("event.state_set_next",
         return invoke<I32>(c, out, [](std::int32_t state) { game::session().set_sys_state_next(state); });),
    BIND("event.step_increment_after_fade",
         return invoke<I32, I32, I32, I32, I32>(c, out, [](std::int32_t cmd, std::int32_t fade_out, std::int32_t type,
                                                          std::int32_t step, std::int32_t sub) {
             Event_ScreenFadeCmd(cmd, fade_out != 0, type, step, sub != 0);
         });),
    BIND("event.fade",
         return invoke<I32, I32, I32, I32, I32>(c, out, [](std::int32_t cmd, std::int32_t fade_out, std::int32_t type,
                                                          std::int32_t step, std::int32_t sub) {
             Event_ScreenFadeCmd(cmd, fade_out != 0, type, step, sub != 0);
         });),
    BIND("event.step_increment_delayed", return invoke<I32, B>(c, out, Event_WaitTimer);),
    BIND("event.wait_timer", return invoke<I32, B>(c, out, Event_WaitTimer);),
    BIND("event.wait_player_stop", return invoke<>(c, out, Event_WaitPlayerStop);),
    BIND("event.branch_on_flag", return invoke<I32, I32, I32, B>(c, out, Event_SysStateBranchOnFlag);),
    BIND("event.bg_texture", return invoke<I32, I32, I32>(c, out, [](std::int32_t cmd, std::int32_t file, std::int32_t sub) {
        Event_BgTextureCmd(cmd, file, sub != 0);
    });),
    BIND("event.bg_texture_fade_out", return invoke<I32, I32>(c, out, Event_BgTextureFadeOut);),
    BIND("event.bg_texture_load_fade_in", return invoke<I32, I32>(c, out, Event_BgTextureLoadFadeIn);),
    BIND("event.inv_item",
         return invoke<I32, Item, I32, I32>(c, out, [](std::int32_t step, int item, std::int32_t count, std::int32_t sub) {
             Event_InvItemCmd(step, item, count, sub != 0);
         });),
    BIND("event.tween_linear", return invoke<I32, I32, I32>(c, out, Event_TweenLinear);),
    BIND("event.tween_reset", return invoke<I32>(c, out, Event_TweenReset);),
    BIND("event.ramp_reset", return invoke<I32>(c, out, Event_RampReset);),
    BIND("event.ramp_advance", return invoke<I32, I32, I32>(c, out, Event_RampAdvance);),
    BIND("event.path_point_set", return invoke<B, I32, I32, I16, I32, I32>(c, out, Event_PathWaypointSet);),
    BIND("event.path_walk_player", return invoke<I32, I32, B>(c, out, Event_PathWaypointExecutePlayer);),
    // Event flags (names are resolved by the host; sh.flag.* is the full family).
    BIND("event.flag", return invoke<I32>(c, out, [](std::int32_t flag) { return game_state::event_flag(flag) ? 1 : 0; });),
    BIND("event.flag_set", return invoke<I32>(c, out, [](std::int32_t flag) { game_state::set_event_flag(flag, true); });),
    BIND("event.flag_clear",
         return invoke<I32>(c, out, [](std::int32_t flag) { game_state::set_event_flag(flag, false); });),
    // event.point_position() -> x, z: the active event's point of interest (Q12).
    BIND("event.point_position", (void)c; std::int32_t x = 0, z = 0; if (!st::event_point(&x, &z)) {
        c.fail("%s", "no active event point");
        return -1;
    } out[0] = x;
         out[1] = z; return 2;),
    BIND("event.item_used", (void)c; out[0] = st::event_item_used() ? 1 : 0; return 1;),
    BIND("event.used_item_is", const int item = item_arg(c, 0); if (c.bad) { return -1; } out[0] =
                                   st::event_used_item() == item ? 1 : 0;
         return 1;),

    // Map messages.
    BIND("msg.show", return invoke<B, I32, I32, I32, I32, B>(c, out, Event_DisplayMapMsg);),
    BIND("msg.selection", return invoke<B, I32, I32, I32, I32, B>(c, out, Event_DisplayMapMsg);),
    BIND("msg.show_with_audio", return invoke<I32, Ref, Ref>(c, out, [](std::int32_t msg, void* index, void* cmds) {
        Event_DisplayMapMsgWithAudio(msg, static_cast<std::uint8_t*>(index), static_cast<const std::uint16_t*>(cmds));
    });),
    BIND("msg.play_with_audio", return invoke<I32, Ref, Ref>(c, out, [](std::int32_t msg, void* index, void* cmds) {
        Map_MessageWithAudio(msg, static_cast<std::uint8_t*>(index), static_cast<const std::uint16_t*>(cmds));
    });),
    BIND("msg.show_dimmed", return invoke<I32, I32, I32, I32, I32>(c, out, Event_DisplayMapMsgWithDimmedBg);),
    BIND("msg.show_with_sfx", return invoke<I32, I32, Ref>(c, out, Event_DisplayMapMsgWithSfx);),
    BIND("msg.draw", return invoke<I32>(c, out, Gfx_MapMsg_Draw);),

    // Camera.
    BIND("camera.position", return invoke<Ref, I32, I32, I32, I32, I32, I32, I32, B>(c, out, Event_CameraPositionSet);),
    BIND("camera.position_now",
         return invoke<Ref, I32, I32, I32, I32, I32, I32, I32, B>(c, out, Event_CameraPositionSet);),
    BIND("camera.look_at", return invoke<Ref, I32, I32, I32, I32, I32, I32, I32, B>(c, out, Event_CameraLookAtSet);),
    BIND("camera.look_at_now", return invoke<Ref, I32, I32, I32, I32, I32, I32, I32, B>(c, out, Event_CameraLookAtSet);),
    BIND("camera.auto_return", return invoke<B>(c, out, vcReturnPreAutoCamWork);),
    BIND("camera.user_target", return invoke<Ref, Ref, B>(c, out, vcUserCamTarget);),
    BIND("camera.user_watch", return invoke<Ref, Ref, B>(c, out, vcUserWatchTarget);),
    BIND("camera.projection", return invoke<I16>(c, out, vcChangeProjectionValue);),
    BIND("camera.look_at_bone", return invoke<Ref, Ref, Ref>(c, out, st::camera_look_at_frame);),
    BIND("camera.look_at_point_update", return invoke<>(c, out, Vc_UpdateLookAtPointSetAlt);),

    // Cutscenes: the letterbox, the DMS tracks in a file buffer, the cutscene clock.
    BIND("cutscene.border_show", return invoke<>(c, out, [] { sys().flow.cutscene_border = 20; });),
    BIND("cutscene.border_reset", return invoke<>(c, out, [] { sys().flow.cutscene_border = 0; });),
    // cutscene.chara(slot, track, timeQ12, buf): the character of the slot follows the track.
    BIND("cutscene.chara", void* chara = st::character_record(static_cast<int>(c.integer(0)));
         const char* track = c.text(1); const auto t = static_cast<std::int32_t>(c.integer(2));
         void* header = buffer_arg(c, 3, "bad FS buffer index");
         if (!c.bad && chara == nullptr) { c.fail("%s", "bad chara slot"); } if (c.bad) { return -1; } (void)out;
         PcCutscene_CharacterApply(chara, track, t, header); return 0;),
    // cutscene.transform(track, timeQ12, buf) -> x, y, z, rx, ry, rz.
    BIND("cutscene.transform", const char* track = c.text(0); const auto t = static_cast<std::int32_t>(c.integer(1));
         void* header = buffer_arg(c, 2, "bad FS buffer index"); if (c.bad) { return -1; }
         std::int32_t position[3] = {}; std::int16_t rotation[4] = {};
         sh::scripting::cutscene_track_pose(position, rotation, track, t, header);
         out[0] = position[0], out[1] = position[1], out[2] = position[2];
         out[3] = rotation[0], out[4] = rotation[1], out[5] = rotation[2]; return 6;),
    // cutscene.camera(timeQ12, buf, posRef, lookRef): the cutscene camera at the track's position and look-at.
    BIND("cutscene.camera", const auto t = static_cast<std::int32_t>(c.integer(0));
         void* header = buffer_arg(c, 1, "bad FS buffer index"); void* position = ref_arg(c, 2);
         void* look = ref_arg(c, 3); if (!c.bad && (position == nullptr || look == nullptr)) {
             c.fail("%s", "camera position/look-at handles are required");
         } if (c.bad) { return -1; } (void)out;
         vcChangeProjectionValue(static_cast<std::int16_t>(sh::scripting::cutscene_camera_pose(position, look, nullptr, t, header)));
         vcUserCamTarget(position, nullptr, 1); vcUserWatchTarget(look, nullptr, 1); return 0;),
    // cutscene.light(timeQ12, buf[, light_track = "LIGHT", target_track = "L_INT"]).
    BIND("cutscene.light", (void)out; return cutscene_light(c);),
    // cutscene.prepare(buf): the DMS read into the buffer made ready.
    BIND("cutscene.prepare", (void)out; void* header = buffer_arg(c, 0, "bad FS buffer number"); if (c.bad) {
        return -1;
    } sh::scripting::cutscene_start(header);
         return 0;),
    // cutscene.transform_to(posRef, rotRef, track, timeQ12, buf).
    BIND("cutscene.transform_to", (void)out; void* position = ref_arg(c, 0); void* rotation = ref_arg(c, 1);
         const char* track = c.text(2); const auto t = static_cast<std::int32_t>(c.integer(3));
         void* header = buffer_arg(c, 4, "bad FS buffer number"); if (c.bad) { return -1; }
         sh::scripting::cutscene_track_pose(position, rotation, track, t, header); return 0;),
    // cutscene.camera_target(posRef, lookRef, angleRef|nil, timeQ12, buf) -> projection value.
    BIND("cutscene.camera_target", void* position = ref_arg(c, 0); void* look = ref_arg(c, 1);
         auto* angle = static_cast<std::uint16_t*>(ref_arg(c, 2)); const auto t = static_cast<std::int32_t>(c.integer(3));
         void* header = buffer_arg(c, 4, "bad FS buffer number"); if (c.bad) { return -1; }
         out[0] = sh::scripting::cutscene_camera_pose(position, look, angle, t, header); return 1;),
    // cutscene.load(path, buf) -> 1|0: a cutscene of convert/cutscenes/ (a mod's own, "ANIM/MYSCENE") into the slot.
    BIND("cutscene.load", const char* path = c.text(0); void* slot = buffer_arg(c, 1, "bad FS buffer number");
         if (c.bad) { return -1; } out[0] = PcCutscene_Load(path, slot); return 1;),
    // cutscene.frames(buf) -> length in frames of the slot's cutscene, -1 without one.
    BIND("cutscene.frames", void* slot = buffer_arg(c, 0, "bad FS buffer number"); if (c.bad) { return -1; }
         out[0] = PcCutscene_FrameCount(slot); return 1;),
    // cutscene.timer_advance(timerRef, rate, start, end, hold_at_end, advance_step): the clock runs inside the window;
    // past its end the event steps on when advance_step is set.
    BIND("cutscene.timer_advance",
         return invoke<Ref, I32, I32, I32, I32, I32>(c, out, [](void* timer, std::int32_t rate, std::int32_t start,
                                                               std::int32_t end, std::int32_t hold, std::int32_t step) {
             auto* clock = static_cast<std::int32_t*>(timer);
             const maps::CutsceneClockStep next = maps::cutscene_clock_step(
                 *clock, static_cast<std::int32_t>(game_value(st::GameField::DeltaTime)), rate, start, end, hold != 0);
             *clock = next.value;
             if (next.reached_end && step != 0) {
                 game::session().advance_sys_step(0);
             }
         });),

    // Characters (slot numbers: 0 = player, 1+ = NPC slots).
    BIND("npc.spawn", return invoke<I32, I32, I32, I32, I16, U32>(c, out, Chara_Spawn);),
    BIND("npc.load", return invoke<I32, I32, Ref, I8, Ref, Ref>(c, out, Chara_Load);),
    BIND("npc.process_loads", return invoke<>(c, out, Chara_ProcessLoads);),
    BIND("npc.clear", return invoke<Chara, I32, I32>(c, out, Chara_ModelCharaIdClear);),
    BIND("npc.image_calc", return invoke<Ref, I32, I32>(c, out, Chara_FsImageCalc);),
    BIND("npc.anim_info_clear", return invoke<>(c, out, Anim_CharaTypeAnimInfoClear);),
    BIND("npc.anim_cmd", return invoke<I32, Chara, I32, B>(c, out, Event_CharaAnimCmdExecute);),
    BIND("npc.anim_play_to_end", return invoke<Chara, I32>(c, out, Event_CharaAnimPlayToEnd);),
    BIND("npc.anim_unlock_play_to_end", return invoke<Chara>(c, out, Event_CharaAnimUnlockPlayToEnd);),
    BIND("npc.anim_flags_set", return invoke<Chara, I32>(c, out, [](void* chara, std::int32_t flags) {
        long long v = 0;
        st::chara_field(chara, st::CharaField::AnimFlags, false, v);
        v |= flags;
        st::chara_field(chara, st::CharaField::AnimFlags, true, v);
    });),
    BIND("npc.anim_flags_clear", return invoke<Chara, I32>(c, out, [](void* chara, std::int32_t flags) {
        long long v = 0;
        st::chara_field(chara, st::CharaField::AnimFlags, false, v);
        v &= ~static_cast<long long>(flags);
        st::chara_field(chara, st::CharaField::AnimFlags, true, v);
    });),
    BIND("npc.transparent_set", return invoke<I32, B>(c, out, [](std::int32_t chara, sh::script::named_calls::CBool on) { sh::chara::chara_models().set_translucent(chara, on != 0); });),
    BIND("npc.material_set", return invoke<I32, I32>(c, out, [](std::int32_t chara, std::int32_t blend) { sh::chara::chara_models().apply_material(chara, blend); });),
    // npc.clear_all(): every NPC slot, the NPC flags and the character group flags cleared.
    BIND("npc.clear_all", (void)c; (void)out; sh::app::view::npcs_clear(); return 0;),
    // npc.set_anim_buffer_size(group, bytes): a character group's animation buffer (the boss scenes widen group 0).
    BIND("npc.set_anim_buffer_size", (void)out; const long long group = c.integer(0); const long long size = c.integer(1);
         if (!c.bad && !st::set_anim_buffer_size(static_cast<int>(group), static_cast<std::int32_t>(size))) {
             c.fail("%s", "bad character group");
         } return c.bad ? -1 : 0;),

    // The player.
    BIND("player.anim_something", return invoke<>(c, out, [] { return game::player().body_clip_start(); });),
    BIND("player.weapon_attack_set", return invoke<I32>(c, out, [](std::int32_t attack) {
        game::player().play_ready_sound(attack);
    });),
    BIND("player.item_remove", return invoke<Item, I32>(c, out, [](int item, std::int32_t count) {
        return Player_ItemRemove(static_cast<int>(item), static_cast<int>(count)) != 0 ? 1 : 0;
    });),
    BIND("player.held_item_show", return invoke<>(c, out, WorldGfx_HeldItemShow);),
    BIND("player.held_item_hide", return invoke<>(c, out, WorldGfx_HeldItemHide);),
    BIND("player.held_item_set", return invoke<Item>(c, out, [](int item) { return WorldGfx_PlayerHeldItemSet(item); });),
    BIND("player.held_item_prev", (void)c; out[0] = WorldGfx_PlayerPrevHeldItem(&sys().combat); return 1;),
    BIND("player.map_anim_load", return invoke<I32>(c, out, GameFs_PlayerMapAnimLoad);),
    // player.map_anims_append(): the map's extra animations into Harry's base table.
    BIND("player.map_anims_append", (void)c; (void)out; game::player().append_map_clips(); return 0;),
    BIND("player.flex_rotation_reset", return invoke<>(c, out, [] { game::player().nudge_flex_lean(); });),
    BIND("player.weapon_stash", return invoke<>(c, out, st::player_weapon_stash);),
    BIND("player.weapon_restore", return invoke<>(c, out, st::player_weapon_restore);),

    // Files: load.file(file, buf) / load.tim(file, buf, imageRef) into file buffer buf.
    BIND("load.file", const auto file = static_cast<std::int32_t>(c.integer(0));
         void* buffer = buffer_arg(c, 1, "bad FS buffer number"); if (c.bad) { return -1; }
         out[0] = sh::loader::queue_read(file, buffer); return 1;),
    BIND("load.tim", const auto file = static_cast<std::int32_t>(c.integer(0));
         void* buffer = buffer_arg(c, 1, "bad FS buffer number"); if (c.bad) { return -1; }
         const void* image = ref_arg(c, 2); if (c.bad) { return -1; }
         out[0] = sh::loader::queue_read_image(file, buffer, static_cast<const sh::loader::ImagePlace*>(image)); return 1;),
    BIND("load.wait", return invoke<>(c, out, sh::loader::queue_wait_empty);),
    BIND("load.chunks", return invoke<>(c, out, sh::loader::queue_load_chunks);),
    BIND("load.seek", return invoke<I32>(c, out, sh::loader::queue_seek);),
    BIND("load.is_loaded", return invoke<I32>(c, out, sh::loader::queue_loaded);),
    BIND("load.staff_roll", return invoke<>(c, out, GameFs_StfRollBinLoad);),
    BIND("load.close_range_chunks", return invoke<>(c, out, Ipd_CloseRangeChunksInit);),

    // Light and flashlight.
    BIND("light.flashlight_fix", return invoke<>(c, out, Game_FlashlightAttributesFix);),
    BIND("light.flashlight_on", return invoke<>(c, out, Game_TurnFlashlightOn);),
    BIND("light.flashlight_off", return invoke<>(c, out, Game_TurnFlashlightOff);),
    // light.bone_detach() / light.lens_flare_detach(): the light and the lens flare no longer follow a bone.
    BIND("light.bone_detach", (void)c; (void)out; sys().light_bone = nullptr; return 0;),
    BIND("light.lens_flare_detach", (void)c; (void)out; sys().lens_flare_bone = nullptr; return 0;),

    // Screen.
    BIND("screen.bg_draw", return invoke<Ref>(c, out, Screen_BackgroundImgDraw);),
    BIND("screen.bg_draw_alt", return invoke<Ref>(c, out, Screen_BackgroundImgDrawAlt);),
    BIND("screen.fade_start", return invoke<I32, I32, I32>(c, out, [](std::int32_t reset, std::int32_t in, std::int32_t white) {
        long long state = screen_fade_start_state(reset != 0, in != 0, white != 0);
        st::game_field(st::GameField::ScreenFadeStatus, true, state);
    });),
    BIND("screen.fade_reset_timestep", return invoke<>(c, out, [] {
        long long state = 4;  // back to the default fade speed, the colour bit dropped
        st::game_field(st::GameField::ScreenFadeStatus, true, state);
    });),
    BIND("screen.cursor_draw",
         return invoke<I32, I16, I32, I16, I16, I16, I16, I32, I32, U32, I16, I32>(
             c, out, [](std::int32_t x0, std::int16_t y0, std::int32_t x1, std::int16_t y1, std::int16_t u, std::int16_t v,
                        std::int16_t w, std::int32_t h, std::int32_t shade, std::uint32_t clut_x, std::int16_t clut_y,
                        std::int32_t page) {
                 screens::draw_puzzle_cursor({x0, y0, x1, y1, u, v, w, h, shade, clut_x, clut_y, page});
             });),
    BIND("screen.puzzle_cursor", return invoke<Ref, Ref>(c, out, [](void* x, void* y) {
        sh::input::mouse_pointer().puzzle_override(static_cast<int*>(x), static_cast<int*>(y));
    });),
    BIND("screen.pickup_animate", return invoke<Item>(c, out, [](int item) {
        return Gfx_PickupItemAnimate(item) != 0 ? 1 : 0;
    });),
    BIND("screen.credits_start", return invoke<>(c, out, st::credits_start);),
    BIND("screen.credits_update", return invoke<>(c, out, [] { return st::credits_update() ? 1 : 0; });),

    // World.
    BIND("world.object_add", return invoke<Ref, Ref, Ref>(c, out, WorldGfx_ObjectAdd);),
    BIND("world.object_model_name_set", return invoke<Ref, Text>(c, out, [](void* model, const char* name) {
        WorldObject_ModelNameSet(model, const_cast<char*>(name));
    });),
    BIND("world.effects_update", return invoke<I32, I32>(c, out, Gfx_MapInitMapEffectsUpdate);),
    BIND("world.effects_reset", return invoke<>(c, out, Map_EffectStatesReset);),
    BIND("world.water_enable", return invoke<>(c, out, Water_EffectEnable);),
    BIND("world.water_disable", return invoke<>(c, out, Water_EffectDisable);),
    BIND("world.surface_get", return invoke<Ref, I32, I32>(c, out, Collision_SurfaceGet);),
    // The live fog raised to at least the stock level this frame (scenes framed for it).
    BIND("world.fog_floor_request", return invoke<>(c, out, Pc_FogFloorRequest);),

    // Game.
    BIND("game.timer_update", return invoke<>(c, out, Game_TimerUpdate);),
    BIND("game.state_update", return invoke<>(c, out, GameState_UnkUpdate);),
    // Controller effects (config/pad_vibration.json by position): at the default level / at an explicit one.
    BIND("pad.effect", return invoke<I32>(c, out, Vibration_Play);),
    BIND("pad.effect_level", return invoke<I32, U8>(c, out, Vibration_PlayAtLevel);),
    BIND("collision.flag_bits_set", return invoke<I32>(c, out, [](std::int32_t bits) {
        Collision_FlagBitsSet(static_cast<std::uint16_t>(bits));
    });),
    BIND("collision.flag_bits_clear", return invoke<I32>(c, out, Collision_FlagBitsClear);),
    BIND("view.aabb_visible",
         return invoke<I32, I32, I32, I32, I32, I32>(c, out, [](std::int32_t x0, std::int32_t x1, std::int32_t y0,
                                                               std::int32_t y1, std::int32_t z0, std::int32_t z1) {
             return Vw_AabbVisibleInScreenCheck(x0, x1, y0, y1, z0, z1) != 0 ? 1 : 0;
         });),
    BIND("savegame.enemy_state_get", return invoke<>(c, out, [] {
        return static_cast<std::int32_t>(game_value(st::GameField::SaveEnemyState));
    });),
    BIND("savegame.enemy_state_set", return invoke<I32>(c, out, [](std::int32_t v) {
        long long value = v;
        st::game_field(st::GameField::SaveEnemyState, true, value);
    });),

    // The game's math and random numbers (bit-exact).
    BIND("math.sin", return invoke<I32>(c, out, world::fixed_sin);),
    BIND("math.cos", return invoke<I32>(c, out, world::fixed_cos);),
    BIND("math.atan2", return invoke<I32, I32>(c, out, world::fixed_atan2);),
    BIND("math.point_from_angle", return invoke<Ref, I16, I32, I32>(c, out, Math_PointFromAngleCalc);),
    BIND("math.vec2_len_q6", return invoke<I32, I32>(c, out, planar_length_q6);),
    BIND("rng.rand16", return invoke<>(c, out, [] { return loop::game_random().next15(); });),
    BIND("rng.int", return invoke<I32, I32>(c, out, [](std::int32_t low, std::int32_t high) {
        return random_in_range(loop::game_random().next15(), low, high);
    });),
    BIND("rng.uint", return invoke<I32, I32>(c, out, [](std::int32_t low, std::int32_t high) {
        return random_in_range_unsigned(loop::game_random().next15(), low, high);
    });),
    BIND("rng.set_seed", return invoke<I32>(c, out, [](std::int32_t seed) {
        loop::game_random().set_seed(static_cast<std::uint32_t>(seed));
    });),

    // Map effects every cutscene map may start.
    BIND("map.ufo_glow_spawn", return invoke<Ref, U16, U16, U32>(c, out, SlotFx_LightGlowSpawn);),
    BIND("map.weather_set", return invoke<I8, U32>(c, out, Map_WeatherEnvironmentSet);),
    BIND("map.particles_update", return invoke<I32, I32, I32>(c, out, Map_WeatherUpdate);),
    BIND("map.particle_emitter_start", return invoke<>(c, out, SlotFx_EmitterStart);),
    BIND("map.weather_density_set",
         return invoke<I32>(c, out, [](std::int32_t level) { return Map_WeatherDensitySet(level) != 0 ? 1 : 0; });),
    // A pane of glass bursts into shards: two corner points, then the size/spread/speed and the shard group.
    BIND("map.glass_shatter", return invoke<Ref, Ref, I16, I32, I16, I16, I32, I32>(c, out, Glass_Shatter);),
    // The stream of water_stream starts (its drops spawned, ripples reset) / stops.
    BIND("map.water_stream_start", return invoke<>(c, out, WaterStream_Start);),
    BIND("map.water_stream_stop", return invoke<>(c, out, WaterStream_Stop);),
    // Map-local effects.
    BIND_ON("map1_s01", "map.smoke_emitter_start", return invoke<>(c, out, SlotFx_SmokeStart);),
    BIND_ON("map5_s00", "map.drain_splash_start", return invoke<Ref>(c, out, DrainSplash_Start);),
    BIND_ON("map5_s00", "map.drain_splash_update", return invoke<>(c, out, DrainSplash_Update);),
    BIND_ON("map6_s02", "map.ufo_ending_sprite_draw", return invoke<I32>(c, out, UfoEnding_SpriteDraw);),
    BIND_ON("map6_s02", "map.ufo_ending_slide_in_draw", return invoke<I16>(c, out, UfoEnding_SlideInDraw);),
    BIND_ON("map6_s00", "map.ground_morph_draw", return invoke<I32>(c, out, GroundMorph_Draw);),
    BIND_ON("map0_s01", "map.player_fall_backward", return invoke<>(c, out, Player_FallBackward);),
    // The plate door's picture into the inspection image -> queue index.
    BIND_ON("map3_s03", "map.plate_door_image_load", (void)c; out[0] = st::plate_door_image_load(); return 1;),
    // The splashes of water_drips[i] spawned (count of them).
    BIND_ON("map1_s03", "map.water_drips_start", return invoke<I32>(c, out, SlotFx_DripsStart);),

    // Script scratch vectors: vec.get(i) -> x, y, z / vec.set(i, x, y, z); short_* on the 16-bit ones.
    BIND("vec.get", return scratch_vector(c, out, false, false);),
    BIND("vec.set", return scratch_vector(c, out, true, false);),
    BIND("vec.short_get", return scratch_vector(c, out, false, true);),
    BIND("vec.short_set", return scratch_vector(c, out, true, true);),

    // Map-state variables: map.get(name[, index]) / map.set(name, value[, index]).
    BIND("map.get", return map_value(c, out, false);),
    BIND("map.set", return map_value(c, out, true);),
};

#undef BIND
#undef BIND_ON

// --- State fields: <group>.<name>() reads, <group>.set_<name>(v) writes --------------------------------------------

using FieldAccess = void (*)(bool set, long long& value);

struct Field {
    const char* name;
    bool writable;
    FieldAccess access;
};

#define FIELD(name, writable, lvalue) {name, writable, [](bool set, long long& v) { read_or_write(&(lvalue), set, v); }}
#define GAME_FIELD(name, writable, field) {name, writable, [](bool set, long long& v) { st::game_field(field, set, v); }}

const Field kStateFields[] = {
    FIELD("event.state_timer", true, sys().flow.sys_step_data[0]),
    FIELD("event.step_timer", true, sys().flow.sys_step_data[1]),
    FIELD("event.sys_state", true, sys().flow.sys_state),
    // The high nibble of the text flags: "yes" is selected without a sound.
    {"msg.silent_yes_selection", true,
     [](bool set, long long& v) {
         std::uint8_t& flags = sys().text_flags;
         if (set) {
             flags = static_cast<std::uint8_t>((flags & 0x0F) | ((v & 0x0F) << 4));
         } else {
             v = flags >> 4;
         }
     }},
    FIELD("light.intensity", true, sys().light_intensity),
    FIELD("light.pos_x", true, sys().light_position.x),
    FIELD("light.pos_y", true, sys().light_position.y),
    FIELD("light.pos_z", true, sys().light_position.z),
    FIELD("light.rot_x", true, sys().light_rotation.x),
    FIELD("light.rot_y", true, sys().light_rotation.y),
    FIELD("light.rot_z", true, sys().light_rotation.z),
    {"light.bone_attached", false, [](bool, long long& v) { v = sys().light_bone != nullptr ? 1 : 0; }},
    {"light.lens_flare_attached", false, [](bool, long long& v) { v = sys().lens_flare_bone != nullptr ? 1 : 0; }},
    FIELD("cutscene.border_state", true, sys().flow.cutscene_border),
    GAME_FIELD("screen.fade_status", true, st::GameField::ScreenFadeStatus),
    GAME_FIELD("screen.bg_gamma", true, st::GameField::BackgroundGamma),
    FIELD("camera.user_max_speed_xz", true, camera::game_camera().user_camera.max_speed_xz),
    FIELD("camera.user_max_speed_y", true, camera::game_camera().user_camera.max_speed_y),
    FIELD("player.weapon_attack", true, sys().combat.weapon_attack),
    FIELD("game.sys_flags", true, sys().process.sys_flags),
    FIELD("game.npc_flags", true, sys().scene.npc_flags),
    GAME_FIELD("game.delta_time", true, st::GameField::DeltaTime),
    GAME_FIELD("game.delta_time_raw", false, st::GameField::DeltaTimeRaw),
    GAME_FIELD("game.gravity_speed", true, st::GameField::GravitySpeed),
    GAME_FIELD("game.vblanks", false, st::GameField::VBlanks),
    GAME_FIELD("game.active_buffer", false, st::GameField::ActiveBuffer),
    FIELD("game.brightness", true, game::session().game.settings.brightness),
    FIELD("game.extra_options_enabled", true, game::session().game.settings.extra_options),
    FIELD("game.fix_second_newspaper", false, g_PcConfig.fixSecondNewspaper),
    FIELD("game.fix_nurse_grab_drain", false, g_PcConfig.fixNurseGrabDrain),
    FIELD("game.stick_radius", false, g_PcConfig.stickRadius),
    FIELD("game.tick30", false, sh::loop::frame_timing().tick30),
    GAME_FIELD("save.map_idx", true, st::GameField::SaveMapIdx),
    GAME_FIELD("save.location_id", true, st::GameField::SaveLocationId),
    GAME_FIELD("save.clear_game_count", true, st::GameField::SaveClearGameCount),
    GAME_FIELD("save.clear_game_endings", true, st::GameField::SaveClearGameEndings),
    GAME_FIELD("save.current_ending_flags", true, st::GameField::SaveCurrentEndingFlags),
    GAME_FIELD("save.next_fear_mode", true, st::GameField::SaveNextFearMode),
    GAME_FIELD("save.game_difficulty", true, st::GameField::SaveGameDifficulty),
};

#undef FIELD
#undef GAME_FIELD

struct CharaFieldName {
    const char* name;
    st::CharaField field;
};

// npc.<name>(slot) / npc.set_<name>(slot, v).
const CharaFieldName kCharaFields[] = {
    {"npc.pos_x", st::CharaField::PosX},
    {"npc.pos_y", st::CharaField::PosY},
    {"npc.pos_z", st::CharaField::PosZ},
    {"npc.rot_x", st::CharaField::RotX},
    {"npc.rot_y", st::CharaField::RotY},
    {"npc.rot_z", st::CharaField::RotZ},
    {"npc.chara_id", st::CharaField::CharaId},
    {"npc.palette_idx", st::CharaField::PaletteIdx},
    {"npc.control_state", st::CharaField::ControlState},
    {"npc.state_step", st::CharaField::StateStep},
    {"npc.anim_status", st::CharaField::AnimStatus},
    {"npc.anim_flag_bits", st::CharaField::AnimFlags},
    {"npc.anim_time", st::CharaField::AnimTime},
    {"npc.anim_keyframe", st::CharaField::AnimKeyframe},
    {"npc.anim_alpha", st::CharaField::AnimAlpha},
    {"npc.angle_to_target", st::CharaField::AngleToTarget},
    {"npc.fall_speed", st::CharaField::FallSpeed},
    {"npc.move_speed", st::CharaField::MoveSpeed},
    {"npc.heading_angle", st::CharaField::HeadingAngle},
    {"npc.flags", st::CharaField::Flags},
    {"npc.attack_received", st::CharaField::AttackReceived},
    {"npc.health", st::CharaField::Health},
    {"npc.death_timer", st::CharaField::DeathTimer},
    {"npc.aux_timer", st::CharaField::AuxTimer},
    {"npc.collision_state", st::CharaField::CollisionState},
    {"npc.scratcher_flags_lo", st::CharaField::ScratcherFlagsLow},
};

// `name` is "<group>.<field>" (get) or "<group>.set_<field>" (set) of `full`.
bool field_name_matches(const char* full, const char* name, bool writable, bool* set) {
    if (std::strcmp(full, name) == 0) {
        *set = false;
        return true;
    }
    const char* dot = std::strchr(full, '.');
    const std::size_t group = static_cast<std::size_t>(dot - full) + 1;
    if (writable && std::strncmp(name, full, group) == 0 && std::strncmp(name + group, "set_", 4) == 0 &&
        std::strcmp(name + group + 4, dot + 1) == 0) {
        *set = true;
        return true;
    }
    return false;
}

const char* current_map() {
    const char* m = ShScript_MapName();
    return m != nullptr ? m : "";
}

int dispatch(const char* name, Call& c, long long* out) {
    bool other_map = false;
    for (const Entry& e : kBinds) {
        if (std::strcmp(e.name, name) != 0) {
            continue;
        }
        if (e.map != nullptr && std::strcmp(e.map, current_map()) != 0) {
            other_map = true;
            continue;
        }
        return e.fn(c, out);
    }
    bool set = false;
    for (const Field& f : kStateFields) {
        if (field_name_matches(f.name, name, f.writable, &set)) {
            long long v = set ? c.integer(0) : 0;
            if (c.bad) {
                return -1;
            }
            f.access(set, v);
            if (set) {
                return 0;
            }
            out[0] = v;
            return 1;
        }
    }
    for (const CharaFieldName& f : kCharaFields) {
        if (field_name_matches(f.name, name, true, &set)) {
            void* record = chara_arg(c, 0);
            long long v = set ? c.integer(1) : 0;
            if (!c.bad && record == nullptr) {
                c.fail("%s", "a character slot number is expected");
            }
            if (c.bad) {
                return -1;
            }
            st::chara_field(record, f.field, set, v);
            if (set) {
                return 0;
            }
            out[0] = v;
            return 1;
        }
    }
    if (other_map) {
        c.fail("'%s' is not available on this map", name);
        return -1;
    }
    c.fail("unknown function '%s'", name);
    return -1;
}

}  // namespace

int call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err, int err_cap) {
    if (out_cap < 8) {
        std::snprintf(err, static_cast<std::size_t>(err_cap), "%s", "result buffer too small");
        return -1;
    }
    Call c{argv, argc, err, err_cap};
    const int n = dispatch(name, c, out);
    return c.bad ? -1 : n;
}

const char* const* api_names() {
    static std::vector<std::string> all;
    static std::vector<const char*> list;
    if (!list.empty()) {
        return list.data();
    }
    for (const Entry& e : kBinds) {
        if (std::find(all.begin(), all.end(), e.name) == all.end()) {
            all.emplace_back(e.name);
        }
    }
    const auto add_field = [](const char* full, bool writable) {
        all.emplace_back(full);
        if (writable) {
            const char* dot = std::strchr(full, '.');
            all.emplace_back(std::string(full, dot + 1) + "set_" + (dot + 1));
        }
    };
    for (const Field& f : kStateFields) {
        add_field(f.name, f.writable);
    }
    for (const CharaFieldName& f : kCharaFields) {
        add_field(f.name, true);
    }
    for (const std::string& s : all) {
        list.push_back(s.c_str());
    }
    list.push_back(nullptr);
    return list.data();
}

}  // namespace sh::script::named

extern "C" int PcScriptNamed_Call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap,
                                  char* err, int err_cap) {
    return sh::script::named::call(name, argc, argv, out, out_cap, err, err_cap);
}

extern "C" const char* const* PcScriptNamed_List(void) {
    return sh::script::named::api_names();
}

extern "C" int PcScriptNamed_RawApiEnabled(void) {
    return g_PcConfig.scriptRawApi;
}

extern "C" int PcScriptNamed_PositionRef(const char* ref, int out[3], char* err, int err_cap) {
    if (ref == nullptr) {
        return 0;
    }
    sh::script::named::Call c{nullptr, 0, err, err_cap};
    const auto* p = static_cast<const std::int32_t*>(sh::script::named::resolve_ref(c, ref));
    if (c.bad) {
        return -1;
    }
    if (p == nullptr) {
        return 0;
    }
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    return 1;
}

extern "C" void* PcScriptNamed_RefAddr(const char* ref, char* err, int err_cap) {
    if (ref == nullptr) {
        return nullptr;
    }
    sh::script::named::Call c{nullptr, 0, err, err_cap};
    void* p = sh::script::named::resolve_ref(c, ref);
    return c.bad ? nullptr : p;
}

extern "C" void* PcScriptNamed_VarAddr(const char* ref) {
    std::string_view name;
    long long index = 0;
    if (ref == nullptr || !sh::script::named::parse_ref(ref, &name, &index)) {
        return nullptr;
    }
    void* address = nullptr;
    sh::script::named_state::VarType type = sh::script::named_state::VarType::Ref;
    char why[8];
    return sh::script::named_state::map_variable(name, index, &address, &type, why, static_cast<int>(sizeof(why))) ==
                   sh::script::named_state::VarLookup::Found
               ? address
               : nullptr;
}
