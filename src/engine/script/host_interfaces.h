// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Engine-owned, game-agnostic script bind families. A family is a group of
// sh.* functions whose Lua side lives in the engine (src/engine/script/bind_*.cpp) and whose
// effect goes through a small host interface that the game implements and installs at boot.
// The game layer holds no Lua code for such a family.

#pragma once

namespace sh::script {

// Event flags / savegame bits: sh.flag.get/set/clear/set_alt (+ flat flag_get/flag_set/flag_set_alt).
class FlagsHost {
public:
    virtual ~FlagsHost() = default;
    virtual int get(int id) = 0;  // 0/1 (not bool: game headers redefine it)
    virtual void set(int id) = 0;
    virtual void clear(int id) = 0;
    // The "complete/disabled" write that map-event code performs.
    virtual void set_alt(int id) = 0;
};

// Install before Host::boot. The pointer must outlive the host; nullptr removes it (the
// functions then raise "not available").
void set_flags_host(FlagsHost* host);
FlagsHost* flags_host();

// Harry (the player character): sh.player.pos/set_pos/freeze + flat player_pos/player_set_pos/
// player_freeze, and the flashlight toggle sh.player.flashlight (flat sh.flashlight).
class PlayerHost {
public:
    virtual ~PlayerHost() = default;
    virtual void pos(int out[3]) = 0;           // Q12 world position
    virtual void set_pos(const int xyz[3]) = 0;  // Q12 world position
    virtual void freeze(int on) = 0;            // 1 freeze, 0 unfreeze
    virtual void unfreeze(int set_idle) = 0;    // unfreeze, optionally forcing the idle pose
    virtual void flashlight(int on) = 0;
};
void set_player_host(PlayerHost* host);
PlayerHost* player_host();

// Camera: sh.camera.pos/look (flat camera_pos/camera_look). The vector is in metres converted to
// Q12 by the binding; args are the seven Q12 offset/accel/speed words, passed verbatim.
class CameraHost {
public:
    virtual ~CameraHost() = default;
    virtual void pos(int warp, int posQ12[3], int argsQ12[7]) = 0;
    virtual void look(int warp, int lookQ12[3], int argsQ12[7]) = 0;
};
void set_camera_host(CameraHost* host);
CameraHost* camera_host();

// Timers: sh.timer.{sys_get,sys_set,cutscene_get,cutscene_set,game_update} (flat sys_timer_get/set,
// cutscene_timer_get/set, game_timer_update). Values are raw Q12 words.
class TimerHost {
public:
    virtual ~TimerHost() = default;
    virtual int sys_get() = 0;
    virtual void sys_set(int valueQ12) = 0;
    virtual int cutscene_get() = 0;  // 0x7FFFFFFF = NO_VALUE
    virtual void cutscene_set(int valueQ12) = 0;
    virtual void game_update() = 0;
};
void set_timer_host(TimerHost* host);

// Sound: sfx, positional sfx, SD bus commands (flat sfx, sfx_stop, sfx_3d, sfx_3d_at, sfx_flags_play,
// sfx_flags_pitch_play, sfx_pitch_play, sfx_play_at_pos, sfx_falloff_pitch_play, sd_call,
// sfx_attributes_update, sd_play_sfx, ambient_sfx, xa_preload; canonical sh.audio.{sfx,sfx_stop};
// the named group sh.sound.* and the sound-name helpers sh.sound.id/name/exists).
// Sound ids are raw (names are resolved by the binding), positions Q12, volumes Q8, pitch raw.
class SoundHost {
public:
    virtual ~SoundHost() = default;
    virtual void play(int sfxId) = 0;
    virtual void stop(int sfxId) = 0;
    virtual void play_3d(int sfxId, int volQ8) = 0;  // at the active event's map point
    virtual void play_3d_at(int sfxId, int x, int y, int z, int volQ8) = 0;
    virtual void play_flags(int sfxId, int x, int y, int z, int volQ8, int flags) = 0;
    virtual void play_flags_pitch(int sfxId, int x, int y, int z, int volQ8, int flags, int pitch) = 0;
    virtual void play_pitch(int sfxId, int x, int y, int z, int volQ8, int pitch) = 0;
    virtual void play_at_pos(int sfxId, int x, int y, int z, int volQ8, int pitch) = 0;
    virtual void play_falloff_pitch(int sfxId, int x, int y, int z, int vol, int falloffQ12, int pitch) = 0;
    virtual void sd_call(int cmd) = 0;
    virtual int sd_command_number(const char* name) = 0;  // -1 unknown
    virtual void attributes_update(int sfxId, int balance, int vol, int pitch) = 0;
    virtual int play_balanced(int sfxId, int balance, int vol) = 0;

    // The named family sh.sound.*. A position is a Q12 point or nullptr (the
    // sound has no place: centred, unattenuated by distance); a string reference ("vec:0", a map
    // variable, "<object>.pos") is resolved by position_ref: 1 = out filled, 0 = the reference
    // names no point (nullptr), -1 = unknown (err filled).
    virtual int position_ref(const char* ref, int out[3], char* err, int errCap) = 0;
    virtual void play_with_flags(int sfxId, const int* pos, int volQ8, int flags) = 0;
    virtual void play_falloff(int sfxId, const int* pos, int vol, int falloffQ12, int pitch) = 0;
    virtual void play_3d_typed(int sfxId, const int* pos, int volQ8, int soundType) = 0;
    // The looping positional sound whose volume follows the distance to (x, y, z).
    virtual void play_3d_falloff(int sfxId, int x, int y, int z, int vol, int falloffQ12, int pitch) = 0;
    virtual int streaming() = 0;           // XA/streamed audio state (0 idle)
    virtual void xa_preload(int xaId) = 0;  // queue an XA line for the next play
    virtual int ambient_ready() = 0;       // the ambient bank finished loading (0/1)
    virtual void radio_stop() = 0;
    virtual void radio_npc_clear() = 0;
};
void set_sound_host(SoundHost* host);

// Music: sh.bgm (flat; canonical sh.audio.bgm), sh.sound.bgm_change/bgm_crossfade and the bgm
// status bits (flat bgm_status_flags_get/set, named sh.sound.bgm_flags/set_bgm_flags).
class MusicHost {
public:
    virtual ~MusicHost() = default;
    virtual void change(int trackIdx) = 0;
    virtual void crossfade(int trackIdx) = 0;
    virtual int status_flags() = 0;
    virtual void set_status_flags(int value) = 0;
};
void set_music_host(MusicHost* host);

// Character spawning: flat chara_spawn, chara_spawn_flags_set, chara_spawn_entry_set
// (canonical sh.chara.{spawn,spawn_flags_set,spawn_entry_set}).
class SpawnHost {
public:
    virtual ~SpawnHost() = default;
    virtual int spawn(int charaId, int spawnFlags, int posX, int posZ, int rotY, int stateStep) = 0;  // slot, -1 full
    virtual void spawn_flags_set(int charaId, int spawnIdx, int spawnFlags) = 0;
    virtual void spawn_entry_set(int entry, int on) = 0;  // 0-based entry of the map's spawn table
};
void set_spawn_host(SpawnHost* host);

// What mods ask the game (bind_world.cpp): sh.game.difficulty, sh.player.facing, sh.world.walkable /
// nearest_walkable and sh.entity.spawn{...}. Q12 world units like the rest of the script API.
class WorldQueryHost {
public:
    virtual ~WorldQueryHost() = default;
    virtual bool in_game() = 0;
    virtual int difficulty() = 0;     // -1 easy, 0 normal, 1 hard
    virtual int player_facing() = 0;  // Q12 angle, 0..4095
    // Ground a character can stand on at (x, z) (its height in *ground_y), and with `from` (x, z) a walk from there
    // that no wall cuts and no big step separates.
    virtual bool walkable(int x, int z, const int* from, int* ground_y) = 0;
    // A character by id ("Groaner") at (x, z) facing `rot` (Q12) with an AI ("auto", "full", "fallback", "none")
    // and an entry state (-1 the character's own); the slot, or -1 with `err` filled.
    virtual int spawn(const char* chara, int x, int z, int rot, const char* ai, int state, char* err, int errCap) = 0;
};
void set_world_query_host(WorldQueryHost* host);

}  // namespace sh::script
