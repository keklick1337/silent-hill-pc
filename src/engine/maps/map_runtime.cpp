#include "engine/maps/map_runtime.h"

#include "engine/log/sh_log.h"
#include "engine/maps/weather_control.h"
#include "formats/common/json.h"

namespace sh::maps {

namespace cam = sh::camera;

namespace {

bool metres(const sh::json::Value& obj, const char* key, std::int32_t* out) {
    const sh::json::Value* v = obj.find(key);
    if (v == nullptr || !v->is_number()) {
        return false;
    }
    // Q12 like the game's constants: the float product, truncated.
    *out = static_cast<std::int32_t>(static_cast<float>(v->number()) * 4096.0f);
    return true;
}

}  // namespace

bool MapRuntime::load_rooms(std::string_view relative_view, std::string_view text, MapRoomsHost& host) {
    rooms_loaded_ = false;
    rooms_ = Rooms{};
    rooms_host_ = nullptr;
    const std::string relative(relative_view);
    auto doc = sh::json::parse(text);
    if (!doc) {
        SH_ERR("[ROOMS] %s: %s", relative.c_str(), doc.error().c_str());
        return false;
    }
    Rooms r;
    if (const sh::json::Value* zones = doc.value().find("zones")) {
        const sh::json::Value* rule = zones->find("rule");
        if (rule == nullptr || !rule->is_string() || rule->string().empty()) {
            SH_ERR("[ROOMS] %s: \"zones\" needs a \"rule\" (a Lua function of the map's hooks)", relative.c_str());
            return false;
        }
        r.rule = rule->string();
        if (const sh::json::Value* table = zones->find("table")) {
            if (!table->is_string()) {
                SH_ERR("[ROOMS] %s: \"zones.table\" is a table name", relative.c_str());
                return false;
            }
            r.zone_table = true;
            r.zone_cells = host.table(name_, table->string());
        }
    }
    if (const sh::json::Value* grid = doc.value().find("grid")) {
        RoomGrid g;
        if (!metres(*grid, "min_x", &g.min_x) || !metres(*grid, "max_x", &g.max_x) ||
            !metres(*grid, "min_z", &g.min_z) || !metres(*grid, "max_z", &g.max_z)) {
            SH_ERR("[ROOMS] %s: \"grid\" needs min_x, max_x, min_z, max_z in metres", relative.c_str());
            return false;
        }
        if (const sh::json::Value* empty = grid->find("empty_room")) {
            if (!empty->is_integer() || empty->integer() < 0 || empty->integer() > 255) {
                SH_ERR("[ROOMS] %s: \"grid.empty_room\" is a room 0..255", relative.c_str());
                return false;
            }
            g.empty_cell_room = static_cast<std::uint8_t>(empty->integer());
        }
        g.cells = host.table(name_, "rooms");
        r.grid = g;
        r.has_grid = true;
    }
    rooms_ = std::move(r);
    rooms_host_ = &host;
    rooms_loaded_ = true;
    return true;
}

std::uint8_t MapRuntime::room_at(std::int32_t x, std::int32_t z) const {
    if (!rooms_loaded_) {
        return 0;
    }
    std::uint8_t room = 0;
    long long r = 0;
    if (!rooms_.rule.empty() && rooms_host_ != nullptr && rooms_host_->call_rule(rooms_.rule, x, z, &r)) {
        if (!rooms_.zone_table) {
            room = static_cast<std::uint8_t>(r);
        } else if (r >= 0 && static_cast<unsigned long long>(r) < rooms_.zone_cells.size()) {
            room = rooms_.zone_cells[static_cast<std::size_t>(r)];
        }
    }
    if (room == 0 && rooms_.has_grid) {
        room = room_grid_lookup(rooms_.grid, x, z);
    }
    return room;
}

MapRuntime::MapRuntime() : weather_control_(std::make_unique<WeatherControl>()) {}
MapRuntime::~MapRuntime() = default;
MapRuntime::MapRuntime(MapRuntime&&) noexcept = default;
MapRuntime& MapRuntime::operator=(MapRuntime&&) noexcept = default;

bool MapRuntime::snowing() const {
    return weather_control_->words().snow_flag != 0;
}

bool MapRuntime::raining() const {
    return weather_control_->words().rain_flag != 0;
}

int MapRuntime::map_type_of(std::string_view name) {
    for (std::size_t i = 0; i < kMapTypeNames.size(); ++i) {
        if (kMapTypeNames[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MapRuntime::begin(std::string_view map) {
    // Once a map was in place the game always has one (its records were rebuilt in place on every load).
    const bool loaded = loaded_;
    *this = MapRuntime{};
    name_ = std::string(map);
    loaded_ = loaded;
}

int MapRuntime::map_effect_of(std::string_view name) {
    for (std::size_t i = 0; i < kMapEffectNames.size(); ++i) {
        if (kMapEffectNames[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MapRuntime::set_event_handler(int index, bool present) {
    if (index >= 0 && index < kEventHandlers) {
        handlers_[static_cast<std::size_t>(index)] = present;
    }
}

bool MapRuntime::event_handler(int index) const {
    if (index < 0 || index >= kEventHandlers) {
        return false;
    }
    return handlers_[static_cast<std::size_t>(index)];
}

void MapRuntime::run_event_handler(int index) {
    if (!event_handler(index)) {
        return;
    }
    // The script dispatcher calls the slot's events.lua function first; reaching here, the function is missing
    // (a broken or trimmed script): the slot does nothing, like an empty handler.
    static bool warned = false;
    if (!warned) {
        warned = true;
        SH_WARN("[MAP] %s: a map event handled by script is missing; the slot does nothing", name_.c_str());
    }
}

void MapRuntime::set_map_messages(const char** table) {
    messages_ = table;
    message_layer_ = MessageLayer::Map;
}

void MapRuntime::install_messages(MessageLayer layer, const char** table) {
    messages_ = table;
    message_layer_ = layer;
}

void MapRuntime::remember_messages() {
    remembered_messages_ = messages_;
    remembered_layer_ = message_layer_;
}

void MapRuntime::restore_messages() {
    if (remembered_messages_ != nullptr) {
        messages_ = remembered_messages_;
        message_layer_ = remembered_layer_;
    }
}

CharaUpdate MapRuntime::chara_update(int chara) const {
    const auto it = chara_updates_.find(chara);
    return it != chara_updates_.end() ? it->second : CharaUpdate::None;
}

void MapRuntime::set_chara_update(int chara, CharaUpdate update) {
    if (chara >= 0) {
        chara_updates_[chara] = update;
    }
}

int MapRuntime::chara_update_as(int chara) const {
    const auto it = chara_updates_as_.find(chara);
    return it != chara_updates_as_.end() ? it->second : -1;
}

void MapRuntime::set_chara_update_as(int chara, int as) {
    if (chara >= 0) {
        chara_updates_as_[chara] = as;
    }
}

bool MapRuntime::chara_effects(int chara) const { return chara_effects_.count(chara) != 0; }

void MapRuntime::set_chara_effects(int chara, bool on) {
    if (chara < 0) {
        return;
    }
    if (on) {
        chara_effects_.insert(chara);
    } else {
        chara_effects_.erase(chara);
    }
}

std::int16_t* MapRuntime::blood_splats() {
    // Outside the object: begin() starts everything else anew, the splats live through map changes.
    static std::array<std::int16_t, kBloodSplats> table{};
    return table.data();
}

MapPointRecord map_point_of(const ShMapPoint& d) {
    MapPointRecord p;
    p.x = d.positionX;
    p.z = d.positionZ;
    // The record's bit widths: paper map 5, weather on enter 4, loading screen 3, unused 4, two parameters of 8.
    p.packed = (static_cast<std::uint32_t>(d.paperMapIdx) & 0x1Fu) |
               ((static_cast<std::uint32_t>(d.weatherOnEnter) & 0xFu) << 5) |
               ((static_cast<std::uint32_t>(d.loadingScreenId) & 0x7u) << 9) |
               ((static_cast<std::uint32_t>(d.unused4_12) & 0xFu) << 12) |
               (static_cast<std::uint32_t>(d.triggerParam0) << 16) | (static_cast<std::uint32_t>(d.triggerParam1) << 24);
    return p;
}

MapEventRecord map_event_of(const ShMapEvent& d) {
    MapEventRecord e;
    e.required_flag = d.requiredFlag;
    e.done_flag = d.completeFlag;
    e.trigger = static_cast<std::uint8_t>((static_cast<std::uint8_t>(d.triggerType) & 0xFu) |
                                          ((static_cast<std::uint8_t>(d.activationType) & 0xFu) << 4));
    e.point = d.mapPointIdx;
    e.required_item = d.requiredItemId;
    e.pad = d.pad7;
    e.action = (static_cast<std::uint32_t>(d.sysState) & 0x1Fu) | (static_cast<std::uint32_t>(d.eventParam) << 5) |
               ((static_cast<std::uint32_t>(d.transitionFlags) & 0x3Fu) << 13) |
               ((static_cast<std::uint32_t>(d.sfxPairIdx) & 0x1Fu) << 19) |
               ((static_cast<std::uint32_t>(d.setCameraRail) & 0x1u) << 24) |
               ((static_cast<std::uint32_t>(d.mapIdx) & 0x3Fu) << 25) | ((static_cast<std::uint32_t>(d.bit31) & 0x1u) << 31);
    return e;
}

CollisionTriggerRecord collision_trigger_of(const ShMapCollision& d) {
    CollisionTriggerRecord t;
    t.packed = (static_cast<std::uint32_t>(d.isEndOfArray) & 0x1u) |
               ((static_cast<std::uint32_t>(static_cast<std::uint16_t>(d.positionX)) & 0x3FFu) << 1) |
               ((static_cast<std::uint32_t>(static_cast<std::uint16_t>(d.positionZ)) & 0x3FFu) << 11) |
               ((static_cast<std::uint32_t>(d.sizeX) & 0xFu) << 21) | ((static_cast<std::uint32_t>(d.sizeZ) & 0xFu) << 25) |
               ((static_cast<std::uint32_t>(d.height) & 0x7u) << 29);
    return t;
}

void MapRuntime::set_spawn(int row, const ShMapSpawn& disc) {
    SpawnPoint& p = spawn(row);
    p.x = disc.positionX;
    p.z = disc.positionZ;
    p.chara = disc.charaId;
    p.rotation_y = disc.rotationY;
    p.flags = disc.flags;
    // A 4-bit signed field on the disc.
    p.difficulty_min = static_cast<std::int8_t>(static_cast<std::int8_t>(disc.gameDifficultyMin << 4) >> 4);
}

void MapRuntime::load_data(const sh::convert::mapdata::MapData& data, const Kinds& kinds) {
    if (kinds.header) {
        music_track_ = data.header.bgmIdx;
        ambient_bank_ = data.header.ambientAudioIdx;
        light_mode_ = data.header.field16;
        weather_ = data.header.field17;
    }
    if (kinds.spawns) {
        for (int g = 0; g < kSpawnGroups; ++g) {
            for (int s = 0; s < kSpawnSlots; ++s) {
                set_spawn(g * kSpawnSlots + s, data.spawns.slots[g][s]);
            }
        }
        for (int i = 0; i < kGroupCharas; ++i) {
            group_charas_[static_cast<std::size_t>(i)] = data.spawns.charaGroupIds[i];
        }
    }
    if (kinds.points) {
        for (std::size_t i = 0; i < data.points.size() && i < points_.size(); ++i) {
            points_[i] = map_point_of(data.points[i]);
        }
    }
    if (kinds.events) {
        for (std::size_t i = 0; i < data.events.size() && i < events_.size(); ++i) {
            events_[i] = map_event_of(data.events[i]);
        }
    }
    if (kinds.cameras) {
        camera_zones_ = camera_zones_of(data.cameras.data(), data.cameras.size());
    }
    if (kinds.collision) {
        for (std::size_t i = 0; i < data.collision.size() && i < collision_.size(); ++i) {
            collision_[i] = collision_trigger_of(data.collision[i]);
        }
    }
}

MapRuntime& map_runtime() {
    static MapRuntime instance;
    return instance;
}

void* MapVariables::block(std::string_view key, std::size_t bytes) {
    for (const std::unique_ptr<Block>& b : blocks_) {
        if (b->key == key) {
            if (bytes > b->bytes.size()) {
                SH_ERR("[MAPVAR] '%.*s': %zu bytes asked, the block has %zu", static_cast<int>(key.size()), key.data(),
                       bytes, b->bytes.size());
                return nullptr;
            }
            return b->bytes.data();
        }
    }
    auto b = std::make_unique<Block>();
    b->key = std::string(key);
    b->bytes.assign(bytes > kMinBlock ? bytes : kMinBlock, 0);
    blocks_.push_back(std::move(b));
    return blocks_.back()->bytes.data();
}

MapVariables& map_variables() {
    static MapVariables instance;
    return instance;
}

cam::CameraRoad camera_zone_of(const ShMapCamera& r) {
    cam::CameraRoad out;
    out.switch_area = cam::Area{r.switchArea[0], r.switchArea[1], r.switchArea[2], r.switchArea[3]};
    out.road_area = cam::Area{r.roadArea[0], r.roadArea[1], r.roadArea[2], r.roadArea[3]};
    // The record's packed widths: flags 8 bits, area size 2, road type 3, Y move 3, lens flare 4 (all unsigned),
    // camera move 4 bits signed.
    out.flags = r.flags;
    out.area_size = static_cast<cam::AreaSize>(r.areaSizeType & 0x3);
    out.road_type = static_cast<cam::RoadType>(r.roadType & 0x7);
    out.move_y_type = static_cast<std::uint8_t>(r.moveYType & 0x7);
    out.road_max_y = r.roadMaxY;
    out.road_min_y = r.roadMinY;
    out.watch_offset_y = r.watchOffsetY;
    out.lens_flare = static_cast<std::uint8_t>(r.lensFlare & 0xF);
    out.camera_move = static_cast<cam::CameraMove>(static_cast<std::int8_t>(static_cast<std::uint8_t>(r.cameraMoveType) << 4) >> 4);
    out.fix_angle_x = r.fixAngleX;
    out.fix_angle_y = r.fixAngleY;
    return out;
}

std::vector<cam::CameraRoad> camera_zones_of(const ShMapCamera* records, std::size_t count) {
    std::vector<cam::CameraRoad> zones;
    for (std::size_t i = 0; i < count && (records[i].flags & cam::road_flags::end_marker) == 0; ++i) {
        zones.push_back(camera_zone_of(records[i]));
    }
    return zones;
}

}  // namespace sh::maps
