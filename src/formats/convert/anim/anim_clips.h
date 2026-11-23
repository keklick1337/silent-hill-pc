#pragma once

#include "formats/common/status.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

// Named animation clips of the ANM files: the playback entries of the game's animation tables (anim_clips.json,
// edited by hand, checked by tools/scripts/check_anim_clips.py, compiled into this library).
namespace sh::convert::anim {

struct Clip {
    std::string name;
    int start = 0;  // first keyframe, inclusive
    int end = 0;    // last keyframe, inclusive
    double fps = 30.0;
    bool loop = false;
    bool variable_speed = false;  // the game computes the speed at run time; fps is 30
    bool reverse = false;         // the game plays it from end to start
};

using ClipCatalog = std::map<std::string, std::vector<Clip>>;  // by game path, "ANIM/HB_BASE.ANM"

Result<ClipCatalog> parse_clip_catalog(std::string_view json_text);

// The catalog compiled into the library (parsed once).
const ClipCatalog& builtin_clips();

// Clips of `anm_path` (case-insensitive), empty when the file has no table.
std::vector<Clip> clips_for(const ClipCatalog& catalog, std::string_view anm_path);

}  // namespace sh::convert::anim
