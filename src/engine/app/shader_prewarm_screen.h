// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// "Preparing shaders… N%" before play (RENDER.md section 2.2d): when the pipeline cache on disk did not match this
// build or driver, the window shows the progress of the pipelines the renderer builds ahead until they are all
// ready. A valid cache builds them in the background without the screen.
#pragma once

namespace sh::app {

// Returns at once unless the renderer is building a fresh cache; then returns when it is done.
void prepare_shaders();

}  // namespace sh::app
