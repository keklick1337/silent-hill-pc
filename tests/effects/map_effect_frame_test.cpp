// The map effect frame (src/engine/effects/map_effect_frame.h): the slot allocator's rotating search and the
// kind -> drawer dispatch table.

#include "engine/effects/map_effect_frame.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace fx = sh::effects;

class Table final : public fx::SlotTable {
public:
    std::vector<std::uint8_t> kinds = std::vector<std::uint8_t>(4, 0);
    std::int32_t at = 0;
    int count() override { return static_cast<int>(kinds.size()); }
    std::uint8_t kind(int s) override { return kinds[static_cast<std::size_t>(s)]; }
    void set_kind(int s, std::uint8_t k) override { kinds[static_cast<std::size_t>(s)] = k; }
    std::int32_t cursor() override { return at; }
    void set_cursor(std::int32_t c) override { at = c; }
};

}  // namespace

SH_TEST(map_effect_frame_alloc_rotates_and_fills) {
    Table t;
    t.at = 2;
    SH_CHECK(fx::MapEffectFrame::alloc(t, 28) == 2 && t.at == 3 && t.kinds[2] == 28);
    SH_CHECK(fx::MapEffectFrame::alloc(t, 5) == 3 && t.at == 4);
    // Past the end the search wraps to the start.
    SH_CHECK(fx::MapEffectFrame::alloc(t, 6) == 0 && t.at == 1);
    SH_CHECK(fx::MapEffectFrame::alloc(t, 7) == 1);
    // Full: nothing taken, the cursor stays.
    SH_CHECK(fx::MapEffectFrame::alloc(t, 9) == -1 && t.at == 2);
    // A cursor past a shrunken table starts over at 0.
    t.kinds = {0, 0};
    t.at = 7;
    SH_CHECK(fx::MapEffectFrame::alloc(t, 1) == 0);
}

SH_TEST(map_effect_frame_dispatch_table) {
    using D = fx::SlotDrawer;
    SH_CHECK(fx::slot_drawer(0) == D::None && fx::slot_drawer(6) == D::None && fx::slot_drawer(12) == D::None);
    SH_CHECK(fx::slot_drawer(1) == D::BloodSpray && fx::slot_drawer(4) == D::BloodPool && fx::slot_drawer(19) == D::MuzzleFlash);
    SH_CHECK(fx::slot_drawer(22) == D::GunSmoke && fx::slot_drawer(31) == D::HitSpark);
    SH_CHECK(fx::slot_drawer(8) == D::Flame && fx::slot_drawer(10) == D::Flame && fx::slot_drawer(11) == D::FireSmoke);
    SH_CHECK(fx::slot_drawer(5) == D::SplitHeadDrag && fx::slot_drawer(7) == D::SplitHeadDrool);
    SH_CHECK(fx::slot_drawer(13) == D::SmokeWait && fx::slot_drawer(14) == D::SmokePuff);
    SH_CHECK(fx::slot_drawer(26) == D::Glass && fx::slot_drawer(27) == D::WaterDrop && fx::slot_drawer(28) == D::Drip);
    SH_CHECK(fx::slot_drawer(32) == D::Script && fx::slot_drawer(34) == D::AcidDrop && fx::slot_drawer(35) == D::LightGlow);
    SH_CHECK(fx::slot_drawer(36) == D::None && fx::slot_drawer(30) == D::None);
}
