// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/version/engine_version.h"

// The only file that sees the per-commit header, so a new commit recompiles this file alone.
#include "sh_build_info.h"

#define SH_BUILD_TEXT_OF_(x) #x
#define SH_BUILD_TEXT_OF(x) SH_BUILD_TEXT_OF_(x)

namespace sh::version {

int build_number() {
    return SH_BUILD_NUMBER;
}

const char* commit() {
    return SH_BUILD_GIT_HASH;
}

const char* build_stamp() {
    return SH_BUILD_STAMP;
}

const char* full() {
    return SH_ENGINE_VERSION_TEXT "+" SH_BUILD_TEXT_OF(SH_BUILD_NUMBER) "." SH_BUILD_GIT_HASH;
}

}  // namespace sh::version
