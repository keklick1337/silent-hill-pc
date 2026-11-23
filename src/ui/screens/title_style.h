// SPDX-License-Identifier: GPL-3.0-or-later
// Which title picture the player sees (config.cfg `menu_style`, console MENUSTYLE).
//
//   us        the background, the logo and all (TIM/TITLE_E.TIM);
//   japanese  black with the stylized logo (TIM/TITLE.TIM).
//
// Both pictures are on the disc and are read from the player's own game data.
#pragma once

namespace sh::ui {

// The values of `menu_style`: the default, or one of the two pictures.
inline constexpr int kTitleStyleAuto = -1;
inline constexpr int kTitleStyleUs = 0;
inline constexpr int kTitleStyleJapanese = 1;

// The style on screen for the configured value: the default is the US picture.
constexpr int shown_title_style(int configured) {
    return configured == kTitleStyleJapanese ? kTitleStyleJapanese : kTitleStyleUs;
}

// The word config.cfg has for a value ("auto" for anything that is not one of the two).
constexpr const char* title_style_name(int configured) {
    return configured == kTitleStyleUs ? "us" : configured == kTitleStyleJapanese ? "japanese" : "auto";
}

// The title art for the configured style.
class TitleArt {
public:
    // The style in effect.
    int style();
    bool is_japanese_picture() { return style() == kTitleStyleJapanese; }

    // Tells the title picture's slot where the style's art lives in texture memory.
    void place();
    // place(), then the art is queued from the disc.
    void load();
    // Reads the art again after `menu_style` changed. The slot belongs to the title screen only while it is up:
    // false, and nothing read, anywhere else.
    bool reload();
};

TitleArt& title_art();

}  // namespace sh::ui
