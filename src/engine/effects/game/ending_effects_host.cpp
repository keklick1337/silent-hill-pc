// SPDX-License-Identifier: GPL-3.0-or-later
// The final bosses' effects of map7_s03 over the
// running game: the EndingFx_* entry points the boss behaviours and the map's Lua binds call. The lightning cloud,
// fire, bolts, heat fields and lens flare are the engine's named effects (named_effects.h); the beams of light and
// the thunder pacing are sh::effects::EndingBeams and ThunderPacing (ending_beams.h). The beams' state, pattern
// table, colour curve and strike points are records of the loaded map's registry (gamedata/maps/map7_s03/tables.json,
// looked up by name at each call, so the state starts from zero on every visit). Every entry point works only on a
// map whose overlay.json lists the Incubus in "character_effects".

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/loader/active_map.h"
#include "engine/maps/map_globals.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/view_space.h"
#include "engine/effects/ending_beams.h"
#include "engine/effects/game/slot_hosts.h"
#include "engine/effects/named_effects.h"
#include "engine/math/view_projection.h"
#include "engine/math/geometry_state.h"
#include "render/draw3d/draw3d.h"

namespace {

namespace fx = sh::effects;
namespace hosts = sh::effects::slot_hosts;

struct Point3 {
    std::int32_t x, y, z;
};

// A fan of the beams as the records keep it: its near and far points live where the pointers lead.
struct BeamFanRecord {
    Point3* near;
    std::int32_t near_lift;
    Point3* far;
    std::int32_t far_lift;
    std::int16_t width;
    std::int16_t intensity;
    std::int16_t cap;
    std::int16_t fade_rate;
    std::int16_t width_speed_max;
    std::int16_t far_speed_max;
    std::int16_t near_speed_max;
    std::int16_t perspective;
};

struct BeamMotionRecord {
    std::int16_t width_speed;
    std::int16_t far_speed;
    std::int16_t far_progress;
    std::int16_t near_speed;
    std::int16_t near_progress;
};

// One step of the pattern table: snap to it, and its two fans.
struct BeamPatternRecord {
    std::int32_t snap;
    BeamFanRecord fans[2];
};

// The beams' state (registry type "BossFxPattern"): its first word is map_vars "boss_fx_mode".
struct BeamsRecord {
    std::int32_t mode;
    std::int32_t step;
    std::int32_t timer;
    std::int32_t strikes;
    std::int32_t mode_timer;
    std::uint8_t reserved[4];
    std::uint8_t color[3];
    std::uint8_t reserved2;
    BeamFanRecord current[2];
    BeamFanRecord* target_near;  // the pattern step's fans the beams move to
    BeamFanRecord* target_far;
    BeamFanRecord start[2];
    BeamMotionRecord motion[2];
    Point3 points[8];  // where the current and start fans' pointers lead
    std::uint8_t cells[6][16][8];
};

constexpr int kPatterns = 18;
constexpr int kCurve = 9;

}  // namespace

extern "C" {
// The bosses' effects time (map_vars "boss_fx_time"; 0 stops them being drawn), the boss aim record whose lead the
// bolts track (the Incubus reads it by symbol: sh.entity.boss_aim_lead), and the boss bone the beams follow (the
// registry's variable "g_BossBonePosition").
std::int32_t g_BossFxTime = 0;
struct BossAimRecord {
    std::int32_t x;
    std::int32_t z;
    sh::camera::Matrix frame;
    void* chara;
    void* bones;
    Point3 player_position;
    std::int32_t lead_x;
    std::int32_t lead_z;
    std::int32_t reserved[2];
} g_BossAim = {};

Point3 g_BossBonePosition = {};

}

namespace {

fx::ThunderPacing s_thunder;
// The lightning cloud's thunder volume and pause (words of the executable, kept between visits).
std::int32_t s_lightningVolume = 0;
std::int32_t s_lightningStopped = 0;
// The beams' generator output word (a word of the executable, kept between visits; its seed is the map's).
std::int32_t s_beamRandomOut = 0;
// Drawn over the fights' armed boss even though the effects time stays 0 during them.
int s_bossFxArmedDraw = 0;

std::int32_t rand16() { return sh::loop::game_random().next15(); }

int thunder_take() { return s_thunder.take([] { return rand16(); }) ? 1 : 0; }

fx::Vec3 vec(const Point3* v) { return v != nullptr ? fx::Vec3{v->x, v->y, v->z} : fx::Vec3{}; }
fx::Vec3 vec(const std::int32_t* v) { return fx::Vec3{v[0], v[1], v[2]}; }

// ---- the beams' records in the map registry ------------------------------------------------------------------

struct BeamRecords {
    BeamsRecord* state;
    std::int32_t* seed;
    BeamPatternRecord* patterns;
    std::int32_t* curve;
    Point3* strike_center;
    Point3* strike_target;
};

bool bind(BeamRecords& r) {
    r.state = static_cast<BeamsRecord*>(sh::loader::active_map_symbol("boss_beam_pattern"));
    r.seed = static_cast<std::int32_t*>(sh::loader::active_map_symbol("boss_beam_seed"));
    r.patterns = static_cast<BeamPatternRecord*>(sh::loader::active_map_symbol("boss_beam_steps"));
    r.curve = static_cast<std::int32_t*>(sh::loader::active_map_symbol("boss_beam_curve"));
    r.strike_center = static_cast<Point3*>(sh::loader::active_map_symbol("boss_strike_center"));
    r.strike_target = static_cast<Point3*>(sh::loader::active_map_symbol("boss_strike_target"));
    return r.state != nullptr && r.seed != nullptr && r.patterns != nullptr && r.curve != nullptr &&
           r.strike_center != nullptr && r.strike_target != nullptr;
}

fx::BeamFan fan_of(const BeamFanRecord& f) {
    fx::BeamFan b;
    b.near = vec(f.near);
    b.near_lift = f.near_lift;
    b.far = vec(f.far);
    b.far_lift = f.far_lift;
    b.width = f.width;
    b.intensity = f.intensity;
    b.cap = f.cap;
    b.fade_rate = f.fade_rate;
    b.width_speed_max = f.width_speed_max;
    b.far_speed_max = f.far_speed_max;
    b.near_speed_max = f.near_speed_max;
    b.perspective = f.perspective;
    return b;
}

void store_fan(BeamFanRecord& f, const fx::BeamFan& b) {
    if (f.near != nullptr) *f.near = Point3{b.near.x, b.near.y, b.near.z};
    f.near_lift = b.near_lift;
    if (f.far != nullptr) *f.far = Point3{b.far.x, b.far.y, b.far.z};
    f.far_lift = b.far_lift;
    f.width = b.width;
    f.intensity = b.intensity;
    f.cap = b.cap;
    f.fade_rate = b.fade_rate;
    f.width_speed_max = b.width_speed_max;
    f.far_speed_max = b.far_speed_max;
    f.near_speed_max = b.near_speed_max;
    f.perspective = b.perspective;
}

class BeamsHost final : public fx::EndingBeamsHost {
public:
    explicit BeamsHost(const BeamRecords& r) : r_(r) {
        for (int i = 0; i < kCurve; i++) curve_[static_cast<std::size_t>(i)] = r_.curve[i];
    }

    void load(fx::BeamsState& s) override {
        const BeamsRecord& d = *r_.state;
        s.mode = d.mode;
        s.step = d.step;
        s.timer = d.timer;
        s.strikes = d.strikes;
        s.mode_timer = d.mode_timer;
        for (int i = 0; i < 3; i++) s.color[i] = d.color[i];
        s.target = -1;
        for (int i = 0; i < kPatterns; i++) {
            if (d.target_near == &r_.patterns[i].fans[0]) s.target = i;
        }
        for (int i = 0; i < 2; i++) {
            s.current[i] = fan_of(d.current[i]);
            s.start[i] = fan_of(d.start[i]);
            const BeamMotionRecord& m = d.motion[i];
            s.motion[i] = {m.width_speed, m.far_speed, m.far_progress, m.near_speed, m.near_progress};
        }
        s.random_out = s_beamRandomOut;
        s.random_seed = *r_.seed;
    }

    void store(const fx::BeamsState& s) override {
        BeamsRecord& d = *r_.state;
        d.mode = s.mode;
        d.step = s.step;
        d.timer = s.timer;
        d.strikes = s.strikes;
        d.mode_timer = s.mode_timer;
        for (int i = 0; i < 3; i++) d.color[i] = s.color[i];
        d.target_near = s.target >= 0 ? &r_.patterns[s.target].fans[0] : nullptr;
        d.target_far = s.target >= 0 ? &r_.patterns[s.target].fans[1] : nullptr;
        for (int i = 0; i < 2; i++) {
            store_fan(d.current[i], s.current[i]);
            store_fan(d.start[i], s.start[i]);
            const fx::BeamMotion& m = s.motion[i];
            d.motion[i] = {m.width_speed, m.far_speed, m.far_progress, m.near_speed, m.near_progress};
        }
        s_beamRandomOut = s.random_out;
        *r_.seed = s.random_seed;
    }

    fx::BeamPattern pattern(int index) override {
        const BeamPatternRecord& p = r_.patterns[index];
        fx::BeamPattern out;
        out.snap = p.snap != 0;
        out.fans[0] = fan_of(p.fans[0]);
        out.fans[1] = fan_of(p.fans[1]);
        return out;
    }
    const std::array<std::int32_t, 9>& curve() override { return curve_; }

    std::int32_t delta_time() override { return sh::loop::frame_delta(); }
    std::int32_t sin(std::int32_t a) override { return sh::world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return sh::world::fixed_cos(a); }

    fx::Vec3 npc_position(int slot) override {
        return hosts::position_of(sh::game::session().system.characters.npcs[slot]);
    }
    void track_boss_bone() override {
        // The boss's second bone, from geometry units to Q12.
        const sh::entities::BoneMatrix& bone = sh::game::session().system.characters.npc_bone_pool[1].world;
        g_BossBonePosition = Point3{bone.t[0] << 4, bone.t[1] << 4, bone.t[2] << 4};
    }
    fx::Vec3 strike_target() override { return vec(r_.strike_target); }

    void bolts_attach_ending() override {
        fx::bolt_pools().attach("BossBolts", "Ending", vec(r_.strike_center), nullptr);
    }
    void bolts_update() override { fx::bolt_pools().update("BossBolts"); }
    void bolts_strike(const fx::Vec3& from, const fx::Vec3& to, std::int32_t roll, std::int32_t life) override {
        fx::bolt_pools().spawn("BossBolts", "strike", from, to, 0, roll, life);
    }

    // The world-screen view; a point is moved by its rotation and projected as the view's origin (the camera's
    // single projection: it leaves the projection history alone).
    void begin_projection() override {
        view_ = sh::camera::camera_host().world_screen();
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) rot_.m[r][c] = view_.m[r][c];
        }
        const sh::math::ViewProjection now = sh::math::game_view_projection();
        screen_.h = now.screen().h;
        screen_.ofx = now.screen().ofx;
        screen_.ofy = now.screen().ofy;
        screen_.whole_map_far = now.precise().options.whole_map_far;
    }
    fx::ScreenPoint project(const fx::Vec3& at) override {
        const sh::math::Vec3i moved = sh::math::apply_matrix_lv(rot_, {at.x, at.y, at.z});
        sh::camera::Matrix m = view_;
        m.t[0] = static_cast<std::int32_t>(static_cast<std::uint32_t>(view_.t[0]) + static_cast<std::uint32_t>(moved.x));
        m.t[1] = static_cast<std::int32_t>(static_cast<std::uint32_t>(view_.t[1]) + static_cast<std::uint32_t>(moved.y));
        m.t[2] = static_cast<std::int32_t>(static_cast<std::uint32_t>(view_.t[2]) + static_cast<std::uint32_t>(moved.z));
        std::int16_t sx = 0;
        std::int16_t sy = 0;
        const std::int32_t depth = sh::camera::project_depth(m, screen_, sh::camera::Vec3{}, sx, sy);
        return fx::ScreenPoint{sx, sy, depth};
    }

    void emit_cell(const sh::draw3d::Polygon& cell, std::int32_t bucket) override {
        sh::draw3d::submit_placed(cell, sh::math::geometry().depth_history(),
                                  {sh::frame::Layer::World, static_cast<std::uint32_t>(bucket)});
        GsOUT_PACKET_P += hosts::packet::kShadedQuad;
    }
    void emit_blend(std::int32_t bucket, std::int32_t blend) override {
        unsigned char* packet = GsOUT_PACKET_P;
        GsOUT_PACKET_P += hosts::packet::kDrawMode;
        sh::draw2d::DrawMode mode;
        mode.page = sh::draw2d::TexturePage::from_word(static_cast<std::uint16_t>((blend & 3) << 5));
        mode.dither = true;
        hosts::place_2d({sh::frame::Layer::World, static_cast<std::uint32_t>(bucket)}, packet, mode);
    }

private:
    sh::camera::Matrix view_;
    sh::math::Matrix3 rot_;
    sh::camera::ScreenProjection screen_;
    BeamRecords r_;
    std::array<std::int32_t, 9> curve_{};
};

void bolts_update() { fx::bolt_pools().update("BossBolts"); }

}  // namespace

extern "C" {

// The final bosses' effects work on a map whose overlay.json lists one of the boss bolt pool's owners (the Incubus,
// Unknown23) in "character_effects".
int EndingFx_Enabled(void) {
    static std::vector<int> owners;
    if (owners.empty()) {
        for (const std::string& name : fx::bolt_pools().owners("BossBolts")) {
            const int number = sh::ids::chara_number(name.c_str());
            if (number >= 0) {
                owners.push_back(number);
            }
        }
    }
    for (const int owner : owners) {
        if (sh::maps::map_runtime().chara_effects(owner)) {
            return 1;
        }
    }
    return 0;
}

// ---- the thunder pacing: at most a few thunder sounds at once ----

void EndingFx_ThunderGapsReset(void) { s_thunder.reset(); }
void EndingFx_ThunderGapsTick(void) { s_thunder.tick(sh::loop::frame_delta()); }

// ---- the lightning cloud (heat field "BossLightning") ----

void EndingFx_LightningVolumeSet(std::int32_t volume) { s_lightningVolume = volume; }
void EndingFx_LightningResume(void) { s_lightningStopped = 0; }
void EndingFx_LightningStop(void) { s_lightningStopped = 1; }
void EndingFx_LightningUpdate(void) { fx::heat_fields().update("BossLightning", s_lightningStopped == 0); }

void EndingFx_LightningBolt(std::int32_t unused) {
    (void)unused;
    if (s_lightningStopped != 0) return;
    // The three draws in the order the original made them (x, y, z).
    Point3 around;
    around.x = rand16() - 0x3FFF;
    around.y = (rand16() >> 2) + 0x4801;
    around.z = (rand16() * 2) - 0x7FFF;
    std::optional<fx::Vec3> origin;
    if (thunder_take() && (origin = fx::heat_fields().origin("BossLightning"))) {
        static const int thunder = sh::ids::sound_number("finale_lightning_strike");
        const std::int32_t at[3] = {around.x + origin->x * 16, around.y + origin->y * 16, around.z + origin->z * 16};
        Sfx_WithFlagsPlay(thunder, at, s_lightningVolume, 0);
    }
    fx::heat_fields().spawn("BossLightning", vec(&around), 0x1E);
}

void EndingFx_LightningInit(const std::int32_t* at) {
    fx::heat_fields().clear("BossLightning");
    EndingFx_LightningVolumeSet(0xFF);
    fx::heat_fields().start("BossLightning", vec(at));
}

void EndingFx_LightningClear(void) {
    fx::heat_fields().clear("BossLightning");
    EndingFx_LightningVolumeSet(0xFF);
}

// ---- the flash over the dying Incubus, the boss fire ----

void EndingFx_FlareDraw(std::int32_t origin) { fx::lens_flares().draw("EndingFlare", origin); }
void EndingFx_FireResume(void) { fx::fire_pools().pause("BossFire", false); }
void EndingFx_FireStop(void) { fx::fire_pools().pause("BossFire", true); }
void EndingFx_FireUpdate(void) { fx::fire_pools().update("BossFire", thunder_take); }
void EndingFx_FireInit(void) { fx::fire_pools().reset("BossFire"); }
void EndingFx_FireCue(const std::int32_t* record) { fx::fire_pools().cue("BossFire", record); }

// ---- the lightning around the bosses (bolt pool "BossBolts") ----

void EndingFx_BoltsLoad(void) { fx::bolt_pools().load("BossBolts"); }

void EndingFx_BoltsTrack(void) {
    // The lead stays in the boss aim record for sh.entity.boss_aim_lead.
    fx::bolt_pools().track("BossBolts", &g_BossAim.lead_x, &g_BossAim.lead_z);
}

// The owner (the Incubus or Unknown23) starts the fire pool empty.
void EndingFx_BoltsAttach(const char* owner, const void* at, sh::entities::BoneCoord* bones) {
    fx::fire_pools().reset("BossFire");
    fx::bolt_pools().attach("BossBolts", owner, vec(static_cast<const std::int32_t*>(at)), bones);
}

// The boss's fire and lightning: drawn while the effects time runs, and around an armed boss's own draw (the
// original gate is the time alone, which stays 0 through the Good+ fight).
void EndingFx_BossDraw(std::int32_t armed) {
    if (armed) s_bossFxArmedDraw = 1;
    if (g_BossFxTime != 0 || s_bossFxArmedDraw) {
        bolts_update();
        EndingFx_FireUpdate();
    }
    if (armed) s_bossFxArmedDraw = 0;
}

// ---- the shock sparks of the bad ending's boss (heat field "ShockSparks") ----

// ---- the beams of light around the bosses ----

void EndingFx_BeamsInit(void) {
    BeamRecords r;
    if (!bind(r)) return;
    // The fans' points live in the state's own points.
    BeamsRecord& d = *r.state;
    d.current[0].near = &d.points[0];
    d.current[0].far = &d.points[1];
    d.current[1].near = &d.points[2];
    d.current[1].far = &d.points[3];
    d.start[0].near = &d.points[4];
    d.start[0].far = &d.points[5];
    d.start[1].near = &d.points[6];
    d.start[1].far = &d.points[7];
    BeamsHost host(r);
    fx::EndingBeams::init(host);
}

void EndingFx_BeamsModeSet(std::int32_t mode) {
    BeamRecords r;
    if (!bind(r)) return;
    BeamsHost host(r);
    fx::EndingBeams::set_mode(host, mode);
}

void EndingFx_BeamsUpdate(void) {
    BeamRecords r;
    if (!bind(r)) return;
    BeamsHost host(r);
    fx::EndingBeams::update(host);
}

}  // extern "C"

namespace {

// The map variables and the Incubus's binds name these records (map_vars "boss_fx_time", "boss_aim").
[[maybe_unused]] const bool g_map_globals_bound = [] {
    sh::maps::map_globals().bind("boss_fx_time", &g_BossFxTime);
    sh::maps::map_globals().bind("boss_aim", &g_BossAim);
    return true;
}();

}  // namespace

// The records the map tables and the Incubus name: the beam pattern's size, the boss bone the beams follow and the
// lead the bolts track (engine/loader/map_table_types.cpp, engine/chara/game/chara_link_game.cpp).
extern "C" {
unsigned BossFx_PatternBytes(void) { return sizeof(BeamsRecord); }
void* BossFx_BonePosition(void) { return &g_BossBonePosition; }
void BossFx_AimLead(std::int32_t* x, std::int32_t* z) { *x = g_BossAim.lead_x, *z = g_BossAim.lead_z; }
}
