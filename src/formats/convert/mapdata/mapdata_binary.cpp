// The PS1 layouts of the overlay's data (GCC for MIPS, little endian: bit fields fill each container from bit 0).

#include "formats/convert/mapdata/mapdata.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sh::convert::mapdata {

namespace {

constexpr MapLayout kLayouts[] = {
#include "formats/convert/mapdata/map_layout.inc"
};

constexpr std::uint32_t kSpawnsOffset = 0x248;  // charaGroupIds, then charaSpawnInfos[2][16]
constexpr std::uint32_t kSpawnsSize = SH_MAP_CHARA_GROUP_IDS + SH_MAP_SPAWN_GROUPS * SH_MAP_SPAWN_SLOTS * 12;
constexpr std::uint32_t kCamerasOffset = 0x3CC;
constexpr std::uint32_t kCamerasSize = SH_MAP_CAMERA_PATHS * 24;
constexpr std::uint32_t kCollisionOffset = 0xD2C;
constexpr std::uint32_t kCollisionSize = SH_MAP_COLLISION_COUNT * 4;
constexpr std::uint32_t kPointsPtr = 0x1C;

// Alternative whole spawn tables an overlay swaps in by story state:
// kSpawnTableRows 12-byte records each, read by decode_overlay into spawn_variant_rows. The
// names are what gamedata/maps/<map>/spawn_overrides.json refers to; the addresses are this game
// version's (map2_s00: three consecutive tables the street-monster chooser indexes 0..2).
struct SpawnVariantTable {
    std::string_view map;
    std::string_view name;
    std::uint32_t va;
};
constexpr SpawnVariantTable kSpawnVariantTables[] = {
    {"map2_s00", "CafeStreets", 0x800F1CAC},
    {"map2_s00", "SchoolStreets", 0x800F1E2C},
    {"map2_s00", "ChurchStreets", 0x800F1FAC},
};
constexpr std::uint32_t kEventsPtr = 0x24;

std::uint32_t rd32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}
void wr32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}
void wr16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

std::uint32_t bits(std::uint32_t word, unsigned lo, unsigned width) {
    return (word >> lo) & ((1u << width) - 1u);
}
std::int32_t sbits(std::uint32_t word, unsigned lo, unsigned width) {
    const std::uint32_t v = bits(word, lo, width);
    const std::uint32_t sign = 1u << (width - 1);
    return static_cast<std::int32_t>((v ^ sign) - sign);
}
// Out-of-range values are cut to the field width, as a C bit-field assignment does; read_json checks ranges first.
std::uint32_t put(std::uint32_t value, unsigned lo, unsigned width) {
    return (value & ((1u << width) - 1u)) << lo;
}

ShMapSpawn decode_spawn(const std::uint8_t* p) {
    ShMapSpawn s{};
    s.positionX = static_cast<std::int32_t>(rd32(p));
    s.charaId = static_cast<std::int8_t>(p[4]);
    s.rotationY = p[5];
    s.flags = static_cast<std::int8_t>(p[6]);
    s.gameDifficultyMin = static_cast<std::int8_t>(sbits(p[7], 0, 4));
    s.pad7 = static_cast<std::uint8_t>(bits(p[7], 4, 4));
    s.positionZ = static_cast<std::int32_t>(rd32(p + 8));
    return s;
}
void encode_spawn(const ShMapSpawn& s, std::uint8_t* p) {
    wr32(p, static_cast<std::uint32_t>(s.positionX));
    p[4] = static_cast<std::uint8_t>(s.charaId);
    p[5] = s.rotationY;
    p[6] = static_cast<std::uint8_t>(s.flags);
    p[7] = static_cast<std::uint8_t>(put(static_cast<std::uint32_t>(s.gameDifficultyMin), 0, 4) | put(s.pad7, 4, 4));
    wr32(p + 8, static_cast<std::uint32_t>(s.positionZ));
}

ShMapPoint decode_point(const std::uint8_t* p) {
    ShMapPoint m{};
    m.positionX = static_cast<std::int32_t>(rd32(p));
    const std::uint32_t w = rd32(p + 4);
    m.paperMapIdx = static_cast<std::uint8_t>(bits(w, 0, 5));
    m.weatherOnEnter = static_cast<std::uint8_t>(bits(w, 5, 4));
    m.loadingScreenId = static_cast<std::uint8_t>(bits(w, 9, 3));
    m.unused4_12 = static_cast<std::uint8_t>(bits(w, 12, 4));
    m.triggerParam0 = static_cast<std::uint8_t>(bits(w, 16, 8));
    m.triggerParam1 = static_cast<std::uint8_t>(bits(w, 24, 8));
    m.positionZ = static_cast<std::int32_t>(rd32(p + 8));
    return m;
}
void encode_point(const ShMapPoint& m, std::uint8_t* p) {
    wr32(p, static_cast<std::uint32_t>(m.positionX));
    wr32(p + 4, put(m.paperMapIdx, 0, 5) | put(m.weatherOnEnter, 5, 4) | put(m.loadingScreenId, 9, 3) |
                    put(m.unused4_12, 12, 4) | put(m.triggerParam0, 16, 8) | put(m.triggerParam1, 24, 8));
    wr32(p + 8, static_cast<std::uint32_t>(m.positionZ));
}

ShMapEvent decode_event(const std::uint8_t* p) {
    ShMapEvent e{};
    e.requiredFlag = static_cast<std::int16_t>(rd16(p));
    e.completeFlag = static_cast<std::int16_t>(rd16(p + 2));
    e.triggerType = static_cast<std::int8_t>(sbits(p[4], 0, 4));
    e.activationType = static_cast<std::uint8_t>(bits(p[4], 4, 4));
    e.mapPointIdx = p[5];
    e.requiredItemId = p[6];
    e.pad7 = p[7];
    const std::uint32_t w = rd32(p + 8);
    e.sysState = static_cast<std::uint8_t>(bits(w, 0, 5));
    e.eventParam = static_cast<std::uint8_t>(bits(w, 5, 8));
    e.transitionFlags = static_cast<std::uint8_t>(bits(w, 13, 6));
    e.sfxPairIdx = static_cast<std::uint8_t>(bits(w, 19, 5));
    e.setCameraRail = static_cast<std::uint8_t>(bits(w, 24, 1));
    e.mapIdx = static_cast<std::uint8_t>(bits(w, 25, 6));
    e.bit31 = static_cast<std::uint8_t>(bits(w, 31, 1));
    return e;
}
void encode_event(const ShMapEvent& e, std::uint8_t* p) {
    wr16(p, static_cast<std::uint16_t>(e.requiredFlag));
    wr16(p + 2, static_cast<std::uint16_t>(e.completeFlag));
    p[4] = static_cast<std::uint8_t>(put(static_cast<std::uint32_t>(e.triggerType), 0, 4) | put(e.activationType, 4, 4));
    p[5] = e.mapPointIdx;
    p[6] = e.requiredItemId;
    p[7] = e.pad7;
    wr32(p + 8, put(e.sysState, 0, 5) | put(e.eventParam, 5, 8) | put(e.transitionFlags, 13, 6) |
                    put(e.sfxPairIdx, 19, 5) | put(e.setCameraRail, 24, 1) | put(e.mapIdx, 25, 6) |
                    put(e.bit31, 31, 1));
}

ShMapCamera decode_camera(const std::uint8_t* p) {
    ShMapCamera c{};
    for (int i = 0; i < 4; ++i) {
        c.switchArea[i] = static_cast<std::int16_t>(rd16(p + 2 * i));
        c.roadArea[i] = static_cast<std::int16_t>(rd16(p + 8 + 2 * i));
    }
    const std::uint32_t w = rd32(p + 0x10);
    c.flags = static_cast<std::uint8_t>(bits(w, 0, 8));
    c.areaSizeType = static_cast<std::uint8_t>(bits(w, 8, 2));
    c.roadType = static_cast<std::uint8_t>(bits(w, 10, 3));
    c.moveYType = static_cast<std::uint8_t>(bits(w, 13, 3));
    c.roadMaxY = static_cast<std::int8_t>(sbits(w, 16, 8));
    c.roadMinY = static_cast<std::int8_t>(sbits(w, 24, 8));
    const std::uint32_t h = rd16(p + 0x14);
    c.watchOffsetY = static_cast<std::int8_t>(sbits(h, 0, 8));
    c.lensFlare = static_cast<std::uint8_t>(bits(h, 8, 4));
    c.cameraMoveType = static_cast<std::int8_t>(sbits(h, 12, 4));
    c.fixAngleX = static_cast<std::int8_t>(p[0x16]);
    c.fixAngleY = static_cast<std::int8_t>(p[0x17]);
    return c;
}
void encode_camera(const ShMapCamera& c, std::uint8_t* p) {
    for (int i = 0; i < 4; ++i) {
        wr16(p + 2 * i, static_cast<std::uint16_t>(c.switchArea[i]));
        wr16(p + 8 + 2 * i, static_cast<std::uint16_t>(c.roadArea[i]));
    }
    wr32(p + 0x10, put(c.flags, 0, 8) | put(c.areaSizeType, 8, 2) | put(c.roadType, 10, 3) | put(c.moveYType, 13, 3) |
                       put(static_cast<std::uint32_t>(c.roadMaxY), 16, 8) |
                       put(static_cast<std::uint32_t>(c.roadMinY), 24, 8));
    wr16(p + 0x14, static_cast<std::uint16_t>(put(static_cast<std::uint32_t>(c.watchOffsetY), 0, 8) |
                                              put(c.lensFlare, 8, 4) |
                                              put(static_cast<std::uint32_t>(c.cameraMoveType), 12, 4)));
    p[0x16] = static_cast<std::uint8_t>(c.fixAngleX);
    p[0x17] = static_cast<std::uint8_t>(c.fixAngleY);
}

ShMapCollision decode_collision(const std::uint8_t* p) {
    const std::uint32_t w = rd32(p);
    ShMapCollision t{};
    t.isEndOfArray = static_cast<std::uint8_t>(bits(w, 0, 1));
    t.positionX = static_cast<std::int16_t>(sbits(w, 1, 10));
    t.positionZ = static_cast<std::int16_t>(sbits(w, 11, 10));
    t.sizeX = static_cast<std::uint8_t>(bits(w, 21, 4));
    t.sizeZ = static_cast<std::uint8_t>(bits(w, 25, 4));
    t.height = static_cast<std::uint8_t>(bits(w, 29, 3));
    return t;
}
void encode_collision(const ShMapCollision& t, std::uint8_t* p) {
    wr32(p, put(t.isEndOfArray, 0, 1) | put(static_cast<std::uint32_t>(t.positionX), 1, 10) |
                put(static_cast<std::uint32_t>(t.positionZ), 11, 10) | put(t.sizeX, 21, 4) | put(t.sizeZ, 25, 4) |
                put(t.height, 29, 3));
}

std::string hex(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%X", v);
    return buf;
}

}  // namespace

std::span<const MapLayout> map_layouts() {
    return kLayouts;
}

const MapLayout* find_layout(std::string_view map) {
    for (const MapLayout& l : kLayouts) {
        if (l.map == map) {
            return &l;
        }
    }
    return nullptr;
}

std::string overlay_path(std::string_view map) {
    std::string name(map);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });
    return "VIN/" + name + ".BIN";
}

std::string_view kind_name(Kind kind) {
    switch (kind) {
        case Kind::Header: return "header";
        case Kind::Spawns: return "spawns";
        case Kind::Points: return "points";
        case Kind::Events: return "events";
        case Kind::Cameras: return "cameras";
        case Kind::Collision: return "collision";
    }
    return "?";
}

std::vector<Region> regions(const MapLayout& layout) {
    return {
        {Kind::Header, kHeaderOffset + 0x08, 4},
        {Kind::Header, kHeaderOffset + 0x14, 4},
        {Kind::Spawns, kHeaderOffset + kSpawnsOffset, kSpawnsSize},
        {Kind::Points, layout.points_va - kOverlayBase, layout.point_count * 12u},
        {Kind::Events, layout.events_va - kOverlayBase, layout.event_count * 12u},
        {Kind::Cameras, kHeaderOffset + kCamerasOffset, kCamerasSize},
        {Kind::Collision, kHeaderOffset + kCollisionOffset, kCollisionSize},
    };
}

Result<MapData> decode_overlay(std::span<const std::uint8_t> overlay, const MapLayout& layout) {
    for (const Region& r : regions(layout)) {
        if (std::uint64_t(r.offset) + r.size > overlay.size()) {
            return Result<MapData>::fail(std::string(kind_name(r.kind)) + " at " + hex(r.offset) +
                                         " lies past the end of the overlay (" + std::to_string(overlay.size()) +
                                         " bytes)");
        }
    }
    const std::uint8_t* hdr = overlay.data() + kHeaderOffset;
    if (rd32(hdr + kPointsPtr) != layout.points_va || rd32(hdr + kEventsPtr) != layout.events_va) {
        return Result<MapData>::fail("the header points to the map points at " + hex(rd32(hdr + kPointsPtr)) +
                                     " and the events at " + hex(rd32(hdr + kEventsPtr)) + ", expected " +
                                     hex(layout.points_va) + " and " + hex(layout.events_va) +
                                     " (another game version?)");
    }

    MapData d;
    d.map = std::string(layout.map);
    d.header.unused8 = static_cast<std::int8_t>(hdr[0x08]);
    std::memcpy(d.header.pad9, hdr + 0x09, 3);
    d.header.bgmIdx = static_cast<std::int8_t>(hdr[0x14]);
    d.header.ambientAudioIdx = hdr[0x15];
    d.header.field16 = static_cast<std::int8_t>(hdr[0x16]);
    d.header.field17 = static_cast<std::int8_t>(hdr[0x17]);

    const std::uint8_t* sp = hdr + kSpawnsOffset;
    for (int i = 0; i < SH_MAP_CHARA_GROUP_IDS; ++i) {
        d.spawns.charaGroupIds[i] = static_cast<std::int8_t>(sp[i]);
    }
    for (int g = 0; g < SH_MAP_SPAWN_GROUPS; ++g) {
        for (int s = 0; s < SH_MAP_SPAWN_SLOTS; ++s) {
            d.spawns.slots[g][s] = decode_spawn(sp + SH_MAP_CHARA_GROUP_IDS + (g * SH_MAP_SPAWN_SLOTS + s) * 12);
        }
    }
    const std::uint8_t* pts = overlay.data() + (layout.points_va - kOverlayBase);
    for (unsigned i = 0; i < layout.point_count; ++i) {
        d.points.push_back(decode_point(pts + i * 12));
    }
    const std::uint8_t* evs = overlay.data() + (layout.events_va - kOverlayBase);
    for (unsigned i = 0; i < layout.event_count; ++i) {
        d.events.push_back(decode_event(evs + i * 12));
    }
    for (int i = 0; i < SH_MAP_CAMERA_PATHS; ++i) {
        d.cameras[i] = decode_camera(hdr + kCamerasOffset + i * 24);
    }
    for (int i = 0; i < SH_MAP_COLLISION_COUNT; ++i) {
        d.collision[i] = decode_collision(hdr + kCollisionOffset + i * 4);
    }
    for (const SpawnVariantTable& t : kSpawnVariantTables) {
        if (t.map != layout.map) {
            continue;
        }
        const std::uint64_t offset = std::uint64_t(t.va) - kOverlayBase;
        if (offset + kSpawnTableRows * 12u > overlay.size()) {
            return Result<MapData>::fail("spawn table " + std::string(t.name) + " at " + hex(t.va) +
                                         " lies past the end of the overlay");
        }
        SpawnVariantRows& rows = d.spawn_variant_rows.emplace_back();
        rows.name = std::string(t.name);
        for (int i = 0; i < kSpawnTableRows; ++i) {
            rows.rows[i] = decode_spawn(overlay.data() + offset + i * 12u);
        }
    }
    return Result<MapData>::ok(std::move(d));
}

Result<void> encode_kind(const MapData& d, Kind kind, const MapLayout& layout, std::span<std::uint8_t> overlay) {
    for (const Region& r : regions(layout)) {
        if (r.kind == kind && std::uint64_t(r.offset) + r.size > overlay.size()) {
            return Result<void>::fail(std::string(kind_name(kind)) + " lies past the end of the overlay");
        }
    }
    std::uint8_t* hdr = overlay.data() + kHeaderOffset;
    switch (kind) {
        case Kind::Header:
            hdr[0x08] = static_cast<std::uint8_t>(d.header.unused8);
            std::memcpy(hdr + 0x09, d.header.pad9, 3);
            hdr[0x14] = static_cast<std::uint8_t>(d.header.bgmIdx);
            hdr[0x15] = d.header.ambientAudioIdx;
            hdr[0x16] = static_cast<std::uint8_t>(d.header.field16);
            hdr[0x17] = static_cast<std::uint8_t>(d.header.field17);
            break;
        case Kind::Spawns: {
            std::uint8_t* sp = hdr + kSpawnsOffset;
            for (int i = 0; i < SH_MAP_CHARA_GROUP_IDS; ++i) {
                sp[i] = static_cast<std::uint8_t>(d.spawns.charaGroupIds[i]);
            }
            for (int g = 0; g < SH_MAP_SPAWN_GROUPS; ++g) {
                for (int s = 0; s < SH_MAP_SPAWN_SLOTS; ++s) {
                    encode_spawn(d.spawns.slots[g][s], sp + SH_MAP_CHARA_GROUP_IDS + (g * SH_MAP_SPAWN_SLOTS + s) * 12);
                }
            }
            break;
        }
        case Kind::Points:
            if (d.points.size() != layout.point_count) {
                return Result<void>::fail("points: " + std::to_string(d.points.size()) + " entries, the game has " +
                                          std::to_string(layout.point_count));
            }
            for (unsigned i = 0; i < layout.point_count; ++i) {
                encode_point(d.points[i], overlay.data() + (layout.points_va - kOverlayBase) + i * 12);
            }
            break;
        case Kind::Events:
            if (d.events.size() != layout.event_count) {
                return Result<void>::fail("events: " + std::to_string(d.events.size()) + " entries, the game has " +
                                          std::to_string(layout.event_count));
            }
            for (unsigned i = 0; i < layout.event_count; ++i) {
                encode_event(d.events[i], overlay.data() + (layout.events_va - kOverlayBase) + i * 12);
            }
            break;
        case Kind::Cameras:
            for (int i = 0; i < SH_MAP_CAMERA_PATHS; ++i) {
                encode_camera(d.cameras[i], hdr + kCamerasOffset + i * 24);
            }
            break;
        case Kind::Collision:
            for (int i = 0; i < SH_MAP_COLLISION_COUNT; ++i) {
                encode_collision(d.collision[i], hdr + kCollisionOffset + i * 4);
            }
            break;
    }
    return Result<void>::ok();
}

std::vector<std::string> compare_kind(const MapData& d, Kind kind, const MapLayout& layout,
                                      std::span<const std::uint8_t> overlay) {
    std::vector<std::string> out;
    std::vector<std::uint8_t> copy(overlay.begin(), overlay.end());
    const Result<void> enc = encode_kind(d, kind, layout, copy);
    if (!enc) {
        out.push_back(enc.error());
        return out;
    }
    for (const Region& r : regions(layout)) {
        if (r.kind != kind) {
            continue;
        }
        for (std::uint32_t i = 0; i < r.size; ++i) {
            if (copy[r.offset + i] != overlay[r.offset + i]) {
                out.push_back(std::string(kind_name(kind)) + ": byte " + hex(r.offset + i) + " is " +
                              hex(copy[r.offset + i]) + ", the disc has " + hex(overlay[r.offset + i]));
                if (out.size() >= 8) {
                    return out;
                }
            }
        }
    }
    return out;
}

}  // namespace sh::convert::mapdata
