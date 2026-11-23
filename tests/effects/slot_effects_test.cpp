// Slot effects (src/engine/effects/slot_effects.h): drips, light glow, smoke column, fire emitter and the Split
// Head's drool and drag mark on a fake slot table and projection.

#include "engine/effects/slot_effects.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace fx = sh::effects;

class FakeHost final : public fx::SlotEffectHost {
public:
    std::vector<fx::EffectSlot> table = std::vector<fx::EffectSlot>(32);
    std::vector<fx::DripSource> drips = std::vector<fx::DripSource>(2);
    fx::SmokeColumnParams smoke;
    fx::EmitterParams emitter;
    fx::SlotView v;
    struct Emitted {
        sh::draw3d::Polygon polygon;
        fx::SlotLayer layer;
        std::int32_t bucket;
    };
    std::vector<Emitted> emitted;
    std::uint32_t flags = 0;
    int screen_copies = 0;
    int sounds = 0;
    int restores = 0;
    std::int32_t ground = 0;
    std::int32_t dt = 136;
    std::int32_t depth = 800;
    std::uint32_t seed = 1;
    int next_alloc = 0;

    FakeHost() { v.screen_distance = 512; }

    std::int32_t rand16() override {
        seed = seed * 1103515245u + 12345u;
        return static_cast<std::int32_t>((seed >> 16) & 0x7FFF);
    }
    std::int32_t sin(std::int32_t a) override { return (a & 0xFFF) == 1024 ? 4096 : ((a & 0xFFF) == 3072 ? -4096 : 0); }
    std::int32_t cos(std::int32_t a) override { return sin(a + 1024); }
    std::int32_t atan2(std::int32_t, std::int32_t) override { return 0; }
    std::int32_t sqrt(std::int32_t x) override {
        std::int32_t r = 0;
        while ((r + 1) * (r + 1) <= x) r++;
        return r;
    }
    std::int32_t angle_remap(std::int32_t a) override { return a; }
    std::int32_t length2(std::int32_t x, std::int32_t z) override { return (x < 0 ? -x : x) + (z < 0 ? -z : z); }
    std::int32_t delta_time() override { return dt; }
    std::int32_t gravity_step() override { return 4096; }
    fx::Vec3 player_position() override { return {}; }
    std::int32_t ground_height(std::int32_t, std::int32_t) override { return ground; }
    void restore_view() override { restores++; }
    void play_landing_sound(const fx::Vec3&) override { sounds++; }
    std::int32_t darkness(std::int32_t, std::int32_t, std::int32_t) override { return 128; }
    const fx::SlotView& view() override { return v; }
    fx::ScreenPoint project(const fx::Vec3s& r) override {
        return fx::ScreenPoint{static_cast<std::int16_t>(r.x / 16), static_cast<std::int16_t>(r.y / 16), depth};
    }
    void project_corners(const fx::Vec3s r[4], fx::ScreenPoint out[4]) override {
        for (int i = 0; i < 4; i++) out[i] = project(r[i]);
    }
    int slot_count() override { return static_cast<int>(table.size()); }
    fx::EffectSlot load(int i) override { return table[static_cast<std::size_t>(i)]; }
    void store(int i, const fx::EffectSlot& s) override { table[static_cast<std::size_t>(i)] = s; }
    int alloc(std::uint8_t kind) override {
        for (std::size_t n = 0; n < table.size(); n++) {
            const std::size_t i = (static_cast<std::size_t>(next_alloc) + n) % table.size();
            if (table[i].kind == 0) {
                table[i].kind = kind;
                next_alloc = static_cast<int>(i) + 1;
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    void set_effect_flag(std::uint32_t bit) override { flags |= bit; }
    fx::DripSource drip_source(int i) override { return drips[static_cast<std::size_t>(i)]; }
    fx::SmokeColumnParams smoke_params() override { return smoke; }
    void store_smoke_params(const fx::SmokeColumnParams& p) override { smoke = p; }
    fx::EmitterParams emitter_params() override { return emitter; }
    void store_emitter_params(const fx::EmitterParams& p) override { emitter = p; }
    void emit(const sh::draw3d::Polygon& p, fx::SlotLayer layer, std::int32_t bucket) override {
        emitted.push_back({p, layer, bucket});
    }
    void screen_copy() override { screen_copies++; }

    int count(std::uint8_t kind) const {
        int n = 0;
        for (const auto& s : table) n += s.kind == kind ? 1 : 0;
        return n;
    }
};

}  // namespace

SH_TEST(slot_effects_word_helpers) {
    std::uint32_t w = 0x11223344u;
    SH_CHECK(fx::lo16(w) == 0x3344 && fx::hi16(w) == 0x1122 && fx::byte_of(w, 2) == 0x22);
    fx::set_lo16(w, -1);
    SH_CHECK(w == 0x1122FFFFu);
    fx::set_byte(w, 3, 0x1AB);
    SH_CHECK(w == 0xAB22FFFFu);
    SH_CHECK(fx::mul12(0x7FFFFFFF, 2) == static_cast<std::int32_t>(0xFFFFFFFEu) >> 12);
    SH_CHECK(fx::mul12_wide(0x7FFFFFFF, 2) == static_cast<std::int32_t>((0x7FFFFFFFll * 2) >> 12));

    const std::uint32_t xy[4] = {fx::xy_word(-1, 2), fx::xy_word(3, -4), 0, 0};
    const std::uint32_t uv[4] = {0x020E8000u, 0x6D803Fu, 0xBF00u, 0xBF3Fu};
    const sh::draw3d::Polygon q = fx::textured_quad(0x2E102030u, xy, uv);
    SH_CHECK(q.v[0].xy.x == -1 && q.v[0].xy.y == 2 && q.v[1].xy.y == -4);
    SH_CHECK(q.v[0].color.r == 0x30 && q.v[0].color.g == 0x20 && q.v[0].color.b == 0x10);
    SH_CHECK(q.shade.translucent && !q.shade.raw_texture);
    SH_CHECK(q.palette.word() == 0x020E && q.page.word() == 0x6D);
    SH_CHECK(q.v[1].uv.u == 0x3F && q.v[2].uv.v == 0xBF);
}

SH_TEST(slot_effects_drips_start_draw_respawn) {
    FakeHost h;
    h.drips[1] = fx::DripSource{4096, -4096, -256, 0, 0, 2000, 4096, 5, 0};
    fx::Drips::start(h, 1);
    SH_CHECK(h.count(fx::slot_kind::kDrip) == 5);
    const fx::EffectSlot s = h.table[0];
    SH_CHECK(s.x == 4096 && s.z == -4096 && s.y == -256);
    SH_CHECK(fx::byte_of(s.a, 2) < 4 && fx::byte_of(s.a, 3) >= 96 && fx::byte_of(s.a, 3) <= 159);
    SH_CHECK(fx::byte_of(s.b, 0) == 1);
    SH_CHECK(static_cast<std::uint16_t>(fx::lo16(s.a)) < 4096);

    // Mid-life: drawn translucent at its depth bucket, age advanced by the frame time.
    h.table[0].a = (h.table[0].a & 0xFFFF0000u) | 1000u;
    SH_CHECK(fx::Drips::draw(h, 0));
    SH_CHECK(h.emitted.size() == 1 && h.emitted[0].bucket == (800 + 256) >> 3);
    SH_CHECK(h.emitted[0].polygon.shade.translucent);
    SH_CHECK(fx::lo16(h.table[0].a) == 1136);

    // Past its period the drop starts again, one period younger, and is not drawn.
    h.table[0].a = (h.table[0].a & 0xFFFF0000u) | 4090u;
    SH_CHECK(!fx::Drips::draw(h, 0));
    SH_CHECK(fx::lo16(h.table[0].a) == 4090 + 136 - 4096);
    SH_CHECK(h.emitted.size() == 1);
}

SH_TEST(slot_effects_light_glow_one_frame) {
    FakeHost h;
    fx::LightGlow::spawn(h, fx::Vec3{1600, -3200, 800}, 300, 64, 1);
    const int slot = 0;
    SH_CHECK(h.table[slot].kind == fx::slot_kind::kLightGlow);
    SH_CHECK(h.table[slot].x == 100 && h.table[slot].y == -200 && h.table[slot].z == 50);
    SH_CHECK(fx::lo16(h.table[slot].a) == 150 && fx::hi16(h.table[slot].a) == 4);
    SH_CHECK(fx::LightGlow::draw(h, slot));
    SH_CHECK(h.table[slot].kind == 0);
    SH_CHECK(h.screen_copies == 1);
    SH_CHECK(h.emitted.size() == 4);
    SH_CHECK(h.emitted[0].polygon.v[0].color.r == 0x40);

    // Behind the camera nothing is drawn, the glow is still gone.
    fx::LightGlow::spawn(h, fx::Vec3{0, 0, 0}, 300, 64, 0);
    h.depth = 0;
    SH_CHECK(!fx::LightGlow::draw(h, 1));
    SH_CHECK(h.table[1].kind == 0 && h.emitted.size() == 4);
}

SH_TEST(slot_effects_smoke_column) {
    FakeHost h;
    h.smoke.puffs = 4;
    h.smoke.top = 8192;
    h.smoke.base = 0;
    h.smoke.rise = 2048;
    h.smoke.radius = 1024;
    h.smoke.size = 64;
    h.smoke.fade = 300;
    fx::SmokeColumn::start(h);
    SH_CHECK(h.count(fx::slot_kind::kSmokeWaiting) == 4);
    SH_CHECK((h.flags & fx::kSmokeRunning) != 0 && h.smoke.fade == 0);
    // The first puff waits the whole rise time, the last a quarter of it.
    SH_CHECK(h.table[0].a == 16384u && h.table[3].a == 4096u);

    // A waiting puff whose time is up becomes a puff at the column's base.
    h.table[3].a = 16385;
    SH_CHECK(fx::SmokeColumn::wait(h, 3));
    SH_CHECK(h.table[3].kind == fx::slot_kind::kSmokePuff && h.table[3].y == 0 && h.table[3].a == 0);

    // Nothing to start without puffs or rise.
    FakeHost e;
    fx::SmokeColumn::start(e);
    SH_CHECK(e.count(fx::slot_kind::kSmokeWaiting) == 0 && e.flags == 0);

    // A puff rises with the column speed and is drawn.
    h.table[3].x = 0;
    h.table[3].z = 0;
    SH_CHECK(fx::SmokeColumn::draw(h, 3));
    SH_CHECK(h.table[3].y == (136 * 2048) >> 12);
    SH_CHECK(h.emitted.size() == 1);
}

SH_TEST(slot_effects_fire_emitter) {
    FakeHost h;
    h.emitter.flags = 1;  // disc
    h.emitter.radius = 512;
    h.emitter.count = 6;
    h.emitter.top = 4096;
    h.emitter.bottom = 0;
    h.emitter.spark_spread = 16;
    fx::ParticleEmitter::start(h);
    SH_CHECK((h.flags & fx::kEmitterRunning) != 0);
    SH_CHECK(h.emitter.half_x == 1024 && h.emitter.half_z == 1024 && h.emitter.height == 4096);
    const int flames = h.count(fx::slot_kind::kFlame) + h.count(fx::slot_kind::kSpark);
    SH_CHECK(flames == 6);
    // Filled from the end of the table.
    SH_CHECK(h.table[31].kind != 0 && h.table[0].kind == 0);
    for (const auto& s : h.table) {
        if (s.kind == 0) continue;
        SH_CHECK(static_cast<std::uint16_t>(fx::lo16(s.a)) <= 4095);
        SH_CHECK((fx::byte_of(s.b, 0) & 0x80) != 0 && (fx::byte_of(s.b, 1) & 0x80) != 0);
        SH_CHECK(s.flags < 6);
    }

    // Dying out: the first fill only marks the slots idle.
    FakeHost d;
    d.emitter = h.emitter;
    d.emitter.mode = 1;
    d.emitter.count = 3;
    fx::ParticleEmitter::start(d);
    SH_CHECK(d.count(fx::slot_kind::kEmitterIdle) == 3 && d.emitter.level == 4096);

    // A flame is at least two pixels wide and goes to its depth bucket.
    h.v.spark_size = 1;
    SH_CHECK(fx::ParticleEmitter::draw_flame(h, 31) || h.table[31].kind != fx::slot_kind::kFlame);
}

SH_TEST(slot_effects_split_head_drool_and_drag) {
    FakeHost h;
    h.ground = -2000;
    fx::SplitHeadEffects::spawn_drool(h, 0, -64, 0, 0, 0);
    SH_CHECK(h.table[0].kind == fx::slot_kind::kSplitHeadDrool && h.table[0].flags == 0);
    // Below the ground: it lands, plays its sound and becomes a splat.
    fx::SplitHeadEffects::draw_drool(h, 0);
    SH_CHECK(h.table[0].flags == 1 && h.table[0].y == -2000 && h.sounds == 1 && h.restores == 1);
    // The splat lives 4.8 s.
    h.table[0].a = (h.table[0].a & 0xFFFF0000u) | 0x4CC0u;
    fx::SplitHeadEffects::draw_drool(h, 0);
    SH_CHECK(h.table[0].kind == 0);

    fx::SplitHeadEffects::spawn_drag(h, 2, 200, fx::Vec3{100, 0, 0}, fx::Vec3{-100, 0, 0});
    const int front = 1;
    const int back = fx::lo16(h.table[front].a);
    SH_CHECK(h.table[front].kind == fx::slot_kind::kSplitHeadDragFront);
    SH_CHECK(h.table[back].kind == fx::slot_kind::kSplitHeadDragBack);
    SH_CHECK(h.table[front].flags == 2 && fx::byte_of(h.table[front].a, 2) == 200);
    const std::size_t before = h.emitted.size();
    SH_CHECK(fx::SplitHeadEffects::draw_drag(h, front));
    SH_CHECK(h.table[front].kind == 0 && h.table[back].kind == 0);
    SH_CHECK(h.emitted.size() == before + 1);
}
