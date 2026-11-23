// SPDX-License-Identifier: GPL-3.0-or-later
// The fonts a character missing from the game font can be drawn with: TrueType
// and OpenType files of the active mods, of gamedata/font and of the system, found by walking the standard font
// directories - no font service of the system is asked. For a character the catalogue names one face: a mod's
// font first, then a family the language asks for, then a font of the game data, then the system face that suits
// the game's bold text best.
#pragma once

#include "i18n/core/font_file.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::i18n {

// Where this system keeps its fonts (they need not exist): the per-user and shared directories of Linux, the
// Fonts folders of Windows, the Library folders of macOS. SH_FONT_DIRS, when set, replaces the list (entries
// separated like PATH; empty = no system fonts).
std::vector<std::filesystem::path> system_font_directories();

enum class FontTier : std::uint8_t { Mod, GameData, System };

// Why a face was chosen, weakest first: the best-suited system font, a font of the game data, a family the
// language names, a mod's font.
enum class FontRank : std::uint8_t { System, GameData, Preferred, Mod };

// How well a face suits game text next to the game's own bold letters: a sturdy upright sans scores highest,
// italics, light weights, monospaced, decorative and symbol fonts lowest.
int font_style_score(const FontFace& face);

class FontCatalog {
public:
    struct Entry {
        FontFace face;
        FontTier tier = FontTier::System;
        int score = 0;  // font_style_score
    };

    // Every .ttf, .otf, .ttc and .otc under `dir`, subdirectories included, in path order. Returns the faces added.
    std::size_t add_directory(const std::filesystem::path& dir, FontTier tier);
    std::size_t add_file(const std::filesystem::path& file, FontTier tier);
    void add_face(FontFace face, FontTier tier);

    const std::vector<Entry>& entries() const { return entries_; }

    // The face to draw `cp` with, null when no face has it. `preferred` are family names in the order the
    // language wants them; `in_use` are faces already drawing other characters, which win a close call so one
    // script is drawn with one font. `rank` receives why the face won; `excluded` faces are passed over (files
    // that turned out unreadable). The faces stay where they are until more are added.
    const FontFace* choose(char32_t cp, std::span<const std::string> preferred,
                           std::span<const FontFace* const> in_use = {}, FontRank* rank = nullptr,
                           std::span<const FontFace* const> excluded = {}) const;

private:
    std::vector<Entry> entries_;
};

}  // namespace sh::i18n
