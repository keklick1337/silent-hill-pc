// The map event entry points of the game on the engine: the
// trigger scan of a gameplay frame (Event_Update), the arrival pose after a door (Chara_PositionSet), the
// collision zones and nearby collision triggers of a character, the player's block of the running save, the door
// sound of an area change. The storage is the engine's (map_event_state, the session, the player); the rest of the
// game is reached through game_link.h.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/devtools/tool_mode.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_world.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/arrival.h"
#include "engine/gameflow/collision_zones.h"
#include "engine/gameflow/event_triggers.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/world/runtime_pickups.h"

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;
using sh::game::PlayerVar;

constexpr std::int32_t kNoItem = 0;  // the inventory's "unequipped" item

class GameEventHost final : public gf::EventHost {
public:
    bool flag(std::int16_t flag) const override { return sh::game_state::event_flag(flag); }
    void set_flag(std::int16_t flag) override { sh::game_state::set_event_flag(flag, true); }
    gf::TriggerPoint point(std::uint8_t index) const override {
        const gf::MapPointRecord p = link::map_point(index);
        gf::TriggerPoint out;
        out.x = p.x;
        out.z = p.z;
        out.param0 = p.param0();
        out.param1 = p.param1();
        return out;
    }
};

gf::EventTriggers& triggers() {
    static gf::EventTriggers t = [] {
        gf::EventTriggers made;
        // Game rule of the event data: a done flag that only marks the paper map blocks only exclusive and set-flag
        // events (the original compares the flag with the first map mark's number).
        made.set_rules(gf::EventRules{[](std::int16_t flag) { return sh::ids::flag_is_paper_map_mark(flag); }});
        return made;
    }();
    return t;
}

const std::vector<gf::EventTrigger>& convert_events() {
    static std::vector<gf::EventTrigger> list;
    list.clear();
    const int count = link::map_event_count(gf::EventTriggers::kMaxEvents);
    for (int i = 0; i < count; ++i) {
        const link::EventInfo e = link::map_event(link::map_event_record(i));
        gf::EventTrigger t;
        t.required_flag = e.required_flag;
        t.blocking_flag = e.done_flag;
        t.shape = static_cast<gf::TriggerShape>(e.trigger_type);
        t.activation = static_cast<gf::Activation>(e.activation);
        t.point = e.point;
        t.item = e.item;
        t.state = static_cast<std::uint8_t>(e.sys_state);
        t.param = static_cast<std::uint8_t>(e.param);
        list.push_back(t);
    }
    return list;
}

bool dark_without_light() {
    const auto& env = sh::game::session().system.environment;
    return (env.current.preset.flags & 2) && !env.flashlight_on &&
           ((env.transition[0].preset.flags & 1) || (env.transition[1].preset.flags & 1));
}

gf::TriggerFrame make_frame(bool disable_button_events, std::size_t count) {
    const sh::game::Player& player = sh::game::player();
    gf::TriggerFrame f;
    f.player_x = static_cast<std::int32_t>(player.get(PlayerVar::PositionX));
    f.player_z = static_cast<std::int32_t>(player.get(PlayerVar::PositionZ));
    f.player_heading = static_cast<std::int16_t>(player.get(PlayerVar::RotationY));
    f.tick = sh::game_state::tick_count();
    f.used_item = link::player_used_item();
    f.current_event = link::map_event_index(gf::map_event_state().event, static_cast<int>(count));
    f.buttons_disabled = disable_button_events;
    f.action_pressed = (sh::game::session().game.pads[0].clicked & sh::game::session().game.settings.binds.action) != 0;
    f.player_busy = player.busy();
    f.dark_without_light = dark_without_light();
    f.facing_reach = link::fps_camera() ? 11468 : 3276;  // Q12 2.8 m / 0.8 m
    return f;
}

// What the scan writes (the state before it is the base: the item-used path reads the slot from there).
struct Outputs {
    void* event;
    std::int32_t state;
    std::uint32_t param;
    std::int32_t event_item;
    std::int32_t used_item;
    std::int32_t slot_items[gf::MapEventState::kItemSlots];
    void* slot_events[gf::MapEventState::kItemSlots];
};

Outputs current_outputs() {
    const gf::MapEventState& s = gf::map_event_state();
    Outputs o;
    o.event = s.event;
    o.state = s.sys_state;
    o.param = s.param;
    o.event_item = s.used_item;
    o.used_item = link::player_used_item();
    for (int i = 0; i < gf::MapEventState::kItemSlots; ++i) {
        o.slot_items[i] = s.item_ids[i];
        o.slot_events[i] = s.item_events[i];
    }
    return o;
}

Outputs apply(const Outputs& before, const gf::TriggerResult& r) {
    Outputs o = before;
    if (r.consume_used_item) {
        o.used_item = kNoItem;
    }
    if (r.has_event) {
        if (r.item_slot >= 0) {
            o.event = before.slot_events[r.item_slot];
        } else {
            o.event = r.event >= 0 ? link::map_event_record(r.event) : nullptr;
        }
    }
    if (r.has_state) {
        o.state = r.state;
    }
    if (r.has_param) {
        o.param = r.param;
    }
    if (r.has_event_item) {
        o.event_item = r.event_item;
    }
    const auto& slots = triggers().item_slots();
    for (int i = 0; i < gf::MapEventState::kItemSlots; ++i) {
        o.slot_items[i] = slots[i].item;
        o.slot_events[i] = slots[i].event >= 0 ? link::map_event_record(slots[i].event) : nullptr;
    }
    return o;
}

void store(const Outputs& o) {
    gf::MapEventState& s = gf::map_event_state();
    s.event = o.event;
    s.sys_state = o.state;
    s.param = o.param;
    s.used_item = o.event_item;
    link::set_player_used_item(o.used_item);
    for (int i = 0; i < gf::MapEventState::kItemSlots; ++i) {
        s.item_ids[i] = o.slot_items[i];
        s.item_events[i] = o.slot_events[i];
    }
}

// The boxes of gamedata/config/collision_zones.json; the built-in ones carry the same numbers.
const gf::CollisionZones& zones() {
    static const gf::CollisionZones* loaded = nullptr;
    if (loaded == nullptr) {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::vector<std::string> warnings;
        std::string source;
        const sh::Result<std::string> text =
            gf::read_config_file(link::gamedata_path(), mods, "collision_zones.json", &source, warnings);
        gf::CollisionZones chosen = gf::CollisionZones::builtin();
        if (text) {
            sh::Result<gf::CollisionZones> parsed = gf::CollisionZones::parse(text.value());
            if (parsed) {
                chosen = std::move(parsed.value());
            } else {
                SH_ERR("[COLLZONE] %s (built-in boxes)", parsed.error().c_str());
            }
        } else {
            SH_ERR("[COLLZONE] config/collision_zones.json: %s (built-in boxes)", text.error().c_str());
        }
        loaded = new gf::CollisionZones(std::move(chosen));
    }
    return *loaded;
}

}  // namespace

extern "C" void Event_Update(unsigned int disableButtonEvents) {
    // The modding editor holds the map's events: the frame reports "no event", as one where no trigger fired. The
    // state is written because the scan after gameplay reads it, and what the last event left there is not "none".
    if (sh::devtools::tool_mode().hold_events) {
        gf::map_event_state().sys_state = static_cast<std::int32_t>(gf::GameState::None);
        return;
    }
    const Outputs before = current_outputs();
    const auto& list = convert_events();
    const gf::TriggerFrame frame = make_frame(disableButtonEvents != 0, list.size());
    GameEventHost host;
    const gf::TriggerResult r =
        triggers().update(frame, std::span<const gf::EventTrigger>(list), link::map_events_present(), host);
    store(apply(before, r));
    // Mods' runtime pickups answer the action button only when no map event took it, like one more button event at
    // the end of the map's list (a dark room without the flashlight hides them too).
    if (!sh::world::pickups().empty() && frame.action_pressed && !frame.buttons_disabled && !frame.player_busy &&
        !frame.dark_without_light && r.has_event && r.event < 0 && r.item_slot < 0 &&
        gf::map_event_state().sys_state == static_cast<std::int32_t>(gf::GameState::None)) {
        const sh::game::Player& player = sh::game::player();
        link::runtime_pickups_try_start(static_cast<std::int32_t>(player.get(PlayerVar::PositionX)),
                                        static_cast<std::int32_t>(player.get(PlayerVar::PositionY)),
                                        static_cast<std::int32_t>(player.get(PlayerVar::PositionZ)),
                                        [&](std::int32_t x, std::int32_t z) {
                                            gf::TriggerPoint p;
                                            p.x = x;
                                            p.z = z;
                                            return triggers().facing_point(frame, p);
                                        });
    }
    if (const int message = sh::world::pending_script_message();
        message >= 0 && !frame.player_busy &&
        gf::map_event_state().sys_state == static_cast<std::int32_t>(gf::GameState::None)) {
        sh::world::clear_script_message();
        sh::game_state::start_message_event(message);
    }
}

// The arrival pose after a door: position and heading from the point, the loading screen and the paper map page
// of the area, the camera's heading.
extern "C" void Chara_PositionSet(const void* mapPoint) {
    gf::MapPointRecord record;
    std::memcpy(&record, mapPoint, sizeof(record));

    gf::ArrivalPoint point;
    point.x = record.x;
    point.z = record.z;
    point.heading_q8 = record.param0();
    point.half_width_q4 = record.param1();
    const gf::ArrivalPose pose = gf::arrival_pose(point);
    const std::int32_t rot_y = pose.heading;

    sh::game::Player& player = sh::game::player();
    link::set_player_rotation(0, static_cast<std::int16_t>(rot_y), 0);
    player.set(PlayerVar::PositionY, 0);
    player.set(PlayerVar::PositionX, pose.x);
    player.set(PlayerVar::PositionZ, pose.z);

    sh::game::session().system.scene.loading_screen = static_cast<std::int8_t>(record.loading_screen());

    // Page 24 of the points means "other places"; the other-places page itself keeps the page the save has.
    constexpr int kOtherPlacesPoint = 24;
    if (record.paper_map() == kOtherPlacesPoint) {
        link::set_save_paper_map(sh::game_state::kPaperMapOtherPlaces);
    } else if (record.paper_map() != sh::game_state::kPaperMapOtherPlaces) {
        link::set_save_paper_map(record.paper_map());
    }

    sh::game::session().system.camera_angle_y = static_cast<std::int16_t>(rot_y);

    // The PC third-person camera's orbit yaw is world-space and survives area loads: it is re-based behind the
    // spawn heading as the game camera above, or the first gameplay frame turns Harry back to it.
    link::reseat_third_person_camera(rot_y);

    player.reset_state();
    link::map_room_update();
}

extern "C" void Game_PlayerHeightUpdate(void) {
    link::overlay_update_world_objects();
    sh::game::Player& player = sh::game::player();
    const std::int32_t ground = sh::game_state::ground_height(static_cast<std::int32_t>(player.get(PlayerVar::PositionX)),
                                                              static_cast<std::int32_t>(player.get(PlayerVar::PositionZ)));
    // 8 m is the ground query's "no chunk loaded"; the floor code also keeps the height as its previous target.
    constexpr std::int32_t kNoGround = 8 * 4096;
    if (ground != kNoGround) {
        player.set(PlayerVar::PositionY, ground);
        player.set(PlayerVar::GroundHeight, ground);
    }
}

extern "C" void World_NearbyPlayerCollisionTriggersGet(void) {
    const sh::game::Player& player = sh::game::player();
    link::nearby_collision_triggers(static_cast<std::int32_t>(player.get(PlayerVar::PositionX)),
                                    static_cast<std::int32_t>(player.get(PlayerVar::PositionZ)));
}

extern "C" void Collision_FlagsLocationUpdate(const void* chara) {
    const std::uint16_t flags = link::collision_flags();
    link::set_collision_trigger_flags(flags);
    std::int32_t x = 0;
    std::int32_t z = 0;
    link::chara_record_xz(chara, x, z);
    // Without a map no zone applies.
    link::set_collision_flags(zones().apply(link::world_map_tag(), x, z, flags));
}

extern "C" void Collision_FlagsUpdate(void) {
    link::set_collision_flags(link::collision_trigger_flags());
}

extern "C" std::int32_t Map_TypeGet(void) {
    return link::world_map_type();
}

extern "C" void AreaLoad_TransitionSound(void) {
    extern std::int32_t GameFlow_DoorCloseSound(std::int32_t set);
    sh::game_state::sound_command(GameFlow_DoorCloseSound(sh::game::session().system.scene.sfx_pair));
}

extern "C" std::int8_t GameState_UnkS8Get(void) {
    if (sh::game::session().system.process.process_flags &
        (sh::game::process_flag::kRoomTransition | sh::game::process_flag::kOverlayTransition)) {
        return sh::game::session().system.scene.area_transition_flags;
    }
    return 0;
}

// The player's block of the running save: the location the event saves at, position, heading and health.
extern "C" void SysWork_SavegameUpdatePlayer(void) {
    const sh::game::Player& player = sh::game::player();
    link::SavedPlayer p;
    p.location = static_cast<std::int32_t>(gf::map_event_state().param);
    p.x = static_cast<std::int32_t>(player.get(PlayerVar::PositionX));
    p.z = static_cast<std::int32_t>(player.get(PlayerVar::PositionZ));
    p.rotation_y = static_cast<std::int16_t>(player.get(PlayerVar::RotationY));
    p.health = static_cast<std::int32_t>(player.get(PlayerVar::Health));
    link::store_saved_player(p);
}

extern "C" void GameState_UnkUpdate(void) {
    if (!sh::game::session().sys_flag(sh::game::sys_flag::kDemo)) {
        SysWork_SavegameUpdatePlayer();
        sh::game::session().game.autosave = sh::game::session().game.save;
    }
}

