// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/device/platform_sinks.h"

#include "engine/timing/vblank.h"

namespace sh::audio::output {

OutputDevice& game_output() {
    static OutputDevice device(SinkProvider{make_platform_sink, [] { return timing::virtual_time::active(); }});
    return device;
}

}  // namespace sh::audio::output

namespace sh::audio {

void render_virtual_vblank(int blanks_per_second) { output::game_output().vblank(blanks_per_second); }

}  // namespace sh::audio
