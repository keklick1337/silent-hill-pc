// Blood and gunfire effects (src/engine/effects/combat_effects.h): the data file, a hit's sprays and anchor, the
// drop's landing, the muzzle flash's mirrored blades and the one-frame hit spark, on a fake slot table.

#include "engine/effects/combat_effects.h"

#include "framework/sh_test.h"

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fx = sh::effects;

fx::CombatEffectParams load_params() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/combat_effects.json", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    auto r = fx::parse_combat_effect_params(ss.str());
    SH_CHECK(static_cast<bool>(r));
    return r ? r.value() : fx::CombatEffectParams{};
}

class FakeHost final : public fx::CombatEffectHost {
public:
    std::vector<fx::EffectSlot> table = std::vector<fx::EffectSlot>(16);
    std::vector<std::int32_t> splats = std::vector<std::int32_t>(10, -1);
    fx::SlotView v;
    fx::AnchorMotion m;
    struct Emitted {
        sh::draw3d::Polygon polygon;
        std::int32_t bucket;
        bool bind;
    };
    std::vector<Emitted> emitted;
    std::uint32_t seed = 7;
    std::int32_t ground = 0;
    std::int32_t ground_type = 1;
    std::int32_t depth = 800;
    int flashes = 0;
    std::vector<std::array<std::int32_t, 3>> shown;  // kind, age, life
    bool lit = true;
    std::int32_t option = 0;

    FakeHost() { v.screen_distance = 512; }

    std::int32_t rand16() override {
        seed = seed * 1103515245u + 12345u;
        return static_cast<std::int32_t>((seed >> 16) & 0x7FFF);
    }
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t atan2(std::int32_t, std::int32_t) override { return 0; }
    std::int32_t sqrt(std::int32_t) override { return 0; }
    std::int32_t angle_remap(std::int32_t a) override { return a; }
    std::int32_t length2(std::int32_t, std::int32_t) override { return 0; }
    std::int32_t delta_time() override { return 136; }
    std::int32_t gravity_step() override { return 4096; }
    fx::Vec3 player_position() override { return {}; }
    std::int32_t ground_height(std::int32_t, std::int32_t) override { return ground; }
    void restore_view() override {}
    void play_landing_sound(const fx::Vec3&) override {}
    std::int32_t darkness(std::int32_t, std::int32_t, std::int32_t) override { return 0; }
    const fx::SlotView& view() override { return v; }
    fx::ScreenPoint project(const fx::Vec3s& r) override { return {r.x, r.z, depth}; }
    void project_corners(const fx::Vec3s r[4], fx::ScreenPoint out[4]) override { project_quad(r, out); }
    int slot_count() override { return static_cast<int>(table.size()); }
    fx::EffectSlot load(int s) override { return table[static_cast<std::size_t>(s)]; }
    void store(int s, const fx::EffectSlot& e) override { table[static_cast<std::size_t>(s)] = e; }
    int alloc(std::uint8_t kind) override {
        for (std::size_t i = 0; i < table.size(); i++) {
            if (table[i].kind == 0) {
                table[i].kind = kind;
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    void set_effect_flag(std::uint32_t) override {}
    fx::DripSource drip_source(int) override { return {}; }
    fx::SmokeColumnParams smoke_params() override { return {}; }
    void store_smoke_params(const fx::SmokeColumnParams&) override {}
    fx::EmitterParams emitter_params() override { return {}; }
    void store_emitter_params(const fx::EmitterParams&) override {}
    void emit(const sh::draw3d::Polygon&, fx::SlotLayer, std::int32_t) override {}
    void screen_copy() override {}

    std::int32_t blood_option() override { return option; }
    bool fog_lit() override { return lit; }
    fx::FogColors fog_colors(std::uint8_t level, std::int32_t) override { return {{level, level, level}, {9, 9, 9}}; }
    sh::draw3d::Rgb faded_tint(std::uint8_t level) override { return {level, level, level}; }
    std::int32_t fog_keep(std::int32_t) override { return 256; }
    fx::GroundSample surface(std::int32_t, std::int32_t) override { return {ground, ground_type}; }
    fx::Vec3 camera_position() override { return {}; }
    std::array<std::int16_t, 2> camera_forward() override { return {0, 4096}; }
    bool raining() override { return false; }
    bool snowing() override { return false; }
    std::array<std::int32_t, 2> wind() override { return {0, 0}; }
    bool npc_present(int) override { return true; }
    void flash_light() override { flashes++; }
    void flash_shown(int, int flash, const fx::Vec3&, std::int32_t age, std::int32_t life) override {
        shown.push_back({flash, age, life});
    }
    fx::EffectAnchor anchor(int i) override { return fx::effect_anchors()[static_cast<std::size_t>(i)]; }
    void store_anchor(int i, const fx::EffectAnchor& a) override { fx::effect_anchors()[static_cast<std::size_t>(i)] = a; }
    const fx::AnchorMotion& motion() override { return m; }
    int blood_splat_count() override { return static_cast<int>(splats.size()); }
    std::int32_t blood_splat_slot(int i) override { return splats[static_cast<std::size_t>(i)]; }
    void set_blood_splat(int i, std::int32_t slot) override { splats[static_cast<std::size_t>(i)] = slot; }
    sh::math::Transform bone_frame(fx::MuzzleBones) override { return {}; }
    fx::Vec3 transform(const sh::math::Transform&, const fx::Vec3s& p) override { return {p.x, p.y, p.z}; }
    void project_quad(const fx::Vec3s r[4], fx::ScreenPoint out[4]) override {
        for (int i = 0; i < 4; i++) out[i] = project(r[i]);
    }
    void emit_quad(const sh::draw3d::Polygon& p, std::int32_t bucket, bool bind) override {
        emitted.push_back({p, bucket, bind});
    }
};

}  // namespace

SH_TEST(combat_effects_data_file) {
    const fx::CombatEffectParams p = load_params();
    SH_CHECK(p.blood_hits.size() == 10 && p.body_pools.size() == 16);
    SH_CHECK(p.flashes.size() == 5 && p.smoke.size() == 3);
    SH_CHECK(p.blood_hits[3].splats_random && p.blood_hits[3].splats_min == 1 && p.blood_hits[3].splats_max == 2);
    SH_CHECK(!p.blood_hits[2].splats_random && p.blood_hits[2].splats_min == 1);
    SH_CHECK(p.body_pools[1].size == 72 && p.body_pools[1].patches.size() == 3);
    SH_CHECK(p.body_pools[1].patches[2].forward_range == 2496 && p.body_pools[1].patches[2].forward_base == -1712);
    const auto kaufmann = p.guns.find("KaufmannGun");
    SH_CHECK(kaufmann != p.guns.end() && kaufmann->second.flash_angles.has_value());
    SH_CHECK(kaufmann->second.bones == fx::MuzzleBones::Second && *kaufmann->second.flash == 3);
    const auto saw = p.guns.find("Chainsaw");
    SH_CHECK(saw != p.guns.end() && !saw->second.flash.has_value() && saw->second.puffs == 3);
    SH_CHECK(p.guns.size() == 7);
    // A bad value is reported with its place.
    SH_CHECK(!fx::parse_combat_effect_params("{\"blood_hits\": 3}"));
}

SH_TEST(combat_effects_hit_takes_an_anchor_and_sprays) {
    const fx::CombatEffectParams p = load_params();
    fx::effect_anchors() = {};
    FakeHost host;
    fx::Bleeder who;
    who.chara = 2;
    who.anchor = {4096, -8192, 12288};
    fx::CombatEffects::spawn_blood(host, p, who, {0, 0, 0}, 2, 0);
    // Strength 2: two sprays following anchor 0, and one drop.
    const fx::EffectAnchor a = fx::effect_anchors()[0];
    SH_CHECK(a.chara == 2 && a.x == 4096 && a.y == -8192 && a.z == 12288);
    int sprays = 0;
    int drops = 0;
    for (const fx::EffectSlot& s : host.table) {
        if (s.kind == fx::combat_kind::kBloodSpray) {
            sprays++;
            SH_CHECK(s.flags == 0 && fx::byte_of(s.a, 1) == 12 && fx::byte_of(s.a, 3) == 32);
        }
        if (s.kind == fx::combat_kind::kBloodDrop) drops++;
    }
    SH_CHECK(sprays == 2 && drops == 1);
    // The drop landed on the ground: the anchor counts the spray and the falling drop.
    SH_CHECK(a.refs == 3);
    // A type whose blood does not follow its body (the Split Head)...
    FakeHost other;
    who.follows_body = false;
    fx::effect_anchors() = {};
    fx::CombatEffects::spawn_blood(other, p, who, {0, 0, 0}, 1, 0);
    SH_CHECK(fx::effect_anchors()[0].refs == 0 && other.table[0].flags == fx::kNoAnchor * 4);
}

SH_TEST(combat_effects_drop_lands_and_releases_its_anchor) {
    fx::effect_anchors() = {};
    fx::effect_anchors()[1].refs = 3;
    FakeHost host;
    fx::EffectSlot& s = host.table[0];
    s.kind = fx::combat_kind::kBloodDrop;
    s.flags = 1 * 4;  // falling, anchor 1
    s.y = -100;
    fx::set_hi16(s.b, 0);  // the ground under it
    fx::set_byte(s.a, 1, 10);
    const fx::EffectSlot falling = s;
    SH_CHECK(fx::CombatEffects::draw_drop(host, 0));
    SH_CHECK(host.table[0].flags == 1 && host.table[0].y == 0 && fx::effect_anchors()[1].refs == 1);
    // A step under the splat clears it.
    host.ground = 2000;
    host.table[1] = falling;
    host.table[1].flags = fx::kNoAnchor * 4;
    fx::CombatEffects::draw_drop(host, 1);
    SH_CHECK(host.table[1].kind == 0);
}

SH_TEST(combat_effects_flash_blades_mirror) {
    const fx::CombatEffectParams p = load_params();
    FakeHost host;
    host.depth = 400;
    fx::EffectSlot& s = host.table[0];
    s.kind = fx::combat_kind::kMuzzleFlash;
    s.flags = 0;
    SH_CHECK(fx::CombatEffects::draw_flash(host, p, 0));
    SH_CHECK(host.flashes == 1 && host.emitted.size() == 6);
    // Every drawn frame reports the flash to the modern lighting, with its kind and age.
    SH_CHECK(host.shown.size() == 1 && host.shown[0][0] == 0 && host.shown[0][1] == 0 &&
             host.shown[0][2] == p.flashes[0].life);
    if (host.emitted.size() != 6) return;
    SH_CHECK(host.emitted[0].bind);
    SH_CHECK(host.emitted[0].polygon.v[0].uv.u == 0xE0 && host.emitted[0].polygon.v[1].uv.u == 0xFF);
    host.emitted.clear();
    host.table[1] = s;
    host.table[1].flags = 1;
    fx::CombatEffects::draw_flash(host, p, 1);
    if (host.emitted.empty()) return;
    SH_CHECK(host.emitted[0].polygon.v[0].uv.u == 0xFF && host.emitted[0].polygon.v[1].uv.u == 0xE0);
    // Past its life the flash is gone.
    fx::set_hi16(host.table[1].b, 4000);
    SH_CHECK(!fx::CombatEffects::draw_flash(host, p, 1) && host.table[1].kind == 0);
}

SH_TEST(combat_effects_spark_lives_one_frame) {
    FakeHost host;
    fx::CombatEffects::spawn_spark(host, {1024, 0, 2048}, 3, 64);
    SH_CHECK(host.table[0].kind == fx::combat_kind::kHitSpark && host.table[0].x == 64 && host.table[0].z == 128);
    host.depth = 300;
    SH_CHECK(fx::CombatEffects::draw_spark(host, 0));
    SH_CHECK(host.table[0].kind == 0 && host.emitted.size() == 1);
    if (host.emitted.empty()) return;
    SH_CHECK(!host.emitted[0].bind);
    const sh::draw3d::Polygon& q = host.emitted[0].polygon;
    // Upside down: the first corners are the lower ones.
    SH_CHECK(q.v[0].xy.y > q.v[2].xy.y && q.shade.translucent);
    SH_CHECK(host.emitted[0].bucket == (300 - 0x20) >> 3);
}
