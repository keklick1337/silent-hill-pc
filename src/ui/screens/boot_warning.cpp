// SPDX-License-Identifier: GPL-3.0-or-later
// The boot warning on the game's display (boot_warning.h).
#include "ui/screens/boot_warning.h"

#include <SDL3/SDL_scancode.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/screens/warning_layout.h"
#include "input/pad.h"

extern "C" const char* PcPort_GetGameDataPath(void);
// The keys as the input system last read them; null before it started.
extern "C" const unsigned char* g_sdlKeyboardState;

namespace sh::ui {

namespace {

namespace gs = sh::game_state;
namespace sc = sh::screens;

const sc::WarningLayout& layout() {
    static const sc::WarningLayout loaded = [] {
        char mods_dir[1024];
        std::string source;
        PcPaths_ExeFile(mods_dir, sizeof(mods_dir), "mods");
        sh::Result<sc::WarningLayout> read = sc::load_warning_layout(PcPort_GetGameDataPath(), mods_dir, &source);
        if (!read) {
            SH_LOG_INFO(GAME, "warning screen layout: %s - built-in layout used", read.error().c_str());
            return sc::WarningLayout{};
        }
        return read.value();
    }();
    return loaded;
}

// Where the picture is read to: an 8-bit image across the texture pages from 13, 32 texels in, with its palette
// at (768, 480).
gs::ImageHome picture_home() {
    gs::ImageHome home;
    home.tpage[0] = 1;
    home.tpage[1] = 13;
    home.u = 32;
    home.clut_x = 768;
    home.clut_y = 480;
    return home;
}

// The game's pad binds are not set up this early, so the keys are read as they are: the confirm keys of the
// default binds, or what the pad layer takes for "skip".
bool skip_pressed() {
    static constexpr SDL_Scancode kSkipKeys[] = {SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_RETURN2,
                                                 SDL_SCANCODE_SPACE,  SDL_SCANCODE_C,        SDL_SCANCODE_V};
    if (g_sdlKeyboardState != nullptr) {
        for (const SDL_Scancode key : kSkipKeys) {
            if (g_sdlKeyboardState[key] != 0) {
                return true;
            }
        }
    }
    return sh::input::skip_held();
}

// The picture lies across several texture pages; one quad for the part on each, stretched over the frame by the
// layout so that the parts meet.
void draw_picture() {
    const sc::WarningLayout& l = layout();
    const gs::ImageHome home = picture_home();
    for (std::size_t part = 0; part < l.quads.size(); part++) {
        const sc::WarningQuad& placed = l.quads[part];
        gs::BootQuad quad;
        quad.x0 = static_cast<std::int16_t>(placed.x);
        quad.x1 = static_cast<std::int16_t>(placed.x + placed.w);
        quad.y0 = static_cast<std::int16_t>(l.quad_y);
        quad.y1 = static_cast<std::int16_t>(l.quad_y + l.quad_h);
        quad.u0 = static_cast<std::uint8_t>(placed.u0);
        quad.u1 = static_cast<std::uint8_t>(placed.u1);
        quad.v0 = 0;
        quad.v1 = static_cast<std::uint8_t>(l.tex_height);
        quad.page = l.first_page + static_cast<int>(part);
        quad.clut_x = home.clut_x;
        quad.clut_y = home.clut_y;
        gs::boot_frame_quad(quad);
    }
}

}  // namespace

void play_boot_warning() {
    if (g_PcConfig.skipIntros != 0) {
        return;
    }

    // The display as the logo screens that follow set it; the picture is read and waited for, then frames are
    // paced at 60 a second whatever the stage before left.
    gs::boot_frame_begin();
    gs::boot_frame_load_tim(gs::DiscFile::WarningTim, picture_home());
    gs::boot_frame_show();

    const sc::WarningLayout& l = layout();
    WarningSequence sequence(l.fade_step, l.hold_frames, l.black_frames);
    while (!sequence.done()) {
        // The darkening is drawn over the picture: it is submitted first.
        if (sequence.darkened()) {
            gs::boot_frame_fade(sequence.darkness());
        }
        draw_picture();
        // A frame is shown one present after it was built, so the last one is presented without starting another:
        // a black frame, not a stale one of the picture, is on the display when the next screen sets it up.
        gs::boot_frame_present(!sequence.final_frame());
        sequence.advance(sequence.skippable() && skip_pressed());
    }
}

}  // namespace sh::ui
