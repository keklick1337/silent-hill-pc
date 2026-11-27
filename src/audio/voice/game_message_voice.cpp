// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/voice/game_message_voice.h"

#include "audio/voice/voice_line_player.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/mods/asset_files_c.h"
#include "engine/timing/vblank.h"
#include "i18n/localization.h"

namespace sh::audio {

MessageVoice& message_voice() {
    static MessageVoice voice(MessageVoice::Host{
        [](int message_index) { return i18n::message_voice_name(game_state::save_map_index(), message_index); },
        [](const std::string& mod_path) {
            char file[1024];
            const bool played = ShModAsset_Find(mod_path.c_str(), file, sizeof file) &&
                                voice_lines().play_file(file, g_PcConfig.msgVoiceLeadMs);
            // Once per page of a box: the log shows which file each page asked for.
            SH_DBG("[MSGBOX] %s%s", mod_path.c_str(), played ? " (plays)" : "");
            return played;
        },
        [] {
            SH_DBG("[MSGBOX] the box's voice stops");
            voice_lines().stop_file();
        },
        [] { return voice_lines().file_audible(); },
        [] { return timing::virtual_time::ticks_ms(); },
        [] { return static_cast<std::uint32_t>(g_PcConfig.msgVoiceTailMs); },
        [] { return sh::game::session().music_status(sh::game::music_status::kPause); },
        [](bool held) { voice_lines().hold_file(held); }});
    return voice;
}

}  // namespace sh::audio
