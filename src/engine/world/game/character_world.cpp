// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world queries of the session's characters (character_world.h).

#include "engine/world/game/character_world.h"

#include <array>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/chara/runtime/chara_data.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/session.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/loop/game_random.h"
#include "engine/vfs/vfs_c.h"
#include "engine/world/cell_grid.h"
#include "engine/world/chara_collision.h"
#include "engine/world/chara_volume.h"
#include "engine/runtime/cell_stream_runtime.h"
#include "engine/world/collision_triggers.h"
#include "engine/world/line_of_sight.h"
#include "engine/world/move_query.h"
#include "engine/world/mover_setup.h"
#include "engine/world/wall_detect.h"
#include "engine/world/world.h"
#include "input/object_pick.h"

extern "C" {
// The PC bullet hitbox overlay (off by default; its knobs are the combat glue's).
extern std::int32_t g_PcBulletHitActive;
extern std::int32_t g_PcBulletVertMul;
extern std::int32_t g_PcBulletRadMul;

// The game's tick counter.

// Bumped by the player's phantom-floor reject (the player bridge).
int g_PhantomRejectCount = 0;
}

namespace sh::world::game {

namespace {

namespace ent = sh::entities;

constexpr int kNoCollisionSlot = sh::world::stream::kMissing;  // a cell file is there but no ready slot holds it
// The game's ground types the classification names.
constexpr std::int8_t kGroundDefault = 0;
constexpr std::uint8_t kGroundNone = 12;
// Collision states as the game numbers them: ignored, the player's own, the low state.
constexpr CollisionStates kStates{0, 1, 4};

// Of the original's ray setup state only the ray length is still read: a ray that cannot be set up (no horizontal
// extent) reports the length the previous ray left behind (0 before the first one).
std::int16_t s_last_ray_length = 0;

ent::CharacterSlots& slots() {
    return sh::game::session().system.characters;
}

bool is_player(const CharacterRecord* chara) {
    return chara == &slots().player;
}

int state_of(const CharacterRecord& c) {
    return c.collision.state();
}

int part_count(const CharacterRecord& c) {
    return c.collision.state_and_count >> 4;
}

Vec3 position_of(const CharacterRecord& c) {
    return Vec3{c.position.x, c.position.y, c.position.z};
}

void log_line(int level, const std::string& message) {
    if (level != 0) {
        SH_LOG_WARN(MAP, "%s", message.c_str());
    } else {
        SH_LOG_INFO(MAP, "%s", message.c_str());
    }
}

const CellCollision& empty_cell() {
    static const CellCollision cell;
    return cell;
}

// The movement rules of a character type from its data (character.json, params "collision_*"); there is no
// fallback in code.
MoverRules mover_rules(int chara_id) {
    constexpr int kAbsent = INT32_MIN;
    MoverRules rules;
    rules.ground_walker = Pc_CharaParamInt(chara_id, "collision_ground_walker", 0) != 0;
    rules.holes_as_walls = Pc_CharaParamInt(chara_id, "collision_holes_as_walls", 0) != 0;
    const int ground = Pc_CharaParamQ12(chara_id, "collision_off_map_ground", kAbsent);
    rules.off_map_drop = ground != kAbsent;
    if (rules.off_map_drop) {
        rules.off_map_ground = ground;
    }
    rules.crowd_slowdown = Pc_CharaParamQ12(chara_id, "collision_crowd_slowdown", rules.crowd_slowdown);
    return rules;
}

// The placed cells of the window slots that take part in this tick's collision, in slot order; slots whose cell no
// area file has are left out.
void active_cells(std::vector<ActiveCell>& out) {
    static std::vector<std::int32_t> window;
    window.resize(static_cast<std::size_t>(sh::world::stream::slot_count()));
    const std::int32_t count = sh::world::stream::collision_slots(window.data(), static_cast<std::int32_t>(window.size()));
    out.clear();
    for (std::int32_t i = 0; i < count; ++i) {
        Placement* p = world().placement(static_cast<std::size_t>(window[i]));
        if (p == nullptr) {
            continue;
        }
        out.push_back(ActiveCell{PlacedCell{p->cell.get(), p->origin_x, p->origin_z}, p->ledger()});
    }
}

// --- Characters ------------------------------------------------------------------------------------------------

CollisionCandidate candidate_of(const CharacterRecord& c) {
    return CollisionCandidate{c.model.chara_id != 0, sh::input::pick_no_collide(&c) != 0, state_of(c)};
}

// A character's volume for rays: the move cylinder (sight rays) or the bullet box, with the PC bullet hitbox
// layered on.
RayTarget ray_target(const CharacterRecord& c, bool cylinder, int id) {
    CharaVolumeSource source{};
    source.x = c.position.x;
    source.y = c.position.y;
    source.z = c.position.z;
    source.cylinder_radius = c.collision.cylinder.radius;
    source.box_radius = c.collision.cylinder.chara_radius;
    source.box_top = c.collision.box.top;
    source.box_bottom = c.collision.box.bottom;
    source.box_height = c.collision.box.height;
    source.cylinder_offset_x = c.collision.offsets.cylinder.x;
    source.cylinder_offset_z = c.collision.offsets.cylinder.z;
    source.box_offset_x = c.collision.offsets.box.x;
    source.box_offset_z = c.collision.offsets.box.z;
    source.path = cylinder ? VolumePath::Cylinder : VolumePath::Box;

    VolumeInflate inflate{};
    inflate.active = g_PcBulletHitActive != 0 && !cylinder;
    inflate.vert_mul = g_PcBulletVertMul;
    inflate.rad_mul = g_PcBulletRadMul;

    const CharaVolume v = build_chara_volume(source, inflate);
    RayTarget t;
    t.x = v.x;
    t.z = v.z;
    t.low = v.low;
    t.high = v.high;
    t.radius = v.radius;
    t.id = id;
    return t;
}

// --- Movement ----------------------------------------------------------------------------------------------------

struct MoveInputs {
    std::vector<ActiveCell> cells;
    std::vector<MoveObstacle> parts;
    std::vector<MoveBody> bodies;
    std::vector<SlowdownBody> slowdown;
    std::vector<CeilingBox> ceilings;
};

MoveObstacle part_obstacle(ColliderPart& p) {
    MoveObstacle o;
    o.x = p.position.x >> 4;
    o.z = p.position.z >> 4;
    o.low = static_cast<std::int16_t>((p.top + p.position.y) >> 4);
    o.high = static_cast<std::int16_t>((p.bottom + p.position.y) >> 4);
    o.other_radius = static_cast<std::int16_t>(p.radius >> 4);
    o.collision_state = static_cast<std::uint8_t>(p.collision_state);
    o.mark = &p.pass_stamp;
    return o;
}

// The other characters as obstacles (main cylinder, then their parts), the ones that slow the mover down and the
// low ceilings of the active collision triggers.
void build_move_inputs(MoveInputs& in, int mover_state, std::span<CharacterRecord* const> charas) {
    in.parts.clear();
    in.bodies.clear();
    in.slowdown.clear();
    in.ceilings.clear();
    std::size_t total = 0;
    for (CharacterRecord* c : charas) {
        total += static_cast<std::size_t>(part_count(*c));
    }
    in.parts.reserve(total);  // the bodies' spans point into it
    for (CharacterRecord* c : charas) {
        const std::size_t first = in.parts.size();
        for (ColliderPart& p : collider_parts(*c)) {
            in.parts.push_back(part_obstacle(p));
        }
        MoveBody body;
        body.main.x = (c->position.x + c->collision.offsets.cylinder.x) >> 4;
        body.main.z = (c->position.z + c->collision.offsets.cylinder.z) >> 4;
        body.main.low = static_cast<std::int16_t>((c->collision.box.top + c->position.y) >> 4);
        body.main.high = static_cast<std::int16_t>((c->collision.box.bottom + c->position.y) >> 4);
        body.main.other_radius = static_cast<std::int16_t>(c->collision.cylinder.radius >> 4);
        body.main.collision_state = static_cast<std::uint8_t>(state_of(*c));
        body.main.mark = &c->collision.pass_stamp;
        body.parts = std::span<MoveObstacle>(in.parts.data() + first, in.parts.size() - first);
        in.bodies.push_back(body);

        if (!slows_mover(state_of(*c), mover_state, kStates)) {
            continue;
        }
        SlowdownBody b;
        b.top = c->collision.box.top + c->position.y;
        b.bottom = c->collision.box.bottom + c->position.y;
        b.x = c->position.x + c->collision.offsets.cylinder.x;
        b.z = c->position.z + c->collision.offsets.cylinder.z;
        b.radius = c->collision.cylinder.radius;
        b.max_slowdown = mover_rules(c->model.chara_id).crowd_slowdown;
        in.slowdown.push_back(b);
    }
    const ActiveTriggers& active = active_triggers();
    for (int i = 0; i < active.count; ++i) {
        in.ceilings.push_back(trigger_ceiling(unpack_trigger(*active.near[i])));
    }
}

// Watches the player's passes while the collision view is on.
const MoveTap* s_player_tap = nullptr;

// The movement pass of one cylinder against the active cells and the given characters. `tap` watches it.
int slide(MoveRecord& out, const Vec3& offset, const MoveCylinder& cylinder, int mover_state, bool walls_to_ceiling,
          std::span<CharacterRecord* const> charas, const MoveTap* tap) {
    boot_world();
    World& w = world();
    static MoveInputs in;
    active_cells(in.cells);
    build_move_inputs(in, mover_state, charas);
    MoveOptions options;
    options.walls_to_ceiling = walls_to_ceiling;
    options.slope_fix = g_PcConfig.slopeFix != 0;
    options.observer = tap != nullptr ? &tap->observer : nullptr;
    ++w.stats().move_queries;
    const std::uint16_t groups = active_triggers().groups;
    if (tap != nullptr && tap->begin != nullptr) {
        tap->begin(tap->observer.user,
                   MovePassView{cylinder, offset, in.cells, in.bodies, in.ceilings, groups, walls_to_ceiling});
    }
    const MoveResult r = move_cylinder(cylinder, offset, in.cells, in.bodies, in.slowdown, in.ceilings, groups, options);
    if (tap != nullptr && tap->end != nullptr) {
        tap->end(tap->observer.user, r);
    }
    out.offset = r.offset;
    out.surface.height = r.ground_height;
    out.surface.slope_x = r.slope_x;
    out.surface.slope_z = r.slope_z;
    out.surface.ground_type = static_cast<std::int8_t>(r.ground_type);
    out.ceiling_height = r.ceiling_height;
    w.log_stats_on_growth();
    return r.code;
}

// The movement query of one character; kMoveWedged when it is wedged.
int move_pass(MoveRecord& out, const Vec3& offset, CharacterRecord& chara, const MoverRules& rules) {
    if (!ground_known(chara.position.x, chara.position.z)) {
        const OffMapAnswer answer = off_map_answer(rules, chara.position.y);
        out.offset = Vec3{};
        out.surface.slope_z = 0;
        out.surface.slope_x = 0;
        out.surface.ground_type = kGroundDefault;
        out.ceiling_height = answer.ceiling_height;
        out.surface.height = answer.ground_height;
        return 1;
    }

    const int state = state_of(chara);
    MoverShape shape;
    shape.position = position_of(chara);
    shape.offset_x = chara.collision.offsets.cylinder.x;
    shape.offset_z = chara.collision.offsets.cylinder.z;
    shape.top = chara.collision.box.top;
    shape.bottom = chara.collision.box.bottom;
    shape.radius = chara.collision.cylinder.radius;
    shape.collision_state = static_cast<std::uint8_t>(state);
    const MoveCylinder cylinder = mover_cylinder(shape);

    const std::span<CharacterRecord* const> charas = collidable_characters(&chara, true);
    const int code = slide(out, offset, cylinder, state, rules.holes_as_walls, charas,
                           is_player(&chara) ? s_player_tap : nullptr);

    // Console NOCOLLIDE: the asked horizontal movement, with the surveyed ground, tilt and ceiling kept.
    if (sh::input::pick_no_collide(&chara)) {
        out.offset.x = offset.x;
        out.offset.z = offset.z;
    }
    return code;
}

// One ground sample for the wall classification.
struct SurfaceProbe {
    WallProbeSample operator()(std::int32_t x, std::int32_t z) const {
        const SurfaceRecord s = surface_at(x, z);
        return WallProbeSample{s.height, static_cast<std::uint8_t>(s.ground_type)};
    }
};

// --- Rays ----------------------------------------------------------------------------------------------------------

bool trace_against(RayTraceRecord& out, const Vec3& from, const Vec3& offset, bool sight, std::int16_t min_radius,
                   std::span<CharacterRecord* const> charas) {
    boot_world();
    World& w = world();
    static std::vector<ActiveCell> cells;
    active_cells(cells);
    std::vector<RayTarget> targets;
    targets.reserve(charas.size());
    for (std::size_t i = 0; i < charas.size(); ++i) {
        targets.push_back(ray_target(*charas[i], sight, static_cast<int>(i)));
    }
    RayRequest request;
    request.from = from;
    request.offset = offset;
    request.mode = sight ? RayMode::Sight : RayMode::Solid;
    request.min_pillar_radius = min_radius;
    request.enabled_groups = active_triggers().groups;
    ++w.stats().ray_queries;
    const RayHit hit = trace_ray(request, cells, targets);
    if (!hit.rejected) {
        s_last_ray_length = hit.length;
    }
    const RayReport r = report_ray(hit, from, offset, s_last_ray_length);
    out.hit = r.hit;
    out.ground_type = r.ground_type;
    out.point = r.point;
    out.character = r.target >= 0 ? charas[static_cast<std::size_t>(r.target)] : nullptr;
    out.distance = r.distance;
    out.ground_height = r.ground_height;
    out.angle = r.angle;
    w.log_stats_on_growth();
    return out.hit != 0;
}

// --- Line of sight -------------------------------------------------------------------------------------------------

// One probe of the line-of-sight policies, leaving out the asking character: the sight ray (walls, one-sided edges,
// characters with the player) or, `with_characters`, the solid ray with characters.
class SightRays {
public:
    explicit SightRays(const CharacterRecord* exclude) : exclude_(exclude) {}

    SightOutcome operator()(const Vec3& from, const Vec3& offset, bool with_characters) const {
        RayTraceRecord r{};
        const bool blocked = trace(r, from, offset, with_characters ? TraceKind::Chara : TraceKind::Sight, exclude_);
        return SightOutcome{blocked, r.character != nullptr};
    }

private:
    const CharacterRecord* exclude_;
};

struct SightRng {
    std::int32_t operator()() const { return sh::loop::game_random().next15(); }
};

LosHead head_of(const CharacterRecord& c) {
    return LosHead{position_of(c), c.rotation.y, c.collision.box.offset_y};
}

// The low two bits of the current world environment preset's flags; this value flattens the sight band.
constexpr std::uint8_t kHeadBandMask = 0x3;
constexpr std::uint8_t kHeadBandValue = 0x2;

}  // namespace

std::span<ColliderPart> collider_parts(const CharacterRecord& chara) {
    auto* parts = static_cast<ColliderPart*>(const_cast<void*>(chara.collision.records));
    if (parts == nullptr) {
        return {};
    }
    return std::span<ColliderPart>(parts, static_cast<std::size_t>(part_count(chara)));
}

void boot_world() {
    World& w = world();
    if (w.booted()) {
        return;
    }
    w.boot(path_file_reader(ShVfs_FindDataFile), log_line);
    std::atexit(log_world_stats);
}

void place_cell_file(int slot, std::int32_t cell_x, std::int32_t cell_z, int file) {
    boot_world();
    World& w = world();
    if (file == -1) {
        w.unplace(static_cast<std::size_t>(slot));
        return;
    }
    const std::string name = sh::loader::disc_files().name(file);
    if (w.place(static_cast<std::size_t>(slot), name, cell_x, cell_z) == nullptr) {
        log_line(1, "[WORLD] " + name + ": no collision in the unified map file; the cell has none");
    }
}

// The cell and the ledger a ground query at (x, z) walks (`ledger` comes in as the no-cell one); false when the
// query has no cell at all.
static bool ground_site(PlacedCell& placed, VisitLedger& ledger, std::int32_t x, std::int32_t z) {
    const std::int32_t slot = sh::world::stream::collision_slot(x, z);
    if (slot == kNoCollisionSlot) {
        return false;
    }
    // The map's open ground (no cell file there) and a cell no area file has: an empty cell at the origin.
    placed.cell = &empty_cell();
    if (Placement* p = slot >= 0 ? world().placement(static_cast<std::size_t>(slot)) : nullptr) {
        placed.cell = p->cell.get();
        placed.origin_x = p->origin_x;
        placed.origin_z = p->origin_z;
        ledger = p->ledger();
    }
    return true;
}

SurfaceRecord surface_at(std::int32_t x, std::int32_t z) {
    boot_world();
    World& w = world();
    VisitLedger ledger = w.no_cell_ledger();
    PlacedCell placed;
    const PlacedCell* where = ground_site(placed, ledger, x, z) ? &placed : nullptr;
    ++w.stats().ground_queries;
    const GroundSample s = sample_ground(where, ledger, x, z, active_triggers().groups);
    w.log_stats_on_growth();
    return SurfaceRecord{s.height, s.slope_x, s.slope_z, s.ground_type};
}

bool ground_known(std::int32_t x, std::int32_t z) {
    return sh::world::stream::collision_slot(x, z) != kNoCollisionSlot;
}

bool cell_has_collision(std::int32_t x, std::int32_t z) {
    return sh::world::stream::collision_slot(x, z) >= 0;
}

std::span<CharacterRecord* const> collidable_characters(const CharacterRecord* excluded, bool include_player) {
    constexpr int kSlots = ent::kNpcSlotCount + 1;
    static std::array<CharacterRecord*, kSlots> found;
    std::array<CharacterRecord*, kSlots> charas{};
    std::array<CollisionCandidate, kSlots> candidates{};
    int excluded_index = -1;
    for (int i = 0; i < kSlots; ++i) {
        charas[i] = i < kSlots - 1 ? &slots().npcs[i] : &slots().player;
        candidates[i] = candidate_of(*charas[i]);
        if (charas[i] == excluded) {
            excluded_index = i;
        }
    }
    const CollisionCandidate excluded_candidate = excluded != nullptr ? candidate_of(*excluded) : CollisionCandidate{};
    std::size_t count = 0;
    for (std::size_t i : sh::world::collidable_characters(candidates, excluded != nullptr ? &excluded_candidate : nullptr,
                                                          excluded_index, include_player, kStates)) {
        found[count++] = charas[i];
        charas[i]->collision.pass_stamp = 0;
    }
    return std::span<CharacterRecord* const>(found.data(), count);
}

std::int32_t player_loudness(const CharacterRecord& listener, std::int32_t distance, std::int32_t scale) {
    const CharacterRecord& player = slots().player;
    return heard_loudness(scale, player.vars.harry.step_sound, distance,
                          player.position.x - listener.position.x, player.position.z - listener.position.z);
}

int move_character(MoveRecord& out, const Vec3& offset, CharacterRecord& chara) {
    const MoverRules rules = mover_rules(chara.model.chara_id);
    int response = move_pass(out, offset, chara, rules);
    if (response == kMoveWedged) {
        response = 1;
        if (is_player(&chara) && chara.health > 0) {
            ground_push(out, position_of(chara), chara.position.y, chara.rotation.y);
        }
    }

    if (!rules.ground_walker) {
        return response;
    }

    WallScan scan{};
    scan.x = chara.position.x;
    scan.z = chara.position.z;
    scan.height = chara.position.y;
    scan.standing.height = out.surface.height;
    scan.standing.ground_type = static_cast<std::uint8_t>(out.surface.ground_type);
    scan.void_ground_type = kGroundNone;
    const WallVerdict verdict = classify_wall(scan, SurfaceProbe{});

    if (s_player_tap != nullptr && s_player_tap->classified != nullptr && is_player(&chara)) {
        s_player_tap->classified(s_player_tap->observer.user, verdict.kind == WallKind::Wall, verdict.forgive,
                                 verdict.kind == WallKind::Void);
    }

    if (verdict.kind == WallKind::Wall) {
        if (verdict.forgive) {  // a ledge: hold the mover's height instead of dropping it
            out.surface.height = chara.position.y;
        }
    } else if (verdict.kind == WallKind::Void && verdict.void_ground_type != kGroundNone) {
        out.surface.height = verdict.void_height;
        out.surface.ground_type = static_cast<std::int8_t>(kGroundNone);
    }
    return response;
}

void ground_push(MoveRecord& out, const Vec3& position, std::int32_t start_height, std::int32_t heading) {
    RadialScan scan{};
    scan.x = position.x;
    scan.z = position.z;
    scan.start_height = start_height;
    scan.heading = heading;
    const RadialBand band = radial_ground_band(scan, SurfaceProbe{});
    out.offset.x = band.offset_x;
    out.offset.z = band.offset_z;
}

bool trace(RayTraceRecord& out, const Vec3& from, const Vec3& offset, TraceKind kind, const CharacterRecord* excluded) {
    constexpr std::int16_t kThinPillar = 76;  // 0.3 in Q8: thinner pillars do not stop the probe
    switch (kind) {
        case TraceKind::Solid:
            return trace_against(out, from, offset, false, 0, {});
        case TraceKind::Chara:
            return trace_against(out, from, offset, false, 0, collidable_characters(excluded, false));
        case TraceKind::Sight:
            return trace_against(out, from, offset, true, 0, collidable_characters(excluded, true));
        case TraceKind::Probe:
            return trace_against(out, from, offset, true, kThinPillar, {});
    }
    return false;
}

std::int32_t clear_heading(CharacterRecord& chara, std::int32_t dist, std::int32_t heading) {
    return sh::world::clear_heading(head_of(chara), dist, heading, SightRays(&chara), SightRng{});
}

std::int32_t heading_toward(CharacterRecord& chara, std::int32_t dist, std::int32_t to_x, std::int32_t to_z,
                            std::int32_t span, bool clockwise) {
    return sh::world::heading_toward(head_of(chara), dist, to_x, to_z, span, clockwise, SightRays(&chara),
                                     SightRng{});
}

bool sees_player(CharacterRecord& chara, std::int32_t count, std::int32_t base_dist_max, std::int32_t dist_step) {
    const CharacterRecord& player = slots().player;
    const std::uint8_t env = sh::game::session().system.environment.current.preset.flags;
    const LosBody body{position_of(player), player.collision.box.top, player.collision.box.offset_y,
                       (env & kHeadBandMask) == kHeadBandValue};
    return is_visible(head_of(chara), body, count, base_dist_max, dist_step, SightRays(&chara));
}

bool wall_toward_character(CharacterRecord& from, const CharacterRecord& to) {
    return wall_toward_body(position_of(from), to.position.x, to.position.z, SightRays(&from));
}

bool wall_ahead(CharacterRecord& from, std::int32_t dist, std::int32_t heading) {
    return sh::world::wall_ahead(position_of(from), dist, heading, SightRays(&from));
}

bool blocked_ahead(CharacterRecord& from, std::int32_t dist, std::int32_t heading) {
    return sh::world::blocked_ahead(position_of(from), dist, heading, SightRays(&from));
}

void set_player_move_tap(const MoveTap* tap) {
    s_player_tap = tap;
}

std::uint16_t collision_scene(std::vector<ActiveCell>& cells) {
    boot_world();
    active_cells(cells);
    return active_triggers().groups;
}

GroundSample peek_ground(std::int32_t x, std::int32_t z) {
    boot_world();
    PlacedCell placed;
    VisitLedger ledger = world().no_cell_ledger();
    const bool found = ground_site(placed, ledger, x, z);
    // The query stamps the ledger it walks: it gets a copy, so the game's next query finds its own untouched.
    std::uint8_t pass = *ledger.pass;
    std::array<std::uint8_t, 256> marks;
    std::memcpy(marks.data(), ledger.marks.data(), marks.size());
    return sample_ground(found ? &placed : nullptr, VisitLedger{&pass, std::span<std::uint8_t, 256>(marks)}, x, z,
                         active_triggers().groups);
}

void log_world_stats() {
    log_line(0, world().stats_line());
}

}  // namespace sh::world::game
