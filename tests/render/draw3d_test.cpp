// The engine's world-space effect list (render/draw3d): the command ring's ids.

#include "framework/sh_test.h"
#include "render/draw3d/draw3d.h"

#include <variant>

namespace {

namespace d3 = sh::draw3d;

d3::Polygon tagged(std::int16_t x) {
    d3::Polygon p;
    p.v[0].xy = {x, 0};
    return p;
}

}  // namespace

SH_TEST(draw3d_ring_finds_recent_commands) {
    d3::CommandRing ring(4);
    const d3::CommandId a = ring.record(tagged(1));
    const d3::CommandId b = ring.record(tagged(2));
    SH_CHECK(a != 0 && b != 0 && a != b);
    SH_CHECK(ring.find(a) != nullptr && std::get<d3::Polygon>(*ring.find(a)).v[0].xy.x == 1);
    SH_CHECK(ring.find(b) != nullptr && std::get<d3::Polygon>(*ring.find(b)).v[0].xy.x == 2);
    SH_CHECK(ring.find(0) == nullptr);
    SH_CHECK(ring.find(b + 1) == nullptr);
    SH_CHECK(ring.find(d3::kIdMask + 1) == nullptr);
    SH_CHECK(ring.recorded() == 2);
}

SH_TEST(draw3d_ring_overwrites_oldest) {
    d3::CommandRing ring(2);
    const d3::CommandId first = ring.record(tagged(0));
    for (int i = 1; i < 4; i++) ring.record(tagged(static_cast<std::int16_t>(i)));
    SH_CHECK(ring.find(first) != nullptr);
    const d3::CommandId fifth = ring.record(tagged(4));
    SH_CHECK(ring.find(first) == nullptr);
    SH_CHECK(ring.find(fifth) != nullptr && std::get<d3::Polygon>(*ring.find(fifth)).v[0].xy.x == 4);
}

SH_TEST(draw3d_ring_ids_skip_zero_on_wrap) {
    d3::CommandRing ring(1);
    const d3::Polygon p = tagged(0);
    d3::CommandId last = 0;
    bool zero = false;
    for (std::uint32_t i = 0; i < d3::kIdMask + 1; i++) {
        last = ring.record(p);
        zero = zero || last == 0;
    }
    // Ids 1..kIdMask, then 1 again.
    SH_CHECK(!zero);
    SH_CHECK(last == 1);
}
