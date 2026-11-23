#pragma once

// The converter list the importer gets: which converters are on, in a fixed order.

#include "formats/convert/converter.h"
#include "formats/convert/model/model_converter.h"
#include "sh-import/options.h"

#include <memory>
#include <vector>

namespace sh::import_tool {

// textures -> "tim"; models -> "model", "anim"; audio -> "vab", "xa", "kdt"; text -> "font" (the English text is
// gamedata/text/en); levels -> "level"; data -> "cutscene", "cmp", "demo", in this order.
// `sibling` goes to the model converter (rest poses and clips from ANM files, texture sizes from TIM files), to the
// anim converter (HB_BASE.ANM, the layout of the headerless HB_M*.ANM) and to the level
// converter (global PLMs, TIM sizes and palettes, the cells of an area).
std::vector<std::unique_ptr<sh::convert::Converter>> build_converters(const Options& options,
                                                                      sh::convert::model::SiblingReader sibling);

}  // namespace sh::import_tool
