/* The maps' run-long words by name (engine/maps/map_globals.h): declared blocks with their first values, bound
 * records winning over them, stable addresses and the malformed declarations. */
#include "engine/maps/map_globals.h"
#include "framework/sh_test.h"

#include <cstdint>
#include <cstring>
#include <string>

using sh::maps::MapGlobals;

SH_TEST(map_globals_declared_blocks_and_first_values) {
    MapGlobals g;
    std::string error;
    SH_CHECK(g.declare_from_json(R"({"globals": {
        "zeros": {"bytes": 256},
        "pos": {"bytes": 12, "type": "s32", "init": [86425, 0, -81920]},
        "ids": {"bytes": 8, "type": "u16", "init": [4109, 65535]}
    }})", &error));
    SH_CHECK(g.declared() == 3);
    const auto* zeros = static_cast<const std::uint8_t*>(g.find("zeros"));
    SH_CHECK(zeros != nullptr);
    bool all_zero = true;
    for (int i = 0; i < 256; ++i) all_zero = all_zero && zeros[i] == 0;
    SH_CHECK(all_zero);
    std::int32_t pos[3];
    std::memcpy(pos, g.find("pos"), sizeof pos);
    SH_CHECK(pos[0] == 86425 && pos[1] == 0 && pos[2] == -81920);
    std::uint16_t ids[4];
    std::memcpy(ids, g.find("ids"), sizeof ids);
    SH_CHECK(ids[0] == 4109 && ids[1] == 65535 && ids[2] == 0 && ids[3] == 0);
    SH_CHECK(g.find("unknown") == nullptr);
    SH_CHECK(g.find("zeros") == zeros);
}

SH_TEST(map_globals_bound_record_wins_and_redeclare_keeps_address) {
    MapGlobals g;
    std::string error;
    SH_CHECK(g.declare_from_json(R"({"globals": {"word": {"bytes": 4, "type": "s32", "init": [7]}}})", &error));
    void* block = g.find("word");
    SH_CHECK(g.declare_from_json(R"({"globals": {"word": {"bytes": 4, "type": "s32", "init": [9]}}})", &error));
    SH_CHECK(g.find("word") == block);
    std::int32_t v = 0;
    std::memcpy(&v, block, 4);
    SH_CHECK(v == 9);
    std::int32_t record = 0;
    g.bind("word", &record);
    SH_CHECK(g.find("word") == &record);
    g.bind("other", &record);
    SH_CHECK(g.find("other") == &record);
}

SH_TEST(map_globals_malformed_declarations) {
    MapGlobals g;
    std::string error;
    SH_CHECK(!g.declare_from_json("{", &error));
    SH_CHECK(!g.declare_from_json(R"({"vars": {}})", &error));
    SH_CHECK(!g.declare_from_json(R"({"globals": {"a": {"bytes": 0}}})", &error));
    SH_CHECK(!g.declare_from_json(R"({"globals": {"a": {"bytes": 4, "type": "f32"}}})", &error));
    SH_CHECK(!g.declare_from_json(R"({"globals": {"a": {"bytes": 2, "type": "s32", "init": [1]}}})", &error));
    SH_CHECK(!g.declare_from_json(R"({"globals": {"a": {"bytes": 4, "init": ["x"]}}})", &error));
    SH_CHECK(!error.empty());
}
