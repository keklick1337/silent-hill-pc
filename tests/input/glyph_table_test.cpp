// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The input prompt table (src/input/glyphs/glyph_table.h).

#include "framework/sh_test.h"

#include "input/glyphs/glyph_table.h"

#include <set>
#include <utility>

using namespace sh::input;

SH_TEST(glyph_keys_by_config_name) {
    SH_CHECK(glyph_for_key("W") == Glyph::KeyW);
    SH_CHECK(glyph_for_key("w") == Glyph::KeyW);
    SH_CHECK(glyph_for_key("0") == Glyph::Key0);
    SH_CHECK(glyph_for_key("9") == Glyph::Key9);
    SH_CHECK(glyph_for_key("F1") == Glyph::KeyF1);
    SH_CHECK(glyph_for_key("f12") == Glyph::KeyF12);
    SH_CHECK(glyph_for_key("F13") == Glyph::None);
    SH_CHECK(glyph_for_key("F0") == Glyph::None);
    SH_CHECK(glyph_for_key("F01") == Glyph::None);
    SH_CHECK(glyph_for_key("Return") == Glyph::KeyEnter);
    SH_CHECK(glyph_for_key("Left Shift") == Glyph::KeyShift);
    SH_CHECK(glyph_for_key("Right Ctrl") == Glyph::KeyCtrl);
    SH_CHECK(glyph_for_key("Right Alt") == Glyph::KeyAlt);
    SH_CHECK(glyph_for_key("CapsLock") == Glyph::KeyCapsLock);
    SH_CHECK(glyph_for_key("Numlock") == Glyph::KeyNumLock);
    SH_CHECK(glyph_for_key("PrintScreen") == Glyph::KeyPrintScreen);
    SH_CHECK(glyph_for_key("`") == Glyph::KeyGrave);
    SH_CHECK(glyph_for_key("\\") == Glyph::KeyBackslash);
    SH_CHECK(glyph_for_key("Left") == Glyph::KeyLeft);
    // A keypad key shows the cap it doubles.
    SH_CHECK(glyph_for_key("Keypad 5") == Glyph::Key5);
    SH_CHECK(glyph_for_key("Keypad Enter") == Glyph::KeyEnter);
    SH_CHECK(glyph_for_key("keypad +") == Glyph::KeyPlus);
    SH_CHECK(glyph_for_key("Mouse1") == Glyph::MouseLeft);
    SH_CHECK(glyph_for_key("Mouse2") == Glyph::MouseRight);
    SH_CHECK(glyph_for_key("Mouse3") == Glyph::MouseMiddle);
    SH_CHECK(glyph_for_key("Mouse4") == Glyph::Mouse);
    SH_CHECK(glyph_for_key("Mouse5") == Glyph::Mouse);
    SH_CHECK(glyph_for_key("MouseWheelUp") == Glyph::MouseWheelUp);
    SH_CHECK(glyph_for_key("mousewheeldown") == Glyph::MouseWheelDown);
    SH_CHECK(glyph_for_key("NONE") == Glyph::None);
    SH_CHECK(glyph_for_key("") == Glyph::None);
    SH_CHECK(glyph_for_key("Left Windows") == Glyph::None);
}

SH_TEST(glyph_pad_buttons_per_family) {
    SH_CHECK(glyph_for_pad(GlyphSet::Xbox, "a") == Glyph::XboxA);
    SH_CHECK(glyph_for_pad(GlyphSet::Auto, "a") == Glyph::XboxA);
    SH_CHECK(glyph_for_pad(GlyphSet::PlayStation, "a") == Glyph::PsCross);
    SH_CHECK(glyph_for_pad(GlyphSet::PlayStation, "y") == Glyph::PsTriangle);
    SH_CHECK(glyph_for_pad(GlyphSet::PlayStation, "back") == Glyph::PsShare);
    SH_CHECK(glyph_for_pad(GlyphSet::PlayStation, "start") == Glyph::PsOptions);
    SH_CHECK(glyph_for_pad(GlyphSet::PlayStationClassic, "back") == Glyph::PsSelect);
    SH_CHECK(glyph_for_pad(GlyphSet::PlayStationClassic, "start") == Glyph::PsStart);
    SH_CHECK(glyph_for_pad(GlyphSet::Nintendo, "lefttrigger") == Glyph::NintendoZL);
    SH_CHECK(glyph_for_pad(GlyphSet::Nintendo, "START") == Glyph::NintendoPlus);
    SH_CHECK(glyph_for_pad(GlyphSet::Steam, "rightshoulder") == Glyph::SteamR1);
    SH_CHECK(glyph_for_pad(GlyphSet::Xbox, "leftshoulder") == Glyph::XboxLB);
    SH_CHECK(glyph_for_pad(GlyphSet::Steam, "leftstick") == Glyph::StickLeftPress);
    SH_CHECK(glyph_for_pad(GlyphSet::Xbox, "rightstick") == Glyph::StickRightPress);
    SH_CHECK(glyph_for_pad(GlyphSet::Xbox, "dpup") == Glyph::DpadUp);
    SH_CHECK(glyph_for_pad(GlyphSet::PlayStationClassic, "dpleft") == Glyph::ClassicDpadLeft);
    SH_CHECK(glyph_for_pad(GlyphSet::PlayStation, "dpright") == Glyph::DpadRight);
    SH_CHECK(glyph_for_pad(GlyphSet::Xbox, "paddle1") == Glyph::PadEmpty);
    SH_CHECK(glyph_for_pad(GlyphSet::Xbox, "") == Glyph::PadEmpty);

    SH_CHECK(glyph_for_stick(GlyphSet::Xbox, GlyphDirection::Up, false) == Glyph::StickUp);
    SH_CHECK(glyph_for_stick(GlyphSet::PlayStationClassic, GlyphDirection::Left, false) == Glyph::StickLeft);
    SH_CHECK(glyph_for_stick(GlyphSet::PlayStationClassic, GlyphDirection::Down, true) == Glyph::ClassicDpadDown);
    SH_CHECK(glyph_for_stick(GlyphSet::Nintendo, GlyphDirection::Right, true) == Glyph::DpadRight);
    SH_CHECK(glyph_for_stick(GlyphSet::Xbox, static_cast<GlyphDirection>(4), true) == Glyph::PadEmpty);
}

SH_TEST(glyph_sets_and_auto_detection) {
    SH_CHECK(glyph_set_from(0) == GlyphSet::Auto);
    SH_CHECK(glyph_set_from(5) == GlyphSet::Steam);
    SH_CHECK(glyph_set_from(6) == GlyphSet::Auto);
    SH_CHECK(glyph_set_from(-1) == GlyphSet::Auto);
    const char* const labels[] = {"Auto", "PS_Classic", "PS_Modern", "Xbox", "Nintendo", "Steam"};
    for (int i = 0; i < kGlyphSets; ++i) {
        SH_CHECK(std::string_view(glyph_set_label(static_cast<GlyphSet>(i))) == labels[i]);
    }

    SH_CHECK(glyph_set_for({}) == GlyphSet::Xbox);  // no controller
    SH_CHECK(glyph_set_for({0, 0, 0, false}) == GlyphSet::Xbox);
    SH_CHECK(glyph_set_for({4, 0x054C, 0x0268, false}) == GlyphSet::PlayStationClassic);
    SH_CHECK(glyph_set_for({5, 0x054C, 0x09CC, false}) == GlyphSet::PlayStation);
    SH_CHECK(glyph_set_for({6, 0x054C, 0x0CE6, false}) == GlyphSet::PlayStation);
    for (int type = 7; type <= 10; ++type) {
        SH_CHECK(glyph_set_for({type, 0x057E, 0x2009, false}) == GlyphSet::Nintendo);
    }
    SH_CHECK(glyph_set_for({3, 0x045E, 0x0B12, false}) == GlyphSet::Xbox);
    // Valve's own controllers, whatever type they report.
    SH_CHECK(glyph_set_for({0, 0x28DE, 0x1102, false}) == GlyphSet::Steam);
    SH_CHECK(glyph_set_for({2, 0x28DE, 0x1142, false}) == GlyphSet::Steam);
    SH_CHECK(glyph_set_for({5, 0x28DE, 0x1205, false}) == GlyphSet::Steam);
    // Steam's virtual pad is the Deck's own controls only on a Deck, and only while it looks generic.
    SH_CHECK(glyph_set_for({2, 0x28DE, 0x11FF, true}) == GlyphSet::Steam);
    SH_CHECK(glyph_set_for({12, 0x28DE, 0x11FF, true}) == GlyphSet::Steam);
    SH_CHECK(glyph_set_for({2, 0x28DE, 0x11FF, false}) == GlyphSet::Xbox);
    SH_CHECK(glyph_set_for({5, 0x28DE, 0x11FF, true}) == GlyphSet::PlayStation);
}

SH_TEST(glyph_cells_on_the_sheet) {
    // The first prompt is the top-left cell; its dark form is 8 cells (64 texels) to the right.
    const GlyphCell one = glyph_cell(Glyph::Key1, false);
    SH_CHECK(one.u == 0 && one.v == 0 && one.width == 8 && one.screen_width == 16);
    const GlyphCell one_dark = glyph_cell(Glyph::Key1, true);
    SH_CHECK(one_dark.u == 64 && one_dark.v == 0);
    // Wide caps take two cells.
    const GlyphCell enter = glyph_cell(Glyph::KeyEnter, false);
    SH_CHECK(enter.width == 16 && enter.screen_width == 32);
    // Anything out of range is the empty button.
    const GlyphCell empty = glyph_cell(Glyph::PadEmpty, false);
    const GlyphCell none = glyph_cell(Glyph::None, false);
    SH_CHECK(none.u == empty.u && none.v == empty.v && none.width == empty.width);

    // No two prompts share a cell, a wide one never crosses into the dark half, and all of it fits a byte.
    std::set<std::pair<int, int>> used;
    for (int i = 0; i < static_cast<int>(Glyph::Count); ++i) {
        const GlyphCell c = glyph_cell(static_cast<Glyph>(i), false);
        const GlyphCell d = glyph_cell(static_cast<Glyph>(i), true);
        SH_CHECK(c.u + c.width <= 64);
        SH_CHECK(d.u == c.u + 64 && d.v == c.v && d.u + d.width <= 128 && c.v + 8 <= 256);
        for (int u = c.u; u < c.u + c.width; u += 8) {
            SH_CHECK(used.insert({u, c.v}).second);
        }
    }
}
