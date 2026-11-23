// The game's random number generator (engine/loop/game_random): the exact sequence demos and
// replays depend on.

#include "framework/sh_test.h"
#include "engine/loop/game_random.h"

SH_TEST(game_random_sequence_from_zero) {
    sh::loop::GameRandom rng;
    SH_CHECK(rng.seed() == 0u);
    SH_CHECK(rng.next32() == 1013904223u);
    SH_CHECK(rng.next32() == 1196435762u);
    SH_CHECK(rng.seed() == 1196435762u);
    rng.set_seed(0);
    SH_CHECK(rng.next15() == static_cast<std::int32_t>(1013904223u >> 17));
    SH_CHECK(rng.next12() == static_cast<std::uint16_t>(1196435762u >> 20));
}

SH_TEST(game_random_ranges) {
    sh::loop::GameRandom rng;
    rng.set_seed(0xDEADBEEFu);
    for (int i = 0; i < 100000; i++) {
        const std::int32_t a = rng.next15();
        SH_CHECK(a >= 0 && a <= 0x7FFF);
        const std::uint16_t b = rng.next12();
        SH_CHECK(b <= 0xFFF);
        const std::int32_t c = rng.next_int(-3, 7);
        SH_CHECK(c >= -3 && c <= 7);
        const std::int32_t d = rng.next_uint(10, 12);
        SH_CHECK(d >= 10 && d <= 12);
        const std::int32_t e = rng.next_q12_fraction();
        SH_CHECK(e >= 0 && e <= 0xFFF);
    }
}

SH_TEST(game_random_draws_follow_one_state) {
    sh::loop::GameRandom a;
    sh::loop::GameRandom b;
    a.set_seed(12345u);
    b.set_seed(12345u);
    for (int i = 0; i < 1000; i++) {
        const std::uint32_t s = b.next32();
        SH_CHECK(a.next15() == static_cast<std::int32_t>(s >> 17));
    }
    SH_CHECK(&sh::loop::game_random() == &sh::loop::game_random());
}
