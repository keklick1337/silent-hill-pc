// The importer's "font" converter: 1ST/FONT16.TIM of the disc -> convert/fonts/game.png + game.font.json, the
// game font (game_font.h) as it stands at the import: the disc's letters with the given glyph sheets laid over
// them. The game takes the disc's part of it and lays the sheets of gamedata/font over that again when it starts,
// so a sheet edited later needs no new import; the files serve the tools that measure text.
#pragma once

#include "formats/convert/converter.h"
#include "formats/convert/font/game_font.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sh::convert::font {

inline constexpr const char* kAtlasPath = "fonts/game.png";      // relative to convert/
inline constexpr const char* kMetricsPath = "fonts/game.font.json";

// `sheets` are the glyph sheets of the game data in the order they apply (read_sheets of <gamedata>/font); none
// gives the disc's part alone.
std::unique_ptr<Converter> make_converter(std::vector<std::pair<std::string, std::string>> sheets = {});

}  // namespace sh::convert::font
