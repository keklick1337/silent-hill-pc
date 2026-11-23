#include "framework/sh_test.h"

#include "engine/input/input_actions.h"

#include <map>
#include <set>
#include <string>

using namespace sh::input;

namespace {

LegacyReader reader(const std::map<std::string, std::string, std::less<>>& values) {
    return [&values](std::string_view key) {
        const auto it = values.find(key);
        return it == values.end() ? std::string() : it->second;
    };
}

}  // namespace

SH_TEST(input_actions_names_are_unique_and_round_trip) {
    std::set<std::string_view> names;
    for (int i = 0; i < kActionCount; ++i) {
        const Action a = static_cast<Action>(i);
        SH_CHECK(names.insert(info(a).name).second);
        SH_CHECK(find_action(info(a).name) == a);
        for (int scheme = 0; scheme < kSchemes; ++scheme) {
            const auto ref = parse_config_key(config_key(a, scheme));
            SH_CHECK(ref.has_value());
            SH_CHECK(ref->action == a);
            SH_CHECK(ref->scheme == ((info(a).flags & kFlagGlobal) ? 0 : scheme));
        }
    }
    SH_CHECK(!parse_config_key("action_nope").has_value());
    SH_CHECK(!parse_config_key("key_cross").has_value());
    SH_CHECK(!parse_config_key("action_quick_save_altcam").has_value());  // global: no altcam key
}

SH_TEST(input_actions_parse_and_format_bindings) {
    auto two = parse_binding(" Left Shift , Right Shift ");
    SH_CHECK(two);
    SH_CHECK(two.value()[0] == "Left Shift");
    SH_CHECK(two.value()[1] == "Right Shift");
    SH_CHECK(format_binding(two.value()) == "Left Shift,Right Shift");

    auto one = parse_binding("M");
    SH_CHECK(one);
    SH_CHECK(one.value()[1] == "NONE");
    SH_CHECK(format_binding(one.value()) == "M");

    auto none = parse_binding("none");
    SH_CHECK(none);
    SH_CHECK(none.value()[0] == "NONE");
    SH_CHECK(format_binding(none.value()) == "NONE");

    auto second_only = parse_binding(",Q");
    SH_CHECK(second_only);
    SH_CHECK(second_only.value()[0] == "NONE");
    SH_CHECK(format_binding(second_only.value()) == "NONE,Q");

    SH_CHECK(!parse_binding("A,B,C"));
    SH_CHECK(!parse_binding("ThisNameIsWayTooLongForTheField"));
}

SH_TEST(input_actions_defaults_are_the_modern_layout) {
    SH_CHECK(default_binding(Action::MoveForward, 0)[0] == "Up");
    SH_CHECK(default_binding(Action::MoveForward, 0)[1] == "W");
    SH_CHECK(default_binding(Action::Run, 0)[0] == "Left Shift");
    SH_CHECK(default_binding(Action::Interact, 0)[0] == "Space");
    SH_CHECK(default_binding(Action::Back, 0)[0] == "Escape");
    SH_CHECK(default_binding(Action::Inventory, 0)[0] == "Tab");
    SH_CHECK(default_binding(Action::Map, 0)[0] == "M");
    SH_CHECK(default_binding(Action::Flashlight, 0)[0] == "F");
    SH_CHECK(default_binding(Action::Aim, 0)[0] == "Mouse2");
    SH_CHECK(default_binding(Action::Attack, 0)[0] == "Mouse1");
    SH_CHECK(default_binding(Action::StepLeft, 1)[0] == "A");  // altcam strafes on A/D
    SH_CHECK(default_binding(Action::QuickSave, 1)[0] == "F6");  // global: scheme ignored

    // No key twice on one scheme's actions.
    for (int scheme = 0; scheme < kSchemes; ++scheme) {
        std::set<std::string> seen;
        for (int i = 0; i < kActionCount; ++i) {
            const Action a = static_cast<Action>(i);
            if ((info(a).flags & kFlagGlobal) && scheme == 1) {
                continue;
            }
            for (const std::string& k : default_binding(a, scheme)) {
                if (!is_unbound(k)) {
                    SH_CHECK(seen.insert(k).second);
                }
            }
        }
    }
}

SH_TEST(input_actions_translate_by_context) {
    const PadConfig c = default_pad_config();
    // Esc: pause in free gameplay, a single cancel bit (Circle) anywhere else.
    SH_CHECK(pad_bits(Action::Back, Context::Gameplay, c) == 0x0008);
    SH_CHECK(pad_bits(Action::Back, Context::Menu, c) == kPadCircle);
    SH_CHECK(pad_bits(Action::Back, Context::Inventory, c) == kPadCircle);
    // The original PAUSED screen resumes on the pause button only.
    SH_CHECK(pad_bits(Action::Back, Context::Paused, c) == 0x0008);
    SH_CHECK(pad_bits(Action::Pause, Context::Paused, c) == 0x0008);
    SH_CHECK(pad_bits(Action::Pause, Context::Menu, c) == 0);
    SH_CHECK(pad_bits(Action::Inventory, Context::Paused, c) == 0);
    // Tab and M open their screens and close them, but stay silent in other menus.
    SH_CHECK(pad_bits(Action::Inventory, Context::Gameplay, c) == 0x0001);
    SH_CHECK(pad_bits(Action::Inventory, Context::Inventory, c) == 0x0001);
    SH_CHECK(pad_bits(Action::Inventory, Context::Menu, c) == 0);
    SH_CHECK(pad_bits(Action::Map, Context::Gameplay, c) == kPadTriangle);
    SH_CHECK(pad_bits(Action::Map, Context::Map, c) == kPadTriangle);
    SH_CHECK(pad_bits(Action::Map, Context::Menu, c) == 0);
    // Run (Square) is a cancel bit in menus: silent there.
    SH_CHECK(pad_bits(Action::Run, Context::World, c) == kPadSquare);
    SH_CHECK(pad_bits(Action::Run, Context::Menu, c) == 0);
    SH_CHECK(pad_bits(Action::Interact, Context::Menu, c) == kPadCross);
    SH_CHECK(pad_bits(Action::Confirm, Context::Menu, c) == 0x0008);
    SH_CHECK(pad_bits(Action::Confirm, Context::Gameplay, c) == kPadCross);
    // Enter is the skip button outside free gameplay, but as a tap: holding it must not fast-forward text.
    SH_CHECK(tap_bits(Action::Confirm, Context::Menu, c) == 0x0008);
    SH_CHECK(tap_bits(Action::Confirm, Context::World, c) == 0x0008);
    SH_CHECK(tap_bits(Action::Confirm, Context::Gameplay, c) == 0);
    SH_CHECK(tap_bits(Action::Interact, Context::Menu, c) == 0);
    SH_CHECK(tap_bits(Action::Pause, Context::Paused, c) == 0);
    SH_CHECK(pad_bits(Action::Attack, Context::Gameplay, c) == kPadCross);
    SH_CHECK(pad_bits(Action::MoveForward, Context::Menu, c) == kPadUp);
    SH_CHECK(pad_bits(Action::Flashlight, Context::Gameplay, c) == 0);

    // Follows the game's controller config: type B aims on R1.
    PadConfig b = c;
    b.aim = 0x0800;
    SH_CHECK(pad_bits(Action::Aim, Context::Gameplay, b) == 0x0800);

    SH_CHECK(pick_cancel_bit(kPadTriangle | kPadSquare) == kPadTriangle);
    SH_CHECK(pick_cancel_bit(kPadCross) == kPadCross);
}

SH_TEST(input_actions_migrate_only_customized_legacy_binds) {
    const std::map<std::string, std::string, std::less<>> old_defaults = {
        {"key_square", "X"}, {"key_square_2", "NONE"}, {"key_select", "M"}, {"key_select_2", "Space"},
        {"key_reload", "R"}, {"key_reload_2", "NONE"},
    };
    std::map<std::string, std::string, std::less<>> current = old_defaults;

    // Untouched: the new default wins.
    SH_CHECK(!migrate_legacy(Action::Run, reader(current), reader(old_defaults)).has_value());
    SH_CHECK(!migrate_legacy(Action::Inventory, reader(current), reader(old_defaults)).has_value());
    // No legacy field at all.
    SH_CHECK(!migrate_legacy(Action::Attack, reader(current), reader(old_defaults)).has_value());

    // A rebind on either slot carries both slots over.
    current["key_square"] = "Left Alt";
    auto run = migrate_legacy(Action::Run, reader(current), reader(old_defaults));
    SH_CHECK(run.has_value());
    SH_CHECK((*run)[0] == "Left Alt");
    SH_CHECK((*run)[1] == "NONE");

    current["key_reload_2"] = "T";
    auto reload = migrate_legacy(Action::Reload, reader(current), reader(old_defaults));
    SH_CHECK(reload.has_value());
    SH_CHECK((*reload)[0] == "R");
    SH_CHECK((*reload)[1] == "T");

    // A cleared primary leaves the secondary in front; case and "" == NONE.
    current["key_select"] = "";
    current["key_select_2"] = "space";
    auto inv = migrate_legacy(Action::Inventory, reader(current), reader(old_defaults));
    SH_CHECK(inv.has_value());
    SH_CHECK((*inv)[0] == "space");
    SH_CHECK((*inv)[1] == "NONE");

    // The same key on both slots is kept once.
    current["key_select_2"] = "space";
    current["key_select"] = "Space";
    inv = migrate_legacy(Action::Inventory, reader(current), reader(old_defaults));
    SH_CHECK(inv.has_value());
    SH_CHECK((*inv)[0] == "Space");
    SH_CHECK((*inv)[1] == "NONE");
}
