// Snow and rain (src/engine/effects/weather.h): every shipped particles.json parses, the
// weather starts, snow falls and settles, the rain draws streaks and splashes, a map's
// rooms switch it off, and the same random numbers give the same picture.

#include "engine/effects/weather.h"

#include "framework/sh_test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fx = sh::effects;

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A host with a counting generator, a flat ground and a projection that shows x and z.
class FakeHost final : public fx::WeatherHost {
public:
    std::uint32_t seed = 12345;
    std::int32_t dt = 136;
    int room_no = 3;
    fx::Vec3 where;
    int density = 3;
    bool ready = false;
    int stops = 0;
    int starts = 0;
    int loops = 0;
    std::vector<fx::EffectPrim> prims;
    int rand_calls = 0;

    std::int32_t rand16() override {
        rand_calls++;
        seed = seed * 1103515245u + 12345u;
        return static_cast<std::int32_t>((seed >> 16) & 0x7FFF);
    }
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t square_root(std::int32_t v) override {
        std::int32_t r = 0;
        while ((r + 1) * (r + 1) <= v) {
            r++;
        }
        return r;
    }
    std::int32_t delta_time() override { return dt; }
    int hz = 30;
    bool tick = true;
    int sim_hz() override { return hz; }
    bool tick30() override { return tick; }
    int room() override { return room_no; }
    fx::Vec3 player() override { return where; }
    int event_param() override { return 0; }
    int event_step() override { return 0; }
    int light_mode() override { return 0; }
    bool flashlight_on() override { return false; }
    bool match_anim() override { return false; }
    int header_density() override { return 3; }
    int density_mode() override { return density; }
    void set_density_mode(int v) override { density = v; }
    bool transition_ready() override { return ready; }
    void set_transition_ready(bool v) override { ready = v; }
    void begin_view(std::int32_t, std::int32_t) override {}
    fx::ScreenPoint project(const fx::Vec3s& r) override {
        return fx::ScreenPoint{r.x, r.y, 100 + r.z};  // depth grows with z
    }
    fx::GroundSample ground(std::int32_t, std::int32_t) override { return fx::GroundSample{0, 0}; }
    std::int32_t fog(std::int32_t) override { return 0; }
    void emit(const fx::EffectPrim& p) override { prims.push_back(p); }
    void rain_loop(int) override { loops++; }
    void rain_start() override { starts++; }
    void rain_stop() override { stops++; }
};

fx::WeatherParams parse(const std::string& text) {
    sh::Result<fx::WeatherParams> r = fx::parse_weather_params(text);
    SH_CHECK(r);
    return r ? r.value() : fx::WeatherParams{};
}

fx::WeatherFrame frame() {
    fx::WeatherFrame f;
    f.snow_color = fx::Rgb{200, 200, 200};
    f.rain_color = fx::Rgb{120, 120, 130};
    return f;
}

// Weather asked for with the words the setter writes: snow (kind 1), or rain (kind 2).
fx::WeatherWords asked(int kind_bits) {
    fx::WeatherWords w;
    w.kind_bits = static_cast<std::uint16_t>(kind_bits);
    w.kind = kind_bits >> 2;
    w.requested = static_cast<std::uint32_t>(kind_bits) << 12;  // the start-up reads the kind from it
    return w;
}

}  // namespace

SH_TEST(weather_shipped_files) {
    namespace fs = std::filesystem;
    int n = 0;
    for (const auto& e : fs::directory_iterator(std::string(SH_PROJECT_ROOT) + "/gamedata/maps")) {
        const fs::path f = e.path() / "particles.json";
        if (!fs::is_regular_file(f)) {
            continue;
        }
        const sh::Result<fx::WeatherParams> r = fx::parse_weather_params(read(f));
        if (!r) {
            SH_CHECK(!"a shipped particles.json must parse");
            continue;
        }
        SH_CHECK(r.value().any());
        n++;
    }
    SH_CHECK(n == 22);

    const fx::WeatherParams m7 = parse(read(std::string(SH_PROJECT_ROOT) + "/gamedata/maps/map7_s03/particles.json"));
    SH_CHECK(m7.particle_max == 450 && m7.snow.range == 8 && m7.snow.drifting &&
             m7.snow.landing == fx::SnowParams::Landing::Flat);
    const fx::WeatherParams m4 = parse(read(std::string(SH_PROJECT_ROOT) + "/gamedata/maps/map4_s02/particles.json"));
    SH_CHECK(m4.rain_on && !m4.snow_on && m4.rain.pool_splash && m4.rain.boundary && m4.update_rules.size() == 1);
    SH_CHECK(!fx::parse_weather_params("{\"snowy\": {}}"));
    SH_CHECK(!fx::parse_weather_params("{\"snow\": {\"landing\": \"sideways\"}}"));
    SH_CHECK(!fx::parse_weather_params("{\"init\": [{\"corners\": [[1]]}]}"));
}

SH_TEST(weather_q12_from_metres_truncates_in_float) {
    SH_CHECK(fx::q12_from_metres(128.3) == 525516);
    SH_CHECK(fx::q12_from_metres(-0.30) == -1228);
    SH_CHECK(fx::q12_from_metres(-6) == -24576);
}

SH_TEST(weather_snow_starts_falls_and_draws) {
    fx::WeatherSystem w(parse("{\"snow\": {}}"));
    FakeHost host;
    fx::WeatherWords words = asked(0x4);  // snow, light
    w.update(host, words, frame(), -1);
    SH_CHECK(host.ready);
    // The start-up spawned snow: the light variant fills half the particle table.
    SH_CHECK(w.live_count(0) == 150 && w.live_count(1) == 0);
    SH_CHECK(host.rand_calls > 0);

    host.prims.clear();
    for (int i = 0; i < 30; i++) {
        host.where.x += 0;
        w.update(host, words, frame(), 1);
    }
    SH_CHECK(!host.prims.empty());
    for (const fx::EffectPrim& p : host.prims) {
        SH_CHECK(p.kind == fx::EffectPrim::Kind::Sprite && p.texture.page == 44 && p.semi_transparent);
        SH_CHECK(p.slot > 32 && p.slot < 2047);
        SH_CHECK(p.h == 1 && p.w >= 1 && p.w <= 4);
    }
    // Each flake is the console's stepped triangle: rows size, size - 1, ... 1, one texel
    // row each, all on one slot.
    for (std::size_t i = 0; i < host.prims.size();) {
        const fx::EffectPrim& first = host.prims[i];
        const int size = first.w;
        SH_CHECK(size == 1 || size == 3 || size == 4);
        for (int row = 0; row < size; row++) {
            SH_CHECK(i + static_cast<std::size_t>(row) < host.prims.size());
            if (i + static_cast<std::size_t>(row) >= host.prims.size()) {
                return;
            }
            const fx::EffectPrim& r = host.prims[i + static_cast<std::size_t>(row)];
            SH_CHECK(r.w == size - row && r.x[0] == first.x[0] && r.y[0] == first.y[0] + row);
            SH_CHECK(r.u[0] == first.u[0] && r.v[0] == first.v[0] + row && r.slot == first.slot);
        }
        i += static_cast<std::size_t>(size);
    }
}

SH_TEST(weather_rain_draws_streaks_and_splashes) {
    fx::WeatherSystem w(parse("{\"rain\": {}}"));
    FakeHost host;
    fx::WeatherWords words = asked(0x8);  // rain
    w.update(host, words, frame(), -1);
    SH_CHECK(w.live_count(1) > 0);
    host.prims.clear();
    for (int i = 0; i < 80; i++) {
        w.update(host, words, frame(), 1);
    }
    bool streak = false;
    bool splash = false;
    for (const fx::EffectPrim& p : host.prims) {
        SH_CHECK(p.kind == fx::EffectPrim::Kind::Quad && p.texture.page == 44);
        streak = streak || (p.u[0] == 10 && p.u[2] == 13);
        splash = splash || (p.u[0] == 16 && p.u[2] == 31);
    }
    SH_CHECK(streak && splash);
}

SH_TEST(weather_rooms_skip_and_rules_set_the_mode) {
    fx::WeatherSystem w(parse(
        "{\"snow\": {\"aperture\": {\"fallback\": {\"lines\": [[0, 1, 0]]}}}, \"rest_gate\": true,"
        " \"update\": [{\"rooms\": [3, 5]}, {\"skip\": true}]}"));
    FakeHost host;
    fx::WeatherWords words = asked(0x4);
    w.update(host, words, frame(), -1);
    host.prims.clear();
    host.room_no = 9;  // not an outdoor room: nothing is simulated or drawn
    for (int i = 0; i < 10; i++) {
        w.update(host, words, frame(), 1);
    }
    SH_CHECK(host.prims.empty());
    host.room_no = 3;
    for (int i = 0; i < 40; i++) {
        w.update(host, words, frame(), 1);
    }
    SH_CHECK(!host.prims.empty());
}

SH_TEST(weather_same_numbers_same_picture) {
    const auto run = [](std::vector<fx::EffectPrim>& out, int* calls) {
        fx::WeatherSystem w(parse("{\"snow\": {}, \"rain\": {}}"));
        FakeHost host;
        fx::WeatherWords words = asked(0x4);
            w.update(host, words, frame(), -1);
        for (int i = 0; i < 20; i++) {
            w.update(host, words, frame(), 1);
        }
        out = host.prims;
        *calls = host.rand_calls;
    };
    std::vector<fx::EffectPrim> a;
    std::vector<fx::EffectPrim> b;
    int ca = 0;
    int cb = 0;
    run(a, &ca);
    run(b, &cb);
    SH_CHECK(!a.empty() && a.size() == b.size() && ca == cb);
    for (std::size_t i = 0; i < a.size() && i < b.size(); i++) {
        SH_CHECK(a[i].x[0] == b[i].x[0] && a[i].y[0] == b[i].y[0] && a[i].slot == b[i].slot &&
                 a[i].rgb[0].r == b[i].rgb[0].r);
    }
}

// The console stepped the weather once per 30 fps frame. At 60 updates a second the weather still steps once per
// 30 Hz tick, by the time of the whole tick: the update that ends a tick shows exactly the picture of 30 updates a
// second and has drawn as many random numbers. The update between two ticks draws no random number and shows the
// same particles moved on by their own speed, so nothing stands still on the screen.
SH_TEST(weather_steps_once_a_tick_at_any_update_rate) {
    struct Seen {
        std::vector<std::vector<fx::EffectPrim>> ticks;  // the picture of each update that ends a tick
        int rand_calls = 0;
        int moved_between = 0;  // updates between two ticks whose picture differs from the tick's before
    };
    auto same = [](const std::vector<fx::EffectPrim>& a, const std::vector<fx::EffectPrim>& b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); i++) {
            for (int v = 0; v < 4; v++) {
                if (a[i].x[v] != b[i].x[v] || a[i].y[v] != b[i].y[v]) {
                    return false;
                }
            }
            if (a[i].slot != b[i].slot || a[i].rgb[0].r != b[i].rgb[0].r) {
                return false;
            }
        }
        return true;
    };
    auto run = [&same](const char* json, int kind, int updates_per_tick) {
        fx::WeatherSystem w(parse(json));
        FakeHost host;
        host.hz = 60;
        host.dt = 136 / updates_per_tick;
        fx::WeatherWords words = asked(kind);
        w.update(host, words, frame(), -1);
        Seen seen;
        const int base = host.rand_calls;
        for (int i = 0; i < 150 * updates_per_tick; i++) {
            host.tick = i % updates_per_tick == updates_per_tick - 1;
            const int before = host.rand_calls;
            host.prims.clear();
            w.update(host, words, frame(), 1);
            if (host.tick) {
                seen.ticks.push_back(host.prims);
            } else {
                SH_CHECK(host.rand_calls == before);
                if (!seen.ticks.empty() && !same(host.prims, seen.ticks.back())) {
                    seen.moved_between++;
                }
            }
        }
        seen.rand_calls = host.rand_calls - base;
        return seen;
    };
    for (const auto& [json, kind] : {std::pair{"{\"snow\": {}}", 0x4}, std::pair{"{\"rain\": {}}", 0x8}}) {
        const Seen at30 = run(json, kind, 1);
        const Seen at60 = run(json, kind, 2);
        SH_CHECK(at30.rand_calls > 1000 && at60.rand_calls == at30.rand_calls);
        SH_CHECK(at30.ticks.size() == 150 && at60.ticks.size() == 150);
        std::size_t equal = 0;
        for (std::size_t i = 0; i < at30.ticks.size() && i < at60.ticks.size(); i++) {
            SH_CHECK(!at30.ticks[i].empty());
            equal += same(at30.ticks[i], at60.ticks[i]) ? 1 : 0;
        }
        SH_CHECK(equal == 150);
        SH_CHECK(at60.moved_between == 149);
    }
}

SH_TEST(weather_transition_ramps_the_counts_and_ends) {
    fx::WeatherSystem w(parse("{\"snow\": {}}"));
    FakeHost host;
    fx::WeatherWords words;  // clear sky
    w.update(host, words, frame(), -1);
    SH_CHECK(w.live_count(0) == 0);
    // Light snow asked for: the target word differs from the current one until the ramp ends.
    words.target = 0x4 | (0x4 << 6);
    words.kind_bits = 0x4;
    host.ready = false;
    int updates = 0;
    while (words.current != words.target && updates < 1000) {
        w.update(host, words, frame(), 1);
        updates++;
    }
    SH_CHECK(words.current == words.target && updates >= 0x200 && host.ready);
    SH_CHECK(words.snow_flag == 1 && words.rain_flag == 0);
}

SH_TEST(weather_rain_sound_follows_the_count) {
    fx::WeatherSystem w(parse("{\"rain\": {\"sound\": true}}"));
    FakeHost host;
    fx::WeatherWords words = asked(0x8);
    w.update(host, words, frame(), -1);
    for (int i = 0; i < 40; i++) {
        w.update(host, words, frame(), 1);
    }
    SH_CHECK(host.starts == 1 && host.loops > 0);
}
