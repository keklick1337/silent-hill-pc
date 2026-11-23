// SPDX-License-Identifier: GPL-3.0-or-later
// The title art of the mounted disc (title_style.h).
#include "ui/screens/title_style.h"

#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"

namespace sh::ui {

namespace {

namespace gs = sh::game_state;

// Where a title picture lives in texture memory: an 8-bit image across the pages from 13, 32 texels in,
// with its palette at (224, 15).
gs::ImageHome picture_home() {
    gs::ImageHome home;
    home.tpage[0] = 1;
    home.tpage[1] = 13;
    home.u = 32;
    home.clut_x = 224;
    home.clut_y = 15;
    return home;
}

}  // namespace

TitleArt& title_art() {
    static TitleArt art;
    return art;
}

int TitleArt::style() { return shown_title_style(g_PcConfig.menuStyle); }

void TitleArt::place() { gs::set_image_home(gs::ImageSlot::Title, picture_home()); }

void TitleArt::load() {
    const bool japanese = is_japanese_picture();
    place();
    gs::queue_tim(japanese ? gs::DiscFile::TitleTim : gs::DiscFile::TitleETim, gs::ImageSlot::Title);
}

bool TitleArt::reload() {
    // The picture has one slot, so another style is another image read into it; away from the title the slot
    // holds what the screen that is up draws from.
    if (gs::game_state() != gs::GameState::MainMenu) {
        return false;
    }
    load();
    SH_LOG("[TITLE] style = %s", title_style_name(style()));
    return true;
}

}  // namespace sh::ui
