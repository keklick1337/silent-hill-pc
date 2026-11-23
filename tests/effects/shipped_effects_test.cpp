// The shipped effect and screen files that no other test reads parse with their strict loaders, so a key the
// loader refuses (a typo, a note it does not skip such as "description") fails here and not at run time.

#include "engine/effects/drain_splash.h"
#include "engine/effects/fire_pool.h"
#include "engine/effects/glass.h"
#include "engine/effects/glow_field.h"
#include "engine/effects/ground_fx.h"
#include "engine/effects/heat_field.h"
#include "engine/effects/lens_flare.h"
#include "engine/effects/water_stream.h"
#include "engine/screens/warning_layout.h"

#include "framework/sh_test.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string shipped(const char* relative) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/" + relative, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

template <typename R>
bool parsed(const char* relative, const R& r) {
    if (!r) {
        std::fprintf(stderr, "%s: %s\n", relative, r.error().c_str());
    }
    return static_cast<bool>(r);
}

}  // namespace

SH_TEST(shipped_effect_files_parse) {
    namespace fx = sh::effects;
    SH_CHECK(parsed("effects/glass.json", fx::parse_glass_params(shipped("effects/glass.json"))));
    SH_CHECK(parsed("effects/drain_splash.json", fx::parse_drain_splash_params(shipped("effects/drain_splash.json"))));
    SH_CHECK(parsed("effects/water_stream.json", fx::parse_water_stream_params(shipped("effects/water_stream.json"))));
    SH_CHECK(parsed("effects/glow_fields.json", fx::parse_glow_fields(shipped("effects/glow_fields.json"))));
    SH_CHECK(parsed("effects/heat_fields.json", fx::parse_heat_fields(shipped("effects/heat_fields.json"))));
    SH_CHECK(parsed("effects/ground_fx.json", fx::parse_ground_fx(shipped("effects/ground_fx.json"))));
    SH_CHECK(parsed("effects/fire_pools.json", fx::parse_fire_pools(shipped("effects/fire_pools.json"))));
    SH_CHECK(parsed("effects/lens_flares.json", fx::parse_lens_flares(shipped("effects/lens_flares.json"))));
}

SH_TEST(shipped_warning_screen_parses) {
    SH_CHECK(parsed("menu/warning_screen.json",
                    sh::screens::parse_warning_layout(shipped("menu/warning_screen.json"))));
}
