#pragma once

#include "formats/common/status.h"
#include "formats/convert/level/level_model.h"

#include <cstdint>
#include <span>

namespace sh::convert::level {

// Parses a whole IPD file (the logical bytes of the archive entry). Every offset is bounds-checked against the file;
// any structure that does not fit is an error that names the structure and the offset.
Result<IpdFile> parse_ipd(std::span<const std::uint8_t> file);

// Parses a stand-alone LM file: a PLM (BG/*_GLB.PLM, BG/MGR.PLM, ITEM/*.PLM). The model library of an IPD is parsed
// by parse_ipd.
Result<LmFile> parse_plm(std::span<const std::uint8_t> file);

}  // namespace sh::convert::level
