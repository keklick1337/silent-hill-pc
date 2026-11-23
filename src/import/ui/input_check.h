#pragma once

#include "import/ui/strings.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace sh::import::ui {

enum class Problem {
    None,
    Empty,             // no text
    NotFound,
    IsDirectory,
    Unreadable,
    CueNoFile,         // .cue without a FILE line
    CueTargetMissing,
    EmptyFile,
    CookedIso,         // 2048 bytes per sector
    BadSectorSize,     // raw sync present but the size is not a multiple of 2352, or 2336-byte sectors
    NotRawImage,       // no CD sync pattern at the start
};

struct ImageCheck {
    Problem problem = Problem::None;
    std::filesystem::path path;  // normalised input, what the importer should be given
    std::filesystem::path bin;   // the file with the sectors: `path` itself or the target of the .cue
    std::uint64_t size = 0;      // size of `bin`
    std::string detail;          // file name from the .cue for CueTargetMissing

    bool ok() const { return problem == Problem::None; }
};

// Text typed or pasted by the user to a path: trims blanks, removes one pair of surrounding quotes (a path dragged into
// a terminal), expands a leading "~/" and makes the path absolute.
std::filesystem::path normalize_path_text(std::string_view text);

// Decides from the file size and the first 12 bytes (CD sync pattern), never reads the whole image.
// A .cue is followed to the file named by its first FILE line, which is then checked the same way.
ImageCheck check_image(std::string_view text);

// Localised explanation of a failed check; empty for Problem::None.
std::string describe(Lang lang, const ImageCheck& check);

}  // namespace sh::import::ui
