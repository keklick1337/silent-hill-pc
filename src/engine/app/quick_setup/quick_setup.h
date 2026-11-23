// SPDX-License-Identifier: GPL-3.0-or-later
// The quick setup: a window of its own, shown before the disc
// import and the game on the first start or with --setup, that asks for the language, the renderer, the graphics
// preset and the display, with defaults from the detected hardware. It needs nothing imported: the text is the
// base data's and the letters come from gamedata/font and the system's fonts.
#pragma once

#include "engine/app/quick_setup/setup_model.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sh::quick_setup {

struct Request {
    std::filesystem::path base_data;      // the gamedata/ beside the executable: text/, font/, menu/menu_style.json
    std::filesystem::path icons;          // the window icon's folder
    std::filesystem::path controller_db;  // gamecontrollerdb.txt
    // --setup-script: the screen is driven by these commands instead of a player, and may then run on SDL's
    // offscreen video driver (tests and screenshots).
    std::filesystem::path script;
    std::string language;          // config.cfg's language ("auto" or a code)
    std::vector<Preset> presets;   // the preset table, in its order
    Resolution window{1024, 768};  // config.cfg's width and height: the size when the display lists none
};

enum class Outcome {
    Applied,    // `choices` holds what to write
    Skipped,    // the player kept the current settings
    Quit,       // the window was closed: the game ends
    NoDisplay,  // no display someone can answer (offscreen or dummy video, no SDL video): nothing was asked
};

struct Answer {
    Outcome outcome = Outcome::NoDisplay;
    Choices choices;
    Hardware hardware;
};

// Runs the screen until it is answered. SDL's video subsystem is started and stopped inside.
Answer run(const Request& request);

}  // namespace sh::quick_setup
