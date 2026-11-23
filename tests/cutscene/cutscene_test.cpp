// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Engine cutscene player (src/engine/cutscene): loading sh-cutscene/1, the key a frame shows, blending, shot
// ends, angle and 16-bit wrapping, track names, the slot director, and every cutscene of the image.
#include "framework/sh_disc.h"
#include "framework/sh_test.h"
#include "engine/cutscene/cutscene.h"
#include "engine/cutscene/cutscene_director.h"
#include "formats/convert/cutscene/cutscene_converter.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

namespace ec = sh::cutscene;

// Frames 0..7: shots [0, 5), [5] (one frame), [6, 8). The actor holds key 1 on frames 1..2.
const char* kDocument = R"({
  "format": "sh-cutscene/1",
  "header": {"origin": [100, -50, 0]},
  "segments": [[0, 5], [5, 1], [6, 2]],
  "camera": {
    "name": "%CAM",
    "holds": [],
    "keyframes": [[0, 0, 0, 10, 10, 10, 0, 100], [64, 0, 0, 10, 10, 10, 100, 200]]
  },
  "characters": [
    {"name": "DAHL", "holds": [[1, 2, 1]], "keyframes": [
      [0, 0, 0, 0, 2000, 0],
      [101, -101, 0, 0, -2000, 0],
      [30000, 0, 0, 0, 0, 0],
      [32767, 0, 0, 0, 0, 0],
      [1, 2, 3, 4, 5, 6],
      [1, 2, 3, 4, 5, 6]]},
    {"name": "DAH", "name_hex": "44414800", "keyframes": [[0, 0, 0, 0, 0, 0]]},
    {"name": "LISA", "name_hex": "44414800", "keyframes": [[0, 0, 0, 0, 0, 0]]}
  ]
})";

ec::Cutscene sample_cutscene() {
    auto cs = ec::parse_cutscene_json(kDocument, "test");
    SH_CHECK(cs);
    return cs ? cs.value() : ec::Cutscene{};
}

}  // namespace

SH_TEST(CutsceneJsonLoads) {
    const ec::Cutscene cs = sample_cutscene();
    SH_CHECK(cs.origin[0] == 100 && cs.origin[1] == -50);
    SH_CHECK(cs.segments.size() == 3);
    SH_CHECK(cs.frame_count() == 8);
    SH_CHECK(cs.camera.keys.size() == 2 && cs.camera.keys[1].projection == 200);
    SH_CHECK(cs.actors.size() == 3);
    SH_CHECK(ec::track_name_text(cs.actors[0].name) == "DAHL");
    SH_CHECK(ec::track_name_text(cs.actors[1].name) == "DAH");  // name_hex kept: "name" shows it unchanged
    SH_CHECK(ec::track_name_text(cs.actors[2].name) == "LISA");  // "name" edited: it wins over name_hex

    SH_CHECK(!ec::parse_cutscene_json(R"({"format": "sh-cutscene/2"})"));
    SH_CHECK(!ec::parse_cutscene_json(R"({"format": "sh-cutscene/1", "segments": [], "camera": {"keyframes": [[1]]}})"));
    SH_CHECK(!ec::parse_cutscene_json(R"({"format": "sh-cutscene/1", "segments": [], "camera": {"keyframes": []},
        "characters": [{"name": "TOOLONG", "keyframes": []}]})"));
}

SH_TEST(CutsceneFrameToKey) {
    const ec::Cutscene cs = sample_cutscene();
    const ec::ActorTrack& t = cs.actors[0];
    const std::size_t expected[] = {0, 1, 1, 2, 3, 4, 5, 5};
    for (int f = 0; f < 8; ++f) {
        SH_CHECK(t.key_for_frame(f) == expected[f]);
    }
    SH_CHECK(t.key_for_frame(-3) == 0);
    SH_CHECK(t.key_for_frame(100) == 5);
    SH_CHECK(ec::ActorTrack{}.key_for_frame(4) == 0);
}

SH_TEST(CutsceneActorBlend) {
    const ec::Cutscene cs = sample_cutscene();
    // Half way from key 0 to key 1: products round towards minus infinity, angles take the short way and wrap.
    const ec::ActorPose a = cs.actor_pose(0, 2048);
    SH_CHECK(a.position[0] == (50 + 100) * 16);
    SH_CHECK(a.position[1] == (-51 - 50) * 16);
    SH_CHECK(a.position[2] == 0);
    SH_CHECK(a.rotation[1] == -2048);

    // Last frame of the first shot: frame 3 -> 4 continued (weight 1.25), the sum wraps in 16 bits.
    const ec::ActorPose b = cs.actor_pose(0, 4 * 4096 + 1024);
    SH_CHECK(b.position[0] == (-32078 + 100) * 16);

    // A one-frame shot shows its key whatever the fraction.
    const ec::ActorPose c = cs.actor_pose(0, 5 * 4096 + 3000);
    SH_CHECK(c.position[0] == 101 * 16 && c.position[1] == -48 * 16 && c.position[2] == 3 * 16);
    SH_CHECK(c.rotation[0] == 4 && c.rotation[1] == 5 && c.rotation[2] == 6);

    // Out of range indices and empty tracks give the zero pose.
    SH_CHECK(cs.actor_pose(9, 0).position[0] == 0);
}

SH_TEST(CutsceneCameraBlend) {
    const ec::Cutscene cs = sample_cutscene();
    const ec::CameraPose p = cs.camera_pose(1024);
    SH_CHECK(p.eye[0] == (16 + 100) * 16);
    SH_CHECK(p.eye[1] == -50 * 16);
    SH_CHECK(p.target[0] == 110 * 16 && p.target[1] == -40 * 16 && p.target[2] == 160);
    SH_CHECK(p.projection == 125);
    SH_CHECK(p.roll == 25);
}

SH_TEST(CutsceneShotAt) {
    const ec::Cutscene cs = sample_cutscene();
    SH_CHECK(cs.shot_at(0) == 0);
    SH_CHECK(cs.shot_at(4 * 4096 + 4095) == 0);
    SH_CHECK(cs.shot_at(5 * 4096) == 1);
    SH_CHECK(cs.shot_at(7 * 4096) == 2);
    SH_CHECK(cs.shot_at(8 * 4096) == -1);
}

SH_TEST(CutsceneTrackNames) {
    const ec::Cutscene cs = sample_cutscene();
    SH_CHECK(cs.find_actor("DAHLIA") == std::optional<std::size_t>(0));  // the first four characters count
    SH_CHECK(cs.find_actor("DAHL") == std::optional<std::size_t>(0));
    SH_CHECK(cs.find_actor("DAH") == std::optional<std::size_t>(1));
    SH_CHECK(cs.find_actor("LISA") == std::optional<std::size_t>(2));
    SH_CHECK(!cs.find_actor("KAU"));
    SH_CHECK(!cs.find_actor(""));
}

SH_TEST(CutsceneDirectorSlots) {
    int loads = 0;
    ec::Director d([&](const std::string& path) -> sh::Result<ec::Cutscene> {
        ++loads;
        if (path == "ANIM/BAD.DMS") {
            return sh::Result<ec::Cutscene>::fail("bad");
        }
        auto cs = ec::parse_cutscene_json(kDocument, path);
        return cs;
    });
    SH_CHECK(d.playing(1) == nullptr);
    d.assign(1, "ANIM/A.DMS");
    SH_CHECK(d.playing(1) == nullptr);  // assigned, not started
    SH_CHECK(d.start(1));
    SH_CHECK(d.playing(1) && d.playing(1)->source == "ANIM/A.DMS");
    SH_CHECK(d.start(1) && loads == 1);  // a second start without a new load keeps the cutscene
    SH_CHECK(d.playing(2) == d.playing(1));  // an unknown slot samples the cutscene started last
    d.assign(2, "ANIM/B.DMS");
    SH_CHECK(d.playing(2) == d.playing(1));  // still the last started until slot 2 starts
    SH_CHECK(d.start(2));
    SH_CHECK(d.playing(2)->source == "ANIM/B.DMS" && d.playing(1)->source == "ANIM/A.DMS");
    std::string error;
    d.assign(1, "ANIM/BAD.DMS");
    SH_CHECK(!d.start(1, &error) && error == "bad");
    SH_CHECK(d.playing(1)->source == "ANIM/B.DMS");
    d.reset();
    SH_CHECK(d.playing(2) == nullptr);
}

SH_TEST(CutsceneEveryDmsOfTheImageLoads) {
    const auto disc = sh::test::open_game_disc();
    if (!disc) {
        SH_SKIP("no disc image");
    }
    std::size_t loaded = 0;
    for (const auto& e : disc->table) {
        if (!e.name.ends_with(".DMS")) {
            continue;
        }
        const auto raw = disc->read(e.name);
        SH_CHECK(raw.has_value());
        if (!raw) {
            continue;
        }
        const auto dms = sh::convert::cutscene::parse_dms(*raw);
        if (!dms) {
            continue;  // TEST/TEST2.DMS, a prototype layout
        }
        const auto cs = ec::parse_cutscene_json(sh::convert::cutscene::dms_to_json(dms.value(), e.name, *raw), e.name);
        SH_CHECK(cs);
        if (!cs) {
            continue;
        }
        ++loaded;
        const ec::Cutscene& c = cs.value();
        SH_CHECK(c.actors.size() == dms.value().characters.size());
        SH_CHECK(c.camera.keys.size() == dms.value().camera.keyframes.size());
        for (std::size_t i = 0; i < c.actors.size(); ++i) {
            SH_CHECK(c.actors[i].keys.size() == dms.value().characters[i].keyframes.size());
            SH_CHECK(std::equal(c.actors[i].name.begin(), c.actors[i].name.end(), dms.value().characters[i].name.begin()));
        }
        // Without holds before it, frame k shows key k unblended at the start of a still shot or at alpha 0.
        for (std::int32_t f = 0; f < c.frame_count(); ++f) {
            const ec::CameraPose p = c.camera_pose(f * ec::kTimeOne);
            const auto& key = c.camera.keys[c.camera.key_for_frame(f)];
            bool shot_end = false;
            for (const auto& s : c.segments) {
                shot_end = shot_end || (f == s.first + s.count - 1 && s.count > 1);
            }
            if (!shot_end) {
                SH_CHECK(p.eye[0] == (key.eye[0] + c.origin[0]) * 16 && p.projection == key.projection);
            }
        }
    }
    std::printf("  cutscenes: %zu loaded\n", loaded);
    SH_CHECK(loaded == 45);
}
