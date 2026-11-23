// SPDX-License-Identifier: GPL-3.0-or-later
// Readable bind names (src/engine/screens/controls_names.h): keys, mouse, pad buttons and
// axes never come out as raw SDL names, and every text key they use is in the shipped
// languages' controls_screen.json.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "engine/screens/controls_names.h"
#include "formats/common/json.h"
#include "framework/sh_test.h"

using namespace sh::screens;

namespace {

std::string en(std::string_view v, bool pad = false, PadLabels labels = PadLabels::Xbox) {
    return english_name(bind_input_name(v, pad, labels));
}

std::string key_of(const InputName& n) { return n.size() == 1 ? n[0].key : std::string(); }

}  // namespace

SH_TEST(controls_names_keyboard) {
    SH_CHECK(en("Space") == "Space");
    SH_CHECK(en("Left Shift") == "Left Shift");
    SH_CHECK(en("Right Ctrl") == "Right Ctrl");
    SH_CHECK(en("Return") == "Enter");
    SH_CHECK(en("Escape") == "Esc");
    SH_CHECK(en("Up") == "Up");
    SH_CHECK(en("PageDown") == "Page Down");
    SH_CHECK(en("CapsLock") == "Caps Lock");
    SH_CHECK(en("Left GUI") == "Left Win");
    SH_CHECK(en("Keypad 1") == "Keypad 1");
    SH_CHECK(en("Keypad Enter") == "Keypad Enter");
    SH_CHECK(en("W") == "W");
    SH_CHECK(en("w") == "W");
    SH_CHECK(en("F5") == "F5");
    SH_CHECK(en("F12") == "F12");
    SH_CHECK(en("`") == "`");
    SH_CHECK(en("VolumeUp") == "Volume Up");  // unknown to the table, still words
    SH_CHECK(key_of(keyboard_input_name("space")) == "ui.input.key.space");
    SH_CHECK(key_of(keyboard_input_name("Left Shift")) == "ui.input.key.left_shift");
    // A letter is the same in every language: no key.
    SH_CHECK(key_of(keyboard_input_name("Q")).empty() && keyboard_input_name("Q").size() == 1);
    const InputName kp = keyboard_input_name("Keypad 7");
    SH_CHECK(kp.size() == 2 && kp[0].key == "ui.input.key.keypad" && kp[1].key.empty() && kp[1].text == "7");
}

SH_TEST(controls_names_mouse) {
    SH_CHECK(en("Mouse1") == "Mouse Left");
    SH_CHECK(en("mouse2") == "Mouse Right");
    SH_CHECK(en("Mouse3") == "Mouse Middle");
    SH_CHECK(en("Mouse5") == "Mouse 5");
    SH_CHECK(en("MouseWheelUp") == "Mouse Wheel Up");
    SH_CHECK(en("MouseWheelDown") == "Mouse Wheel Down");
    SH_CHECK(key_of(mouse_input_name("Mouse1")) == "ui.input.mouse.left");
    SH_CHECK(english_name(mouse_short_name("Mouse1")) == "Left");
    SH_CHECK(english_name(mouse_short_name("MouseWheelDown")) == "Down");
    SH_CHECK(key_of(mouse_short_name("Mouse4")).empty() && english_name(mouse_short_name("Mouse4")) == "4");
    SH_CHECK(is_mouse_bind("MOUSE4") && !is_mouse_bind("M") && !is_mouse_bind("Space"));
    SH_CHECK(is_wheel_bind("MouseWheelDown") && !is_wheel_bind("Mouse1"));
}

SH_TEST(controls_names_device) {
    SH_CHECK(bind_device("", false) == BindDevice::None);
    SH_CHECK(bind_device("NONE", true) == BindDevice::None);
    SH_CHECK(bind_device("none", false) == BindDevice::None);
    SH_CHECK(bind_device("Mouse1", false) == BindDevice::Mouse);
    SH_CHECK(bind_device("Left Shift", false) == BindDevice::Keyboard);
    SH_CHECK(bind_device("a", true) == BindDevice::Pad);
    SH_CHECK(bind_input_name("NONE", false, PadLabels::Xbox).empty());
}

SH_TEST(controls_names_pad_by_label_set) {
    SH_CHECK(en("leftshoulder", true, PadLabels::Xbox) == "LB");
    SH_CHECK(en("leftshoulder", true, PadLabels::PlayStation) == "L1");
    SH_CHECK(en("leftshoulder", true, PadLabels::Nintendo) == "L");
    SH_CHECK(en("righttrigger", true, PadLabels::Xbox) == "RT");
    SH_CHECK(en("righttrigger", true, PadLabels::PlayStationClassic) == "R2");
    SH_CHECK(en("righttrigger", true, PadLabels::Nintendo) == "ZR");
    SH_CHECK(en("a", true, PadLabels::Xbox) == "A");
    SH_CHECK(en("a", true, PadLabels::PlayStation) == "Cross");
    SH_CHECK(key_of(pad_input_name("b", PadLabels::PlayStation)) == "ui.input.pad.circle");
    SH_CHECK(en("y", true, PadLabels::PlayStationClassic) == "Triangle");
    SH_CHECK(en("back", true, PadLabels::PlayStationClassic) == "Select");
    SH_CHECK(en("start", true, PadLabels::PlayStation) == "Options");
    SH_CHECK(en("start", true, PadLabels::Xbox) == "Menu");
    SH_CHECK(en("guide", true, PadLabels::Xbox) == "Guide");
    SH_CHECK(en("leftstick", true, PadLabels::PlayStation) == "L3");
    SH_CHECK(en("dpup", true, PadLabels::Xbox) == "D-pad Up");
    SH_CHECK(en("dpleft", true, PadLabels::PlayStation) == "D-pad Left");
    SH_CHECK(en("lefty", true) == "Left Stick Y");
    SH_CHECK(en("touchpad", true) == "Touchpad");
    SH_CHECK(en("paddle2", true) == "Paddle 2");
    SH_CHECK(en("misc1", true) == "Misc 1");
    SH_CHECK(stick_input_name(0, false)[0].text == "Left Stick Up");
    SH_CHECK(stick_input_name(1, true)[0].text == "D-pad Right");
    SH_CHECK(stick_input_name(3, true)[0].text == "D-pad Left");
    SH_CHECK(stick_input_name(4, true).empty());
}

SH_TEST(controls_names_never_raw_sdl) {
    // Every name a bind can hold reads as words, never as the lower-case SDL id.
    const char* const pad[] = {"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
                               "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright",
                               "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
                               "lefttrigger", "righttrigger", "leftx", "lefty", "rightx", "righty"};
    for (const PadLabels set : {PadLabels::Xbox, PadLabels::PlayStation, PadLabels::PlayStationClassic,
                                PadLabels::Nintendo, PadLabels::Steam}) {
        for (const char* id : pad) {
            const std::string name = en(id, true, set);
            SH_CHECK(!name.empty() && name != id);
        }
    }
    for (const char* id : {"leftshoulder", "lefttrigger", "dpup"}) {
        SH_CHECK(en(id, true).find(id) == std::string::npos);
    }
}

SH_TEST(controls_names_text_files_complete) {
    const std::vector<InputTextKey>& keys = input_text_keys();
    SH_CHECK(keys.size() > 50);
    for (const char* lang : {"en", "ru", "pl", "pt"}) {
        const std::filesystem::path file =
            std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "text" / lang / "controls_screen.json";
        std::ifstream in(file, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const auto doc = sh::json::parse(ss.str());
        SH_CHECK(static_cast<bool>(doc));
        if (!doc) {
            continue;
        }
        const sh::json::Value* strings = doc.value().find("strings");
        SH_CHECK(strings != nullptr && strings->is_object());
        if (strings == nullptr) {
            continue;
        }
        for (const InputTextKey& k : keys) {
            const sh::json::Value* v = strings->find(k.key);
            SH_CHECK(v != nullptr && v->is_string() && !v->string().empty());
            if (v != nullptr && std::string(lang) == "en") {
                SH_CHECK(v->string() == k.english);
            }
            // The Cyrillic menu font's Latin cells hold Cyrillic letters.
            if (v != nullptr && v->is_string() && std::string(lang) == "ru") {
                for (const char c : v->string()) {
                    SH_CHECK(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')));
                }
            }
        }
        for (const char* screen : {"ui.controls_screen.mouse", "ui.controls_screen.gamepad",
                                   "ui.controls_screen.primary", "ui.controls_screen.secondary"}) {
            SH_CHECK(strings->find(screen) != nullptr);
        }
    }
}
