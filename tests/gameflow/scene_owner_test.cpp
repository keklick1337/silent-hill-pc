// Who owns the scene and the shot, and the world's vertical anchor (engine/gameflow/scene_owner.h): every state
// pair and flag combination against the recorded table, a recorded run of the held anchor, a long seeded run
// against its recorded digest, the rules in words, and the config parser.

#include <cstdint>
#include <string>

#include "engine/gameflow/scene_owner.h"

#include "framework/golden.h"
#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

constexpr const char* kTable = "gameflow/scene_owner.txt";
constexpr int kInGame = 11;

gf::SceneOwner owner() { return gf::SceneOwner(gf::SceneOwnerParams::builtin()); }

// A plain gameplay tick: in game, no script, no screen.
gf::AnchorSnapshot playing() {
    gf::AnchorSnapshot a;
    a.game_state = kInGame;
    return a;
}

}  // namespace

SH_TEST(scene_owner_every_state_pair_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "scene");
    SH_CHECK(rows.size() == 16 * 22);
    const gf::SceneOwner o = owner();
    for (const auto& r : rows) {
        std::uint32_t scene = 0, shot = 0;
        for (std::uint32_t i = 0; i < 32; ++i) {
            gf::SceneSnapshot s;
            s.sys_state = static_cast<std::int32_t>(r.num(0));
            s.game_state = static_cast<std::int32_t>(r.num(1));
            s.cutscene_flag = (i & 1) != 0;
            s.border_active = (i & 2) != 0;
            s.fade_in_started = (i & 4) != 0;
            s.camera_user_flags = (i & 8) != 0;
            s.control_disabled = (i & 16) != 0;
            if (o.owns_scene(s)) scene |= 1u << i;
            if (o.owns_shot(s)) shot |= 1u << i;
        }
        const bool same = scene == r.hex(2) && shot == r.hex(3);
        SH_CHECK(same);
        if (!same) {
            char got[32];
            std::snprintf(got, sizeof(got), "%08x %08x", scene, shot);
            sh::test::golden_mismatch(kTable, r, got);
        }
    }
}

SH_TEST(scene_owner_anchor_run_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "anchor");
    SH_CHECK(rows.size() == 2500);
    gf::SceneOwner o = owner();
    int differences = 0;
    for (const auto& r : rows) {
        gf::AnchorSnapshot a;
        a.game_state = static_cast<std::int32_t>(r.num(0));
        a.sys_state = static_cast<std::int32_t>(r.num(1));
        a.pickup_active = r.flag(2);
        a.cutscene_active = r.flag(3);
        a.border_active = r.flag(4);
        a.third_person = r.flag(5);
        a.cutscene_shift = static_cast<std::int32_t>(r.num(6));
        a.world_shift = static_cast<std::int32_t>(r.num(7));
        const std::int32_t got = o.world_anchor(a);
        if (got != r.num(8) && ++differences <= 3) sh::test::golden_mismatch(kTable, r, std::to_string(got));
    }
    SH_CHECK(differences == 0);
}

SH_TEST(scene_owner_seeded_run_digest) {
    const auto rows = sh::test::golden_rows(kTable, "digest");
    SH_CHECK(rows.size() == 1);
    for (const auto& r : rows) {
        sh::test::Sequence q(static_cast<std::uint64_t>(r.num(0)));
        sh::test::Digest d;
        gf::SceneOwner o = owner();
        for (std::int64_t i = 0; i < r.num(1); ++i) {
            gf::SceneSnapshot s;
            gf::AnchorSnapshot a;
            s.cutscene_flag = q.one_in(4);
            s.border_active = a.border_active = q.one_in(5);
            s.sys_state = a.sys_state = q.one_in(3) ? 0 : q.below(16);
            s.game_state = a.game_state = q.one_in(3) ? q.below(22) : kInGame;
            s.fade_in_started = q.one_in(2);
            s.camera_user_flags = q.below(2) != 0;
            s.control_disabled = q.below(2) != 0;
            a.cutscene_active = q.one_in(3);
            a.pickup_active = q.one_in(4);
            a.third_person = q.below(2) != 0;
            a.cutscene_shift = q.below(801) - 400;
            a.world_shift = q.below(1201) - 600;
            d.add(o.owns_scene(s) ? 1 : 0);
            d.add(o.owns_shot(s) ? 1 : 0);
            d.add(o.world_anchor(a));
        }
        SH_CHECK(sh::test::golden_digest_is(kTable, r, 2, d));
    }
}

// A cutscene or the letterbox always owns the scene. Otherwise a script owns it only when it both moved the
// camera and took the controls, and never on a screen the player opened.
SH_TEST(scene_owner_script_scenes) {
    const gf::SceneOwner o = owner();
    const gf::SceneOwnerParams p = gf::SceneOwnerParams::builtin();
    gf::SceneSnapshot s;
    s.game_state = kInGame;
    SH_CHECK(!o.owns_scene(s));
    s.camera_user_flags = true;
    SH_CHECK(!o.owns_scene(s));
    s.control_disabled = true;
    SH_CHECK(o.owns_scene(s));

    SH_CHECK(!p.player_sys_states.empty() && !p.player_game_states.empty());
    gf::SceneSnapshot menu = s;
    menu.sys_state = p.player_sys_states.front();
    SH_CHECK(!o.owns_scene(menu));
    menu.cutscene_flag = true;
    SH_CHECK(o.owns_scene(menu));
    gf::SceneSnapshot screen = s;
    screen.game_state = p.player_game_states.front();
    SH_CHECK(!o.owns_scene(screen));
    screen.border_active = true;
    SH_CHECK(o.owns_scene(screen));
}

// The shot is the scene's, except that a script loses it once the screen fade has begun to come in; a cutscene
// keeps it through the fade.
SH_TEST(scene_owner_shot_ends_with_the_fade) {
    const gf::SceneOwner o = owner();
    gf::SceneSnapshot s;
    s.game_state = kInGame;
    s.camera_user_flags = s.control_disabled = true;
    SH_CHECK(o.owns_shot(s));
    s.fade_in_started = true;
    SH_CHECK(o.owns_scene(s) && !o.owns_shot(s));
    s.cutscene_flag = true;
    SH_CHECK(o.owns_shot(s));
}

// During plain gameplay the anchor is the base plus the shift of the camera in use (none in third person), and the
// cutscene framing's shift while that is on. A pickup or another system state holds the last gameplay anchor;
// outside the game the anchor is the base.
SH_TEST(scene_owner_anchor_is_held_between_gameplay_ticks) {
    gf::SceneOwner o = owner();
    const std::int32_t base = gf::SceneOwnerParams::builtin().anchor_base;
    gf::AnchorSnapshot a = playing();
    a.world_shift = 5;
    a.cutscene_shift = -3;
    SH_CHECK(o.world_anchor(a) == base + 5);
    a.third_person = true;
    SH_CHECK(o.world_anchor(a) == base);
    a.cutscene_active = true;
    SH_CHECK(o.world_anchor(a) == base - 3);

    a = playing();
    a.world_shift = 7;
    SH_CHECK(o.world_anchor(a) == base + 7);
    a.pickup_active = true;
    a.world_shift = 20;
    SH_CHECK(o.world_anchor(a) == base + 7);  // held
    a.pickup_active = false;
    a.sys_state = 2;
    SH_CHECK(o.world_anchor(a) == base + 7);
    a.cutscene_shift = 4;
    a.border_active = true;  // the letterbox frames the picture even off the gameplay state
    SH_CHECK(o.world_anchor(a) == base + 4);

    a = gf::AnchorSnapshot{};
    a.game_state = 7;
    a.world_shift = 9;
    SH_CHECK(o.world_anchor(a) == base);
    SH_CHECK(owner().world_anchor([] {
        gf::AnchorSnapshot held = playing();
        held.pickup_active = true;
        return held;
    }()) == base);  // nothing held yet: the base
}

SH_TEST(scene_owner_parse) {
    auto ok = gf::SceneOwnerParams::parse(R"({"world_anchor_base":4,"player_sys_states":["ReadMessage"],"player_game_states":[]})");
    SH_CHECK(ok);
    SH_CHECK(ok.value().anchor_base == 4 && ok.value().player_sys_states.size() == 1);
    SH_CHECK(!gf::SceneOwnerParams::parse(R"({"player_sys_states":["Nope"],"player_game_states":[]})"));
    SH_CHECK(!gf::SceneOwnerParams::parse(R"({"player_sys_states":[]})"));
}
