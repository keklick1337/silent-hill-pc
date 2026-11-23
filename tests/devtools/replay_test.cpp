// The replay harness's formats (engine/devtools/replay.h): the input file,
// the recorder's button list and the dump hashes.

#include "engine/devtools/replay.h"

#include "framework/sh_test.h"

#include <cstring>

namespace rp = sh::devtools::replay;

SH_TEST(devtools_replay_input) {
    const rp::Input in = rp::parse_input("r.txt",
                                         "# comment\n"
                                         "\n"
                                         "  10 Cross Up  # hold\n"
                                         "20 -\n"
                                         "end 50\n"
                                         "25\tSquare\tStart\r\n");
    SH_CHECK(in.error.empty());
    SH_CHECK(in.events.size() == 3);
    SH_CHECK(in.events[0].tick == 10 && in.events[0].pressed == ((1 << 14) | (1 << 4)));
    SH_CHECK(in.events[1].tick == 20 && in.events[1].pressed == 0);
    SH_CHECK(in.events[2].tick == 25 && in.events[2].pressed == ((1 << 15) | (1 << 3)));
    SH_CHECK(in.end_tick == 50);

    // Without an end line the run ends 30 ticks after the last event.
    SH_CHECK(rp::parse_input("r", "5 L1\n").end_tick == 35);
    SH_CHECK(rp::parse_input("r", "").end_tick == 0);
}

SH_TEST(devtools_replay_input_errors) {
    SH_CHECK(rp::parse_input("r", "5 Jump\n").error == "r:1: unknown button 'Jump'");
    SH_CHECK(rp::parse_input("r", "5 Up\n5 Down\n").error == "r:2: ticks must increase '5 Down\n'");
    SH_CHECK(rp::parse_input("r", "x Up").error == "r:1: bad tick 'x Up'");
    SH_CHECK(rp::parse_input("r", "0 Up").error == "r:1: bad tick '0 Up'");
    SH_CHECK(rp::parse_input("r", "end x").error == "r:1: bad end tick 'end x'");
}

SH_TEST(devtools_replay_buttons) {
    SH_CHECK(rp::button_list(0) == " -");
    SH_CHECK(rp::button_list((1 << 0) | (1 << 15)) == " Select Square");
    // A recorded list parses back to the same bits.
    const std::uint16_t all = 0xFFFF;
    const rp::Input in = rp::parse_input("r", "1" + rp::button_list(all) + "\n");
    SH_CHECK(in.error.empty() && in.events.size() == 1 && in.events[0].pressed == all);
}

SH_TEST(devtools_replay_hashes) {
    // FNV-1a of "a" (published test vector).
    SH_CHECK(rp::fnv(rp::kFnvBasis, "a", 1) == 0xE40C292Cu);
    const std::int32_t v = -2;
    SH_CHECK(rp::fnv_s32(rp::kFnvBasis, v) == rp::fnv(rp::kFnvBasis, &v, sizeof(v)));

    sh::devtools::ReplayCharacter c;
    c.chara_id = 1;
    c.position[0] = 4096;
    c.rotation[1] = -1024;
    c.health = 409600;
    c.timer = 7;
    const std::uint32_t h = rp::character_hash(rp::kFnvBasis, c);
    c.timer = 8;  // hashed, not printed
    SH_CHECK(rp::character_hash(rp::kFnvBasis, c) != h);
    SH_CHECK(rp::character_text("pl", c) ==
             " pl=1,0,0,0,0,0,0,4096,0,0,0,-1024,0,0,0,0,409600,0,0,0");
}
