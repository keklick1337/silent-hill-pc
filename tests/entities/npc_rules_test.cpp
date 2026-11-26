#include "framework/sh_test.h"
#include "engine/entities/npc_director.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// The rules of the map NPCs (src/engine/entities/npc_rules.*, npc_director.*): the shipped data, the distance
// arithmetic, the radio tracker and channels, and the spawn pass and review against a fake game.

namespace {

namespace ent = sh::entities;

constexpr std::int32_t kM = 4096;

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

ent::NpcRules shipped() {
    const auto r = ent::parse_npc_rules(read_file(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/npc/rules.json"));
    SH_CHECK(static_cast<bool>(r));
    return r.value();
}

struct FakeHost final : ent::NpcHost {
    std::int32_t px = 0, pz = 0, cx = 0, cz = 0;
    bool chunked = false, long_view_on = false, in_view = false, from_player = false, blocked = false;
    std::int32_t vblanks = 0, limit = 3, min_diff = 0;
    std::uint32_t taken = 0, occupied = 0, disabled = 0;
    std::vector<ent::SpawnRequest> spawned;
    std::vector<std::string> radio;
    void player_position(std::int32_t& x, std::int32_t& z) override { x = px; z = pz; }
    void camera_position(std::int32_t& x, std::int32_t& z) override { x = cx; z = cz; }
    bool chunked_interior() override { return chunked; }
    bool long_view() override { return long_view_on; }
    bool point_in_view(std::int32_t, std::int32_t) override { return in_view; }
    bool hide_distance_from_player() override { return from_player; }
    std::int32_t frame_vblanks() override { return vblanks; }
    bool spawning_blocked() override { return blocked; }
    bool row_enabled(int row) override { return (disabled >> row & 1u) == 0; }
    bool row_occupied(int row) override { return (occupied >> row & 1u) != 0; }
    bool difficulty_allows(std::int32_t m) override { return m <= min_diff; }
    bool ground_allows(std::int32_t, std::int32_t) override { return true; }
    std::uint32_t npc_slots_taken() override { return taken; }
    std::int32_t npc_slot_limit() override { return limit; }
    void spawn(const ent::SpawnRequest& r) override {
        spawned.push_back(r);
        taken |= 1u << r.slot;
        occupied |= 1u << r.row;
    }
    std::int32_t stereo_balance(std::int32_t x, std::int32_t) override { return x >> 12; }
    std::int32_t square_root_q12(std::int32_t v) override { return v >> 4; }
    void radio_start(int c) override { radio.push_back("start" + std::to_string(c)); }
    void radio_update(int c, std::int32_t b, std::int32_t v) override {
        radio.push_back("update" + std::to_string(c) + ":" + std::to_string(b) + ":" + std::to_string(v));
    }
    void radio_stop(int c) override { radio.push_back("stop" + std::to_string(c)); }
};

}  // namespace

SH_TEST(npc_rules_shipped_data_parses) {
    const ent::NpcRules r = shipped();
    SH_CHECK(r.spawn.ring_outer == 22 && r.spawn.ring_inner == 20 && r.spawn.cooldown_ticks == 60);
    SH_CHECK(r.despawn.alive == 40 && r.despawn.long_view_hard == 80 && r.despawn.hide_distance == 15);
    SH_CHECK(r.radio_range_sq == 1024 && r.kill_persist_threshold == 1228 && r.idle_clips.size() == 4);
    SH_CHECK(!static_cast<bool>(ent::parse_npc_rules("{\"spawn\": {}}")));
    SH_CHECK(!static_cast<bool>(ent::parse_npc_rules("{\"bogus\": 1}")));
}

SH_TEST(npc_rules_distance_arithmetic) {
    SH_CHECK(!ent::outside_radius(0, 0, 3 * kM, 4 * kM, 5 * kM));  // exactly on the circle: inside
    SH_CHECK(ent::outside_radius(0, 0, 3 * kM, 4 * kM, 5 * kM - 1));
    SH_CHECK(ent::outside_radius(0, 0, 6 * kM, 0, 5 * kM));         // per-axis rejection
    SH_CHECK(ent::tracked_distance_sq(10 * kM, 0, 7 * kM, 4 * kM) == 25u);
    SH_CHECK(ent::shifted_difference_sq(10 * kM, 0, 7 * kM, 4 * kM) == 25);
    SH_CHECK(ent::kill_credit_due(0, 0x200000u) && !ent::kill_credit_due(1, 0x200000u) && !ent::kill_credit_due(0, 0x600000u));
    SH_CHECK(ent::damage_pending(1) && !ent::damage_pending(0));
    const ent::NpcRules r = shipped();
    int rolls = 0;
    SH_CHECK(ent::saved_kill_persists(r, false, [&] { ++rolls; return 0; }) && rolls == 0);
    SH_CHECK(!ent::saved_kill_persists(r, true, [&] { ++rolls; return 1227; }) && rolls == 1);
    SH_CHECK(ent::saved_kill_persists(r, true, [&] { ++rolls; return 1228; }) && rolls == 2);
}

SH_TEST(npc_rules_radio_tracker_keeps_three_nearest) {
    ent::RadioTracker t(1024);
    t.offer(5, 900, 1, 1);
    t.offer(6, 100, 2, 2);
    t.offer(7, 500, 3, 3);
    t.offer(8, 800, 4, 4);   // beaten by the three above except the 900 one
    t.offer(9, 2000, 5, 5);  // out of range
    SH_CHECK(t.entry(0).slot == 6 && t.entry(1).slot == 7 && t.entry(2).slot == 8);
    SH_CHECK(t.pitch_state() == 3);
    SH_CHECK(t.index_of(7) == 1 && t.index_of(8) == ent::kNoSlot);  // only two are heard
    ent::RadioSlotSet taken;
    taken.set(6);
    SH_CHECK(t.take_free(taken) == 1 && taken.test(6) && taken.test(7) && taken.count() == 2);
    t.reset();
    SH_CHECK(t.pitch_state() == 0);
}

SH_TEST(npc_director_spawn_pass_gates_and_cooldown) {
    ent::NpcDirector d(shipped(), ent::NpcCharaIds{0, 29, 24});
    FakeHost h;
    std::array<ent::SpawnRow, 32> rows{};
    rows[0] = {1, 0, 10 * kM, 0, 64, 0};   // near, group default
    rows[1] = {1, 7, 21 * kM, 0, 0, 0};    // inside the ring, explicit chara
    rows[2] = {1, 7, 30 * kM, 0, 0, 0};    // too far
    rows[3] = {0, 7, 10 * kM, 0, 0, 0};    // empty row
    rows[17] = {2, 0, 5 * kM, 0, 0, 0};    // second group
    d.spawn_pass(h, false, rows, 11, 12);
    SH_CHECK(h.spawned.size() == 3);
    SH_CHECK(h.spawned[0].row == 0 && h.spawned[0].slot == 0 && h.spawned[0].chara == 11);
    SH_CHECK(h.spawned[1].row == 1 && h.spawned[1].slot == 1 && h.spawned[1].chara == 7);
    SH_CHECK(h.spawned[2].row == 17 && h.spawned[2].slot == 2 && h.spawned[2].chara == 12);
    // The cap (three slots) is full: nothing more, and the inner ring stops row 0 when not on entry.
    FakeHost g;
    ent::NpcDirector e(shipped(), ent::NpcCharaIds{0, 29, 24});
    e.spawn_pass(g, true, rows, 11, 12);
    SH_CHECK(g.spawned.size() == 1 && g.spawned[0].row == 1);  // only the row between the rings (20..22 m)
    // Cooldown: a freed row cannot spawn again for 60 passes.
    g.occupied = 0;
    g.taken = 0;
    g.spawned.clear();
    for (int i = 0; i < 59; ++i) {
        e.spawn_pass(g, true, rows, 11, 12);
    }
    SH_CHECK(g.spawned.empty());
    e.spawn_pass(g, true, rows, 11, 12);
    SH_CHECK(g.spawned.size() == 1);
}

SH_TEST(npc_director_never_frees_kept_remains) {
    ent::NpcDirector d(shipped(), ent::NpcCharaIds{0, 29, 24});
    FakeHost h;
    d.begin_tick(h);
    ent::NpcView body{5, 200 * kM, 0, -1, false};
    SH_CHECK(d.review(h, 40, body).free_slot);
    // A kept body (entities/remains.h) is only shown or hidden by the fog edge; its keeper takes it away.
    body.remains = true;
    ent::NpcReview r = d.review(h, 40, body);
    SH_CHECK(!r.free_slot && r.visibility == ent::Visibility::Hide);
    body.x = 3 * kM;
    r = d.review(h, 40, body);
    SH_CHECK(!r.free_slot && r.visibility == ent::Visibility::Show);
    h.long_view_on = true;
    body.x = 200 * kM;
    SH_CHECK(!d.review(h, 40, body).free_slot);
}

SH_TEST(npc_director_review_frees_hides_and_tracks) {
    ent::NpcDirector d(shipped(), ent::NpcCharaIds{0, 29, 24});
    FakeHost h;
    d.begin_tick(h);
    ent::NpcView n{5, 12 * kM, 0, 0, false};
    ent::NpcReview r = d.review(h, 1, n);
    SH_CHECK(!r.free_slot && r.visibility == ent::Visibility::Show);
    n.x = 16 * kM;  // past the fog edge, seen from the camera at the origin
    r = d.review(h, 1, n);
    SH_CHECK(!r.free_slot && r.visibility == ent::Visibility::Hide);
    n.x = 41 * kM;
    SH_CHECK(d.review(h, 1, n).free_slot);
    n.x = 25 * kM;
    n.health = -1;  // a corpse goes at 24 m
    SH_CHECK(d.review(h, 1, n).free_slot);
    n.chara = 26;  // not a monster: untouched
    SH_CHECK(!d.review(h, 1, n).free_slot && d.review(h, 1, n).visibility == ent::Visibility::Keep);
    h.long_view_on = true;
    n = ent::NpcView{5, 50 * kM, 0, 0, false};
    SH_CHECK(d.review(h, 1, n).free_slot);          // beyond 48 m and out of view
    h.in_view = true;
    SH_CHECK(!d.review(h, 1, n).free_slot);         // in view: stays
    n.x = 81 * kM;
    SH_CHECK(d.review(h, 1, n).free_slot);          // 80 m frees regardless
}

SH_TEST(npc_director_radio_channels) {
    ent::NpcDirector d(shipped(), ent::NpcCharaIds{0, 29, 24});
    FakeHost h;
    std::array<ent::RadioChannel, 2> ch{};
    d.begin_tick(h);
    d.review(h, 3, ent::NpcView{5, 8 * kM, 0, 0, false});
    d.review(h, 4, ent::NpcView{5, 4 * kM, 0, 0, false});
    d.review(h, 6, ent::NpcView{5, 2 * kM, 0, -1, false});  // dead: not tracked
    SH_CHECK(d.end_tick(h, false, ch) == 2 && h.radio.empty());
    SH_CHECK(d.end_tick(h, true, ch) == 2);
    SH_CHECK(ch[0].target == 4 && ch[1].target == 3 && ch[0].playing == 4);
    SH_CHECK(h.radio.size() == 2 && h.radio[0] == "start0" && h.radio[1] == "start1");
    h.radio.clear();
    SH_CHECK(d.end_tick(h, true, ch) == 2);  // the second tick updates both, outdoors volume halved
    SH_CHECK(h.radio.size() == 2 && h.radio[0].rfind("update0:", 0) == 0);
    d.begin_tick(h);  // nobody near any more
    SH_CHECK(d.end_tick(h, true, ch) == 0);
    SH_CHECK(ch[0].target == ent::kNoSlot && h.radio.back() == "stop1");
}

// A monster a script spawned above the room spawner's 32 slots is heard on the radio as well.
SH_TEST(npc_director_radio_hears_script_slots) {
    ent::NpcDirector d(shipped(), ent::NpcCharaIds{0, 29, 24});
    FakeHost h;
    std::array<ent::RadioChannel, 2> ch{};
    d.begin_tick(h);
    d.review(h, 32, ent::NpcView{5, 6 * kM, 0, 0, false});
    d.review(h, 100, ent::NpcView{5, 3 * kM, 0, 0, false});
    SH_CHECK(d.end_tick(h, true, ch) == 2);
    SH_CHECK(ch[0].target == 100 && ch[1].target == 32);
    SH_CHECK(h.radio.size() == 2 && h.radio[0] == "start0" && h.radio[1] == "start1");
}

namespace {

// A fake set of NPC slots that records what the tick pass asked of it.
struct FakeSlots final : ent::NpcSlots {
    struct Slot {
        ent::NpcView view;
        bool visible = true, loaded = true, behaviour = true, ready = true;
        std::int32_t control = 0;
    };
    std::vector<Slot> s;
    std::vector<std::string> log;
    int slot_count() override { return static_cast<int>(s.size()); }
    ent::NpcView view(int k) override { return s[k].view; }
    void free(int k) override { log.push_back("free" + std::to_string(k)); s[k].view.chara = 0; }
    bool visible(int k) override { return s[k].visible; }
    void set_visible(int k, bool v) override { s[k].visible = v; log.push_back((v ? "show" : "hide") + std::to_string(k)); }
    void unlock_animation(int k) override { log.push_back("unlock" + std::to_string(k)); }
    bool animation_loaded(int k) override { return s[k].loaded; }
    bool has_behaviour(int k) override { return s[k].behaviour; }
    void pose_still(int k, const ent::NpcRules::IdleClip* idle, std::int32_t dur) override {
        log.push_back("still" + std::to_string(k) + (idle ? ":" + std::to_string(idle->last) + ":" + std::to_string(dur) : ""));
    }
    void draw(int k) override { log.push_back("draw" + std::to_string(k)); }
    std::int32_t control_state(int k) override { return s[k].control; }
    void restart_state(int k) override { log.push_back("restart" + std::to_string(k)); }
    void before_behaviour(int k) override { log.push_back("pre" + std::to_string(k)); }
    bool pose_ready(int k) override { return s[k].ready; }
    void behave(int k) override { log.push_back("behave" + std::to_string(k)); }
};

std::string joined(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& x : v) {
        out += (out.empty() ? "" : " ") + x;
    }
    return out;
}

// Ids for the tick tests: 25 Cheryl, 26 Cybil, 27 Lisa (idle clip), 5 a monster; 29 the padlock.
ent::NpcDirector tick_director() {
    ent::NpcDirector d(shipped(), ent::NpcCharaIds{0, 29, 24});
    d.resolve([](const std::string& id) {
        return id == "silenthill:Cheryl" ? 25 : id == "silenthill:Cybil" ? 26 : id == "silenthill:Lisa" ? 27 : 100;
    });
    return d;
}

}  // namespace

SH_TEST(npc_rules_state_restart_data) {
    const ent::NpcRules r = shipped();
    SH_CHECK(r.state_restart.session.size() == 1 && r.state_restart.session[0] == "silenthill:Cheryl");
    SH_CHECK(r.state_restart.spawn.size() == 2 && r.state_restart.spawn_slots == 3);
    SH_CHECK(!static_cast<bool>(ent::parse_npc_rules(
        read_file(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/npc/rules.json").replace(
            read_file(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/npc/rules.json").find("\"spawn_slots\""), 13,
            "\"bogus\""))));
}

SH_TEST(npc_radio_channels_forget_and_silence) {
    FakeHost h;
    ent::RadioChannels r;
    // Zeroed, not "nobody": the first tick takes the update branch, as the game's memory did.
    SH_CHECK(r.channels()[0].playing == 0 && r.channels()[1].target == 0 && r.channels()[1].entry == 0);
    r.channels()[0] = ent::RadioChannel{3, 4, 1, 7};
    r.silence(h);
    SH_CHECK(r.channels()[0].playing == ent::kNoSlot && r.channels()[0].target == 4 && r.channels()[0].spare == 7);
    SH_CHECK(joined(h.radio) == "stop0 stop1");
    h.radio.clear();
    r.channels()[1] = ent::RadioChannel{3, 4, 1, 7};
    r.forget(h);
    for (const ent::RadioChannel& c : r.channels()) {
        SH_CHECK(c.playing == ent::kNoSlot && c.target == ent::kNoSlot && c.entry == ent::kNoSlot && c.spare == 0);
    }
    SH_CHECK(joined(h.radio) == "stop0 stop1");
}

SH_TEST(npc_director_tick_pass_order) {
    ent::NpcDirector d = tick_director();
    FakeHost h;
    ent::RadioChannels radio;
    FakeSlots f;
    f.s.resize(6);
    f.s[0].view.chara = 0;                         // empty
    f.s[1].view.chara = 29;                        // the padlock: not an actor
    f.s[2].view.chara = 5;                         // a monster far away: freed
    f.s[2].view.x = 100 * kM;
    f.s[3].view.chara = 27;                        // Lisa without behaviour: idle clip
    f.s[3].behaviour = false;
    f.s[4].view.chara = 5;                         // a near monster, still loading
    f.s[4].loaded = false;
    f.s[5].view.chara = 5;                         // a near monster with a behaviour, hidden after it
    f.s[5].view.x = 20 * kM;                       // past the camera's fog edge
    d.update(h, f, radio, false);
    SH_CHECK(joined(f.log) == "free2 unlock3 still3:9:20480 draw3 show4 unlock4 hide5 unlock5 pre5 behave5");
    f.log.clear();
    f.s[3].visible = false;                        // a hidden actor is neither posed nor drawn
    f.s[5].ready = false;                          // no animation bound: no behaviour
    d.update(h, f, radio, false);
    SH_CHECK(joined(f.log) == "unlock3 show4 unlock4 hide5 unlock5 pre5");
}

SH_TEST(npc_director_tick_state_restarts) {
    ent::NpcDirector d = tick_director();
    FakeHost h;
    ent::RadioChannels radio;
    FakeSlots f;
    f.s.resize(4);
    f.s[0].view.chara = 25;  // Cheryl: once while the game runs
    f.s[1].view.chara = 26;  // Cybil: once per spawn, in a low slot, with the control state at zero
    f.s[1].control = 2;
    f.s[3].view.chara = 26;  // slot 3 is past spawn_slots
    auto restarts = [&] {
        f.log.clear();
        d.update(h, f, radio, false);
        std::string out;
        for (const std::string& x : f.log) {
            if (x.rfind("restart", 0) == 0) {
                out += x + " ";
            }
        }
        return out;
    };
    SH_CHECK(restarts() == "restart0 ");
    SH_CHECK(restarts() == "");
    f.s[1].control = 0;
    SH_CHECK(restarts() == "restart1 ");
    SH_CHECK(restarts() == "");
    f.s[1].view.chara = 25;  // another character in the slot does not re-arm Cybil's restart...
    SH_CHECK(restarts() == "");
    f.s[1].view.chara = 26;  // ...but Cybil's memory of the slot only re-arms when the spawn group's id changes
    SH_CHECK(restarts() == "");
    f.s[0].view.chara = 26;  // a new Cybil in slot 0
    SH_CHECK(restarts() == "restart0 ");
}

SH_TEST(npc_rules_kill_tally) {
    const ent::NpcRules r = shipped();
    SH_CHECK(r.kill_tally.player_attacks == 39 && r.kill_tally.ranged_state == 3);
    int reads = 0;
    auto state = [&](std::int32_t a) { ++reads; return a == 7 ? 3 : 1; };
    SH_CHECK(ent::kill_tally_counter(r, 7, state) == 1);
    SH_CHECK(ent::kill_tally_counter(r, 38, state) == 0);
    SH_CHECK(ent::kill_tally_counter(r, 39, state) == -1 && reads == 2);  // a monster's attack is not read
}
