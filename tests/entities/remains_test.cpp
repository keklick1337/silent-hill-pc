// Persistent remains (engine/entities/remains.h): the store, its JSON and the reach test.

#include "engine/entities/remains.h"
#include "framework/sh_test.h"

namespace {

namespace ent = sh::entities;

ent::CorpseRemains groaner(std::int32_t x, std::int32_t z) {
    ent::CorpseRemains c;
    c.chara = "silenthill:Groaner";
    c.room = 13;
    c.position = {x, -12, z};
    c.rotation = {0, 1024, -3};
    c.clip_status = 39;
    c.clip_time = 450560;
    c.keyframe = 110;
    c.palette = 2;
    c.timer = -7;
    c.variant = 3;
    return c;
}

ent::BloodStain pool(std::int32_t x, std::int32_t z) {
    ent::BloodStain s;
    s.room = 13;
    s.position = {x, 0, z};
    s.size = 126;
    s.color = 5;
    s.shape = 2;
    return s;
}

}  // namespace

SH_TEST(remains_json_round_trip_is_stable) {
    ent::RemainsStore store;
    SH_CHECK(store.empty() && store.find("map1_s00") == nullptr);
    store.add_corpse("map1_s00", groaner(569591, 83133));
    store.add_stain("map1_s00", pool(569769, 83136));
    store.add_stain("map1_s00", pool(569900, 83000));
    store.add_corpse("map0_s00", groaner(-4096, 8192));

    const std::string text = store.to_json();
    SH_CHECK(text.find("\"sh-remains/1\"") != std::string::npos);
    // Maps in name order whatever the order they were filled in.
    SH_CHECK(text.find("map0_s00") < text.find("map1_s00"));
    auto back = ent::RemainsStore::from_json(text);
    SH_CHECK(static_cast<bool>(back));
    SH_CHECK(back.value() == store);
    SH_CHECK(back.value().to_json() == text);
    const ent::MapRemains* school = back.value().find("map1_s00");
    SH_CHECK(school != nullptr && school->corpses.size() == 1 && school->stains.size() == 2);
    SH_CHECK(school->corpses[0] == groaner(569591, 83133));
    SH_CHECK(school->stains[1] == pool(569900, 83000));
}

SH_TEST(remains_same_body_replaces_and_patches_stay_apart) {
    ent::RemainsStore store;
    const std::uint64_t start = store.revision();
    SH_CHECK(store.add_corpse("m", groaner(40960, 40960)));
    // The same body noted again a hand away (it settled) replaces the first note.
    ent::CorpseRemains settled = groaner(40960 + 300, 40960 - 200);
    settled.keyframe = 111;
    SH_CHECK(!store.add_corpse("m", settled));
    SH_CHECK(store.find("m")->corpses.size() == 1 && store.find("m")->corpses[0].keyframe == 111);
    // Another body a metre away, and another kind at the same spot, are their own.
    SH_CHECK(store.add_corpse("m", groaner(40960 + 4096, 40960)));
    ent::CorpseRemains other = groaner(40960, 40960);
    other.chara = "silenthill:Romper";
    SH_CHECK(store.add_corpse("m", other));
    SH_CHECK(store.find("m")->corpses.size() == 3);

    // A body pool is several patches a few centimetres apart: each is kept, only the very same one is a repeat.
    SH_CHECK(store.add_stain("m", pool(1000, 1000)));
    SH_CHECK(store.add_stain("m", pool(1100, 1000)));
    SH_CHECK(!store.add_stain("m", pool(1000, 1000)));
    SH_CHECK(store.find("m")->stains.size() == 2);
    SH_CHECK(store.revision() > start);

    store.clear();
    SH_CHECK(store.empty());
}

SH_TEST(remains_oldest_go_past_the_cap) {
    ent::RemainsStore store;
    const int extra = 5;
    for (int i = 0; i < static_cast<int>(ent::RemainsStore::kMaxCorpses) + extra; ++i) {
        store.add_corpse("m", groaner(i * 8192, 0));
    }
    for (int i = 0; i < static_cast<int>(ent::RemainsStore::kMaxStains) + extra; ++i) {
        store.add_stain("m", pool(i * 64, 0));
    }
    const ent::MapRemains* m = store.find("m");
    SH_CHECK(m->corpses.size() == ent::RemainsStore::kMaxCorpses && m->stains.size() == ent::RemainsStore::kMaxStains);
    SH_CHECK(m->corpses.front().position.x == extra * 8192 && m->stains.front().position.x == extra * 64);
}

SH_TEST(remains_json_rejects_damage) {
    SH_CHECK(!ent::RemainsStore::from_json("{"));
    SH_CHECK(!ent::RemainsStore::from_json("{\"format\": \"sh-savegame/1\", \"maps\": []}"));
    SH_CHECK(!ent::RemainsStore::from_json("{\"format\": \"sh-remains/1\"}"));
    SH_CHECK(!ent::RemainsStore::from_json(
        "{\"format\": \"sh-remains/1\", \"maps\": [{\"map\": \"m\", \"corpses\": [{\"room\": 1}]}]}"));
    // A position that is not three numbers, a keyframe that does not fit.
    SH_CHECK(!ent::RemainsStore::from_json(
        "{\"format\": \"sh-remains/1\", \"maps\": [{\"map\": \"m\", \"corpses\": [{\"chara\": \"a\", \"position\": [1, 2],"
        " \"rotation\": [0, 0, 0], \"pose\": {\"clip\": 1, \"time\": 2, \"keyframe\": 3}}]}]}"));
    SH_CHECK(!ent::RemainsStore::from_json(
        "{\"format\": \"sh-remains/1\", \"maps\": [{\"map\": \"m\", \"corpses\": [{\"chara\": \"a\", \"position\": [1, 2, 3],"
        " \"rotation\": [0, 0, 0], \"pose\": {\"clip\": 1, \"time\": 2, \"keyframe\": 99999}}]}]}"));
    // The optional members may be missing; an empty document is an empty store.
    auto bare = ent::RemainsStore::from_json(
        "{\"format\": \"sh-remains/1\", \"maps\": [{\"map\": \"m\", \"corpses\": [{\"chara\": \"a\", \"position\": [1, 2, 3],"
        " \"rotation\": [0, 0, 0], \"pose\": {\"clip\": 1, \"time\": 2, \"keyframe\": 3}}],"
        " \"stains\": [{\"position\": [4, 5, 6], \"size\": 7}]}]}");
    SH_CHECK(static_cast<bool>(bare) && bare.value().find("m")->corpses[0].keyframe == 3 &&
             bare.value().find("m")->stains[0].size == 7);
    auto none = ent::RemainsStore::from_json("{\"format\": \"sh-remains/1\", \"maps\": []}");
    SH_CHECK(static_cast<bool>(none) && none.value().empty());
}

SH_TEST(remains_reach_is_a_circle_on_the_ground) {
    const ent::RemainsReach reach;
    SH_CHECK(reach.hide > reach.show);
    SH_CHECK(ent::remains_within(0, 0, 0, 0, reach.show));
    SH_CHECK(ent::remains_within(22 * 4096, 0, 0, 0, reach.show));
    SH_CHECK(!ent::remains_within(22 * 4096 + 1, 0, 0, 0, reach.show));
    SH_CHECK(!ent::remains_within(17 * 4096, 17 * 4096, 0, 0, reach.show));
    SH_CHECK(ent::remains_within(17 * 4096, 17 * 4096, 0, 0, reach.hide));
    // Far apart on the map: no overflow.
    SH_CHECK(!ent::remains_within(400 * 4096, -400 * 4096, -400 * 4096, 400 * 4096, reach.hide));
}
