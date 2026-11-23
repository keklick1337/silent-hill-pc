// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the bolt pools (sh::effects::BoltPool) with a scripted host.

#include "engine/effects/bolt_pool.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "framework/sh_test.h"

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

using namespace sh::effects;

namespace {

class Host final : public BoltPoolHost {
public:
    std::int32_t dt = 4096 / 30;
    std::int32_t depth = 400;
    std::array<std::int32_t, 3> chest{1 << 20, 1 << 20, 1 << 20};
    std::vector<BoltQuad> quads;
    std::vector<std::int32_t> sounds;
    std::vector<std::int32_t> hurts;
    int bursts = 0;
    int flashes = 0;
    std::uint32_t seed = 1;

    std::int32_t rand16() override {
        seed = seed * 1103515245u + 12345u;
        return static_cast<std::int32_t>((seed >> 16) & 0x7FFF);
    }
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t atan2(std::int32_t, std::int32_t) override { return 0; }
    std::int32_t sqrt12(std::int32_t v) override {
        std::int64_t r = 0;
        while ((r + 1) * (r + 1) <= static_cast<std::int64_t>(v) * 4096) {
            r++;
        }
        return static_cast<std::int32_t>(r);
    }
    std::int32_t delta_time() override { return dt; }
    bool easy() override { return false; }
    Vec3 turn_xyz(std::int16_t, std::int16_t, std::int16_t length) override { return Vec3{0, 0, length}; }
    Vec3 player_position() override { return Vec3{}; }
    std::array<std::int32_t, 3> player_chest() override { return chest; }
    std::array<std::int32_t, 3> bone(BoltAnchor, int) override { return {0, 0, 0}; }
    bool npc_is(int, const std::string&) override { return false; }
    void begin_view(std::int32_t, std::int32_t) override {}
    BoltPair project_pair(const BoltSegment&, const std::array<Vec3s, 2>&) override {
        BoltPair p;
        p.depth = depth;
        p.second = true;
        return p;
    }
    void emit_quad(const BoltQuad& q) override { quads.push_back(q); }
    void hurt_player(std::int32_t, std::int32_t amount) override { hurts.push_back(amount); }
    void play_sound(std::int32_t id, const Vec3&, std::int32_t) override { sounds.push_back(id); }
    void fire_flash(const std::string&, const Vec3&, std::int32_t) override { flashes++; }
    void fire_burst(const std::string&, const Vec3&, std::int32_t) override { bursts++; }
};

BoltPoolParams base_pool() {
    std::ifstream in(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "effects" / "bolt_pools.json");
    std::stringstream ss;
    ss << in.rdbuf();
    auto r = parse_bolt_pools(ss.str());
    if (!r) {
        throw std::runtime_error(r.error());
    }
    return r.value().at("BossBolts");
}

}  // namespace

SH_TEST(BoltPoolShippedFileParses) {
    const BoltPoolParams p = base_pool();
    SH_CHECK(p.capacity == 30);
    SH_CHECK(p.segments == 29);
    SH_CHECK(p.waypoints.size() == 7);
    SH_CHECK(p.sounds.size() == 5);
    // Sounds are named in the file; they resolve to the numbers the game plays.
    SH_CHECK(p.sounds[0].id == 1680 && p.sounds[1].id == 1681 && p.sounds[2].id == 1682 && p.sounds[3].id == 1683 &&
             p.sounds[4].id == 1682);
    SH_CHECK(p.owners.size() == 3);
    SH_CHECK(p.spawner_names.count("spark") == 1);
    SH_CHECK(p.spawners[static_cast<std::size_t>(p.spawner_names.at("spark"))].end_bolt == p.spawner_names.at("arc"));
    SH_CHECK(p.color == 0xC0C0C0u);
    SH_CHECK(p.fire_pool == "BossFire");
}

SH_TEST(BoltPoolRejectsBadData) {
    SH_CHECK(!parse_bolt_pools("{\"pools\": {\"X\": {\"capacity\": 1}}}"));
    SH_CHECK(!parse_bolt_pools("{\"pools\": {}, \"other\": 1}"));
}

// A sweep lays sweep_steps + 1 segments and grows to all of them, fading every drawn frame.
SH_TEST(BoltPoolSweepGrowsAndFades) {
    Host host;
    BoltPool pool(base_pool());
    SH_CHECK(pool.attach(host, "Ending", 0, 0, 0));
    SH_CHECK(pool.spawn(host, "arc", Vec3{0, -4096, 0}, Vec3{0, -4096, 40960}));
    const Bolt& b = pool.bolts()[0];
    SH_CHECK(b.active);
    SH_CHECK(b.count == pool.params().walk.sweep_steps);
    for (int f = 0; f < 4; f++) {
        pool.update(host);
    }
    SH_CHECK(!host.quads.empty());
    SH_CHECK(host.quads.back().tpage == 53);
    SH_CHECK(host.quads.front().color == 0xC0C0C0u - 0x080808u);
    SH_CHECK(host.hurts.empty());  // the ending's bolts never hurt
    for (int f = 0; f < 60; f++) {
        pool.update(host);
    }
    SH_CHECK(!pool.bolts()[0].active);
}

// A bolt through Harry's chest hurts him once, flashes the fire there and ends at the hit; a
// second hit right after hurts half as much.
SH_TEST(BoltPoolHitHurtsOnceAndHalvesStreak) {
    Host host;
    host.chest = {0, -256, 0};
    BoltPool pool(base_pool());
    SH_CHECK(pool.attach(host, "Incubus", 0, 0, 0));
    SH_CHECK(pool.spawn(host, "arc", Vec3{0, -4096, 0}, Vec3{0, -4096, 40960}));
    SH_CHECK(pool.spawn(host, "arc", Vec3{0, -4096, 0}, Vec3{0, -4096, 40960}));
    for (int f = 0; f < 10; f++) {
        pool.update(host);
    }
    SH_CHECK(host.hurts.size() == 2);
    SH_CHECK(host.hurts.size() == 2 && host.hurts[0] == 122880 && host.hurts[1] == 61440);
    SH_CHECK(host.flashes == 2);
    SH_CHECK(pool.bolts()[0].hit && pool.bolts()[0].count < pool.params().walk.sweep_steps);
}

SH_TEST(BoltPoolSoundsKeepTheirGap) {
    Host host;
    BoltPool pool(base_pool());
    SH_CHECK(pool.attach(host, "Incubus", 0, 0, 0));
    pool.update(host);  // a reset leaves every sound exactly at its first gap: one frame later it may play
    pool.spawn(host, "strike", Vec3{0, -4096, 0}, Vec3{0, -4096, 40960}, 0, 2048, 4096);
    pool.spawn(host, "strike", Vec3{0, -4096, 0}, Vec3{0, -4096, 40960}, 0, 2048, 4096);
    // A strong strike plays slot 0 (1680) once: the second is within its gap.
    SH_CHECK(host.sounds.size() == 1 && host.sounds[0] == 1680);
}
