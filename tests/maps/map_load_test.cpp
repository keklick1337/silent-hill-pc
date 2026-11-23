/* What a map load reads into the running map besides its data kinds (engine/maps/map_description.h,
 * map_tables.h, map_runtime.h): overlay.json and handlers.json, the disc tables with their relocation, the
 * packed records, the event layer, the message layers and the character updates. */
#include "engine/maps/map_description.h"
#include "engine/maps/map_runtime.h"
#include "engine/maps/map_tables.h"
#include "framework/sh_test.h"

#include <cstring>
#include <string>
#include <vector>

using sh::maps::CharaUpdate;
using sh::maps::MapEffect;
using sh::maps::MapRuntime;
using sh::maps::MessageLayer;

SH_TEST(map_description_overlay_json) {
    MapRuntime rt;
    rt.begin("map1_s05");
    SH_CHECK(sh::maps::apply_overlay_description(
        rt, "maps/map1_s05/overlay.json",
        R"({"map_type": "SC", "characters": ["SplitHead"], "character_effects": ["Incubus"],
            "effect_draws": ["split_head", "particle_emitter"]})"));
    SH_CHECK(rt.map_type() == 1);
    SH_CHECK(rt.draws(MapEffect::SplitHead) && rt.draws(MapEffect::ParticleEmitter));
    SH_CHECK(!rt.draws(MapEffect::Water) && !rt.draws(MapEffect::LakeSurface));
    int behaviours = 0;
    for (int c = 0; c < 256; ++c) {
        behaviours += rt.chara_update(c) == CharaUpdate::Behaviour;
    }
    SH_CHECK(behaviours == 1);
    SH_CHECK(!sh::maps::apply_overlay_description(rt, "x", R"({"map_type": "NOPE"})"));
    SH_CHECK(!sh::maps::apply_overlay_description(rt, "x", R"({"map_type": "SC", "effect_draws": ["fog"]})"));
    SH_CHECK(!sh::maps::apply_overlay_description(rt, "x", R"({"map_type": "SC", "characters": ["Nobody"]})"));
    SH_CHECK(MapRuntime::map_effect_of("lake_surface") == static_cast<int>(MapEffect::LakeSurface));
    SH_CHECK(MapRuntime::map_effect_of("") == -1);
}

SH_TEST(map_description_handler_slots) {
    MapRuntime rt;
    rt.begin("map0_s01");
    SH_CHECK(sh::maps::apply_event_handlers(
        rt, "h", R"({"handlers": [{"index": 0, "shape": null}, {"index": 2, "shape": "script"},
                                  {"index": 255, "shape": "cutscene"}]})"));
    SH_CHECK(!rt.event_handler(0) && !rt.event_handler(1) && rt.event_handler(2) && rt.event_handler(255));
    SH_CHECK(!rt.event_handler(-1) && !rt.event_handler(256));
    SH_CHECK(!sh::maps::apply_event_handlers(rt, "h", R"({"handlers": [{"index": 256, "shape": "script"}]})"));
    SH_CHECK(!sh::maps::apply_event_handlers(rt, "h", R"({})"));
}

SH_TEST(map_runtime_event_handlers_reset_with_the_map) {
    MapRuntime rt;
    rt.begin("map2_s00");
    rt.set_event_handler(3, true);
    rt.set_event_handler(40, true);
    rt.events()[0].required_flag = 7;
    SH_CHECK(rt.event_handler(3) && rt.event_handler(40) && !rt.event_handler(12));
    rt.run_event_handler(3);  // a script slot whose function is missing: nothing
    rt.begin("map2_s01");
    SH_CHECK(!rt.event_handler(3) && !rt.event_handler(40) && rt.events()[0].required_flag == 0);
}

SH_TEST(map_runtime_message_layers) {
    MapRuntime rt;
    const char* base[] = {"a", "b"};
    const char* lang[] = {"x", "y"};
    const char* mod[] = {"m", "n"};
    rt.set_map_messages(base);
    SH_CHECK(rt.messages() == base && rt.message_layer() == MessageLayer::Map);
    rt.remember_messages();
    rt.install_messages(MessageLayer::Lang, lang);
    SH_CHECK(rt.messages() == lang && rt.message_layer() == MessageLayer::Lang);
    rt.restore_messages();
    SH_CHECK(rt.messages() == base && rt.message_layer() == MessageLayer::Map);
    rt.install_messages(MessageLayer::Mod, mod);
    SH_CHECK(rt.message_layer() == MessageLayer::Mod && rt.messages()[1][0] == 'n');
}

SH_TEST(map_runtime_chara_updates_and_blood_splats) {
    MapRuntime rt;
    rt.begin("map7_s03");
    rt.set_chara_update(5, CharaUpdate::BehaviourAs);
    rt.set_chara_update_as(5, 23);
    rt.set_chara_effects(44, true);
    SH_CHECK(rt.chara_update(5) == CharaUpdate::BehaviourAs && rt.chara_update(6) == CharaUpdate::None);
    SH_CHECK(rt.chara_update_as(5) == 23 && rt.chara_update_as(6) == -1);
    SH_CHECK(rt.chara_update(45) == CharaUpdate::None && rt.chara_update(-1) == CharaUpdate::None);
    SH_CHECK(rt.chara_effects(44) && !rt.chara_effects(45));
    rt.blood_splats()[3] = 77;
    rt.effect_slots()[4].kind = 9;
    rt.set_loaded(true);
    rt.begin("map0_s00");
    SH_CHECK(rt.blood_splats()[3] == 77);  // one table for the whole run
    SH_CHECK(rt.effect_slots()[4].kind == 0 && rt.chara_update(5) == CharaUpdate::None);
    SH_CHECK(rt.loaded());
}

SH_TEST(map_runtime_packs_disc_records) {
    ShMapPoint p{};
    p.positionX = -4096;
    p.positionZ = 8192;
    p.paperMapIdx = 0x25;      // 5 bits: 5
    p.weatherOnEnter = 0x13;   // 4 bits: 3
    p.loadingScreenId = 6;
    p.unused4_12 = 0;
    p.triggerParam0 = 0xC0;
    p.triggerParam1 = 0x11;
    const sh::maps::MapPointRecord r = sh::maps::map_point_of(p);
    SH_CHECK(r.x == -4096 && r.z == 8192 && r.paper_map() == 5 && r.weather_on_enter() == 3 && r.loading_screen() == 6);
    SH_CHECK(r.param0() == 0xC0 && r.param1() == 0x11);

    ShMapEvent e{};
    e.requiredFlag = -2;
    e.completeFlag = 300;
    e.triggerType = -1;
    e.activationType = 3;
    e.mapPointIdx = 200;
    e.sysState = 10;
    e.eventParam = 0xAB;
    e.mapIdx = 42;
    e.bit31 = 1;
    const sh::maps::MapEventRecord er = sh::maps::map_event_of(e);
    SH_CHECK(er.trigger_type() == -1 && (er.trigger >> 4) == 3 && er.point == 200);
    SH_CHECK((er.action & 0x1F) == 10 && ((er.action >> 5) & 0xFF) == 0xAB && ((er.action >> 25) & 0x3F) == 42);
    SH_CHECK((er.action >> 31) == 1);

    ShMapCollision c{};
    c.isEndOfArray = 1;
    c.positionX = -3;
    c.positionZ = 511;
    c.sizeX = 15;
    c.sizeZ = 2;
    c.height = 7;
    const std::uint32_t w = sh::maps::collision_trigger_of(c).packed;
    SH_CHECK((w & 1) == 1 && ((w >> 1) & 0x3FF) == 0x3FD && ((w >> 11) & 0x3FF) == 511);
    SH_CHECK(((w >> 21) & 0xF) == 15 && ((w >> 25) & 0xF) == 2 && (w >> 29) == 7);
}

namespace {

struct Element {
    void* pointer;
    std::int16_t value;
};

constexpr sh::maps::TableField kElementFields[] = {
    {0, offsetof(Element, pointer), 4, sh::maps::FieldFill::Pointer},
    {4, offsetof(Element, value), 2, sh::maps::FieldFill::Copy},
};

int g_variable = 0;

class TestTypes final : public sh::maps::MapTableTypes {
public:
    const sh::maps::TableType* relocating(std::string_view type) const override {
        static const sh::maps::TableType element = {8, sizeof(Element), kElementFields};
        return type == "Element" ? &element : nullptr;
    }
    unsigned flat_size(std::string_view type) const override { return type == "u16" ? 2 : type == "u8" ? 1 : 0; }
    unsigned port_size(std::string_view type) const override { return type == "Big" ? 64 : 0; }
    void* function(std::string_view) const override { return nullptr; }
    void* variable(std::string_view name) const override { return name == "live" ? &g_variable : nullptr; }
};

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) { std::memcpy(&b[at], &v, 4); }

}  // namespace

SH_TEST(map_tables_registry_fill_and_header) {
    const TestTypes types;
    sh::maps::MapTables t;
    const char* json = R"({"bin": "VIN/T.BIN", "symbols": [
        {"symbol": "rooms", "va": "0x800C9778", "type": "u16", "count": 4, "bytes": 6},
        {"symbol": "links", "va": "0x800C9790", "type": "Element", "count": 3},
        {"symbol": "scratch", "port_word": true, "type": "Big", "count": 2}],
        "variables": [{"symbol": "live", "va": "0x80010000"}]})";
    SH_CHECK(t.load_registry("tmap", "maps/tmap/tables.json", json, types));
    SH_CHECK(t.loaded() && t.map() == "tmap" && t.bin() == "VIN/T.BIN");

    std::vector<std::uint8_t> bin(0x300, 0);
    for (int i = 0; i < 8; ++i) {
        bin[0x200 + i] = static_cast<std::uint8_t>(0x10 + i);  // rooms
    }
    put32(bin, 0x218, 0x800C977Au);  // links[0] -> rooms + 2 bytes
    bin[0x21C] = 5;
    put32(bin, 0x220, 0x80010000u);  // links[1] -> the live variable
    put32(bin, 0x228, 0x800C9798u);  // links[2] -> links[1]
    put32(bin, 0x30, 0x800C9778u);   // header word -> rooms
    put32(bin, 0x34, 0x800C9790u);   // header word -> links
    put32(bin, 0x38, 0x800CA000u);   // header word -> outside the data
    bin[0x54] = 0x2C;
    bin[0x55] = 0x01;
    SH_CHECK(t.fill(bin));

    std::uint32_t bytes = 0;
    auto* rooms = static_cast<std::uint8_t*>(t.array("rooms", &bytes));
    SH_CHECK(rooms != nullptr && bytes == 6);
    SH_CHECK(rooms[0] == 0x10 && rooms[5] == 0x15 && rooms[6] == 0);  // only the given prefix
    auto* links = static_cast<Element*>(t.array("links"));
    SH_CHECK(links[0].pointer == rooms + 2 && links[0].value == 5);
    SH_CHECK(links[1].pointer == &g_variable && links[2].pointer == &links[1]);
    SH_CHECK(t.array("scratch") != nullptr && t.array("nothing") == nullptr);
    SH_CHECK(t.header_pointer(0x30) == rooms && t.header_pointer(0x34) == links);
    SH_CHECK(t.header_pointer(0x38) == nullptr && t.header_pointer(0x3C) == nullptr);
    std::int16_t n = 0;
    SH_CHECK(t.header_s16(0x54, &n) && n == 300);
    SH_CHECK(!t.header_s16(0x1FF, &n));

    SH_CHECK(!t.load_registry("tmap", "x", R"({"bin": "B", "symbols": [{"symbol": "a", "va": "zz", "type": "u8",
                                                "count": 1}]})", types));
    SH_CHECK(!t.loaded());
    SH_CHECK(!t.load_registry("tmap", "x", R"({"bin": "B", "symbols": [{"symbol": "a", "va": "0x800C9578",
                                                "type": "u8", "count": 1, "bytes": 2}]})", types));
    SH_CHECK(!t.load_registry("tmap", "x", R"({"bin": "B", "symbols": [{"symbol": "a", "va": "0x800C9578",
                                                "type": "f32", "count": 1}]})", types));
}
