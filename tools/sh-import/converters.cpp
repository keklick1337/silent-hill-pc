#include "sh-import/converters.h"

#include "engine/audio/music_converter.h"
#include "formats/convert/anim/anim_converter.h"
#include "formats/convert/archive/archive_converters.h"
#include "formats/convert/audio/audio_converter.h"
#include "formats/convert/cutscene/cutscene_converter.h"
#include "formats/convert/font/font_converter.h"
#include "formats/convert/level/level_converter.h"
#include "formats/convert/tim/tim_converter.h"

#include <utility>

namespace sh::import_tool {

std::vector<std::unique_ptr<sh::convert::Converter>> build_converters(const Options& options,
                                                                      sh::convert::model::SiblingReader sibling) {
    std::vector<std::unique_ptr<sh::convert::Converter>> list;
    if (options.is_enabled(Category::Textures)) {
        list.push_back(sh::convert::tim::make_converter());
    }
    if (options.is_enabled(Category::Models)) {
        sh::convert::model::Options model_options;
        model_options.sibling = sibling;
        list.push_back(sh::convert::model::make_converter(std::move(model_options)));
        list.push_back(sh::convert::anim::make_converter(sibling));
    }
    if (options.is_enabled(Category::Audio)) {
        list.push_back(sh::convert::audio::make_vab_converter());
        list.push_back(sh::convert::audio::make_xa_converter());
        list.push_back(sh::convert::audio::make_kdt_converter());
        if (options.music) {
            list.push_back(sh::audio::make_music_converter(
                sibling, {}, options.music_opus ? sh::audio::MusicFile::Opus : sh::audio::MusicFile::Wav));
        }
    }
    if (options.is_enabled(Category::Text)) {
        // The game's English text is gamedata/text/en of the repository: of the disc's text only the font is
        // imported. The glyph sheets of the base game data, when the target directory holds them: the font written is
        // then the whole game font. Without them it is the disc's letters, which is all the game needs from the import.
        const std::filesystem::path font_dir = options.font_dir.empty() ? options.out / "font" : options.font_dir;
        auto sheets = sh::convert::font::read_sheets(font_dir);
        list.push_back(sh::convert::font::make_converter(sheets ? std::move(sheets.value())
                                                               : std::vector<std::pair<std::string, std::string>>{}));
    }
    // The unified map files are written after the import (run.cpp); the converter gives the per-cell views only.
    if (options.is_enabled(Category::Levels) && options.level_views) {
        sh::convert::level::Options level_options;
        level_options.sibling = sibling;
        list.push_back(sh::convert::level::make_converter(std::move(level_options)));
    }
    if (options.is_enabled(Category::Data)) {
        list.push_back(sh::convert::cutscene::make_converter());
        list.push_back(sh::convert::archive::make_cmp_converter());
        list.push_back(sh::convert::archive::make_demo_converter());
    }
    return list;
}

}  // namespace sh::import_tool
