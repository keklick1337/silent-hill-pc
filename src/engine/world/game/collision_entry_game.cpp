// SPDX-License-Identifier: GPL-3.0-or-later
//
// The C entry-point names of the character world queries (character_world.h). The argument records are the
// engine's, laid out as the callers' own. A `bool` of the C callers is 4 bytes, an angle 16 bits: the parameters
// keep those widths so a caller's value is cut where it was.

#include <cstdint>

#include "engine/runtime/world_map.h"
#include "engine/game_state/session.h"
#include "engine/script/sh_script.h"
#include "engine/world/game/character_world.h"
#include "engine/world/collision_triggers.h"
#include "engine/world/world_script.h"

namespace wg = ::sh::world::game;
namespace wd = ::sh::world;
using wg::CharacterRecord;

extern "C" {

void Collision_SurfaceGet(wg::SurfaceRecord* surface, std::int32_t x, std::int32_t z) {
    *surface = wg::surface_at(x, z);
}

std::int32_t Collision_WallDetect(wg::MoveRecord* out, const wd::Vec3* offset, CharacterRecord* chara) {
    return wg::move_character(*out, *offset, *chara);
}

std::uint32_t Ray_TraceQuery(wg::RayTraceRecord* out, const wd::Vec3* from, const wd::Vec3* to) {
    const auto wrap_sub = [](std::int32_t a, std::int32_t b) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
    };
    const wd::Vec3 offset{wrap_sub(to->x, from->x), wrap_sub(to->y, from->y), wrap_sub(to->z, from->z)};
    return wg::trace(*out, *from, offset, wg::TraceKind::Solid, nullptr);
}

std::uint32_t Ray_CharaTraceQuery(wg::RayTraceRecord* out, const wd::Vec3* from, wd::Vec3* offset,
                                  CharacterRecord* excluded) {
    return wg::trace(*out, *from, *offset, wg::TraceKind::Chara, excluded);
}

std::uint32_t WorldBridge_CellHasCollision(std::int32_t x, std::int32_t z) {
    return wg::cell_has_collision(x, z);
}

}  // extern "C"

// ---------------------------------------------------------------------------------------------------------------
// The script group sh.world.* (engine/world/world_script.h) over the same queries.

namespace {

class GameWorldScript final : public wd::WorldScriptHost {
public:
    wd::ScriptGround ground(std::int32_t x, std::int32_t z) override {
        const wg::SurfaceRecord s = wg::surface_at(x, z);
        return wd::ScriptGround{s.height, s.slope_x, s.slope_z, s.ground_type};
    }

    wd::ScriptRayHit ray(wd::ScriptRay mode, const wd::Vec3& from, const wd::Vec3& to,
                         long long exclude_slot) override {
        // The offset wraps like the game's 32-bit coordinates.
        const wd::Vec3 offset{wrap_sub(to.x, from.x), wrap_sub(to.y, from.y), wrap_sub(to.z, from.z)};
        const CharacterRecord* excluded = exclude_slot >= 0 && exclude_slot <= sh::entities::kNpcSlotCount
                                              ? characters().record(static_cast<int>(exclude_slot))
                                              : nullptr;
        wg::RayTraceRecord trace{};
        switch (mode) {
            case wd::ScriptRay::Solid:
                wg::trace(trace, from, offset, wg::TraceKind::Solid, nullptr);
                break;
            case wd::ScriptRay::Chara:
                wg::trace(trace, from, offset, wg::TraceKind::Chara, excluded);
                break;
            case wd::ScriptRay::Sight:
                wg::trace(trace, from, offset, wg::TraceKind::Sight, excluded);
                break;
            case wd::ScriptRay::Probe:
                wg::trace(trace, from, offset, wg::TraceKind::Probe, nullptr);
                break;
        }
        wd::ScriptRayHit hit;
        hit.hit = trace.hit != 0;
        hit.point = trace.point;
        hit.distance = trace.distance;
        hit.slot = characters().slot_of(trace.character);
        hit.ground_type = trace.ground_type;
        hit.angle = trace.angle;
        hit.ground_height = trace.ground_height;
        return hit;
    }

    bool cell_has_collision(std::int32_t x, std::int32_t z) override { return wg::cell_has_collision(x, z); }

    std::uint32_t collision_groups() override { return wd::active_triggers().groups; }

    std::int32_t speed_zone(std::int32_t x, std::int32_t z) override;

private:
    static std::int32_t wrap_sub(std::int32_t a, std::int32_t b) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
    }
    static sh::entities::CharacterSlots& characters() { return sh::game::session().system.characters; }
};

}  // namespace

std::int32_t GameWorldScript::speed_zone(std::int32_t x, std::int32_t z) {
    return sh::world::world_map().speed_zone(x, z);
}

extern "C" void PcWorldScript_Bind(ShScriptGameApi* api) {
    static GameWorldScript host;
    wd::bind_world_script(*api, host);
}
static ShScriptGameBinderEntry s_ScriptBinder(110, PcWorldScript_Bind); /* registration order: sh_script.h */
