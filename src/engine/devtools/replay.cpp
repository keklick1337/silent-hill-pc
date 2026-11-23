// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Replay harness: per-tick input from a file, per-tick state dump, live recording. The format of both files:
// docs/TESTING.md.

#include "engine/devtools/replay.h"

#include "engine/camera/game_camera.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/loop/game_random.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

namespace sh::devtools {

namespace {

// NPC slots of the session; the first kLegacyNpcSlots are the room spawner's.
constexpr int kNpcSlots = 120;
constexpr int kLegacyNpcSlots = 32;

struct State {
    bool active = false;
    bool input_active = false;
    std::vector<replay::InputEvent> events;
    std::size_t cursor = 0;
    std::uint16_t pressed = 0;
    std::int32_t end_tick = 0;
    std::FILE* dump = nullptr;
    std::FILE* record = nullptr;
    bool record_have_prev = false;
    std::uint16_t record_prev = 0;
    std::int32_t record_last_tick = 0;
};

State g_state;
ReplayProbe* g_probe = nullptr;

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "[REPLAY] %s\n", message.c_str());
    std::exit(2);
}

const char* env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0' ? v : nullptr;
}

void load_input(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        fail(std::string(path) + ":0: cannot open input '" + path + "'");
    std::ostringstream text;
    text << f.rdbuf();
    replay::Input in = replay::parse_input(path, text.str());
    if (!in.error.empty())
        fail(in.error);
    g_state.events = std::move(in.events);
    g_state.end_tick = in.end_tick;
    g_state.input_active = true;
}

void record_end() {
    if (g_state.record != nullptr) {
        std::fprintf(g_state.record, "end %d\n", static_cast<int>(g_state.record_last_tick));
        std::fclose(g_state.record);
        g_state.record = nullptr;
    }
    if (g_state.dump != nullptr) {
        std::fclose(g_state.dump);
        g_state.dump = nullptr;
    }
}

void record_pad(std::int32_t tick, std::uint16_t pressed) {
    if (g_state.record_have_prev && pressed == g_state.record_prev)
        return;
    g_state.record_have_prev = true;
    g_state.record_prev = pressed;
    std::fprintf(g_state.record, "%d%s\n", static_cast<int>(tick), replay::button_list(pressed).c_str());
    std::fflush(g_state.record);
}

void write_dump(std::FILE* f, const ReplayClock& clk) {
    const sh::game::Session& s = sh::game::session();
    const sh::game::SaveRecord& sg = sh::game::save_record();
    const auto& cam = sh::camera::game_camera().state();
    const ReplayDigests digests = g_probe->digests();

    std::fprintf(f, "t=%d gs=%d gss=%d,%d,%d ss=%d sss=%d,%d,%d sf=%x bgm=%x map=%d room=%d rng=%08x btn=%04x",
                 static_cast<int>(clk.tick), static_cast<int>(s.game.game_state),
                 static_cast<int>(s.game.game_steps[0]), static_cast<int>(s.game.game_steps[1]),
                 static_cast<int>(s.game.game_steps[2]), static_cast<int>(s.system.flow.sys_state),
                 static_cast<int>(s.system.flow.sys_steps[0]), static_cast<int>(s.system.flow.sys_steps[1]),
                 static_cast<int>(s.system.flow.sys_steps[2]), static_cast<unsigned>(s.system.process.sys_flags),
                 static_cast<unsigned>(s.system.process.music_status), static_cast<int>(sg.map),
                 static_cast<int>(sg.room), static_cast<unsigned>(sh::loop::game_random().seed()),
                 static_cast<unsigned>(s.game.pads[0].raw.buttons));
    std::fprintf(f, " vbl=%d uvbl=%d ivbl=%d dt=%d dtr=%d grav=%d dfc=%d c1c=%d,%d,%d warm=%d",
                 static_cast<int>(clk.vblanks), static_cast<int>(clk.uncapped_vblanks),
                 static_cast<int>(clk.interval_vblanks), static_cast<int>(clk.delta_time),
                 static_cast<int>(clk.delta_time_raw), static_cast<int>(clk.gravity),
                 static_cast<int>(clk.demo_frames), static_cast<int>(s.system.flow.frame_counters[0]),
                 static_cast<int>(s.system.flow.frame_counters[1]), static_cast<int>(s.system.flow.frame_counters[2]),
                 static_cast<int>(clk.warm_boot_timer));
    std::fprintf(f, " msg=%d mgs=%d t2c=%d f28=%d cut=%d npcf=%x cgf=%x,%x,%x,%x tgt=%d",
                 static_cast<int>(s.system.map_message_timer), static_cast<int>(s.system.flow.message_shown),
                 static_cast<int>(s.system.flow.sys_step_data[1]), static_cast<int>(s.system.flow.sys_step_data[0]),
                 static_cast<int>(s.system.flow.cutscene_border), static_cast<unsigned>(s.system.scene.npc_flags),
                 static_cast<unsigned>(s.system.scene.chara_group_flags[0]),
                 static_cast<unsigned>(s.system.scene.chara_group_flags[1]),
                 static_cast<unsigned>(s.system.scene.chara_group_flags[2]),
                 static_cast<unsigned>(s.system.scene.chara_group_flags[3]), static_cast<int>(s.system.target_npc));

    ReplayCharacter pl;
    g_probe->character(0, pl);
    ReplayPlayerExtra ex;
    g_probe->player_extra(ex);
    const std::span<const std::uint8_t> props = g_probe->player_properties();
    std::fputs(replay::character_text("pl", pl).c_str(), f);
    std::fprintf(f, " plx=%d,%d,%d,%d,%d,%d,%d,%d plp=%08x cmb=%08x hp=%d hsat=%d", static_cast<int>(ex.anim_status),
                 static_cast<int>(ex.keyframe), static_cast<int>(ex.anim_time), static_cast<int>(ex.anim_alpha),
                 static_cast<int>(ex.state), static_cast<int>(ex.upper_body), static_cast<int>(ex.lower_body),
                 static_cast<int>(ex.last_used_item),
                 static_cast<unsigned>(replay::fnv(replay::kFnvBasis, props.data(), props.size())),
                 static_cast<unsigned>(digests.combat),
                 static_cast<int>(sg.health), static_cast<int>(sg.health_saturation));
    std::fprintf(f, " cam=%d,%d,%d,%d,%d,%d", static_cast<int>(cam.cam_pos.x), static_cast<int>(cam.cam_pos.y),
                 static_cast<int>(cam.cam_pos.z), static_cast<int>(cam.cam_rotation.x),
                 static_cast<int>(cam.cam_rotation.y), static_cast<int>(cam.cam_rotation.z));

    // Event flags sparse (word:value), so a story divergence names the word.
    std::fputs(" ef=", f);
    bool first = true;
    for (int i = 0; i < sh::game::SaveRecord::kEventFlagWords; i++) {
        if (sg.event_flags[i] != 0) {
            std::fprintf(f, first ? "%d:%08x" : ",%d:%08x", i, static_cast<unsigned>(sg.event_flags[i]));
            first = false;
        }
    }
    if (first)
        std::fputc('-', f);

    std::fprintf(f, " sav=%08x gw=%08x", static_cast<unsigned>(digests.save), static_cast<unsigned>(digests.game));
    std::uint32_t h = replay::kFnvBasis;

    // Slots past the original's are engine spawns: empty ones add nothing, so the hash of a run that never uses
    // them does not depend on the slot count.
    static ReplayCharacter npcs[kNpcSlots];
    for (int i = 0; i < kNpcSlots; i++) {
        g_probe->character(i + 1, npcs[i]);
        if (i < kLegacyNpcSlots || npcs[i].chara_id != 0)
            h = replay::character_hash(h, npcs[i]);
    }
    std::fprintf(f, " npc=%08x", static_cast<unsigned>(h));
    for (int i = 0; i < kNpcSlots; i++) {
        if (npcs[i].chara_id != 0) {
            char tag[8];
            std::snprintf(tag, sizeof(tag), "n%d", i);
            std::fputs(replay::character_text(tag, npcs[i]).c_str(), f);
        }
    }
    std::fputc('\n', f);
    // A crash or a hang still leaves every tick up to it in the file.
    std::fflush(f);
}

}  // namespace

void set_replay_probe(ReplayProbe* probe) { g_probe = probe; }

void replay_init() {
    const char* in_path = env("SH_REPLAY_INPUT");
    const char* dump_path = env("SH_REPLAY_DUMP");
    const char* record_path = env("SH_REPLAY_RECORD");
    const char* end_env = env("SH_REPLAY_END");

    if (in_path != nullptr)
        load_input(in_path);
    if (end_env != nullptr)
        g_state.end_tick = std::atoi(end_env);
    if (dump_path != nullptr) {
        g_state.dump = std::fopen(dump_path, "w");
        if (g_state.dump == nullptr)
            fail(std::string(dump_path) + ":0: cannot create dump '" + dump_path + "'");
        std::setvbuf(g_state.dump, nullptr, _IOFBF, 1 << 16);
        std::fputs("# SH replay dump v1\n", g_state.dump);
    }
    if (record_path != nullptr && !g_state.input_active) {
        g_state.record = std::fopen(record_path, "w");
        if (g_state.record == nullptr)
            fail(std::string(record_path) + ":0: cannot create record '" + record_path + "'");
        std::fputs("# SH replay input v1, recorded\n", g_state.record);
    }

    g_state.active = g_state.input_active || g_state.dump != nullptr || g_state.record != nullptr || g_state.end_tick > 0;
    if (!g_state.active)
        return;
    std::atexit(record_end);
    const bool virtual_time = g_probe->virtual_time();
    if (!virtual_time)
        std::fprintf(stderr,
                     "[REPLAY] warning: no SH_REPLAY_INPUT or SH_REPLAY_RECORD, time is real, dumps of two runs differ\n");
    std::fprintf(stderr, "[REPLAY] input=%s events=%d end=%d dump=%s record=%s virtual_time=%d paced=%d\n",
                 g_state.input_active ? in_path : "-", static_cast<int>(g_state.events.size()),
                 static_cast<int>(g_state.end_tick), g_state.dump != nullptr ? dump_path : "-",
                 g_state.record != nullptr ? record_path : "-", virtual_time ? 1 : 0,
                 g_probe->virtual_time_paced() ? 1 : 0);
}

void replay_pad(sh::game::RawPad& pad) {
    if (!g_state.active)
        return;

    std::uint16_t pressed;
    if (g_state.input_active) {
        const std::int32_t tick = g_probe->clock().tick;
        while (g_state.cursor < g_state.events.size() && g_state.events[g_state.cursor].tick <= tick) {
            g_state.pressed = g_state.events[g_state.cursor].pressed;
            g_state.cursor++;
        }
        pressed = g_state.pressed;
    } else if (g_state.record != nullptr) {
        // The recorded session runs on the same digital-only pad its replay will.
        pressed = static_cast<std::uint16_t>(~pad.buttons);
        record_pad(g_probe->clock().tick, pressed);
    } else {
        return;
    }

    // A digital pad as the demo player writes one: analog type (3 byte pairs, terminal 7), sticks centred.
    pad.status = 0;
    pad.kind = 0x73;
    pad.buttons = static_cast<std::uint16_t>(~pressed);
    pad.right_x = 0x80;
    pad.right_y = 0x80;
    pad.left_x = 0x80;
    pad.left_y = 0x80;
}

void replay_tick() {
    if (!g_state.active)
        return;
    const ReplayClock clk = g_probe->clock();
    if (g_state.end_tick > 0 && clk.tick > g_state.end_tick) {
        const char* hold = env("SH_REPLAY_HOLD");
        const char* shot = env("SH_REPLAY_SHOT");

        std::fprintf(stderr, "[REPLAY] end after tick %d\n", static_cast<int>(g_state.end_tick));
        record_end();
        // The engine's own capture of the last composed frame: works with SDL_VIDEODRIVER=offscreen, where no X
        // screen exists to grab, and keeps the render size and opaque alpha of the shot console command.
        if (shot != nullptr) {
            g_probe->console(std::string("shot ") + shot);
            std::fprintf(stderr, "[REPLAY] shot %s\n", shot);
        }
        // For writing routes: the last frame stays on screen for a screenshot.
        if (hold != nullptr && std::strcmp(hold, "0") != 0) {
            std::fprintf(stderr, "[REPLAY] holding\n");
            for (;;)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::exit(0);
    }
    // A frame sequence for checking motion (camera jitter): <dir>/t<tick>.png from SH_REPLAY_SHOTS_FROM on.
    if (const char* dir = env("SH_REPLAY_SHOTS")) {
        const char* from = env("SH_REPLAY_SHOTS_FROM");
        if (from == nullptr || clk.tick >= std::atoi(from))
            g_probe->console(std::string("shot ") + dir + "/t" + std::to_string(clk.tick) + ".png");
    }
    // The last tick that ran its update: a session closed mid-tick ends before that tick.
    g_state.record_last_tick = clk.tick;
    if (g_state.dump != nullptr)
        write_dump(g_state.dump, clk);
}

}  // namespace sh::devtools
