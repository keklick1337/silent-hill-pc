// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_sound.h"

#include <filesystem>
#include <optional>
#include <string>

#include "engine/audio/sound_registry.h"
#include "engine/mods/registry.h"
#include "engine/script/caller_mod.h"
#include "engine/script/host_interfaces.h"
#include "engine/script/script_host.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

SoundHost* g_sound = nullptr;
MusicHost* g_music = nullptr;
SpawnHost* g_spawn = nullptr;

Host* host_of(lua_State* L) { return static_cast<Host*>(lua_touserdata(L, lua_upvalueindex(1))); }

// Map-port scripts pass the bools of the original calls; every integer argument takes a boolean as 0/1.
int arg_int(lua_State* L, int idx) {
    if (lua_isboolean(L, idx)) return lua_toboolean(L, idx) ? 1 : 0;
    return static_cast<int>(luaL_checkinteger(L, idx));
}
int opt_int(lua_State* L, int idx, int def) { return lua_isnoneornil(L, idx) ? def : arg_int(L, idx); }

const sh::audio::SoundRegistry& registry(lua_State* L) {
    const HostConfig& c = host_of(L)->config();
    return sh::audio::SoundRegistry::shared(c.gamedata_dir, c.mods_dir);
}

// A registry name, the calling mod's own "<mod>:<name>" first (caller_mod.h); -1 unknown.
int sound_id(lua_State* L, const char* name) {
    const sh::audio::SoundRegistry& r = registry(L);
    return find_scoped(L, name, [&r](const char* n) { return r.id(n); });
}

// A sound: an id or a registry name ("footstep_concrete"); an unknown name is an error.
int sfx_arg(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char* name = lua_tostring(L, idx);
        const int id = sound_id(L, name);
        if (id < 0) return luaL_error(L, "unknown sound '%s' (gamedata/sounds)", name);
        return id;
    }
    return arg_int(L, idx);
}

int chara_arg(lua_State* L, int idx) {
    if (lua_type(L, idx) != LUA_TSTRING) return arg_int(L, idx);
    const char* name = lua_tostring(L, idx);
    const auto find = host_of(L)->game_api().chara_id_find;
    const int n = find != nullptr ? find_scoped(L, name, find) : -1;
    if (n < 0) return luaL_error(L, "unknown character '%s'", name);
    return n;
}

// GameApi pointers (tests and older game layers) behind the interfaces.
class GameApiSound final : public SoundHost {
public:
    explicit GameApiSound(const GameApi& a) : a_(a) {}
    void play(int id) override { a_.sfx_play(id); }
    void stop(int id) override { a_.sfx_stop(id); }
    void play_3d(int id, int v) override { a_.sfx_3d(id, v); }
    void play_3d_at(int id, int x, int y, int z, int v) override { a_.sfx_3d_at(id, x, y, z, v); }
    void play_flags(int id, int x, int y, int z, int v, int f) override { a_.sfx_flags_play(id, x, y, z, v, f); }
    void play_flags_pitch(int id, int x, int y, int z, int v, int f, int p) override {
        a_.sfx_flags_pitch_play(id, x, y, z, v, f, p);
    }
    void play_pitch(int id, int x, int y, int z, int v, int p) override { a_.sfx_pitch_play(id, x, y, z, v, p); }
    void play_at_pos(int id, int x, int y, int z, int v, int p) override { a_.sfx_play_at_pos(id, x, y, z, v, p); }
    void play_falloff_pitch(int id, int x, int y, int z, int v, int f, int p) override {
        a_.sfx_falloff_pitch_play(id, x, y, z, v, f, p);
    }
    void sd_call(int c) override { a_.sd_call(c); }
    int sd_command_number(const char* n) override { return a_.sd_command_number != nullptr ? a_.sd_command_number(n) : -1; }
    void attributes_update(int id, int b, int v, int p) override { a_.sfx_attributes_update(id, b, v, p); }
    int play_balanced(int id, int b, int v) override { return a_.sd_play_sfx(id, b, v); }
    // The named family has no GameApi pointers; only these two had flat ones.
    int position_ref(const char*, int*, char* err, int errCap) override {
        if (errCap > 0) err[0] = '\0';
        return -1;
    }
    void play_with_flags(int, const int*, int, int) override {}
    void play_falloff(int, const int*, int, int, int) override {}
    void play_3d_typed(int, const int*, int, int) override {}
    void play_3d_falloff(int, int, int, int, int, int, int) override {}
    int streaming() override { return 0; }
    void xa_preload(int id) override { a_.xa_preload(id); }
    int ambient_ready() override { return a_.ambient_sfx(); }
    void radio_stop() override {}
    void radio_npc_clear() override {}

private:
    const GameApi& a_;
};

class GameApiMusic final : public MusicHost {
public:
    explicit GameApiMusic(const GameApi& a) : a_(a) {}
    void change(int t) override { if (a_.bgm_change) a_.bgm_change(t); }
    void crossfade(int) override {}
    int status_flags() override { return a_.bgm_status_flags_get ? a_.bgm_status_flags_get() : 0; }
    void set_status_flags(int v) override { if (a_.bgm_status_flags_set) a_.bgm_status_flags_set(v); }

private:
    const GameApi& a_;
};

class GameApiSpawn final : public SpawnHost {
public:
    explicit GameApiSpawn(const GameApi& a) : a_(a) {}
    int spawn(int c, int f, int x, int z, int r, int s) override { return a_.chara_spawn(c, f, x, z, r, s); }
    void spawn_flags_set(int c, int i, int f) override { a_.chara_spawn_flags_set(c, i, f); }
    void spawn_entry_set(int e, int on) override { a_.chara_spawn_entry_set(e, on); }

private:
    const GameApi& a_;
};

// Pick the engine-owned host, else the GameApi path when the pointer is set, else raise.
#define SOUND_HOST(field, what) \
    GameApiSound legacy(host_of(L)->game_api()); \
    SoundHost* h = g_sound; \
    if (h == nullptr) { \
        if (host_of(L)->game_api().field == nullptr) return luaL_error(L, what " is not available"); \
        h = &legacy; \
    }
#define MUSIC_HOST(field, what) \
    GameApiMusic legacy(host_of(L)->game_api()); \
    MusicHost* h = g_music; \
    if (h == nullptr) { \
        if (host_of(L)->game_api().field == nullptr) return luaL_error(L, what " is not available"); \
        h = &legacy; \
    }
#define SPAWN_HOST(field, what) \
    GameApiSpawn legacy(host_of(L)->game_api()); \
    SpawnHost* h = g_spawn; \
    if (h == nullptr) { \
        if (host_of(L)->game_api().field == nullptr) return luaL_error(L, what " is not available"); \
        h = &legacy; \
    }

int l_sfx(lua_State* L) {
    SOUND_HOST(sfx_play, "sh.sfx")
    h->play(sfx_arg(L, 1));
    return 0;
}
int l_sfx_stop(lua_State* L) {
    SOUND_HOST(sfx_stop, "sh.sfx_stop")
    h->stop(sfx_arg(L, 1));
    return 0;
}
int l_sfx_3d(lua_State* L) {
    SOUND_HOST(sfx_3d, "sh.sfx_3d")
    h->play_3d(sfx_arg(L, 1), opt_int(L, 2, 128));
    return 0;
}
int l_sfx_3d_at(lua_State* L) {
    SOUND_HOST(sfx_3d_at, "sh.sfx_3d_at")
    h->play_3d_at(sfx_arg(L, 1), arg_int(L, 2), arg_int(L, 3), arg_int(L, 4), opt_int(L, 5, 128));
    return 0;
}
int l_sfx_flags_play(lua_State* L) {
    SOUND_HOST(sfx_flags_play, "sh.sfx_flags_play")
    h->play_flags(sfx_arg(L, 1), arg_int(L, 2), opt_int(L, 3, 0), opt_int(L, 4, 0), opt_int(L, 5, 128),
                  opt_int(L, 6, 0));
    return 0;
}
int l_sfx_flags_pitch_play(lua_State* L) {
    SOUND_HOST(sfx_flags_pitch_play, "sh.sfx_flags_pitch_play")
    h->play_flags_pitch(sfx_arg(L, 1), arg_int(L, 2), opt_int(L, 3, 0), opt_int(L, 4, 0), opt_int(L, 5, 128),
                        opt_int(L, 6, 0), opt_int(L, 7, 0));
    return 0;
}
int l_sfx_pitch_play(lua_State* L) {
    SOUND_HOST(sfx_pitch_play, "sh.sfx_pitch_play")
    h->play_pitch(sfx_arg(L, 1), arg_int(L, 2), opt_int(L, 3, 0), opt_int(L, 4, 0), opt_int(L, 5, 128),
                  opt_int(L, 6, 0));
    return 0;
}
int l_sfx_play_at_pos(lua_State* L) {
    SOUND_HOST(sfx_play_at_pos, "sh.sfx_play_at_pos")
    h->play_at_pos(sfx_arg(L, 1), arg_int(L, 2), opt_int(L, 3, 0), opt_int(L, 4, 0), opt_int(L, 5, 128),
                   opt_int(L, 6, 0));
    return 0;
}
int l_sfx_falloff_pitch_play(lua_State* L) {
    SOUND_HOST(sfx_falloff_pitch_play, "sh.sfx_falloff_pitch_play")
    h->play_falloff_pitch(sfx_arg(L, 1), arg_int(L, 2), opt_int(L, 3, 0), opt_int(L, 4, 0), opt_int(L, 5, 128),
                          opt_int(L, 6, 0), opt_int(L, 7, 0));
    return 0;
}
int l_sd_call(lua_State* L) {
    SOUND_HOST(sd_call, "sh.sd_call")
    // A sound name plays that sound; a command name ("stop_xa_audio") runs an SD bus command;
    // numbers keep working for everything else (KDT/VAB loads are data, not commands).
    if (lua_type(L, 1) == LUA_TSTRING) {
        const char* name = lua_tostring(L, 1);
        int id = sound_id(L, name);
        if (id < 0) {
            id = h->sd_command_number(name);
            if (id < 0) return luaL_error(L, "unknown sound or SD command '%s'", name);
        }
        h->sd_call(id);
        return 0;
    }
    h->sd_call(arg_int(L, 1));
    return 0;
}
int l_sfx_attributes_update(lua_State* L) {
    SOUND_HOST(sfx_attributes_update, "sh.sfx_attributes_update")
    h->attributes_update(sfx_arg(L, 1), arg_int(L, 2), opt_int(L, 3, 128), opt_int(L, 4, 0));
    return 0;
}
int l_sd_play_sfx(lua_State* L) {
    SOUND_HOST(sd_play_sfx, "sh.sd_play_sfx")
    lua_pushinteger(L, h->play_balanced(sfx_arg(L, 1), arg_int(L, 2), arg_int(L, 3)));
    return 1;
}

// Flat helpers that are sound data or sound-system state.
int l_ambient_sfx(lua_State* L) {
    SOUND_HOST(ambient_sfx, "sh.ambient_sfx")
    lua_pushboolean(L, h->ambient_ready() != 0);
    return 1;
}
int l_xa_preload(lua_State* L) {
    SOUND_HOST(xa_preload, "sh.xa_preload")
    h->xa_preload(sfx_arg(L, 1));  // an XA voice line of the sound registry ("phone_call_voice") or its id
    return 0;
}

// sh.chara_sfx(chara, slot[, fallback=-1]): a character's sound by slot from the character data
// (gamedata/chara). An unknown character answers the fallback, like an unset slot.
int l_chara_sfx(lua_State* L) {
    const GameApi& api = host_of(L)->game_api();
    if (api.chara_sfx == nullptr) return luaL_error(L, "sh.chara_sfx is not available");
    int id = -1;
    if (lua_type(L, 1) == LUA_TNUMBER) {
        id = static_cast<int>(luaL_checkinteger(L, 1));
    } else {
        const char* name = luaL_checkstring(L, 1);
        id = api.chara_id_find != nullptr ? find_scoped(L, name, api.chara_id_find) : -1;
    }
    const char* slot = luaL_checkstring(L, 2);
    const int fallback = lua_isnoneornil(L, 3) ? -1 : sfx_arg(L, 3);
    lua_pushinteger(L, id < 0 ? fallback : api.chara_sfx(id, slot, fallback));
    return 1;
}

// sh.sound.id(name) -> id (an unknown name is an error: a typo fails at load, not in play);
// sh.sound.name(id) -> name | nil; sh.sound.exists(name) -> boolean. Host-side: no game needed.
int l_sound_id(lua_State* L) {
    lua_pushinteger(L, sfx_arg(L, 1));
    return 1;
}
int l_sound_name(lua_State* L) {
    const std::string* name = registry(L).name(static_cast<int>(luaL_checkinteger(L, 1)));
    if (name == nullptr) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, name->data(), name->size());
    }
    return 1;
}
int l_sound_exists(lua_State* L) {
    lua_pushboolean(L, registry(L).find(luaL_checkstring(L, 1)) != nullptr);
    return 1;
}

// sh.sound.swap(sound, file[, {volume, pitch, loop}]) -> bool: while set, the game's calls of the sound play the file
// of the active mods (a path inside a mod folder, "mymod/sounds/hiss.wav"; WAV or Ogg Opus) instead; the sound stops
// first if it plays. sh.sound.swap(sound) (no file) ends it; sh.sound.swap() ends every one. Not saved.
int l_sound_swap(lua_State* L) {
    if (lua_gettop(L) == 0) {
        lua_pushboolean(L, sh::audio::clear_sound_swap(-1));
        return 1;
    }
    const int id = sfx_arg(L, 1);
    if (g_sound != nullptr) g_sound->stop(id);
    if (lua_isnoneornil(L, 2)) {
        lua_pushboolean(L, sh::audio::clear_sound_swap(id));
        return 1;
    }
    const char* file = luaL_checkstring(L, 2);
    const std::optional<std::filesystem::path> path = sh::mods::registry().find(file);
    if (!path) {
        lua_pushboolean(L, 0);
        return 1;
    }
    sh::audio::SoundInfo info;
    if (const std::string* name = registry(L).name(id)) info.name = *name;
    info.id = id;
    info.file = file;
    info.file_path = *path;
    info.loop = true;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "volume");
        if (lua_isnumber(L, -1)) info.volume = static_cast<float>(lua_tonumber(L, -1));
        lua_getfield(L, 3, "pitch");
        if (lua_isnumber(L, -1)) info.pitch = static_cast<float>(lua_tonumber(L, -1));
        lua_getfield(L, 3, "loop");
        if (lua_isboolean(L, -1)) info.loop = lua_toboolean(L, -1) != 0;
        lua_pop(L, 3);
    }
    sh::audio::set_sound_swap(id, std::move(info));
    lua_pushboolean(L, 1);
    return 1;
}

// The named group sh.sound.* takes its arguments by these rules: nil is 0, a
// boolean 0/1, a string only where a sound (first argument) or a position is expected, and more
// arguments than the function takes is an error.
int named_int(lua_State* L, int idx, const char* fn) {
    switch (lua_type(L, idx)) {
        case LUA_TNONE:
        case LUA_TNIL:
            return 0;
        case LUA_TBOOLEAN:
            return lua_toboolean(L, idx) ? 1 : 0;
        case LUA_TNUMBER:
            if (!lua_isinteger(L, idx)) return luaL_error(L, "sh.%s: argument %d is not an integer", fn, idx);
            return static_cast<int>(lua_tointeger(L, idx));
        case LUA_TSTRING:
            return luaL_error(L, "sh.%s: argument %d is a string where an integer is expected (%s)", fn, idx,
                              lua_tostring(L, idx));
        default:
            return luaL_error(L, "sh.%s: argument %d has an unsupported type", fn, idx);
    }
}
int named_sfx(lua_State* L, int idx, const char* fn) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char* name = lua_tostring(L, idx);
        const int id = sound_id(L, name);
        if (id < 0) return luaL_error(L, "sh.%s: unknown sound '%s' (gamedata/sounds)", fn, name);
        return id;
    }
    return named_int(L, idx, fn);
}
void named_arity(lua_State* L, int n, const char* fn) {
    if (lua_gettop(L) > n) luaL_error(L, "sh.%s: too many arguments", fn);
}
// A sound's place: nil (or 0/false) = none, a reference string resolved by the host, or {x, y, z} (Q12).
const int* named_pos(lua_State* L, SoundHost* h, int idx, const char* fn, int buf[3]) {
    switch (lua_type(L, idx)) {
        case LUA_TNONE:
        case LUA_TNIL:
            return nullptr;
        case LUA_TBOOLEAN:
            if (!lua_toboolean(L, idx)) return nullptr;
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx) && lua_tointeger(L, idx) == 0) return nullptr;
            break;
        case LUA_TSTRING: {
            char err[160] = {};
            const int r = h->position_ref(lua_tostring(L, idx), buf, err, static_cast<int>(sizeof(err)));
            if (r < 0) {
                luaL_error(L, "sh.%s: %s", fn, err[0] != '\0' ? err : "unknown position reference");
                return nullptr;
            }
            return r > 0 ? buf : nullptr;
        }
        case LUA_TTABLE:
            for (int i = 0; i < 3; ++i) {
                lua_rawgeti(L, idx, i + 1);
                if (!lua_isinteger(L, -1)) luaL_error(L, "sh.%s: a position table takes three integers (Q12)", fn);
                buf[i] = static_cast<int>(lua_tointeger(L, -1));
                lua_pop(L, 1);
            }
            return buf;
        default:
            break;
    }
    luaL_error(L, "sh.%s: a position takes a reference string, {x, y, z} or nil", fn);
    return nullptr;
}

#define NAMED_SOUND(what) \
    SoundHost* h = g_sound; \
    if (h == nullptr) return luaL_error(L, "sh." what " is not available");
#define NAMED_MUSIC(what) \
    MusicHost* h = g_music; \
    if (h == nullptr) return luaL_error(L, "sh." what " is not available");

int l_sound_sd_call(lua_State* L) {
    NAMED_SOUND("sound.sd_call")
    named_arity(L, 1, "sound.sd_call");
    h->sd_call(named_sfx(L, 1, "sound.sd_call"));
    return 0;
}
int l_sound_stop(lua_State* L) {
    NAMED_SOUND("sound.stop")
    named_arity(L, 1, "sound.stop");
    h->stop(named_sfx(L, 1, "sound.stop"));
    return 0;
}
int l_sound_attributes(lua_State* L) {
    NAMED_SOUND("sound.attributes")
    const char* fn = "sound.attributes";
    named_arity(L, 4, fn);
    const int id = named_sfx(L, 1, fn);
    const int balance = named_int(L, 2, fn);
    const int vol = named_int(L, 3, fn);
    h->attributes_update(id, balance, vol, named_int(L, 4, fn));
    return 0;
}
// sh.sound.fade(sound, level[, seconds[, stop]]) -> bool: a sound that plays a file (a "file" entry of sounds/*.json,
// or sh.sound.swap) moves to `level` of its volume (0 silent .. 1 as it plays) over `seconds` (0: at once), smoothly
// in the mix, and stops when it gets there if `stop`. A sound that is not playing keeps the level for its next play,
// so "fade to 0, play, fade to 1 in a second" is a fade-in from silence. True when the sound was playing.
int l_sound_fade(lua_State* L) {
    NAMED_SOUND("sound.fade")
    const char* fn = "sound.fade";
    named_arity(L, 4, fn);
    const int id = named_sfx(L, 1, fn);
    const lua_Number level = luaL_checknumber(L, 2);
    const lua_Number seconds = luaL_optnumber(L, 3, 0.0);
    luaL_argcheck(L, level >= 0.0 && level <= 4.0, 2, "a level is 0 (silent) to 4");
    luaL_argcheck(L, seconds >= 0.0 && seconds <= 600.0, 3, "a time is 0 to 600 seconds");
    lua_pushboolean(L, h->fade(id, static_cast<float>(level), static_cast<float>(seconds), lua_toboolean(L, 4) != 0));
    return 1;
}
int l_sound_play(lua_State* L) {
    NAMED_SOUND("sound.play")
    const char* fn = "sound.play";
    named_arity(L, 3, fn);
    const int id = named_sfx(L, 1, fn);
    const int balance = named_int(L, 2, fn);
    lua_pushinteger(L, h->play_balanced(id, balance, named_int(L, 3, fn)));
    return 1;
}
int l_sound_play_with_flags(lua_State* L) {
    NAMED_SOUND("sound.play_with_flags")
    const char* fn = "sound.play_with_flags";
    named_arity(L, 4, fn);
    int buf[3];
    const int id = named_sfx(L, 1, fn);
    const int* pos = named_pos(L, h, 2, fn, buf);
    const int vol = named_int(L, 3, fn);
    h->play_with_flags(id, pos, vol, named_int(L, 4, fn));
    return 0;
}
int l_sound_play_falloff(lua_State* L) {
    NAMED_SOUND("sound.play_falloff")
    const char* fn = "sound.play_falloff";
    named_arity(L, 5, fn);
    int buf[3];
    const int id = named_sfx(L, 1, fn);
    const int* pos = named_pos(L, h, 2, fn, buf);
    const int vol = named_int(L, 3, fn);
    const int falloff = named_int(L, 4, fn);
    h->play_falloff(id, pos, vol, falloff, named_int(L, 5, fn));
    return 0;
}
int l_sound_play_3d(lua_State* L) {
    NAMED_SOUND("sound.play_3d")
    const char* fn = "sound.play_3d";
    named_arity(L, 4, fn);
    int buf[3];
    const int id = named_sfx(L, 1, fn);
    const int* pos = named_pos(L, h, 2, fn, buf);
    const int vol = named_int(L, 3, fn);
    h->play_3d_typed(id, pos, vol, named_int(L, 4, fn));
    return 0;
}
int l_sound_play_3d_falloff(lua_State* L) {
    NAMED_SOUND("sound.play_3d_falloff")
    const char* fn = "sound.play_3d_falloff";
    named_arity(L, 7, fn);
    int v[7];
    v[0] = named_sfx(L, 1, fn);
    for (int i = 1; i < 7; ++i) v[i] = named_int(L, i + 1, fn);
    h->play_3d_falloff(v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
    return 0;
}
int l_sound_streaming(lua_State* L) {
    NAMED_SOUND("sound.streaming")
    named_arity(L, 0, "sound.streaming");
    lua_pushinteger(L, h->streaming());
    return 1;
}
int l_sound_xa_preload(lua_State* L) {
    NAMED_SOUND("sound.xa_preload")
    named_arity(L, 1, "sound.xa_preload");
    h->xa_preload(named_sfx(L, 1, "sound.xa_preload"));
    return 0;
}
int l_sound_radio_stop(lua_State* L) {
    NAMED_SOUND("sound.radio_stop")
    named_arity(L, 0, "sound.radio_stop");
    h->radio_stop();
    return 0;
}
int l_sound_radio_npc_clear(lua_State* L) {
    NAMED_SOUND("sound.radio_npc_clear")
    named_arity(L, 0, "sound.radio_npc_clear");
    h->radio_npc_clear();
    return 0;
}
int l_sound_bgm_change(lua_State* L) {
    NAMED_MUSIC("sound.bgm_change")
    named_arity(L, 1, "sound.bgm_change");
    h->change(named_int(L, 1, "sound.bgm_change"));
    return 0;
}
int l_sound_bgm_crossfade(lua_State* L) {
    NAMED_MUSIC("sound.bgm_crossfade")
    named_arity(L, 1, "sound.bgm_crossfade");
    h->crossfade(named_int(L, 1, "sound.bgm_crossfade"));
    return 0;
}
int l_sound_bgm_flags(lua_State* L) {
    NAMED_MUSIC("sound.bgm_flags")
    lua_pushinteger(L, h->status_flags());
    return 1;
}
int l_sound_set_bgm_flags(lua_State* L) {
    NAMED_MUSIC("sound.set_bgm_flags")
    h->set_status_flags(named_int(L, 1, "sound.set_bgm_flags"));
    return 0;
}

#undef NAMED_SOUND
#undef NAMED_MUSIC

int l_bgm(lua_State* L) {
    MUSIC_HOST(bgm_change, "sh.bgm")
    h->change(arg_int(L, 1));
    return 0;
}
int l_bgm_status_flags_get(lua_State* L) {
    MUSIC_HOST(bgm_status_flags_get, "sh.bgm_status_flags_get")
    lua_pushinteger(L, h->status_flags());
    return 1;
}
int l_bgm_status_flags_set(lua_State* L) {
    MUSIC_HOST(bgm_status_flags_set, "sh.bgm_status_flags_set")
    h->set_status_flags(arg_int(L, 1));
    return 0;
}

int l_chara_spawn(lua_State* L) {
    SPAWN_HOST(chara_spawn, "sh.chara_spawn")
    lua_pushinteger(L, h->spawn(chara_arg(L, 1), arg_int(L, 2), arg_int(L, 3), arg_int(L, 4), opt_int(L, 5, 0),
                                opt_int(L, 6, 0)));
    return 1;
}
int l_chara_spawn_flags_set(lua_State* L) {
    SPAWN_HOST(chara_spawn_flags_set, "sh.chara_spawn_flags_set")
    h->spawn_flags_set(chara_arg(L, 1), arg_int(L, 2), arg_int(L, 3));
    return 0;
}
// Entry (0-based, 0..31) of the map's character spawn table is in play (on) or free again.
int l_chara_spawn_entry_set(lua_State* L) {
    SPAWN_HOST(chara_spawn_entry_set, "sh.chara_spawn_entry_set")
    if (!lua_isinteger(L, 1)) return luaL_error(L, "sh.chara_spawn_entry_set: entry must be an integer 0..31");
    const lua_Integer entry = lua_tointeger(L, 1);
    if (entry < 0 || entry > 31) {
        return luaL_error(L, "sh.chara_spawn_entry_set: entry %d is outside 0..31", static_cast<int>(entry));
    }
    luaL_checkany(L, 2);
    h->spawn_entry_set(static_cast<int>(entry), lua_toboolean(L, 2) ? 1 : 0);
    return 0;
}

}  // namespace

int sound_arg(lua_State* L, int idx) { return sfx_arg(L, idx); }
int sound_id_find(lua_State* L, const char* name) { return sound_id(L, name); }

void set_sound_host(SoundHost* host) { g_sound = host; }
void set_music_host(MusicHost* host) { g_music = host; }
void set_spawn_host(SpawnHost* host) { g_spawn = host; }

void install_sound_families(lua_State* L, Host* host) {
    struct Entry {
        const char* group;  // canonical group, nullptr = flat only
        const char* canonical;
        const char* flat;
        lua_CFunction fn;
    };
    const Entry entries[] = {
        {"audio", "sfx", "sfx", l_sfx},
        {"audio", "sfx_stop", "sfx_stop", l_sfx_stop},
        {"audio", "bgm", "bgm", l_bgm},
        {nullptr, nullptr, "sfx_3d", l_sfx_3d},
        {nullptr, nullptr, "sfx_3d_at", l_sfx_3d_at},
        {nullptr, nullptr, "sfx_flags_play", l_sfx_flags_play},
        {nullptr, nullptr, "sfx_flags_pitch_play", l_sfx_flags_pitch_play},
        {nullptr, nullptr, "sfx_pitch_play", l_sfx_pitch_play},
        {nullptr, nullptr, "sfx_play_at_pos", l_sfx_play_at_pos},
        {nullptr, nullptr, "sfx_falloff_pitch_play", l_sfx_falloff_pitch_play},
        {nullptr, nullptr, "sd_call", l_sd_call},
        {nullptr, nullptr, "sfx_attributes_update", l_sfx_attributes_update},
        {nullptr, nullptr, "sd_play_sfx", l_sd_play_sfx},
        {nullptr, nullptr, "bgm_status_flags_get", l_bgm_status_flags_get},
        {nullptr, nullptr, "bgm_status_flags_set", l_bgm_status_flags_set},
        {"chara", "spawn", "chara_spawn", l_chara_spawn},
        {"chara", "spawn_flags_set", "chara_spawn_flags_set", l_chara_spawn_flags_set},
        {"chara", "spawn_entry_set", "chara_spawn_entry_set", l_chara_spawn_entry_set},
        {nullptr, nullptr, "ambient_sfx", l_ambient_sfx},
        {nullptr, nullptr, "xa_preload", l_xa_preload},
        {nullptr, nullptr, "chara_sfx", l_chara_sfx},
        // The named group: sh.sound.<name> only (no flat alias).
        {"sound", "id", nullptr, l_sound_id},
        {"sound", "name", nullptr, l_sound_name},
        {"sound", "exists", nullptr, l_sound_exists},
        {"sound", "swap", nullptr, l_sound_swap},
        {"sound", "sd_call", nullptr, l_sound_sd_call},
        {"sound", "stop", nullptr, l_sound_stop},
        {"sound", "attributes", nullptr, l_sound_attributes},
        {"sound", "fade", nullptr, l_sound_fade},
        {"sound", "play", nullptr, l_sound_play},
        {"sound", "play_with_flags", nullptr, l_sound_play_with_flags},
        {"sound", "play_falloff", nullptr, l_sound_play_falloff},
        {"sound", "play_3d", nullptr, l_sound_play_3d},
        {"sound", "play_3d_falloff", nullptr, l_sound_play_3d_falloff},
        {"sound", "streaming", nullptr, l_sound_streaming},
        {"sound", "xa_preload", nullptr, l_sound_xa_preload},
        {"sound", "bgm_crossfade", nullptr, l_sound_bgm_crossfade},
        {"sound", "bgm_change", nullptr, l_sound_bgm_change},
        {"sound", "radio_stop", nullptr, l_sound_radio_stop},
        {"sound", "radio_npc_clear", nullptr, l_sound_radio_npc_clear},
        {"sound", "bgm_flags", nullptr, l_sound_bgm_flags},
        {"sound", "set_bgm_flags", nullptr, l_sound_set_bgm_flags},
    };
    for (const Entry& e : entries) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, e.fn, 1);
        if (e.group != nullptr) {
            if (e.flat != nullptr) {
                lua_pushvalue(L, -1);
                lua_setfield(L, -3, e.flat);
            }
            if (lua_getfield(L, -2, e.group) != LUA_TTABLE) {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushvalue(L, -1);
                lua_setfield(L, -4, e.group);
            }
            lua_pushvalue(L, -2);
            lua_setfield(L, -2, e.canonical);
            lua_pop(L, 2);
        } else {
            lua_setfield(L, -2, e.flat);
        }
    }
}

}  // namespace sh::script
