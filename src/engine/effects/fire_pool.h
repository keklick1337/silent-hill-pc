// SPDX-License-Identifier: GPL-3.0-or-later
// Fire pools: a fixed pool of textured flame sprites that live around a
// pool origin and play short frame animations. Each particle has a behaviour (drift, burn, fall,
// flash) that moves it, fades it and spawns other particles, and a way it is drawn: a billboard
// scaled by the distance, or a quad rolled in the view plane. A second, smaller table of flames
// is pinned to the bones of a character. The map7_s03 Incubus fights and the endings throw their
// fire this way (falling fire balls that land and burn, embers, sparks, the flashes of the boss's
// hits, the flames on Harry). The module owns the particles; the game gives the random numbers,
// the trigonometry, the frame time, the projection, the ground height, the sound and the bones,
// and turns the sprites into primitives (FirePoolHost). Every number is data:
// gamedata/effects/fire_pools.json.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

// A cell of a sprite sheet: texels (u, v, w, h), the texture page and palette words, and the
// anchor (ox, oy) that sits on the particle's position.
struct FireSprite {
    std::int16_t u = 0;
    std::int16_t v = 0;
    std::int16_t w = 0;
    std::int16_t h = 0;
    std::int16_t tpage = 0;
    std::int16_t clut = 0;
    std::uint16_t ox = 0;
    std::uint16_t oy = 0;
};

// One step of an animation: show `sprite` for `hold` 1/30 s, or jump back to step `target`
// (Loop; the target's sprite shows while the jump waits), or end (Stop). A particle normally ends
// on the frame it reaches its stop, but a paused pool keeps it there and draws it every frame:
// the original read the stop's link (its script's first step) as a sprite, so a stop may name
// that sprite (`sprite`, -1 when nothing shows).
struct FireStep {
    enum class Kind : std::uint8_t { Show, Loop, Stop };
    Kind kind = Kind::Show;
    std::int32_t hold = 0;
    int sprite = 0;
    int target = 0;
};

struct FireAnimation {
    std::vector<FireStep> steps;
};

enum class FireUpdate : std::uint8_t {
    None,     // the particle does nothing and is not drawn
    Invalid,  // never set since the pool was reset: dropped when it is activated
    Ember,    // drifts, its speed damped every frame, until its animation ends
    Rise,     // drifts until its animation ends
    Burn,     // burns on the spot, throws tongues and sparks, then fades out
    Fall,     // falls until it hits the ground, then throws embers and burns
    Flash,    // waits, then plays its animation once
};

enum class FireDraw : std::uint8_t {
    None,
    Billboard,  // a screen-aligned sprite sized by the distance
    Rolled,     // a quad in the view plane turned by the particle's roll
};

struct FireParticle {
    bool active = false;
    bool pending = false;  // spawned this frame: activated after the pass over the active ones
    int animation = 0;
    int step = 0;
    std::int32_t hold = 0;     // Q12 time left on the step
    std::int32_t z_drift = 0;  // Q12 per second added to pos.z
    std::int16_t state = 0;
    std::int16_t palette = 0;  // added to the sprite's palette word (billboards)
    std::int32_t timer = 0;
    std::int32_t scale = 0;    // Q12
    Vec3 pos;                  // Q12, relative to the pool origin
    Vec3 vel;                  // Q12 per second
    std::int32_t roll = 0;
    std::int32_t alpha = 0;    // brightness of all three colour channels
    std::int32_t fade = 0;     // taken from alpha every frame
    FireUpdate update = FireUpdate::Invalid;
    FireDraw draw = FireDraw::None;
};

struct AttachedFlame {
    bool active = false;
    int animation = 0;
    int step = 0;
    std::int32_t hold = 0;
    std::int32_t scale = 0;
    std::int32_t alpha = 0;
    int bone = 0;
};

// A cue of a scene (gamedata/maps/<map>/ending_cues.json): where and how many particles to throw.
struct FireCue {
    Vec3 position;            // Q12 world
    std::int32_t near = 0;    // spread: inner radius (ring) or width (box)
    std::int32_t far = 0;     // spread: outer radius (ring) or depth (box)
    std::int32_t lead_min = 0;   // Fall: head start along the throw; Burn: time it burns
    std::int32_t lead_max = 0;
    std::int32_t scale_min = 0;
    std::int32_t scale_max = 0;
    std::int32_t shape = 0;   // 0 ring, 1 box
    std::int32_t kind = 0;    // 0 fire ball, 1 burn, 2 place only, 99 clear the pool
    std::int32_t z_drift = 0;
    std::int32_t fade = 0;
    std::int32_t rate = 0;    // Q12 particles per unit of area and second
    std::int32_t flags = 0;   // bit 1: the count scales by the lead range instead of the frame time
};

struct FirePoolParams {
    int capacity = 80;
    int attached_capacity = 400;
    Vec3 origin;              // Q12 world
    std::int32_t hold_rate = 30;   // steps per second of the animations' hold counts
    std::array<std::int32_t, 4> cull{-160, 160, -160, 112};  // left, right, top, bottom
    std::vector<FireSprite> sprites;
    std::vector<FireAnimation> animations;
    std::map<std::string, int> animation_names;

    struct Ember {
        int animation = 0;
        std::int32_t speed = 4096;
        std::int32_t scale = 8192;
        std::int32_t damping = 3276;
    } ember;
    struct Tongue {
        int animation = 0;
        std::int32_t lift = -1024;
        std::int32_t rise = -4096;
    } tongue;
    struct Spark {
        int animation = 0;
        std::int32_t rise = 4096;
    } spark;
    struct Trail {
        int animation = 0;
        std::int32_t speed_divisor = 41;
        std::int32_t speed_base = 1228;
    } trail;
    struct Burn {
        int animation = 0;     // start step: rand16() / start_divisor
        std::int32_t start_divisor = 4096;
        std::int32_t sound = 0;
        std::int32_t volume = 255;
        int tongue_sprite = -1;
        std::int32_t fade_time = 0;
        std::int32_t fade = 4096;
    } burn;
    struct Fireball {
        int animation = 0;
        std::int32_t ceiling = 0;
        int embers = 5;
        std::int32_t land_time = 0;
        std::int32_t burn_time = 0;
        std::int32_t burn_scale = 8192;
        int trail_sprite = -1;
    } fireball;
    struct Flash {
        std::int32_t wait = 8;
        std::int32_t fade = 10;
    } flash;
    struct Glow {
        int animation = 0;
        std::int32_t alpha = 30;
        std::int32_t delay = 0;   // glow_burst()
        std::int32_t scale = 10240;
    } glow;
    struct Burst {
        std::vector<int> animations;
        std::int32_t alpha = 128;
        std::int32_t pick_divisor = 10923;
        std::int32_t count_mask = 0x50;
        int count_if_set = 2;
        int count_if_clear = 3;
        std::vector<std::int32_t> delays;
        std::vector<std::int32_t> scales;
    } burst;
    struct Cue {
        std::int32_t tilt = 170;
        std::int32_t tilt_speed = 20480;
        std::int32_t area = 25734;
        std::int32_t brightness = 128;
        std::int32_t brightness_top = 40960;
        std::int32_t brightness_divisor = 320;
        int reseed_above = 50;
    } cue;
    struct Attached {
        int animation = 0;     // start step: rand16() / start_divisor
        std::int32_t start_divisor = 4096;
        int bone_first = 1;
        int bone_last = 17;
        std::int32_t scale_divisor = 16;
        std::int32_t scale_base = 2048;
        std::int32_t alpha = 96;
    } attached;
};

// A sprite ready to draw: four corners, texels, colour word (alpha in each channel), texture
// words and the depth slot (not clamped to the ordering table).
struct FireQuad {
    std::array<std::int16_t, 4> x{};
    std::array<std::int16_t, 4> y{};
    std::array<std::uint8_t, 4> u{};
    std::array<std::uint8_t, 4> v{};
    std::uint32_t color = 0;
    std::uint16_t tpage = 0;
    std::uint16_t clut = 0;
    std::int32_t slot = 0;
};

// A projected point; `near` when the projection overflowed (too close to the camera), `far`
// when its depth saturated.
struct FireProjection {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int32_t depth = 0;
    bool near = false;
    bool far = false;
};

struct FireRolledProjection {
    std::array<std::int16_t, 4> x{};
    std::array<std::int16_t, 4> y{};
    std::int32_t depth = 0;
    bool near = false;
    bool far = false;
};

class FirePoolHost {
public:
    virtual ~FirePoolHost() = default;
    // A particle was drawn at this point (world units, PS1 axes) with this share of light: the fire's light for
    // the modern lighting follows it (map_lights.h FireLight). Render only.
    virtual void flame_shown(const std::array<float, 3>& position, float amount) {
        (void)position;
        (void)amount;
    }

    virtual std::int32_t rand16() = 0;  // 0..32767
    virtual void reseed(std::uint32_t seed) = 0;
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t delta_time() = 0;  // Q12 seconds of the frame
    virtual std::int32_t geom_screen() = 0;

    // The view of the pool for this frame: the world-screen matrix at the origin (Q12).
    virtual void begin_view(const Vec3& origin) = 0;
    // Projects a point relative to the origin (Q8).
    virtual FireProjection project(const Vec3s& p) = 0;
    // Projects the corners (Q8, in the view plane, turned by roll) of a quad around a point
    // relative to the origin.
    virtual FireRolledProjection project_rolled(const Vec3s& centre, std::int32_t roll,
                                                const std::array<Vec3s, 4>& corners) = 0;
    // Height of the ground at a world point (Q12).
    virtual std::int32_t ground_height(std::int32_t x, std::int32_t z) = 0;
    // Whether a fire sound may start now (the map's voice limiter).
    virtual bool sound_allowed() = 0;
    virtual void play_sound(std::int32_t id, const Vec3& world, std::int32_t volume) = 0;
    // World position (Q12) of a bone of the character the flames are attached to.
    virtual Vec3 bone_position(int bone) = 0;

    virtual void emit_sprite(const FireQuad& q) = 0;
};

class FirePool {
public:
    explicit FirePool(FirePoolParams params);
    const FirePoolParams& params() const { return params_; }
    const std::vector<FireParticle>& particles() const { return particles_; }
    const std::vector<AttachedFlame>& attached() const { return attached_; }
    bool paused() const { return paused_; }

    // Makes the pool (it has no slots before the first reset) and empties it; every slot drops a
    // particle activated without a behaviour.
    void reset();
    // While paused the particles only play their animations and are drawn.
    void set_paused(bool paused) { paused_ = paused; }
    void clear();

    // Throws the particles of a scene cue.
    void cue(FirePoolHost& host, const FireCue& c);
    // A glow at a world point (Q12); palette is the palette row offset.
    void glow(FirePoolHost& host, const Vec3& world, std::int32_t palette, std::int32_t delay, std::int32_t scale);
    // Two or three sparks of the burst animations at a world point.
    void burst(FirePoolHost& host, const Vec3& world, std::int32_t palette);
    // glow() with the glow's delay and scale, then burst().
    void glow_burst(FirePoolHost& host, const Vec3& world, std::int32_t palette);

    // One frame: moves, animates and draws the particles.
    void update(FirePoolHost& host);

    // The attached flames: cleared, then one flame per bone, drawn on the bones each frame.
    void attached_reset();
    void attach(FirePoolHost& host);
    void draw_attached(FirePoolHost& host);

private:
    FireParticle* acquire();
    // Plays a step of the animation; the sprite to show or -1.
    int step(int animation, int& at, std::int32_t& hold, std::int32_t dt);
    int step(FireParticle& p, std::int32_t dt) { return step(p.animation, p.step, p.hold, dt); }
    int run(FirePoolHost& host, FireParticle& p);
    int ember(FirePoolHost& host, FireParticle& p);
    int rise(FirePoolHost& host, FireParticle& p);
    int burn(FirePoolHost& host, FireParticle& p);
    int fall(FirePoolHost& host, FireParticle& p);
    int flash(FirePoolHost& host, FireParticle& p);
    void spawn_ember(FirePoolHost& host, const FireParticle& from);
    void spawn_tongue(const FireParticle& from);
    void spawn_spark(const FireParticle& from);
    void spawn_trail(FirePoolHost& host, const FireParticle& from);
    void spawn_cue(FirePoolHost& host, const FireCue& c);
    void spark_at(const Vec3& world, int animation, std::int32_t palette, std::int32_t delay, std::int32_t scale);
    std::int32_t spread(FirePoolHost& host, const FireCue& c, Vec3& out);
    std::int32_t tilt(FirePoolHost& host, Vec3& vel);
    void draw(FirePoolHost& host, const FireParticle& p, int sprite);
    void billboard(FirePoolHost& host, const Vec3& pos, std::int32_t scale, std::int32_t alpha, std::int32_t palette,
                   int sprite);
    void rolled(FirePoolHost& host, const FireParticle& p, int sprite);

    FirePoolParams params_;
    std::vector<FireParticle> particles_;
    std::vector<AttachedFlame> attached_;
    bool paused_ = false;
};

// Reads every pool of gamedata/effects/fire_pools.json (or the last mod's copy), by name.
Result<std::map<std::string, FirePoolParams>> parse_fire_pools(std::string_view json_text);
Result<std::map<std::string, FirePoolParams>> load_fire_pools(const std::filesystem::path& gamedata_dir,
                                                             const std::filesystem::path& mods_dir,
                                                             std::string* source);

}  // namespace sh::effects
