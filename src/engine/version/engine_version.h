// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The engine version: MAJOR.MINOR.PATCH set by hand in the VERSION file at the repository root (the only place),
// plus the build metadata of this binary (commit count and commit hash, cmake/gen_build_info.cmake). Mods require
// the first with "min_engine_version"; the second only identifies a build in logs.
#pragma once

#include "engine/version/engine_version_numbers.h"

#include <string_view>

namespace sh::version {

inline constexpr int kMajor = SH_ENGINE_VERSION_MAJOR;
inline constexpr int kMinor = SH_ENGINE_VERSION_MINOR;
inline constexpr int kPatch = SH_ENGINE_VERSION_PATCH;
inline constexpr std::string_view kText = SH_ENGINE_VERSION_TEXT;  // "1.0.0"

int build_number();          // commits in the history of this build, 0 when built outside git
const char* commit();        // short hash, "unknown" outside git
const char* build_stamp();   // when the build identification was last written
const char* full();          // "1.0.0+2890.1a2b3c4d5": the version with its build metadata

}  // namespace sh::version
