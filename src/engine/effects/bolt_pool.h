// SPDX-License-Identifier: GPL-3.0-or-later
// Bolt pools: a fixed pool of lightning bolts. A bolt is a chain of
// short straight segments laid once when it is thrown: a walker steps from the start toward an
// aim that its path moves (straight at the target, a hop over a waypoint and down to the ground, a
// lob over the target, a fixed number of steps), each segment turned toward the aim by at most
// the bolt's turn. The bolt then grows from its start to its end and withers from the start over
// its life; each frame its visible segments are drawn as twisted ribbons (a textured quad between
// two projected crosses) whose brightness fades. A bolt that passes through Harry's chest hurts
// him once (repeated hits in a short time hurt less) and ends there; a bolt may throw a burst of
// fire at its head when fully grown, or throw another bolt from its head back to a bone of its
// owner when it ends. The map7_s03 Incubus fights and the endings throw their lightning this way.
// The module owns the bolts; the game gives the random numbers, the trigonometry, the frame time,
// the projection, Harry, the owner's bones, the sounds and the fire, and turns the quads into
// primitives (BoltPoolHost). Every number is data: gamedata/effects/bolt_pools.json.

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

// How the walker of a bolt moves its aim while the segments are laid.
enum class BoltPath : std::uint8_t {
    Wander,    // picks one of the other paths at random
    Waypoint,  // to a random waypoint, then straight down until below the ground line
    Drift,     // a few steps toward a random direction
    Lob,       // up over the middle of the way, then down near the target, then to the ground there
    Roam,      // random directions until a roll sends it home (the last waypoint)
    Seek,      // straight at the target until within half a step
    Sweep,     // a fixed number of steps toward the target
};

// A thrown bolt.
struct BoltSpawner {
    BoltPath path = BoltPath::Seek;
    std::int32_t step = 0;  // segment length (Q12); 0: given by the caller
    std::int32_t life = 0;  // Q12 seconds; 0: given by the caller
    std::int32_t turn = 0;  // largest turn between two segments (Q12 angle)
    // The start is moved this far (Q12) from the point it is thrown at in a random direction,
    // and the bolt aims back at that point.
    std::int32_t scatter = 0;
    // The target is Harry's chest moved by the given offset.
    bool aim_at_player = false;
    std::vector<int> sounds;  // sound slots at the start
    std::map<std::string, std::vector<int>> owner_sounds;  // instead of `sounds`, by owner
    // A step above strong_step plays strong_sounds instead of `sounds` (none when -1).
    std::int32_t strong_step = -1;
    std::vector<int> strong_sounds;
    bool head_burst = false;  // a fire burst at the head once the bolt is fully grown
    int end_bolt = -1;        // spawner thrown from the head to the owner's bone when the bolt ends
};

// Who throws the bolts (attach()): whether they hurt Harry, and the fire they throw.
struct BoltOwner {
    std::string name;
    bool hits = false;
    std::int32_t damage = 0;      // Q12
    std::int32_t hit_palette = 0; // fire flash at the hit point
    std::vector<int> hit_sounds;  // sound slots at Harry
    std::int32_t burst_palette = 0;
};

// A sound with a minimum gap: it plays when the time since it last played exceeds the gap, and the
// next gap is interval + a roll of 0..interval/8 (Q12).
struct BoltSound {
    std::int32_t id = 0;
    std::int32_t volume = 0;
    std::int32_t interval = 0;
    std::int32_t first_gap = 0;  // the gap before the first play after the map is loaded
};

struct BoltPoolParams {
    int capacity = 30;
    int segments = 29;
    std::int32_t grow_rate = 30;        // segments per half life
    std::int32_t width = 3686;          // ribbon half width as a factor of the step (Q12)
    std::int32_t twist_min = -1024;     // twist added per segment: a roll of min..max
    std::int32_t twist_max = 1023;
    std::uint32_t color = 0xC0C0C0;     // colour word of a new segment...
    std::uint32_t fade = 0x080808;      // ...less this every frame it is drawn
    std::int32_t tpage = 0;
    std::int32_t clut = 0;
    // The palette while a character is in an NPC slot (clut_npc -1: never).
    int clut_npc = -1;
    std::string clut_chara;
    std::int32_t clut_alt = 0;
    std::array<std::int32_t, 2> u{0, 64};
    std::array<std::int32_t, 2> v{127, 0};  // by segment parity
    std::int32_t v_height = 127;
    std::int32_t elapsed_after_reset = 4096;

    struct Hit {
        std::int32_t radius_sq = 0;   // Q8 squared, the sum of the squares >> 4
        std::int32_t attack = 0;
        std::int32_t streak_window = 0;  // hits within this time (Q12 s) of the last halve the damage
    } hit;
    struct Lead {
        std::int32_t gain_easy = 0;  // Q12
        std::int32_t gain = 0;
    } lead;
    struct Walk {
        std::int32_t wander_mask = 0xF0;
        std::int32_t wander_step = 819;
        std::int32_t wander_low = 2048;
        std::int32_t wander_drift_mask = 0x80;
        std::int32_t wander_turn = 170;
        int waypoint_count = 6;
        std::int32_t waypoint_reach = 1228;
        int drift_steps = 4;
        std::int32_t lob_height = -15564;
        std::int32_t lob_spread = 0xFFF;
        int lob_jitter_shift = 2;
        int lob_reach_divisor = 4;
        std::int32_t dive_spread = 0x7FF;
        int dive_jitter_shift = 3;
        std::int32_t dive_height = 17611;
        std::int32_t dive_reach = 6144;  // Q12 factor of the step
        int dive_lead_divisor = 8;
        std::int32_t roam_length = 0x4000;
        std::int32_t roam_mask = 0xF0;
        int home = 6;
        int sweep_steps = 25;
    } walk;
    std::vector<Vec3> waypoints;
    std::vector<BoltSound> sounds;
    std::vector<BoltOwner> owners;
    std::vector<BoltSpawner> spawners;
    std::map<std::string, int> spawner_names;
    std::string fire_pool;  // the fire pool the hits and head bursts throw into
};

// A segment: its rotation (Q12) and its start relative to the pool origin (Q8; y absolute).
struct BoltSegment {
    std::int16_t m[3][3] = {};
    std::int32_t t[3] = {};
};

struct Bolt {
    bool active = false;
    int spawner = 0;
    std::int32_t timer = 0;
    std::int32_t life = 0;
    int state = 0;           // the walker's state while it is laid
    std::int32_t turn = 0;
    int count = 0;           // segments laid
    int filled = 0;          // segments holding data (the walker's last one included)
    int tail = 0;            // first segment drawn
    int head = 0;            // one past the last segment drawn
    std::int32_t step = 0;
    std::int32_t counter = 0;
    bool hit = false;
    int bone = 0;
    bool burst_done = false;
    std::vector<std::uint32_t> color;
    std::vector<std::int32_t> twist;
    std::vector<BoltSegment> seg;
};

// The two corners of a ribbon cross projected through a segment's view: the packed screen points
// of each (y << 16 | x), the mean depth (0 when a corner is too close to the camera) and whether
// the second corner was projected (it is not when the first is too close).
struct BoltPair {
    std::uint32_t xy0 = 0;
    std::uint32_t xy1 = 0;
    std::int32_t depth = 0;
    bool second = false;
};

struct BoltQuad {
    std::array<std::uint32_t, 4> xy{};
    std::array<std::uint8_t, 4> u{};
    std::array<std::uint8_t, 4> v{};
    std::uint32_t color = 0;
    std::uint16_t tpage = 0;
    std::uint16_t clut = 0;
    std::int32_t slot = 0;  // ordering-table entry (not clamped)
};

// The game's handle of the skeleton bolts return to (a bone array).
using BoltAnchor = std::uintptr_t;

class BoltPoolHost {
public:
    virtual ~BoltPoolHost() = default;

    virtual std::int32_t rand16() = 0;  // 0..32767
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t atan2(std::int32_t y, std::int32_t x) = 0;
    virtual std::int32_t sqrt12(std::int32_t v) = 0;
    virtual std::int32_t delta_time() = 0;
    virtual bool easy() = 0;
    // (0, 0, length) turned by the rotation of the angles (x, y, 0) (Q12).
    virtual Vec3 turn_xyz(std::int16_t ax, std::int16_t ay, std::int16_t length) = 0;
    virtual Vec3 player_position() = 0;
    // World translation (Q8) of Harry's chest; of a bone of a skeleton.
    virtual std::array<std::int32_t, 3> player_chest() = 0;
    virtual std::array<std::int32_t, 3> bone(BoltAnchor bones, int bone) = 0;
    virtual bool npc_is(int slot, const std::string& chara) = 0;

    // The view of the frame: the world-screen matrix at (x, 0, z).
    virtual void begin_view(std::int32_t x, std::int32_t z) = 0;
    // Projects two points (Q8) of a segment's model space through the view.
    virtual BoltPair project_pair(const BoltSegment& seg, const std::array<Vec3s, 2>& v) = 0;
    virtual void emit_quad(const BoltQuad& q) = 0;

    virtual void hurt_player(std::int32_t attack, std::int32_t amount) = 0;
    virtual void play_sound(std::int32_t id, const Vec3& at, std::int32_t volume) = 0;
    // Fire of the pool named in the data: a flash where a bolt hit, a burst at a grown bolt's head.
    virtual void fire_flash(const std::string& pool, const Vec3& at, std::int32_t palette) = 0;
    virtual void fire_burst(const std::string& pool, const Vec3& at, std::int32_t palette) = 0;
};

class BoltPool {
public:
    explicit BoltPool(BoltPoolParams params);
    const BoltPoolParams& params() const { return params_; }
    const std::vector<Bolt>& bolts() const { return bolts_; }

    // The sound gaps as the map's data has them (on every map load).
    void restore_sounds();
    // Owner (by name in "owners"), pool origin (world, snapped toward zero to the 64 m grid) and the
    // owner's bones (0 keeps the last ones); every bolt ends.
    bool attach(BoltPoolHost& host, std::string_view owner, std::int32_t x, std::int32_t z, BoltAnchor bones);
    // Harry's movement since the last call, scaled by the lead gain, per second.
    void track(BoltPoolHost& host);
    std::int32_t lead_x() const { return lead_x_; }
    std::int32_t lead_z() const { return lead_z_; }

    // Throws a bolt of a spawner from `from` toward `to` (bone: the owner's bone an end bolt returns
    // to; step, life: used when the spawner's are 0). False when the pool is full.
    bool spawn(BoltPoolHost& host, std::string_view spawner, const Vec3& from, const Vec3& to, int bone = 0,
               std::int32_t step = 0, std::int32_t life = 0);
    // One frame: grows, draws and ends the bolts.
    void update(BoltPoolHost& host);

    // The hit streak (shared by every bolt).
    int streak() const { return streak_; }
    std::int32_t streak_timer() const { return streak_timer_; }
    const std::vector<std::int32_t>& sound_gaps() const { return gap_; }
    const std::vector<std::int32_t>& sound_elapsed() const { return elapsed_; }
    std::int32_t origin_x() const { return origin_x_; }
    std::int32_t origin_z() const { return origin_z_; }
    int owner() const { return owner_; }

private:
    Bolt* acquire();
    bool throw_bolt(BoltPoolHost& host, int spawner, const Vec3& from, const Vec3& to, int bone, std::int32_t step,
                    std::int32_t life);
    void lay(BoltPoolHost& host, Bolt& b, const Vec3& start, const Vec3& target);
    bool walk(BoltPoolHost& host, Bolt& b, Vec3& cur, Vec3& aim, const Vec3& target);
    void orient(BoltPoolHost& host, BoltSegment& out, const Vec3& cur, const Vec3& aim, const BoltSegment* prev,
                std::int32_t turn);
    void grow(Bolt& b);
    void draw(BoltPoolHost& host, Bolt& b);
    void end(BoltPoolHost& host, Bolt& b);
    bool hits_player(BoltPoolHost& host, const BoltSegment& seg, Vec3& at);
    void hurt(BoltPoolHost& host, std::int32_t amount);
    void sound(BoltPoolHost& host, int slot, const Vec3& at);
    Vec3 segment_world(const BoltSegment& seg) const;

    BoltPoolParams params_;
    std::vector<Bolt> bolts_;
    std::int32_t origin_x_ = 0;
    std::int32_t origin_z_ = 0;
    BoltAnchor bones_ = 0;
    Vec3 prev_player_;
    std::int32_t lead_x_ = 0;
    std::int32_t lead_z_ = 0;
    int owner_ = 0;
    int streak_ = 0;
    std::int32_t streak_timer_ = 0;
    std::vector<std::int32_t> gap_;
    std::vector<std::int32_t> elapsed_;
};

// Reads every pool of gamedata/effects/bolt_pools.json (or the last mod's copy), by name.
Result<std::map<std::string, BoltPoolParams>> parse_bolt_pools(std::string_view json_text);
Result<std::map<std::string, BoltPoolParams>> load_bolt_pools(const std::filesystem::path& gamedata_dir,
                                                             const std::filesystem::path& mods_dir,
                                                             std::string* source);

}  // namespace sh::effects
