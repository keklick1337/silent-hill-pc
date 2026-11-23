// Acid spit (src/engine/effects/acid.h): the shipped file, a spit's launch angles, the trail's
// fade, the burst where it lands and a drop's growth and life.

#include "engine/effects/acid.h"

#include "framework/sh_test.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fx = sh::effects;

fx::AcidConfig shipped() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/acid.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    sh::Result<fx::AcidConfig> r = fx::parse_acid_config(text);
    if (!r) {
        std::fprintf(stderr, "acid.json: %s\n", r.error().c_str());
    }
    SH_CHECK(r);
    return r ? r.value() : fx::AcidConfig{};
}

// A flat world: ground at the given height, the table has room for `slots` particles.
class FakeHost final : public fx::AcidHost {
public:
    std::vector<fx::AcidParticle> table = std::vector<fx::AcidParticle>(40);
    std::vector<fx::EffectPrim> prims;
    std::int32_t dt = 68;
    std::int32_t ground = 0;
    bool hit = false;
    bool hit_player = false;
    int strikes = 0;
    int draws = 0;

    std::int32_t rand16() override { return 12345 + 977 * draws++; }
    std::int32_t sin(std::int32_t a) override { return a == 1024 ? 4096 : 0; }
    std::int32_t cos(std::int32_t a) override { return a == 1024 ? 0 : 4096; }
    std::int32_t delta_time() override { return dt; }
    std::int32_t atan2(std::int32_t a, std::int32_t b) override { return a * 2 + b; }
    std::int32_t length2_q6(std::int32_t a, std::int32_t b) override { return a + b; }
    std::int32_t darkness(const fx::Vec3&) override { return 255; }
    fx::ScreenPoint project(const fx::Vec3s& r) override { return fx::ScreenPoint{r.x, r.y, 256 + r.z}; }
    std::int32_t geometry_screen() override { return 512; }
    fx::Vec3s view_origin() override { return {}; }
    int alloc(std::uint8_t state, fx::AcidParticle* p) override {
        for (std::size_t i = 0; i < table.size(); i++) {
            if (table[i].state == 0) {
                table[i].state = state;
                *p = table[i];
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    void store(int slot, const fx::AcidParticle& p) override { table[static_cast<std::size_t>(slot)] = p; }
    fx::AcidProbe probe(const fx::Vec3&, const fx::Vec3&) override {
        fx::AcidProbe out;
        out.hit = hit;
        out.hit_player = hit_player;
        out.ground_height = ground;
        return out;
    }
    void strike_player() override { strikes++; }
    void emit(const fx::EffectPrim& p) override { prims.push_back(p); }
};

fx::AcidParams normal_params(const fx::AcidConfig& c) { return c.find("map4_s03")->normal; }

}  // namespace

SH_TEST(acid_shipped_file) {
    const fx::AcidConfig c = shipped();
    SH_CHECK(c.maps.size() == 2);
    const fx::AcidMapParams* m = c.find("map4_s03");
    SH_CHECK(m != nullptr);
    if (m == nullptr) {
        return;
    }
    SH_CHECK(m->normal.speed == 14336 && m->hard.speed == 20480);
    SH_CHECK(m->hard.length == m->normal.length && m->normal.segments == 18 && m->normal.life == 24576);
    SH_CHECK(c.find("map4_s05")->normal.speed == 15360 && c.find("map4_s05")->hard.speed == 22528);
    SH_CHECK(c.find("map0_s00") == nullptr);
    SH_CHECK(c.look.drop_life == 5734 && c.look.drop_fade_in == 1638 && c.look.drop_spread == 1024 &&
             c.look.drop_age_jitter == 1433);
    SH_CHECK(c.look.glow.page == 45 && c.look.core.page == 77 && c.look.core.palette_y == 1);
    SH_CHECK(!fx::parse_acid_config("{}"));
}

SH_TEST(acid_spit_launch) {
    fx::AcidEffects acid(shipped());
    FakeHost host;
    acid.spawn_spit(host, normal_params(acid.config()), {0, 100, 0}, {10, 30, 20});
    SH_CHECK(host.table[0].state == fx::kAcidSpitState);
    SH_CHECK(host.table[0].c0 == 10 * 2 + 20 && host.table[0].c1 == -70 * 2 + 30);
    SH_CHECK(host.table[0].t0 == 24576 && host.table[0].t1 == 4096 && host.table[0].flag == 0);
}

SH_TEST(acid_spit_trail_fades_and_burst) {
    fx::AcidEffects acid(shipped());
    FakeHost host;
    const fx::AcidParams params = normal_params(acid.config());
    acid.spawn_spit(host, params, {0, 0, 0}, {0, 0, 4096});
    fx::AcidParticle p = host.table[0];
    SH_CHECK(acid.update_spit(host, params, 0, p));
    // A fresh spit draws one segment: its two quads share the slot, the halo is a quarter of the core.
    SH_CHECK(host.prims.size() == 2 && host.prims[0].slot == host.prims[1].slot);
    SH_CHECK(host.prims[0].rgb[0].r == host.prims[1].rgb[0].r / 4 && host.prims[1].rgb[0].r > 0);
    SH_CHECK(host.prims[0].texture.page == 45 && host.prims[1].texture.page == 77);
    SH_CHECK(host.table[0].t0 == 24576 - 68);

    // It lands: flag set, droplets (the configured count) take the next slots.
    host.ground = -100000;
    p = host.table[0];
    SH_CHECK(acid.update_spit(host, params, 0, p));
    SH_CHECK(host.table[0].flag == 1);
    int drops = 0;
    for (const fx::AcidParticle& q : host.table) {
        drops += q.state == fx::kAcidDropState ? 1 : 0;
    }
    SH_CHECK(drops == 12 && host.strikes == 0);

    // A hit on Harry bursts twice and strikes him.
    FakeHost h2;
    acid.spawn_spit(h2, params, {0, 0, 0}, {0, 0, 4096});
    h2.hit = h2.hit_player = true;
    p = h2.table[0];
    acid.update_spit(h2, params, 0, p);
    int drops2 = 0;
    for (const fx::AcidParticle& q : h2.table) {
        drops2 += q.state == fx::kAcidDropState ? 1 : 0;
        SH_CHECK(q.state != fx::kAcidDropState || q.flag == 1);
    }
    SH_CHECK(drops2 == 24 && h2.strikes == 1);
}

SH_TEST(acid_spit_without_numbers_dies) {
    fx::AcidEffects acid(shipped());
    FakeHost host;
    acid.spawn_spit(host, fx::AcidParams{}, {0, 0, 0}, {0, 0, 4096});
    fx::AcidParticle p = host.table[0];
    SH_CHECK(!acid.update_spit(host, fx::AcidParams{}, 0, p));
    SH_CHECK(host.table[0].state == 0 && host.prims.empty());
}

SH_TEST(acid_drop_grows_and_expires) {
    fx::AcidEffects acid(shipped());
    FakeHost host;
    fx::AcidParticle p;
    p.state = fx::kAcidDropState;
    p.c0 = 0;
    p.c1 = 1843;
    p.t0 = 100;
    host.table[0] = p;
    SH_CHECK(acid.update_drop(host, 0, p));
    SH_CHECK(host.prims.size() == 2 && host.prims[1].rgb[0].r == ((100 * 68 / 1638) * 255) >> 8);
    // Over its life it is gone.
    host.dt = 6000;
    SH_CHECK(!acid.update_drop(host, 0, p));
    SH_CHECK(host.table[0].state == 0);
}
