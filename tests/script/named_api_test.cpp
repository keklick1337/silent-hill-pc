// The pure parts of the named script API (src/engine/script/named_api.h): references, scratch handles, the
// game's ranged random numbers, the fade start state and the Q6 planar length.

#include "framework/sh_test.h"

#include "engine/script/named_api.h"

#include <string_view>

namespace {

using namespace sh::script::named;

SH_TEST(named_api_parse_ref) {
    std::string_view name;
    long long index = -1;
    SH_CHECK(parse_ref("cutscene_timer", &name, &index));
    SH_CHECK(name == "cutscene_timer" && index == 0);
    SH_CHECK(parse_ref("ufo_light_pos[14]", &name, &index));
    SH_CHECK(name == "ufo_light_pos" && index == 14);
    SH_CHECK(!parse_ref("", &name, &index));
    SH_CHECK(!parse_ref("[2]", &name, &index));
    SH_CHECK(!parse_ref("a[]", &name, &index));
    SH_CHECK(!parse_ref("a[1", &name, &index));
    SH_CHECK(!parse_ref("a[1]x", &name, &index));
    SH_CHECK(!parse_ref("a[x]", &name, &index));
    SH_CHECK(!parse_ref(std::string_view("0123456789012345678901234567890123456789012345678901234567890123456789"
                                         "01234567890123456789012345"),
                        &name, &index));
}

SH_TEST(named_api_handle_index) {
    SH_CHECK(handle_index("vec:3", "vec:") == 3);
    SH_CHECK(handle_index("npc_bone:120", "npc_bone:") == 120);
    SH_CHECK(handle_index("vec:", "vec:") == -1);
    SH_CHECK(handle_index("vec:1a", "vec:") == -1);
    SH_CHECK(handle_index("svec:1", "vec:") == -1);
}

SH_TEST(named_api_random_ranges) {
    // Signed and unsigned remainders agree on the game's 15-bit draws.
    for (int draw = 0; draw < 0x8000; draw += 7) {
        const int a = random_in_range(draw, -10, 10);
        const int b = random_in_range_unsigned(draw, -10, 10);
        SH_CHECK(a == b && a >= -10 && a <= 10);
    }
    SH_CHECK(random_in_range(100, 5, 14) == 5);
    // A negative draw keeps its sign in rng.int and wraps in rng.uint.
    SH_CHECK(random_in_range(-3, 0, 9) == -3);
    SH_CHECK(random_in_range_unsigned(-3, 0, 9) == static_cast<int>(0xFFFFFFFDu % 10u));
}

SH_TEST(named_api_fade_state) {
    SH_CHECK(screen_fade_start_state(true, false, false) == 2);
    SH_CHECK(screen_fade_start_state(false, false, false) == 3);
    SH_CHECK(screen_fade_start_state(true, true, false) == 6);
    SH_CHECK(screen_fade_start_state(false, true, true) == 15);
}

SH_TEST(named_api_planar_length) {
    SH_CHECK(planar_length_q6(0, 0) == 0);
    SH_CHECK(planar_length_q6(3 * 4096, 4 * 4096) == 5 * 4096);
    SH_CHECK(planar_length_q6(-3 * 4096, 4 * 4096) == 5 * 4096);
    // Positive components under 64 vanish in Q6; a negative one floors to -1.
    SH_CHECK(planar_length_q6(63, 63) == 0);
    SH_CHECK(planar_length_q6(63, -63) == 64);
}

}  // namespace
