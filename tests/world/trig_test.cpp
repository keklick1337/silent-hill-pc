// The engine's fixed-point trigonometry (engine/world/fixed_trig.h) against its specification: sine and cosine
// against the runtime formula the game evaluates, atan2 and the vector length against values worked out by hand
// from the original integer algorithms (the tests do not link the game layer).

#include "engine/world/fixed_trig.h"

#include "framework/sh_test.h"

#include <cmath>

namespace {

namespace w = sh::world;

constexpr double kPi = 3.14159265358979323846;

int32_t spec_sin(int32_t angle) {
    return static_cast<int32_t>(std::sin((static_cast<double>(angle) / 4096.0) * 2.0 * kPi) * 4096.0);
}

int32_t spec_cos(int32_t angle) {
    return static_cast<int32_t>(std::cos((static_cast<double>(angle) / 4096.0) * 2.0 * kPi) * 4096.0);
}

}  // namespace

SH_TEST(world_trig_sin_cos) {
    // The whole turn, against the formula.
    for (int32_t a = 0; a < 4096; ++a) {
        SH_CHECK(w::fixed_sin(a) == spec_sin(a));
        SH_CHECK(w::fixed_cos(a) == spec_cos(a));
    }
    // Angles outside [0, 4096) wrap in the formula; a few samples.
    for (int32_t a : {-4096, -3072, -1024, -1, 4096, 4097, 8192, 12345}) {
        SH_CHECK(w::fixed_sin(a) == spec_sin(a));
        SH_CHECK(w::fixed_cos(a) == spec_cos(a));
    }
    // Cardinal points written out by hand.
    SH_CHECK(w::fixed_sin(0) == 0);
    SH_CHECK(w::fixed_sin(1024) == 4096);
    SH_CHECK(w::fixed_sin(2048) == 0);
    SH_CHECK(w::fixed_sin(3072) == -4096);
    SH_CHECK(w::fixed_cos(0) == 4096);
    SH_CHECK(w::fixed_cos(1024) == 0);
    SH_CHECK(w::fixed_cos(2048) == -4096);
    SH_CHECK(w::fixed_cos(3072) == 0);
}

SH_TEST(world_trig_atan2) {
    struct Case {
        int32_t y;
        int32_t x;
        int32_t want;
    };
    // Expected values from the original integer algorithm: quadrant signs, the table's
    // quantisation and the large-input scaling path (components beyond 2^21).
    const Case cases[] = {
        {0, 0, 0},        {0, 5, 0},        {5, 0, 1024},      {0, -5, 2048},
        {-5, 0, -1024},   {1, 1, 512},      {-1, 1, -512},     {1, -1, 1536},
        {-1, -1, -1536},  {3, 4, 420},      {4, 3, 604},       {-3, 4, -420},
        {3, -4, 1628},    {-3, -4, -1628},  {100, -7, 1069},   {-100, 7, -979},
        {4096, 4096, 512}, {-4096, -4096, -1536}, {123456, 789, 1020},
        {1 << 22, 1, 1024}, {1, 1 << 22, 0}, {-(1 << 22), 1, -1024},
    };
    for (const Case& c : cases) {
        SH_CHECK(w::fixed_atan2(c.y, c.x) == c.want);
    }
    // Sign symmetry of the algorithm.
    SH_CHECK(w::fixed_atan2(-25, 64) == -w::fixed_atan2(25, 64));
    SH_CHECK(w::fixed_atan2(25, -64) == 2048 - w::fixed_atan2(25, 64));
}

SH_TEST(world_trig_length3) {
    // Zero, exact right triangles, and each down-shift tier of the overflow guard.
    SH_CHECK(w::fixed_length3(0, 0, 0) == 0);
    SH_CHECK(w::fixed_length3(3 * 4096, 4 * 4096, 0) == 5 * 4096);
    SH_CHECK(w::fixed_length3(-(3 * 4096), 4 * 4096, 0) == 5 * 4096);
    SH_CHECK(w::fixed_length3(5, 7, 9) == 12);
    SH_CHECK(w::fixed_length3(1 << 15, 0, 0) == 1 << 15);           // shift 4
    SH_CHECK(w::fixed_length3(1 << 19, 1 << 19, 0) == 741376);      // shift 8, table root
    SH_CHECK(w::fixed_length3(1 << 23, 1, 1) == 1 << 23);           // shift 12
    // Powers of two come out exact on every tier.
    for (int32_t bit : {12, 14, 18, 22}) {
        const int32_t v = 1 << bit;
        SH_CHECK(w::fixed_length3(v, 0, 0) == v);
        SH_CHECK(w::fixed_length3(0, -v, 0) == v);
    }
}
