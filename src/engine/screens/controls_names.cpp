// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/controls_names.h"

#include <array>
#include <cctype>

namespace sh::screens {

namespace {

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool istarts(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && ieq(s.substr(0, prefix.size()), prefix);
}

struct Named {
    const char* id;       // the bind value, compared without case
    const char* slug;     // ui.input.<group>.<slug>
    const char* english;
};

// SDL key names (SDL_GetScancodeName / SDL_GetKeyName) that are words rather than the
// character on the key cap.
constexpr Named kKeys[] = {
    {"return", "enter", "Enter"},
    {"enter", "enter", "Enter"},
    {"escape", "escape", "Esc"},
    {"backspace", "backspace", "Backspace"},
    {"tab", "tab", "Tab"},
    {"space", "space", "Space"},
    {"capslock", "caps_lock", "Caps Lock"},
    {"printscreen", "print_screen", "Print Screen"},
    {"scrolllock", "scroll_lock", "Scroll Lock"},
    {"pause", "pause", "Pause"},
    {"insert", "insert", "Insert"},
    {"home", "home", "Home"},
    {"pageup", "page_up", "Page Up"},
    {"delete", "delete", "Delete"},
    {"end", "end", "End"},
    {"pagedown", "page_down", "Page Down"},
    {"up", "up", "Up"},
    {"down", "down", "Down"},
    {"left", "left", "Left"},
    {"right", "right", "Right"},
    {"numlock", "num_lock", "Num Lock"},
    {"numlockclear", "num_lock", "Num Lock"},
    {"clear", "clear", "Clear"},
    {"application", "menu", "Menu"},
    {"menu", "menu", "Menu"},
    {"left ctrl", "left_ctrl", "Left Ctrl"},
    {"right ctrl", "right_ctrl", "Right Ctrl"},
    {"left shift", "left_shift", "Left Shift"},
    {"right shift", "right_shift", "Right Shift"},
    {"left alt", "left_alt", "Left Alt"},
    {"right alt", "right_alt", "Right Alt"},
    {"left gui", "left_win", "Left Win"},
    {"right gui", "right_win", "Right Win"},
    {"nonusbackslash", "iso_backslash", "ISO \\"},
};

// Literal names: the same in every language.
constexpr std::array<std::array<const char*, 2>, 5> kKeyLiterals = {{
    {"nonushash", "#"},
    {"sysreq", "SysRq"},
    {"modeswitch", "AltGr"},
    {"alterase", "Alt Erase"},
    {"ac back", "AC Back"},
}};

constexpr Named kMouse[] = {
    {"mouse1", "left", "Mouse Left"},
    {"mouse2", "right", "Mouse Right"},
    {"mouse3", "middle", "Mouse Middle"},
    {"mouse4", "button4", "Mouse 4"},
    {"mouse5", "button5", "Mouse 5"},
    {"mousewheelup", "wheel_up", "Mouse Wheel Up"},
    {"mousewheeldown", "wheel_down", "Mouse Wheel Down"},
};

// Beside a mouse icon, which already says "mouse" (an empty slug is a literal).
constexpr Named kMouseShort[] = {
    {"mouse1", "short_left", "Left"},
    {"mouse2", "short_right", "Right"},
    {"mouse3", "short_middle", "Middle"},
    {"mouse4", "", "4"},
    {"mouse5", "", "5"},
    {"mousewheelup", "short_wheel_up", "Up"},
    {"mousewheeldown", "short_wheel_down", "Down"},
};

// Pad names that read the same with every label set.
constexpr Named kPadCommon[] = {
    {"dpup", "dpad_up", "D-pad Up"},
    {"dpdown", "dpad_down", "D-pad Down"},
    {"dpleft", "dpad_left", "D-pad Left"},
    {"dpright", "dpad_right", "D-pad Right"},
    {"touchpad", "touchpad", "Touchpad"},
    {"leftx", "left_stick_x", "Left Stick X"},
    {"lefty", "left_stick_y", "Left Stick Y"},
    {"rightx", "right_stick_x", "Right Stick X"},
    {"righty", "right_stick_y", "Right Stick Y"},
};

constexpr Named kSticks[] = {
    {"", "left_stick_up", "Left Stick Up"},
    {"", "left_stick_right", "Left Stick Right"},
    {"", "left_stick_down", "Left Stick Down"},
    {"", "left_stick_left", "Left Stick Left"},
};

// Buttons whose label is printed on the pad and differs by family. A label starting
// with '@' is a text key slug (translated: Cross, Guide, ...), else a literal. The SDL
// names follow the Xbox layout, and each column matches its glyph set's icons
// (input/glyphs/glyph_table.cpp).
struct PadButton {
    const char* id;
    std::array<const char*, 5> label;  // PadLabels order
};

constexpr PadButton kPadButtons[] = {
    //                  Xbox      PlayStation  PS classic   Nintendo   Steam
    {"a",             {{"A", "@cross", "@cross", "A", "A"}}},
    {"b",             {{"B", "@circle", "@circle", "B", "B"}}},
    {"x",             {{"X", "@square", "@square", "X", "X"}}},
    {"y",             {{"Y", "@triangle", "@triangle", "Y", "Y"}}},
    {"leftshoulder",  {{"LB", "L1", "L1", "L", "L1"}}},
    {"rightshoulder", {{"RB", "R1", "R1", "R", "R1"}}},
    {"lefttrigger",   {{"LT", "L2", "L2", "ZL", "L2"}}},
    {"righttrigger",  {{"RT", "R2", "R2", "ZR", "R2"}}},
    {"leftstick",     {{"LS", "L3", "L3", "LS", "L3"}}},
    {"rightstick",    {{"RS", "R3", "R3", "RS", "R3"}}},
    {"back",          {{"View", "Share", "Select", "-", "View"}}},
    {"start",         {{"Menu", "Options", "Start", "+", "Menu"}}},
    {"guide",         {{"@guide", "PS", "PS", "@home", "Steam"}}},
};

constexpr Named kPadKeys[] = {
    {"", "cross", "Cross"},
    {"", "circle", "Circle"},
    {"", "square", "Square"},
    {"", "triangle", "Triangle"},
    {"", "guide", "Guide"},
    {"", "home", "Home"},
};

// misc1..misc6, paddle1..paddle4: the word and its number.
constexpr Named kPadNumbered[] = {
    {"paddle", "paddle", "Paddle"},
    {"misc", "misc", "Misc"},
};

constexpr Named kKeypadName = {"", "keypad", "Keypad"};

InputNamePart part(const char* group, const Named& n) {
    return {std::string("ui.input.") + group + "." + n.slug, n.english};
}

InputNamePart literal(std::string text) { return {std::string(), std::move(text)}; }

// "VolumeUp" -> "Volume Up", "audioplay" -> "Audioplay": an input this table does not
// know still reads as words.
std::string words(std::string_view raw) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        const bool upper_after_lower =
            i > 0 && std::isupper(c) && std::islower(static_cast<unsigned char>(raw[i - 1]));
        if (upper_after_lower) {
            out += ' ';
        }
        out += (i == 0) ? static_cast<char>(std::toupper(c)) : static_cast<char>(c);
    }
    return out;
}

template <std::size_t N>
const Named* find(const Named (&table)[N], std::string_view id) {
    for (const Named& n : table) {
        if (ieq(id, n.id)) {
            return &n;
        }
    }
    return nullptr;
}

}  // namespace

const std::vector<InputTextKey>& input_text_keys() {
    static const std::vector<InputTextKey> keys = [] {
        std::vector<InputTextKey> out;
        auto add = [&](const char* group, const Named& n) {
            const InputNamePart p = part(group, n);
            for (const InputTextKey& have : out) {
                if (have.key == p.key) {
                    return;
                }
            }
            out.push_back({p.key, p.text});
        };
        for (const Named& n : kKeys) add("key", n);
        add("key", kKeypadName);
        for (const Named& n : kMouse) add("mouse", n);
        for (const Named& n : kMouseShort) {
            if (n.slug[0] != '\0') add("mouse", n);
        }
        for (const Named& n : kPadCommon) add("pad", n);
        for (const Named& n : kSticks) add("pad", n);
        for (const Named& n : kPadKeys) add("pad", n);
        for (const Named& n : kPadNumbered) add("pad", n);
        return out;
    }();
    return keys;
}

bool is_mouse_bind(std::string_view value) { return find(kMouse, value) != nullptr; }

bool is_wheel_bind(std::string_view value) { return ieq(value, "mousewheelup") || ieq(value, "mousewheeldown"); }

BindDevice bind_device(std::string_view value, bool pad_column) {
    if (value.empty() || ieq(value, "none")) {
        return BindDevice::None;
    }
    if (pad_column) {
        return BindDevice::Pad;
    }
    return is_mouse_bind(value) ? BindDevice::Mouse : BindDevice::Keyboard;
}

InputName keyboard_input_name(std::string_view name) {
    if (name.empty()) {
        return {};
    }
    if (const Named* n = find(kKeys, name)) {
        return {part("key", *n)};
    }
    for (const auto& lit : kKeyLiterals) {
        if (ieq(name, lit[0])) {
            return {literal(lit[1])};
        }
    }
    if (istarts(name, "keypad ") && name.size() > 7) {
        InputName out{part("key", kKeypadName)};
        const InputName rest = keyboard_input_name(name.substr(7));
        out.insert(out.end(), rest.begin(), rest.end());
        return out;
    }
    if (name.size() == 1) {
        return {literal(std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])))))};
    }
    // F1..F24 and other names that are already words or a key cap's own text.
    if ((name[0] == 'F' || name[0] == 'f') && name.size() <= 3 &&
        std::isdigit(static_cast<unsigned char>(name[1]))) {
        return {literal("F" + std::string(name.substr(1)))};
    }
    // UTF-8 key caps of other layouts ("Ö") arrive as one character in several bytes.
    if (static_cast<unsigned char>(name[0]) >= 0x80) {
        return {literal(std::string(name))};
    }
    return {literal(words(name))};
}

InputName mouse_input_name(std::string_view value) {
    if (const Named* n = find(kMouse, value)) {
        return {part("mouse", *n)};
    }
    return {literal(words(value))};
}

InputName mouse_short_name(std::string_view value) {
    if (const Named* n = find(kMouseShort, value)) {
        return {n->slug[0] != '\0' ? part("mouse", *n) : literal(n->english)};
    }
    return mouse_input_name(value);
}

InputName pad_input_name(std::string_view name, PadLabels labels) {
    if (name.empty()) {
        return {};
    }
    for (const PadButton& b : kPadButtons) {
        if (!ieq(name, b.id)) {
            continue;
        }
        const char* label = b.label[static_cast<std::size_t>(labels)];
        if (label[0] == '@') {
            for (const Named& k : kPadKeys) {
                if (std::string_view(k.slug) == label + 1) {
                    return {part("pad", k)};
                }
            }
        }
        return {literal(label)};
    }
    if (const Named* n = find(kPadCommon, name)) {
        return {part("pad", *n)};
    }
    for (const Named& n : kPadNumbered) {
        const std::string_view word = n.id;
        if (istarts(name, word) && name.size() > word.size()) {
            return {part("pad", n), literal(std::string(name.substr(word.size())))};
        }
    }
    return {literal(words(name))};
}

InputName stick_input_name(int dir, bool dpad) {
    if (dir < 0 || dir > 3) {
        return {};
    }
    if (dpad) {
        return {part("pad", kPadCommon[dir == 0 ? 0 : dir == 1 ? 3 : dir == 2 ? 1 : 2])};
    }
    return {part("pad", kSticks[dir])};
}

InputName bind_input_name(std::string_view value, bool pad_column, PadLabels labels) {
    switch (bind_device(value, pad_column)) {
        case BindDevice::None: return {};
        case BindDevice::Pad: return pad_input_name(value, labels);
        case BindDevice::Mouse: return mouse_input_name(value);
        case BindDevice::Keyboard: return keyboard_input_name(value);
    }
    return {};
}

std::string english_name(const InputName& name) {
    std::string out;
    for (const InputNamePart& p : name) {
        if (!out.empty()) {
            out += ' ';
        }
        out += p.text;
    }
    return out;
}

}  // namespace sh::screens
