/* The weather requests of every map (engine/maps/weather_control.h): environment, light mode and
 * density words, the kept light request and the transition gate. */
#include "engine/maps/weather_control.h"
#include "framework/sh_test.h"

namespace {

struct Host final : sh::maps::WeatherControlHost {
    int light = 0;
    bool ready = true;
    int density = -1;
    int light_mode() override { return static_cast<signed char>(light); }
    void set_light_mode(int mode) override { light = static_cast<signed char>(mode); }
    bool transition_ready() override { return ready; }
    void set_transition_ready(bool r) override { ready = r; }
    void set_density_mode(int level) override { density = level; }
};

constexpr sh::maps::WeatherFeatures kSnowAndRain{true, true};
constexpr sh::maps::WeatherFeatures kSnowOnly{true, false};

}  // namespace

SH_TEST(weather_control_light_snow_environment) {
    Host host;
    sh::maps::WeatherControl c;
    c.environment_set(host, kSnowAndRain, 1, sh::maps::WeatherControl::kKeepLightMode);
    SH_CHECK(c.words().requested == 0x14900u);
    SH_CHECK(c.words().kind_bits == 0x10);
    SH_CHECK(c.words().kind == 1 && c.words().variant == 0);
    SH_CHECK(c.words().snow_flag == 1 && c.words().rain_flag == 0);
    SH_CHECK(c.environment() == 1);
    SH_CHECK(host.light == 0);
}

SH_TEST(weather_control_heavy_rain_light_request_two_is_mode_three) {
    Host host;
    sh::maps::WeatherControl c;
    c.environment_set(host, kSnowAndRain, 6, 2);
    SH_CHECK(host.light == 3);
    SH_CHECK(c.words().requested == 0x1902Du);
    SH_CHECK(c.words().rain_ramp == sh::maps::WeatherControl::kRainRampFull);
    SH_CHECK(c.words().snow_flag == 0 && c.words().rain_flag == 1);

    sh::maps::WeatherControl dry;
    dry.environment_set(host, kSnowOnly, 6, 2);
    SH_CHECK(dry.words().rain_ramp == 0);  // a map without rain has no ramp
}

SH_TEST(weather_control_light_mode_bits) {
    Host host;
    sh::maps::WeatherControl c;
    c.light_mode_set(host, 6);
    SH_CHECK(c.words().requested == 0x1010000u);
    SH_CHECK(c.words().kind_bits == 0x1010);
    c.light_mode_set(host, 4);
    SH_CHECK(c.words().requested == 0x100000u);
    SH_CHECK(c.words().kind_bits == 0x100);
    c.light_mode_set(host, 1);
    SH_CHECK(c.words().requested == 0u && c.words().kind_bits == 0);
}

SH_TEST(weather_control_density_waits_for_the_transition) {
    Host host;
    sh::maps::WeatherControl c;
    host.ready = false;
    SH_CHECK(!c.density_set(host, kSnowAndRain, 6));
    SH_CHECK(c.words().target == 0 && host.density == -1);

    host.ready = true;
    SH_CHECK(c.density_set(host, kSnowAndRain, 6));
    SH_CHECK(host.density == 6);
    SH_CHECK(c.words().kind == 2 && c.words().variant == 1);
    SH_CHECK(c.words().target == 0x9005);
    SH_CHECK(c.words().kind_bits == 0x9);
    SH_CHECK(!host.ready);  // the target differs from what falls now
    SH_CHECK(c.words().prev_height == 0);

    host.ready = true;
    SH_CHECK(c.density_set(host, kSnowAndRain, 1));
    SH_CHECK(c.words().target == 0x4128);
    SH_CHECK(c.words().prev_height == sh::maps::WeatherControl::kLightSnowLift);
}

SH_TEST(weather_control_prepare_update_clears_missing_features) {
    sh::maps::WeatherControl c;
    c.words().rain_ramp = 5;
    c.words().prev_height = 7;
    c.prepare_update(kSnowOnly);
    SH_CHECK(c.words().rain_ramp == 0 && c.words().prev_height == 7);
    c.prepare_update(sh::maps::WeatherFeatures{});
    SH_CHECK(c.words().prev_height == 0);
}
