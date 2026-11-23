// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The draw packet arena and its monitor (engine/gameflow/packet_arena.h): the parameters, the canary layout, the
// monitor's reports over recorded rounds and over a long seeded run against its recorded digest, and the rules in
// words.

#include <cstdint>
#include <string>
#include <vector>

#include "framework/golden.h"
#include "framework/sh_test.h"

#include "engine/gameflow/packet_arena.h"

using sh::gameflow::PacketArena;
using sh::gameflow::PacketArenaMonitor;
using sh::gameflow::PacketArenaParams;
using sh::gameflow::PacketArenaReport;

namespace {

constexpr const char* kTable = "gameflow/packet_arena.txt";

// The reports of one frame as the table writes them.
std::string events_of(const PacketArenaReport& r) {
    std::string out;
    const auto add = [&](const std::string& e) {
        if (!out.empty()) out += ',';
        out += e;
    };
    if (r.cull_stat) {
        add("cull:" + std::to_string(r.cull_avg) + ":" + std::to_string(r.cull_frames) + ":" + std::to_string(r.peak));
    }
    for (int i = 0; i < 2; ++i) {
        if (r.overrun_new[i]) {
            add("overrun:" + std::to_string(i) + ":" + std::to_string(r.used) + ":" + std::to_string(r.peak));
        }
    }
    if (r.high_water_new) add("high:" + std::to_string(r.peak));
    return out.empty() ? "-" : out;
}

}  // namespace

SH_TEST(packet_arena_params_parse) {
    SH_CHECK(PacketArenaParams::builtin().size_bytes == 16u * 1024u * 1024u);
    const auto p = PacketArenaParams::parse("{\"size_bytes\": 8192, \"canary_value\": 1}");
    SH_CHECK(p && p.value().size_bytes == 8192 && p.value().canary_value == 1);
    SH_CHECK(!PacketArenaParams::parse("{\"canary_bytes\": 0}"));
    SH_CHECK(!PacketArenaParams::parse("{\"size_bytes\": \"x\"}"));
}

SH_TEST(packet_arena_canary_layout) {
    PacketArenaParams params = PacketArenaParams::builtin();
    params.size_bytes = 8192;
    PacketArena arena(params);
    SH_CHECK(!arena.allocated());
    arena.ensure_allocated();
    SH_CHECK(arena.allocated());
    SH_CHECK(arena.end(0) == arena.base(0) + 8192);
    SH_CHECK(arena.base(0)[0] == 0 && arena.base(1)[8191] == 0);
    SH_CHECK(arena.canary_intact(0) && arena.canary_intact(1));
    arena.end(1)[63] = 0;
    SH_CHECK(arena.canary_intact(0) && !arena.canary_intact(1));
}

SH_TEST(packet_arena_monitor_rounds_as_recorded) {
    std::vector<sh::test::GoldenRow> rows = sh::test::golden_rows(kTable, "frame");
    const std::vector<sh::test::GoldenRow> rounds = sh::test::golden_rows(kTable, "round");
    SH_CHECK(rows.size() == 1800 && rounds.size() == 3);
    PacketArenaMonitor mon(PacketArenaParams::builtin());
    std::size_t round = 0;
    int differences = 0;
    for (const auto& r : rows) {
        if (round < rounds.size() && r.line > rounds[round].line) {
            mon = PacketArenaMonitor(PacketArenaParams::builtin());
            ++round;
        }
        const bool ok[2] = {r.flag(1), r.flag(2)};
        const std::string got = events_of(mon.observe(static_cast<long>(r.num(0)), ok));
        if (got != r.str(3) && ++differences <= 3) sh::test::golden_mismatch(kTable, r, got);
    }
    SH_CHECK(differences == 0);
}

SH_TEST(packet_arena_monitor_seeded_run_digest) {
    const auto rows = sh::test::golden_rows(kTable, "digest");
    SH_CHECK(rows.size() == 1);
    const PacketArenaParams params = PacketArenaParams::builtin();
    const int size = static_cast<int>(params.size_bytes);
    const int canary = static_cast<int>(params.canary_bytes);
    for (const auto& r : rows) {
        sh::test::Sequence q(static_cast<std::uint64_t>(r.num(0)));
        sh::test::Digest d;
        for (std::int64_t round = 0; round < r.num(1); ++round) {
            // The canaries of the two buffers; the monitor is told whether each is whole.
            std::vector<std::uint32_t> a(static_cast<std::size_t>(canary), params.canary_value), b = a;
            const std::vector<std::uint32_t> whole = a;
            PacketArenaMonitor mon(params);
            for (int f = 0; f < 600; ++f) {
                // The peak walks both below and above the headroom mark.
                const long big = q.below(size + 1000);
                const bool use_big = q.one_in(3);
                const long used = (use_big ? big : 0) + q.below(400000);
                if (q.one_in(150)) {
                    const int at = q.below(canary);
                    a[static_cast<std::size_t>(at)] = static_cast<std::uint32_t>(q.below(3));
                }
                if (q.one_in(220)) {
                    const int at = q.below(canary);
                    b[static_cast<std::size_t>(at)] = static_cast<std::uint32_t>(q.below(3));
                }
                const bool ok[2] = {a == whole, b == whole};
                d.add(events_of(mon.observe(used, ok)));
            }
        }
        SH_CHECK(sh::test::golden_digest_is(kTable, r, 2, d));
    }
}

// Every 120th observed frame closes a window: the average of the window and the peak since the start are reported,
// and the next window starts empty.
SH_TEST(packet_arena_monitor_reports_each_window) {
    PacketArenaMonitor mon(PacketArenaParams::builtin());
    const bool ok[2] = {true, true};
    for (int f = 1; f <= 240; ++f) {
        const long used = f <= 120 ? 1000 + (f == 7 ? 5000 : 0) : 300;
        const PacketArenaReport r = mon.observe(used, ok);
        SH_CHECK(r.cull_stat == (f % 120 == 0));
        SH_CHECK(r.used == used && r.peak == (f < 7 ? 1000 : 6000));
        if (f == 120) SH_CHECK(r.cull_frames == 120 && r.cull_avg == (120 * 1000 + 5000) / 120);
        if (f == 240) SH_CHECK(r.cull_frames == 120 && r.cull_avg == 300);
    }
}

// A damaged canary is reported once per buffer, the first frame it is seen; the high-water mark once, the first
// frame the peak is above half the arena.
SH_TEST(packet_arena_monitor_reports_once) {
    const PacketArenaParams params = PacketArenaParams::builtin();
    PacketArenaMonitor mon(params);
    const long half = static_cast<long>(params.size_bytes / 2);
    bool ok[2] = {true, true};
    PacketArenaReport r = mon.observe(half, ok);
    SH_CHECK(!r.high_water_new && !r.overrun_new[0] && !r.overrun_new[1]);
    r = mon.observe(half + 1, ok);
    SH_CHECK(r.high_water_new && r.peak == half + 1);
    r = mon.observe(half + 2, ok);
    SH_CHECK(!r.high_water_new);

    ok[1] = false;
    r = mon.observe(10, ok);
    SH_CHECK(!r.overrun_new[0] && r.overrun_new[1] && r.used == 10 && r.peak == half + 2);
    r = mon.observe(10, ok);
    SH_CHECK(!r.overrun_new[1]);
    ok[0] = false;
    r = mon.observe(10, ok);
    SH_CHECK(r.overrun_new[0] && !r.overrun_new[1]);
}
