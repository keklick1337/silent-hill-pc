// SPDX-License-Identifier: GPL-3.0-or-later
// Ties the game's display (render/gpu/game_display.h) and the engine frame to the running game's host frame: the
// host's scene open and close around the draws, the buffer the frame's lists are built for, and the per-frame
// retirement of the precise-capture tables. Installed by a static initializer of the exe.

#include "engine/loop/frame_view.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/math/precise_capture.h"
#include "engine/platform/host_api.h"
#include "engine/platform/window.h"
#include "render/frame/frame.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/game_display.h"

namespace {

int build_buffer() { return sh::game_state::frame_buffer_index(); }

struct InstallDisplayHost {
    InstallDisplayHost() {
        sh::gpu::display::FrameHooks& hooks = sh::gpu::display::frame_hooks();
        hooks.begin_scene = [] { return sh::platform::begin_frame(); };
        hooks.end_scene = [] { sh::platform::end_frame(); };
        sh::frame::set_buffer_source(&build_buffer);
        // The captures of an update live until the last frame that presents it.
        sh::gpu::add_frame_end_listener([] {
            if (sh::loop::frame_timing().render_only_next == 0) sh::math::capture::end_frame();
        });
    }
};

const InstallDisplayHost g_install;

}  // namespace
