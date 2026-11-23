// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/glyphs/glyph_table.h"

#include <array>
#include <cctype>
#include <cstdint>

namespace sh::input {
namespace {

struct Cell {
    std::uint8_t column, row, cells;
};

constexpr Cell kCells[] = {
#define SH_INPUT_GLYPH(name, column, row, cells) {column, row, cells},
#include "input/glyphs/glyph_cells.inc"
#undef SH_INPUT_GLYPH
};
static_assert(std::size(kCells) == static_cast<std::size_t>(Glyph::Count));

// A sheet cell is 8 texels of the page; the dark forms are 8 cells to the right of the light ones.
constexpr int kCellTexels = 8;
constexpr int kDarkColumns = 8;

bool same_ignoring_case(std::string_view a, std::string_view b) {
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

struct Named {
    std::string_view name;
    Glyph glyph;
};

// Config names of keys (SDL's scancode names) and of the mouse. Letters, digits and F-keys are not listed: their
// name is their cap.
constexpr Named kNamedKeys[] = {
    {"Return", Glyph::KeyEnter},
    {"Enter", Glyph::KeyEnter},
    {"Escape", Glyph::KeyEscape},
    {"Backspace", Glyph::KeyBackspace},
    {"Tab", Glyph::KeyTab},
    {"Space", Glyph::KeySpace},
    {"CapsLock", Glyph::KeyCapsLock},
    {"PageUp", Glyph::KeyPageUp},
    {"PageDown", Glyph::KeyPageDown},
    {"Delete", Glyph::KeyDelete},
    {"Insert", Glyph::KeyInsert},
    {"Home", Glyph::KeyHome},
    {"End", Glyph::KeyEnd},
    {"PrintScreen", Glyph::KeyPrintScreen},
    {"ScrollLock", Glyph::KeyScrollLock},
    {"Pause", Glyph::KeyPause},
    {"Numlock", Glyph::KeyNumLock},
    {"Up", Glyph::KeyUp},
    {"Down", Glyph::KeyDown},
    {"Left", Glyph::KeyLeft},
    {"Right", Glyph::KeyRight},
    {"Left Shift", Glyph::KeyShift},
    {"Right Shift", Glyph::KeyShift},
    {"Left Ctrl", Glyph::KeyCtrl},
    {"Right Ctrl", Glyph::KeyCtrl},
    {"Left Alt", Glyph::KeyAlt},
    {"Right Alt", Glyph::KeyAlt},
    {"-", Glyph::KeyMinus},
    {"+", Glyph::KeyPlus},
    {"=", Glyph::KeyEquals},
    {"[", Glyph::KeyLeftBracket},
    {"]", Glyph::KeyRightBracket},
    {"\\", Glyph::KeyBackslash},
    {"'", Glyph::KeyApostrophe},
    {";", Glyph::KeySemicolon},
    {"*", Glyph::KeyAsterisk},
    {"`", Glyph::KeyGrave},
    {",", Glyph::KeyComma},
    {".", Glyph::KeyPeriod},
    {"/", Glyph::KeySlash},
    {"Mouse1", Glyph::MouseLeft},
    {"Mouse2", Glyph::MouseRight},
    {"Mouse3", Glyph::MouseMiddle},
    {"Mouse4", Glyph::Mouse},
    {"Mouse5", Glyph::Mouse},
    {"MouseWheelUp", Glyph::MouseWheelUp},
    {"MouseWheelDown", Glyph::MouseWheelDown},
};

constexpr Glyph kLetterKeys[26] = {
    Glyph::KeyA, Glyph::KeyB, Glyph::KeyC, Glyph::KeyD, Glyph::KeyE, Glyph::KeyF, Glyph::KeyG, Glyph::KeyH, Glyph::KeyI,
    Glyph::KeyJ, Glyph::KeyK, Glyph::KeyL, Glyph::KeyM, Glyph::KeyN, Glyph::KeyO, Glyph::KeyP, Glyph::KeyQ, Glyph::KeyR,
    Glyph::KeyS, Glyph::KeyT, Glyph::KeyU, Glyph::KeyV, Glyph::KeyW, Glyph::KeyX, Glyph::KeyY, Glyph::KeyZ,
};
constexpr Glyph kDigitKeys[10] = {
    Glyph::Key0, Glyph::Key1, Glyph::Key2, Glyph::Key3, Glyph::Key4,
    Glyph::Key5, Glyph::Key6, Glyph::Key7, Glyph::Key8, Glyph::Key9,
};
constexpr Glyph kFunctionKeys[12] = {
    Glyph::KeyF1, Glyph::KeyF2, Glyph::KeyF3, Glyph::KeyF4,  Glyph::KeyF5,  Glyph::KeyF6,
    Glyph::KeyF7, Glyph::KeyF8, Glyph::KeyF9, Glyph::KeyF10, Glyph::KeyF11, Glyph::KeyF12,
};

// The buttons every family has, in the order of the rows below.
constexpr std::string_view kFaceButtons[] = {
    "a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger", "back", "start",
};
using FaceRow = std::array<Glyph, std::size(kFaceButtons)>;

constexpr FaceRow kPlayStationClassic = {Glyph::PsCross, Glyph::PsCircle, Glyph::PsSquare, Glyph::PsTriangle,
                                         Glyph::PsL1,    Glyph::PsR1,     Glyph::PsL2,     Glyph::PsR2,
                                         Glyph::PsSelect, Glyph::PsStart};
constexpr FaceRow kPlayStation = {Glyph::PsCross, Glyph::PsCircle, Glyph::PsSquare, Glyph::PsTriangle,
                                  Glyph::PsL1,    Glyph::PsR1,     Glyph::PsL2,     Glyph::PsR2,
                                  Glyph::PsShare, Glyph::PsOptions};
constexpr FaceRow kXbox = {Glyph::XboxA,  Glyph::XboxB,  Glyph::XboxX,  Glyph::XboxY,    Glyph::XboxLB,
                           Glyph::XboxRB, Glyph::XboxLT, Glyph::XboxRT, Glyph::XboxView, Glyph::XboxMenu};
constexpr FaceRow kNintendo = {Glyph::NintendoA, Glyph::NintendoB,  Glyph::NintendoX,  Glyph::NintendoY,
                               Glyph::NintendoL, Glyph::NintendoR,  Glyph::NintendoZL, Glyph::NintendoZR,
                               Glyph::NintendoMinus, Glyph::NintendoPlus};
constexpr FaceRow kSteam = {Glyph::SteamA,  Glyph::SteamB,  Glyph::SteamX,  Glyph::SteamY,    Glyph::SteamL1,
                            Glyph::SteamR1, Glyph::SteamL2, Glyph::SteamR2, Glyph::SteamView, Glyph::SteamMenu};

const FaceRow& face_row(GlyphSet set) {
    switch (set) {
        case GlyphSet::PlayStationClassic: return kPlayStationClassic;
        case GlyphSet::PlayStation: return kPlayStation;
        case GlyphSet::Nintendo: return kNintendo;
        case GlyphSet::Steam: return kSteam;
        default: return kXbox;  // Auto with nothing to go by
    }
}

// SDL_GamepadType, by value (glyph_sheet.cpp checks them against SDL).
namespace pad_type {
constexpr int kUnknown = 0, kStandard = 1, kXbox360 = 2, kXboxOne = 3, kPlayStation3 = 4, kPlayStation4 = 5;
constexpr int kPlayStation5 = 6, kSwitchPro = 7, kJoyConLeft = 8, kJoyConRight = 9, kJoyConPair = 10;
constexpr int kSteam = 12;  // not in every SDL release; a release without it never reports the number
}  // namespace pad_type

constexpr int kValve = 0x28DE;
constexpr int kSteamControllerWired = 0x1102, kSteamControllerWireless = 0x1142, kSteamDeckPad = 0x1205;
constexpr int kSteamVirtualPad = 0x11FF;

}  // namespace

GlyphSet glyph_set_from(int value) {
    return value >= 0 && value < kGlyphSets ? static_cast<GlyphSet>(value) : GlyphSet::Auto;
}

const char* glyph_set_label(GlyphSet set) {
    switch (set) {
        case GlyphSet::PlayStationClassic: return "PS_Classic";
        case GlyphSet::PlayStation: return "PS_Modern";
        case GlyphSet::Xbox: return "Xbox";
        case GlyphSet::Nintendo: return "Nintendo";
        case GlyphSet::Steam: return "Steam";
        default: return "Auto";
    }
}

GlyphSet glyph_set_for(const PadIdentity& pad) {
    if (pad.type < 0) {
        return GlyphSet::Xbox;
    }
    if (pad.vendor == kValve) {
        if (pad.product == kSteamControllerWired || pad.product == kSteamControllerWireless ||
            pad.product == kSteamDeckPad) {
            return GlyphSet::Steam;
        }
        // On a Deck the built-in controls arrive as Steam's virtual pad, which reports itself as a generic or an
        // Xbox controller.
        const bool generic = pad.type == pad_type::kUnknown || pad.type == pad_type::kStandard ||
                             pad.type == pad_type::kXbox360 || pad.type == pad_type::kXboxOne ||
                             pad.type == pad_type::kSteam;
        if (pad.on_steam_deck && pad.product == kSteamVirtualPad && generic) {
            return GlyphSet::Steam;
        }
    }
    switch (pad.type) {
        case pad_type::kPlayStation3: return GlyphSet::PlayStationClassic;
        case pad_type::kPlayStation4:
        case pad_type::kPlayStation5: return GlyphSet::PlayStation;
        case pad_type::kSwitchPro:
        case pad_type::kJoyConLeft:
        case pad_type::kJoyConRight:
        case pad_type::kJoyConPair: return GlyphSet::Nintendo;
        default: return GlyphSet::Xbox;
    }
}

Glyph glyph_for_key(std::string_view name) {
    // A keypad key shows the cap of the key it doubles.
    constexpr std::string_view kKeypad = "Keypad ";
    if (name.size() >= kKeypad.size() && same_ignoring_case(name.substr(0, kKeypad.size()), kKeypad)) {
        name.remove_prefix(kKeypad.size());
    }
    if (name.size() == 1) {
        const int c = std::tolower(static_cast<unsigned char>(name[0]));
        if (c >= 'a' && c <= 'z') {
            return kLetterKeys[c - 'a'];
        }
        if (c >= '0' && c <= '9') {
            return kDigitKeys[c - '0'];
        }
    }
    if ((name.size() == 2 || name.size() == 3) && (name[0] == 'F' || name[0] == 'f')) {
        int n = 0;
        bool digits = true;
        for (const char c : name.substr(1)) {
            digits = digits && c >= '0' && c <= '9';
            n = n * 10 + (c - '0');
        }
        if (digits && name[1] != '0' && n >= 1 && n <= 12) {
            return kFunctionKeys[n - 1];
        }
    }
    for (const Named& k : kNamedKeys) {
        if (same_ignoring_case(name, k.name)) {
            return k.glyph;
        }
    }
    return Glyph::None;
}

Glyph glyph_for_stick(GlyphSet set, GlyphDirection direction, bool dpad) {
    constexpr Glyph kStick[] = {Glyph::StickUp, Glyph::StickRight, Glyph::StickDown, Glyph::StickLeft};
    constexpr Glyph kDpad[] = {Glyph::DpadUp, Glyph::DpadRight, Glyph::DpadDown, Glyph::DpadLeft};
    constexpr Glyph kClassicDpad[] = {Glyph::ClassicDpadUp, Glyph::ClassicDpadRight, Glyph::ClassicDpadDown,
                                      Glyph::ClassicDpadLeft};
    const int d = static_cast<int>(direction);
    if (d < 0 || d > 3) {
        return Glyph::PadEmpty;
    }
    if (!dpad) {
        return kStick[d];
    }
    return set == GlyphSet::PlayStationClassic ? kClassicDpad[d] : kDpad[d];
}

Glyph glyph_for_pad(GlyphSet set, std::string_view button) {
    if (set == GlyphSet::Auto) {
        set = GlyphSet::Xbox;
    }
    for (std::size_t i = 0; i < std::size(kFaceButtons); ++i) {
        if (same_ignoring_case(button, kFaceButtons[i])) {
            return face_row(set)[i];
        }
    }
    if (same_ignoring_case(button, "leftstick")) return Glyph::StickLeftPress;
    if (same_ignoring_case(button, "rightstick")) return Glyph::StickRightPress;
    if (same_ignoring_case(button, "dpup")) return glyph_for_stick(set, GlyphDirection::Up, true);
    if (same_ignoring_case(button, "dpright")) return glyph_for_stick(set, GlyphDirection::Right, true);
    if (same_ignoring_case(button, "dpdown")) return glyph_for_stick(set, GlyphDirection::Down, true);
    if (same_ignoring_case(button, "dpleft")) return glyph_for_stick(set, GlyphDirection::Left, true);
    return Glyph::PadEmpty;
}

GlyphCell glyph_cell(Glyph glyph, bool dark) {
    int index = static_cast<int>(glyph);
    if (index < 0 || index >= static_cast<int>(Glyph::Count)) {
        index = static_cast<int>(Glyph::PadEmpty);
    }
    const Cell& c = kCells[index];
    GlyphCell out;
    out.u = (c.column + (dark ? kDarkColumns : 0)) * kCellTexels;
    out.v = c.row * kCellTexels;
    out.width = c.cells * kCellTexels;
    out.screen_width = c.cells * kGlyphScreenSize;
    return out;
}

}  // namespace sh::input
