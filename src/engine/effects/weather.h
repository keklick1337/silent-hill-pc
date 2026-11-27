// SPDX-License-Identifier: GPL-3.0-or-later
// Snow and rain. One class simulates the particles
// that fall around the camera, steps the weather transitions, draws the flakes and
// streaks as effect records and drives the rain sound. What differs between maps (snow
// radius, rain gravity, the doorway apertures that keep weather out of rooms, the rooms
// where it is switched off) comes from gamedata/maps/<map>/particles.json; the game
// supplies the camera, the ground, the projection and the random numbers (WeatherHost).

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

// Q12 from metres the way the original's float constants converted: multiply in float,
// truncate toward zero.
inline std::int32_t q12_from_metres(double metres) {
    return static_cast<std::int32_t>(static_cast<float>(metres) * 4096.0f);
}

// A segment of an aperture: the weather is mirrored back across the line a-b of the
// corner table. `flag` 1 only tests the side (the particle is not moved).
struct ApertureLine {
    int a = 0;
    int b = 1;
    int flag = 0;
};

// What an aperture mode does to a flake before it is drawn.
struct ApertureKind {
    enum class Type : std::uint8_t {
        Chain,  // the lines in order; the next line is tried only while the previous left the flake outside
        Roof,   // a sloped roof: the line is shifted back by the flake's height
    };
    Type type = Type::Chain;
    std::vector<ApertureLine> lines;
    struct Extra {  // a line applied unconditionally in one room, after the chain
        int room = 0;
        ApertureLine line;
    };
    std::vector<Extra> extras;
    // While Harry stands west of x_below the chain is replaced by `override_lines`.
    std::optional<std::int32_t> x_below;
    std::vector<ApertureLine> override_lines;
};

struct Aperture {
    // mode value -> kind; `fallback` serves every other non-zero mode when present.
    std::vector<std::pair<int, ApertureKind>> by_mode;
    std::optional<ApertureKind> fallback;
};

struct CornerSet {
    int x = 0;
    int y = 0;
    int z = 0;
    bool has_y = false;  // a corner given as [x, z] leaves its height as it was
};

// Position tests on Harry (world Q12), all must hold.
struct RuleWhen {
    std::optional<std::int32_t> x_gt, x_lt, x_le, z_gt, z_lt;
};

// A rule of the room tables: when the room and position match, its mode and corners are
// stored; `skip` ends the update without simulating or drawing.
struct RoomRule {
    std::vector<int> rooms;  // empty = every room
    RuleWhen when;
    std::optional<int> mode;
    std::vector<CornerSet> corners;  // assigned from corner 0 on
    bool skip = false;
};

struct SnowParams {
    int range = 5;                           // metres: the flakes live within |x| + |z| <= range
    std::int32_t start_height = -6 * 4096;   // Q12, where flakes spawn
    enum class Landing : std::uint8_t {
        Settle,   // rest on the ground height under them
        Recycle,  // fall through and respawn
        Flat,     // the ground is height 0
    };
    Landing landing = Landing::Settle;
    bool drifting = false;  // a slow drift toward the camera that grows over the first seconds
    std::vector<int> hidden_rooms;  // flakes are not drawn there
    std::vector<int> only_rooms;    // flakes are drawn only there (empty = everywhere)
    int far_slot_mode = -1;         // aperture mode whose flakes go 256 slots further back
    std::optional<Aperture> aperture;
};

struct RainParams {
    bool light = false;       // the thin mist of the hospital: gentle fall, no tail motion
    bool boundary = false;    // drops are clamped by the aperture line 0-1
    bool match_anim = false;  // the colour while Harry lights the match
    bool pool_splash = false; // drops landing on water (ground type 5) splash on the pool page
    bool after_rest = false;  // a resting drop keeps falling as a thin streak
    bool sound = false;       // the rain is heard
    int gravity() const { return light ? 3 : 30; }
    int density() const { return light ? 1 : 10; }
};

struct WeatherParams {
    bool snow_on = false;
    bool rain_on = false;
    SnowParams snow;
    RainParams rain;
    int particle_max = 300;
    bool rest_gate = false;  // a resting particle is left alone while the aperture mode is set
    bool wind_x_off = false; // the x wind is cleared on every update
    bool insects = false;    // the map runs the insect swarms (gamedata/effects/map0_s00_effect.json)
    struct Suspend {
        int event_param = 0;
        int step = 0;
    };
    std::optional<Suspend> suspend;  // no update while this event step runs
    std::vector<RoomRule> init_rules;
    std::vector<RoomRule> update_rules;
    bool any() const { return snow_on || rain_on; }
};

// The state words the game's weather setters (the map events) write between updates; the
// bridge copies them in before each update and back after it.
struct WeatherWords {
    std::uint32_t requested = 0;   // the weather asked for by the setter
    std::uint16_t target = 0;      // two 6-bit weather codes, one per kind
    std::uint16_t current = 0;     // the same for what is falling now
    std::uint16_t kind_bits = 0;   // kind and variant bits of the running weather
    std::int32_t kind = 0;         // 0 clear, 1 snow, 2 rain
    std::int32_t variant = 0;      // 0..3, 2 and 3 are windy
    std::int32_t snow_flag = 0;
    std::int32_t rain_flag = 0;
    std::int32_t rain_ramp = 0;    // how long it has rained; the rain gets denser the longer
    std::int32_t prev_height = 0;  // the previous origin's height, the setter writes it
    bool paused = false;           // out: the update ran with a zero time step
};

// Per update: what the overlay measured.
struct WeatherFrame {
    Vec3 view_position;         // camera, Q12
    std::int32_t view_yaw = 0;  // Q12 angle
    Rgb snow_color;             // the light colours the flakes and streaks take
    Rgb rain_color;
};

struct GroundSample {
    std::int32_t height = 0;  // Q12
    int type = 0;
};

class WeatherHost {
public:
    virtual ~WeatherHost() = default;
    virtual std::int32_t rand16() = 0;  // 0..0x7FFF
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t square_root(std::int32_t v) = 0;
    virtual std::int32_t delta_time() = 0;     // Q12 seconds of this frame
    virtual int sim_hz() = 0;                  // weather_sim_hz
    virtual bool tick30() = 0;                 // this update ends a 30 Hz game tick (every update at 30 fps)
    virtual int room() = 0;
    virtual Vec3 player() = 0;
    virtual int event_param() = 0;
    virtual int event_step() = 0;
    virtual int light_mode() = 0;
    virtual bool flashlight_on() = 0;
    virtual bool match_anim() = 0;
    virtual int header_density() = 0;          // the map header's weather level
    virtual int density_mode() = 0;            // the running density level (1 or other)
    virtual void set_density_mode(int v) = 0;
    virtual bool transition_ready() = 0;
    virtual void set_transition_ready(bool v) = 0;
    // The view of the particles: origin in Q8 (x, z; the height is 0).
    virtual void begin_view(std::int32_t x_q8, std::int32_t z_q8) = 0;
    virtual ScreenPoint project(const Vec3s& relative) = 0;
    virtual GroundSample ground(std::int32_t x, std::int32_t z) = 0;
    virtual std::int32_t fog(std::int32_t depth) = 0;  // the fog ramp byte for a depth word
    virtual void emit(const EffectPrim& prim) = 0;
    // A flake `size` screen pixels wide was drawn at the point last projected: the modern particle shadows read it
    // (particle_shadows); the game's own state never does.
    virtual void note_flake(int size) { (void)size; }
    // Rain sound; `volume` is 0..255.
    virtual void rain_loop(int volume) = 0;
    virtual void rain_start() = 0;
    virtual void rain_stop() = 0;
};

class WeatherSystem {
public:
    explicit WeatherSystem(WeatherParams params);

    const WeatherParams& params() const { return params_; }
    // True when the overlay must skip this update altogether (the map event is running).
    bool suspended(WeatherHost& host) const;
    // arg 0 or -1: (re)initialise the weather; anything else: one update.
    void update(WeatherHost& host, WeatherWords& words, const WeatherFrame& frame, int arg);
    void stop_sound(WeatherHost& host);

    // Test access.
    std::int32_t live_count(int pass) const { return counts_[pass]; }
    std::int32_t wind_x() const { return wind_x_; }
    std::int32_t wind_z() const { return wind_z_; }

private:
    struct Particle {
        Vec3 head;   // snow: position; rain: the streak's lower end
        Vec3 tail;   // rain: the streak's upper end; snow: a fall-speed credit in y
        std::int16_t mx = 0;
        std::int16_t my = 0;
        std::int16_t mz = 0;
        std::uint8_t step = 0;
        std::uint8_t type = 0;
    };

    struct Context;  // per-call references, see weather.cpp

    void init(Context& c);
    void step_transitions(Context& c);
    void simulate_and_draw(Context& c);
    bool apply_rules(Context& c, const std::vector<RoomRule>& rules);
    void sim_tick(Context& c);
    void update_wind(Context& c);
    void spawn(Context& c, int pass, Particle& p, std::uint16_t& rnd);
    void spawn_move(Context& c, int pass, Particle& p, std::uint16_t& rnd, std::int32_t dt);
    void move(Context& c, int pass, Particle& p, std::uint16_t& rnd, std::int32_t dt);
    void rest(Context& c, int pass, Particle& p);
    void lead(int pass, Particle& p) const;
    void rain_after_rest(Context& c, Particle& p, std::int32_t dt);
    void randomize_position(Context& c, std::uint16_t& rnd, Vec3& v, int range);
    void clamp_to_line(const Vec3& a, const Vec3& b, int flag, Vec3& point);
    void apply_aperture(Context& c, const ApertureKind& kind, Vec3& point, const Particle& p);
    void draw_snow(Context& c, Particle& p);
    void draw_rain(Context& c, Particle& p, int shift);
    void rain_sound(Context& c);
    bool camera_moved(Context& c) const;

    WeatherParams params_;
    std::vector<Particle> parts_;
    std::array<std::int32_t, 2> counts_{};
    std::array<Vec3, 8> corners_{};
    int mode_ = 0;
    std::int32_t wind_x_ = 0;
    std::int32_t wind_z_ = 0;
    std::uint8_t spawn_count_ = 0;
    Vec3 origin_;       // the spot in front of the camera the particles are relative to
    Vec3 prev_origin_;
    bool moved_ = false;
    Vec3 view_ = {};       // camera of this update
    Vec3 prev_view_ = {};  // camera of the update before
    std::int32_t transition_tick_ = 0;
    int snow_kind_flag_ = 1;  // 1 = a new flake is plain snow, 0 = the other flake kind
    std::int32_t rain_sound_ramp_ = 0;
    bool rain_sound_on_ = false;
    std::int32_t drift_clock_ = 0;
    std::int32_t drift_step_ = 0;
    // The simulation steps at 30 Hz: on the game's tick, or by its own count with weather_sim_hz 30.
    std::int32_t sim_accum_ = 0;
    bool sim_step_ = true;          // the weather steps on this update
    std::int32_t sim_dt_ = 136;     // the time of that step, Q12 seconds
    std::int32_t tick_time_ = 0;    // game time since the last step
    std::int32_t lead_ = 0;         // how far past the last step this update's picture is
    std::uint16_t step_rnd_ = 0;
};

Result<WeatherParams> parse_weather_params(std::string_view json_text);
// <gamedata>/maps/<map>/particles.json (or the last mod's copy). A map without the file
// has no weather; `found` tells the two apart.
Result<WeatherParams> load_weather_params(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string_view map,
                                          std::string* source, bool* found);

}  // namespace sh::effects
