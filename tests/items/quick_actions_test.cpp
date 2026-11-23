// The PC quick actions (engine/items/quick_actions.h) against a fake game: weapon cycling and the number keys,
// quick heal (spend first, then heal), the reload latch and the safe-gameplay gates.
#include "engine/items/quick_actions.h"
#include "framework/sh_test.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct Slot {
    int item = 255, amount = 0;
};

struct FakeGame final : sh::items::QuickActionsHost {
    std::array<Slot, 40> slots{};
    int count = 0, equipped = 0;
    bool control = true, safe = true, reload_ok = false, console = false;
    std::int32_t hp = 4096 * 50, sat = 0;
    bool turn = false;
    int lights = 0;
    std::vector<std::string> events;

    bool player_in_control() override { return control; }
    bool action_safe() override { return control && safe; }
    bool reload_possible() override { return reload_ok; }
    std::int32_t frame_time() override { return 68; }
    bool console_open() override { return console; }
    bool debug_menu_open() override { return false; }
    bool keyframe_viewer() override { return false; }
    bool quick_weapons_enabled() override { return true; }
    int slot_count() override { return count; }
    int slot_item(int s) override { return slots[static_cast<std::size_t>(s)].item; }
    int slot_amount(int s) override { return slots[static_cast<std::size_t>(s)].amount; }
    int equipped_item() override { return equipped; }
    int item_number(std::string_view n) override {
        static const std::pair<const char*, int> kItems[] = {
            {"HealthDrink", 32}, {"FirstAidKit", 33}, {"Ampoule", 34},     {"KitchenKnife", 128},
            {"SteelPipe", 129},  {"Handgun", 160},    {"Shotgun", 162},    {"HyperBlaster", 163}};
        for (const auto& [name, v] : kItems) {
            if (n == name) return v;
        }
        return -1;
    }
    // The shipped gamedata/config/quick_actions.json.
    const sh::items::QuickActionsData& quick_data() override {
        static const sh::items::QuickActionsData s_data = [] {
            std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/config/quick_actions.json");
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto parsed = sh::items::QuickActionsData::parse(text);
            return parsed ? parsed.value() : sh::items::QuickActionsData{};
        }();
        return s_data;
    }
    bool is_gun(int item) override { return item >> 5 == 5; }
    int ammo_item(int item) override { return item + 32; }
    bool fills_saturation(int item) override { return item == 34; }
    void equip(int item, int slot, int ammo, int reserve) override {
        equipped = item;
        events.push_back("equip " + std::to_string(item) + " " + std::to_string(slot) + " " + std::to_string(ammo) +
                         " " + std::to_string(reserve));
    }
    std::int32_t health() override { return hp; }
    void set_health(std::int32_t h) override { hp = h; }
    void set_saturation(std::int32_t s) override { sat = s; }
    std::int32_t heal_amount(int item) override { return item == 34 ? 4096 * 100 : 4096 * 40; }
    std::int32_t max_health() override { return 4096 * 100; }
    std::int32_t max_saturation() override { return 4096 * 300; }
    // The mods' answer (player.quick_heal): -1 cancels, else the amount to heal instead (0: unchanged).
    std::int32_t hook_amount = 0;
    bool quick_heal_allowed(int item, std::int32_t health, std::int32_t& amount) override {
        events.push_back("ask " + std::to_string(item) + " " + std::to_string(health >> 12) + " " +
                         std::to_string(amount >> 12));
        if (hook_amount < 0) return false;
        if (hook_amount > 0) amount = hook_amount;
        return true;
    }
    bool spend_item(int item) override {
        for (int i = 0; i < count; ++i) {
            if (slots[static_cast<std::size_t>(i)].item == item && slots[static_cast<std::size_t>(i)].amount > 0) {
                --slots[static_cast<std::size_t>(i)].amount;
                return true;
            }
        }
        return false;
    }
    void heal_feedback() override { events.push_back("heal"); }
    void toggle_flashlight() override { ++lights; }
    void set_quick_turn_request(bool r) override { turn = r; }
    void log(const std::string&) override {}
    void toast(const std::string& line) override { events.push_back(line); }
};

SH_TEST(quick_actions_cycle_skips_unowned_and_wraps) {
    FakeGame g;
    g.count = 3;
    g.slots[0] = {128, 0};
    g.slots[1] = {160, 7};
    g.slots[2] = {192, 30};  // handgun bullets: the reserve
    g.equipped = 128;
    sh::items::QuickActions qa;
    qa.cycle_weapons(g);
    SH_CHECK(g.events.size() == 1 && g.events[0] == "equip 160 1 7 30");
    qa.cycle_weapons(g);
    SH_CHECK(g.events.size() == 2 && g.events[1] == "equip 128 0 0 0");
}

SH_TEST(quick_actions_number_key_and_dead_slots) {
    FakeGame g;
    g.count = 1;
    g.slots[0] = {128, 0};
    g.slots[1] = {162, 2};  // past the slot count: a ghost, never equipped
    sh::items::QuickActions qa;
    sh::items::QuickActionPresses p;
    p.weapon_key = 3;  // Shotgun
    qa.frame(p, g);
    SH_CHECK(g.events.empty());
    p.weapon_key = 0;  // KitchenKnife
    qa.frame(p, g);
    SH_CHECK(g.events.size() == 1 && g.events[0] == "equip 128 0 0 0");
}

SH_TEST(quick_actions_heal_spends_first) {
    FakeGame g;
    g.count = 2;
    g.slots[0] = {32, 0};  // a spent drink keeps its item: not owned
    g.slots[1] = {34, 1};
    g.hp = 4096 * 10;
    sh::items::QuickActions qa;
    qa.quick_heal(g);
    SH_CHECK(g.hp == 4096 * 100 && g.sat == 4096 * 300);
    SH_CHECK(g.events.size() == 3 && g.events[0] == "ask 34 10 100" && g.events[1] == "heal");
    SH_CHECK(g.events[2] == "Used Ampoule  -  Kits 0, Drinks 0, Ampoules 0");
    g.hp = 4096 * 10;
    qa.quick_heal(g);  // nothing left
    SH_CHECK(g.hp == 4096 * 10 && g.events.size() == 3);
}

// A mod's player.quick_heal handler: a cancel keeps the item, a new amount heals that much.
SH_TEST(quick_actions_heal_asks_the_mods) {
    FakeGame g;
    g.count = 1;
    g.slots[0] = {32, 2};
    g.hp = 4096 * 10;
    g.hook_amount = -1;
    sh::items::QuickActions qa;
    qa.quick_heal(g);
    SH_CHECK(g.hp == 4096 * 10 && g.slots[0].amount == 2);
    SH_CHECK(g.events.size() == 1 && g.events[0] == "ask 32 10 40");
    g.hook_amount = 4096 * 5;
    qa.quick_heal(g);
    SH_CHECK(g.hp == 4096 * 15 && g.slots[0].amount == 1);
}

SH_TEST(quick_actions_reload_latch_and_gates) {
    FakeGame g;
    sh::items::QuickActions qa;
    sh::items::QuickActionPresses p;
    p.reload = true;
    qa.frame(p, g);
    SH_CHECK(!qa.reload_requested());  // conditions false at press time
    g.reload_ok = true;
    qa.frame(p, g);
    SH_CHECK(qa.reload_requested());
    p.reload = false;
    for (int i = 0; i < 18; ++i) qa.frame(p, g);  // 18 * 68 = 1224 < 1228: still latched
    SH_CHECK(qa.reload_requested());
    qa.frame(p, g);
    SH_CHECK(!qa.reload_requested());

    p.quick_turn = true;
    p.flashlight = true;
    qa.frame(p, g);
    SH_CHECK(g.turn && g.lights == 1);
    g.console = true;
    qa.frame(p, g);
    SH_CHECK(!g.turn && g.lights == 1);
    g.console = false;
    g.safe = false;
    qa.frame(p, g);
    SH_CHECK(!g.turn && g.lights == 1);
}

}  // namespace
