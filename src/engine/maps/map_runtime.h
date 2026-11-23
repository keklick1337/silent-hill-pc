#pragma once

// The running map as the engine sees it: what a map is,
// loaded from gamedata/maps/<map>/ (MapStore: the data kinds; overlay.json: the map type, the characters it
// updates and the effects it draws; handlers.json: its event handler slots; tables.json: its disc tables). One
// object for the game, map_runtime(); a map load starts it anew (begin) and fills it (load_data and the setters).
//
// Holds: the map type, the scalar header values (music track, ambient bank, light mode, weather), the character
// spawn table with its two group characters, the camera zones, the map points and events with their handler
// slots, the collision triggers, the rooms by position (rooms.json), the map's weather state, its message table
// (with the layers a language, a mod or a feature puts over it), the music event, the character updates and
// effects of the map, the disc tables the map points at (loadable items, Harry's map clips and reaction rows,
// the effect parameter blocks) and the effect slot and blood splat tables. Values keep the widths the disc gives
// them: a write is cut to the stored width like the original's byte fields.

#include "engine/camera/camera_types.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/maps/room_grid.h"
#include "formats/convert/mapdata/mapdata.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::maps {

// engine/maps/weather_control.h; only the .cpp includes it (its effect header clashes with others in the game's
// translation units).
class WeatherControl;

inline constexpr int kSpawnGroups = 2;
inline constexpr int kSpawnSlots = 16;
inline constexpr int kSpawnRowCount = kSpawnGroups * kSpawnSlots;
inline constexpr int kGroupCharas = 4;  // the header's group words; the spawn table uses the first two

inline constexpr int kMapPoints = 256;  // an event names its point by a byte
inline constexpr int kMapEvents = 256;
inline constexpr int kEventHandlers = 256;
inline constexpr int kCollisionTriggers = 200;
inline constexpr int kEffectSlots = 500;   // the largest map's table
inline constexpr int kBloodSplats = 150;

// One row of the spawn table: where a character of the group starts and in which entry state.
struct SpawnPoint {
    std::int32_t x = 0;              // Q12 metres
    std::int32_t z = 0;
    std::int32_t chara = 0;          // > 0: the character; 0: the group's character
    std::uint8_t rotation_y = 0;     // Q8 turn
    std::int8_t flags = 0;           // 0: unused row; else the character's entry state
    std::int8_t difficulty_min = 0;  // 4-bit signed on the disc
};

using MapPointRecord = sh::gameflow::MapPointRecord;

// An event of the map's list as the map data stores it (12 bytes): the flags that gate it, how it triggers and
// what it does. The game's event code reads the record in place.
struct MapEventRecord {
    std::int16_t required_flag = 0;
    std::int16_t done_flag = 0;
    std::uint8_t trigger = 0;  // bits 0-3: trigger type (4-bit signed, -1 ends the list); 4-7: activation
    std::uint8_t point = 0;    // the map point it happens at
    std::uint8_t required_item = 0;
    std::uint8_t pad = 0;
    std::uint32_t action = 0;  // system state 5 bits, parameter 8, options 6, sound pair 5, camera rail 1, map 6,
                               // one more bit

    int trigger_type() const { return static_cast<std::int8_t>(static_cast<std::uint8_t>(trigger << 4)) >> 4; }
};
static_assert(sizeof(MapEventRecord) == 12);

// A collision trigger (4 bytes): end-of-list bit, position (10-bit signed metres), size (4-bit metres), height
// (3-bit half metres).
struct CollisionTriggerRecord {
    std::uint32_t packed = 0;
};
static_assert(sizeof(CollisionTriggerRecord) == 4);

// One slot of the map's effect table (20 bytes): position, the drawer that owns it (0: free), a per-effect byte
// and two per-effect words.
struct EffectSlotRecord {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int16_t y = 0;
    std::uint8_t kind = 0;
    std::uint8_t flags = 0;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
};
static_assert(sizeof(EffectSlotRecord) == 20);

// A row of the map's table of Harry's reaction clips (8 bytes): the map clip, Harry's clip it stands for and the
// keyframe range.
struct PlayerAnimRow {
    std::int16_t status = 0;
    std::int16_t harry_status = 0;
    std::int16_t first_keyframe = 0;
    std::int16_t last_keyframe = 0;
};
static_assert(sizeof(PlayerAnimRow) == 8);

// The disc records packed as the game's structures keep them.
MapPointRecord map_point_of(const ShMapPoint& disc);
MapEventRecord map_event_of(const ShMapEvent& disc);
CollisionTriggerRecord collision_trigger_of(const ShMapCollision& disc);

// What the room rules of rooms.json need from the rest of the game.
class MapRoomsHost {
public:
    virtual ~MapRoomsHost() = default;
    // The bytes of a table of the map's disc data by its data name ("rooms", "street_rooms", "block_rooms");
    // empty (and logged) when the map has none.
    virtual std::span<const std::uint8_t> table(const std::string& map, const std::string& name) = 0;
    // Runs rule(x, z) of the map's hook script; false when it did not run.
    virtual bool call_rule(const std::string& rule, std::int32_t x, std::int32_t z, long long* out) = 0;
};

// One colour of the weather light (snow or rain), as the light lerp writes it.
struct WeatherLight {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t code = 0;
};

// The effects a map draws besides the engine's own (overlay.json "effect_draws").
enum class MapEffect : std::uint8_t {
    GlassShards,      // slot kinds 23..26
    Acid,             // 33 the spit, 34 the drops
    LightGlow,        // 35
    Script,           // 32: effect_slot_draw of the map's world_objects.lua
    ParticleEmitter,  // 8..11, its parameter block EffectBlock::Fire
    SplitHead,        // 5, 7: the boss's drool; Harry pushing the boss
    SmokeColumn,      // 13, 14, its parameter block EffectBlock::Smoke
    Water,            // 27, its parameter block EffectBlock::Water
    Drips,            // 28, the drip table EffectBlock::Drips
    LakeSurface,      // drawn while bit 5 of the map effect flags is set
    Count
};

// The parameter blocks of the map's effects: arrays of the map's disc tables (null on maps without one).
enum class EffectBlock : std::uint8_t { Fire, Smoke, Glass, Water, Drips, Count };

// How the map updates a character type (overlay.json "characters", the pool, the scripts). BehaviourAs: the slot
// keeps its type but runs another type's behaviour (MapRuntime::chara_update_as; the Incubator's slot running
// Unknown23's fight on map7_s03).
enum class CharaUpdate : std::uint8_t { None, Behaviour, BehaviourAlt, BehaviourAs };

// The message tables over the map's own: a language or a mod's text.
enum class MessageLayer : std::uint8_t { Map, Lang, Mod };

class MapRuntime {
public:
    MapRuntime();
    ~MapRuntime();
    MapRuntime(MapRuntime&&) noexcept;
    MapRuntime& operator=(MapRuntime&&) noexcept;

    // The map type names of overlay.json "map_type"; the index is the type.
    static constexpr std::array<std::string_view, 16> kMapTypeNames = {
        "THR", "SC", "SU", "SPR", "SPU", "RSR", "RSU", "APR", "APU", "ER", "ER2", "DR", "DRU", "HP", "HU", "XXX"};
    // -1 for an unknown name.
    static int map_type_of(std::string_view name);

    // The effect names of overlay.json "effect_draws"; the index is the MapEffect.
    static constexpr std::array<std::string_view, static_cast<std::size_t>(MapEffect::Count)> kMapEffectNames = {
        "glass_shards", "acid", "light_glow", "script", "particle_emitter", "split_head", "smoke_column", "water",
        "drips", "lake_surface"};
    // -1 for an unknown name.
    static int map_effect_of(std::string_view name);

    // A new map: everything of the previous one is gone (the blood splats stay: one table for the whole run).
    void begin(std::string_view map);
    // The kinds MapStore found (a kind without a source keeps its cleared values).
    struct Kinds {
        bool header = false;
        bool spawns = false;
        bool points = false;
        bool events = false;
        bool cameras = false;
        bool collision = false;
    };
    void load_data(const sh::convert::mapdata::MapData& data, const Kinds& kinds);

    const std::string& name() const { return name_; }
    // A map has been in place (a load finished); false before the first one.
    bool loaded() const { return loaded_; }
    void set_loaded(bool loaded) { loaded_ = loaded; }

    int map_type() const { return map_type_; }
    void set_map_type(int type) { map_type_ = type; }

    // Header values; the setters cut to the stored width.
    int music_track() const { return music_track_; }
    void set_music_track(int track) { music_track_ = track; }
    int ambient_bank() const { return ambient_bank_; }
    void set_ambient_bank(int bank) { ambient_bank_ = bank; }
    int light_mode() const { return light_mode_; }  // ambient tint and draw distance (3: night)
    int weather() const { return weather_; }        // rain, heavy rain or snow

    // The spawn table: row = group * kSpawnSlots + slot.
    SpawnPoint& spawn(int row) { return spawns_[static_cast<std::size_t>(row)]; }
    const SpawnPoint& spawn(int row) const { return spawns_[static_cast<std::size_t>(row)]; }
    SpawnPoint& spawn(int group, int slot) { return spawn(group * kSpawnSlots + slot); }
    void set_spawn(int row, const ShMapSpawn& disc);
    int group_chara(int group) const { return group_charas_[static_cast<std::size_t>(group)]; }
    void set_group_chara(int group, int chara) { group_charas_[static_cast<std::size_t>(group)] = chara; }

    // The camera zones up to the end marker of the list.
    const std::vector<sh::camera::CameraRoad>& camera_zones() const { return camera_zones_; }

    // The map points (kMapPoints, zero past the map's own).
    MapPointRecord* points() { return points_.data(); }
    // The event list (kMapEvents, zero past the map's own; the list ends at trigger type -1).
    MapEventRecord* events() { return events_.data(); }

    // The event handler slots (handlers.json): a slot with a handler runs the map's events.lua function of that
    // index.
    void set_event_handler(int index, bool present);
    bool event_handler(int index) const;
    // Runs slot `index` when its Lua function did not: nothing (logged once: a slot handled by script whose
    // function is missing does nothing).
    void run_event_handler(int index);

    // The collision triggers of the map (kCollisionTriggers; the list ends at the end-of-list bit).
    CollisionTriggerRecord* collision_triggers() { return collision_.data(); }

    // The map draws world objects through the engine (gamedata/maps/<map>/world_objects.json): its world object
    // hooks (init, update, room spawn pass, after-camera draw) run.
    bool has_world_objects() const { return has_world_objects_; }
    void set_has_world_objects(bool has) { has_world_objects_ = has; }

    // The map has a cast (gamedata/maps/<map>/charas.json): the story-character helpers (path walks, animation
    // lock and state) work on it.
    bool has_cast() const { return has_cast_; }
    void set_has_cast(bool has) { has_cast_ = has; }

    // Rooms by position (gamedata/maps/<map>/rooms.json):
    //   "zones": {"rule": "<hook function>", "table": "<zone table>"} (optional): rule(x, z) of the map's hooks
    //       gives the room (0: none), or with "table" a cell of that table (-1: none) whose value is the room.
    //   "grid": {"min_x", "max_x", "min_z", "max_z" (metres), "empty_room"} (optional): the disc's room grid
    //       (40 m cells), asked when the zones give no room.
    // False (logged under `relative`) when the text is broken; the map then has no rooms. The host must outlive
    // the map.
    bool load_rooms(std::string_view relative, std::string_view text, MapRoomsHost& host);
    bool has_rooms() const { return rooms_loaded_; }
    // The room under a world point (Q12); 0 without rooms.
    std::uint8_t room_at(std::int32_t x, std::int32_t z) const;

    // The weather state of the map: the request words (weather_control()), the wind the blood splats drift with
    // (Q12, the weather's own wind after each update) and the snow and rain light. All start at zero on a map
    // load, as the overlay's zero-filled data did.
    WeatherControl& weather_control() { return *weather_control_; }
    const WeatherControl& weather_control() const { return *weather_control_; }
    bool snowing() const;
    bool raining() const;
    std::int32_t wind(int axis) const { return wind_[axis != 0 ? 1 : 0]; }
    void set_wind(std::int32_t x, std::int32_t z) { wind_ = {x, z}; }
    WeatherLight& snow_light() { return snow_light_; }
    WeatherLight& rain_light() { return rain_light_; }

    // The message table in force: the map's own (gamedata/text), or a layer over it. remember_messages() keeps the
    // table in force; restore_messages() puts it back (a second language install starts from the map's text).
    const char** messages() const { return messages_; }
    MessageLayer message_layer() const { return message_layer_; }
    void set_map_messages(const char** table);
    void install_messages(MessageLayer layer, const char** table);
    void remember_messages();
    void restore_messages();

    // The music event of the map, called every music update (first: the map's first one); none: no event.
    using MusicEvent = void (*)(bool first);
    MusicEvent music_event() const { return music_event_; }
    void set_music_event(MusicEvent event) { music_event_ = event; }

    // How the map updates character type `chara` (None for a type the map does not name).
    CharaUpdate chara_update(int chara) const;
    void set_chara_update(int chara, CharaUpdate update);
    // The type whose behaviour a BehaviourAs type runs (-1 when none was set).
    int chara_update_as(int chara) const;
    void set_chara_update_as(int chara, int as);
    // The map draws the own effects of character type `chara` (overlay.json "character_effects").
    bool chara_effects(int chara) const;
    void set_chara_effects(int chara, bool on);

    // The effects the map draws (overlay.json "effect_draws").
    bool draws(MapEffect effect) const { return (effects_ & (1u << static_cast<unsigned>(effect))) != 0; }
    void set_draws(MapEffect effect) { effects_ |= 1u << static_cast<unsigned>(effect); }

    // Arrays of the map's disc tables the map points at (tables.json storage; null when the map has none):
    // the items the inventory may show (0-terminated), Harry's map clips (the game's animation records, from
    // status 38 on), the rows of his reaction clips, the effect parameter blocks.
    const std::uint8_t* loadable_items() const { return loadable_items_; }
    void set_loadable_items(const std::uint8_t* items) { loadable_items_ = items; }
    void* player_anim_infos() const { return player_anim_infos_; }
    void set_player_anim_infos(void* infos) { player_anim_infos_ = infos; }
    PlayerAnimRow* player_anim_rows() const { return player_anim_rows_; }
    void set_player_anim_rows(PlayerAnimRow* rows) { player_anim_rows_ = rows; }
    void* effect_block(EffectBlock block) const { return effect_blocks_[static_cast<std::size_t>(block)]; }
    void set_effect_block(EffectBlock block, void* data) { effect_blocks_[static_cast<std::size_t>(block)] = data; }

    // The effect slot table (zero on every map load) and the blood splat table (one for the whole run: the map's
    // init marks its splats free); the counts are the map's, from its disc header.
    EffectSlotRecord* effect_slots() { return effect_slots_.data(); }
    int effect_slot_count() const { return effect_slot_count_; }
    void set_effect_slot_count(int count) { effect_slot_count_ = count; }
    std::int16_t* blood_splats();
    int blood_splat_count() const { return blood_splat_count_; }
    void set_blood_splat_count(int count) { blood_splat_count_ = count; }

private:
    struct Rooms {
        std::string rule;
        bool zone_table = false;
        std::span<const std::uint8_t> zone_cells;
        bool has_grid = false;
        RoomGrid grid;
    };

    std::string name_;
    bool loaded_ = false;
    int map_type_ = 0;
    std::int32_t music_track_ = 0;
    std::int32_t ambient_bank_ = 0;
    std::int8_t light_mode_ = 0;
    std::int8_t weather_ = 0;
    std::array<SpawnPoint, kSpawnRowCount> spawns_{};
    std::array<std::int32_t, kGroupCharas> group_charas_{};
    std::vector<sh::camera::CameraRoad> camera_zones_;
    std::array<MapPointRecord, kMapPoints> points_{};
    std::array<MapEventRecord, kMapEvents> events_{};
    std::array<bool, kEventHandlers> handlers_{};
    std::array<CollisionTriggerRecord, kCollisionTriggers> collision_{};
    bool has_world_objects_ = false;
    bool has_cast_ = false;
    bool rooms_loaded_ = false;
    Rooms rooms_;
    MapRoomsHost* rooms_host_ = nullptr;
    std::unique_ptr<WeatherControl> weather_control_;
    std::array<std::int32_t, 2> wind_{};
    WeatherLight snow_light_;
    WeatherLight rain_light_;
    const char** messages_ = nullptr;
    MessageLayer message_layer_ = MessageLayer::Map;
    const char** remembered_messages_ = nullptr;
    MessageLayer remembered_layer_ = MessageLayer::Map;
    MusicEvent music_event_ = nullptr;
    // By run-time character type (any number a mod's character gets).
    std::map<int, CharaUpdate> chara_updates_;
    std::map<int, std::int32_t> chara_updates_as_;
    std::set<int> chara_effects_;
    std::uint32_t effects_ = 0;
    const std::uint8_t* loadable_items_ = nullptr;
    void* player_anim_infos_ = nullptr;
    PlayerAnimRow* player_anim_rows_ = nullptr;
    std::array<void*, static_cast<std::size_t>(EffectBlock::Count)> effect_blocks_{};
    std::array<EffectSlotRecord, kEffectSlots> effect_slots_{};
    int effect_slot_count_ = 0;
    int blood_splat_count_ = 0;
};

MapRuntime& map_runtime();

// Storage of the map variables the maps' scripts keep without a home of their own in the map's tables (Lua map
// variables, MOD_API): zeroed blocks by key that live for the whole run, through map changes. A block never
// moves; it is sized by its first request (at least kMinBlock bytes).
class MapVariables {
public:
    static constexpr std::size_t kMinBlock = 2048;
    // The block of `key`, made on first use; nullptr when a later request needs more than the block has.
    void* block(std::string_view key, std::size_t bytes);
    std::size_t count() const { return blocks_.size(); }

private:
    struct Block {
        std::string key;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<std::unique_ptr<Block>> blocks_;
};

MapVariables& map_variables();

// A camera zone record of the map data, as the stored bit widths read it back.
sh::camera::CameraRoad camera_zone_of(const ShMapCamera& record);
// The zones of a list up to (without) the first record with the end-of-list flag.
std::vector<sh::camera::CameraRoad> camera_zones_of(const ShMapCamera* records, std::size_t count);

}  // namespace sh::maps
