// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The text-box voices of the running game: sh::audio::MessageVoice on the active mods' files, the voice-line
// player, the game's clock and the setting msg_voice_tail_ms.

#include "engine/audio/message_voice.h"

namespace sh::audio {

MessageVoice& message_voice();

}  // namespace sh::audio
