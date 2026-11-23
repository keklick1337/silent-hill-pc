// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The named screen and world effects of the game: glow fields, heat
// fields, lens flares, fire pools, bolt pools and ground effect sets, each loaded by name from its file in
// gamedata/effects/ (or the last mod's copy) the first time one is asked for. A map script or a map's behaviour
// drives them by name; they draw through the engine projection (effect_view.h) into the world layer of the frame by
// order key. The things an effect follows are passed as references to the game's records (`const void*`): a world
// position (three Q12 words), a rotation (three Q12 angles), a scene timer (one Q12 word) or a skeleton (the bone
// frames the hierarchy walk takes, render/game_view.h).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/effects/effect_prims.h"

namespace sh::effects {

// gamedata/effects/glow_fields.json
class GlowFields {
public:
    void reset(const char* name);
    void decay(const char* name);
    void draw(const char* name);
    void draw_mesh(const char* name, const char* mesh);
    void add_point(const char* name, const void* at, const void* turn);
    void add_bones(const char* name, const void* at, const void* skeleton, const void* timer);
    void add_ray(const char* name, const void* at, const void* target);
    void add_flash(const char* name, const void* timer);
    // Ends the emitters of a kind: "Point", "Bones", "Ray" or "Flash".
    void remove(const char* name, const char* kind);
    void clear_emitters(const char* name);
    void emit(const char* name);
};

// gamedata/effects/heat_fields.json
class HeatFields {
public:
    // Placed at a world point (Q12), cleared, its particles thrown.
    void start(const char* name, const Vec3& at);
    // Placed between two world points, turned from `from` towards `to`.
    void start_between(const char* name, const Vec3& from, const Vec3& to);
    // A field that shows for a while: placed now, thrown at its next update.
    void arm(const char* name, const Vec3& at, std::int32_t yaw);
    void stop(const char* name);
    void clear(const char* name);
    // Up to `count` particles around a point relative to the origin (Q12).
    void spawn(const char* name, const Vec3& around, std::int32_t count);
    // One frame; `advance` false keeps the particles still.
    void update(const char* name, bool advance);
    // The origin (Q8 world units); nothing when there is no such field.
    std::optional<Vec3> origin(const char* name);
};

// gamedata/effects/lens_flares.json
class LensFlares {
public:
    // Draws a flare at its origin number `origin`.
    void draw(const char* name, std::int32_t origin);
};

// gamedata/effects/fire_pools.json
class FirePools {
public:
    void reset(const char* name);
    // While paused the particles only play their animations and are drawn.
    void pause(const char* name, bool paused);
    // A scene cue: 15 words (position x, y, z; near, far, lead min/max, scale min/max, shape, kind, z drift, fade,
    // rate; flags), the record of maps/<map>/ending_cues.json.
    void cue(const char* name, const std::int32_t* record);
    // A glow at a world point (Q12); `palette` is the palette row offset.
    void glow(const char* name, const Vec3& at, std::int32_t palette, std::int32_t delay, std::int32_t scale);
    void burst(const char* name, const Vec3& at, std::int32_t palette);
    void glow_burst(const char* name, const Vec3& at, std::int32_t palette);
    // One frame; `sound_gate` (may be null) says whether a fire sound may start now.
    void update(const char* name, int (*sound_gate)());
    void attached_reset(const char* name);
    void attach(const char* name);
    // The attached flames on the bones of a skeleton.
    void draw_attached(const char* name, const void* skeleton);
};

// gamedata/effects/bolt_pools.json
class BoltPools {
public:
    // The sound gaps as the map's data has them.
    void load(const char* name);
    // Owner (bolt_pools.json "owners"), pool origin around a world point (Q12) and the owner's skeleton (null keeps
    // the last one); every bolt ends.
    void attach(const char* name, const char* owner, const Vec3& at, const void* skeleton);
    // The owner names of the pool (empty when there is no such pool).
    std::vector<std::string> owners(const char* name);
    // The player's movement since the last call, scaled by the lead gain (x, z, Q12 per second).
    void track(const char* name, std::int32_t* lead_x, std::int32_t* lead_z);
    // A bolt of a spawner (step, life: used when the spawner has none; bone: the owner's bone an end bolt returns to).
    void spawn(const char* name, const char* spawner, const Vec3& from, const Vec3& to, std::int32_t bone,
               std::int32_t step, std::int32_t life);
    void update(const char* name);
};

// gamedata/effects/ground_fx.json
class GroundFxSets {
public:
    // Starts over around a point (Q12): the origin snaps to its grid, the palettes are rebuilt, everything clears.
    void reset(const char* name, const Vec3& at);
    void dust_at(const char* name, const Vec3& at, std::int32_t index);
    // The first free dust cloud of a kind at a point; -1 when every cloud is alive.
    std::int32_t dust_free(const char* name, const Vec3& at, std::int32_t kind);
    void mark_at(const char* name, const Vec3& at, std::int32_t index);
    // A hole of timeline `timeline` under a character (its record and position); false when all holes are busy.
    bool hole(const char* name, std::int32_t timeline, const void* chara, const Vec3& position);
    // One frame: the holes advance and the pools are drawn, viewed with the character's rotation (Q12 angles).
    void frame(const char* name, const Vec3s& rotation);
};

GlowFields& glow_fields();
HeatFields& heat_fields();
LensFlares& lens_flares();
FirePools& fire_pools();
BoltPools& bolt_pools();
GroundFxSets& ground_fx_sets();

}  // namespace sh::effects
