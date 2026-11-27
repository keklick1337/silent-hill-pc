// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/message_voice.h"

#include <algorithm>
#include <utility>

namespace sh::audio {

namespace {

// A box that was not drawn for more than this many frames has closed.
constexpr int kUndrawnFrames = 2;

}  // namespace

MessageVoice::MessageVoice(Host host) : host_(std::move(host)) {}

std::string MessageVoice::mod_path(const std::string& message_key) {
    std::string name = message_key;
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');
    return "XA/msg_" + name + ".wav";
}

void MessageVoice::page_started(int message_index) {
    if (game_voiced_) return;
    const std::string key = host_.message_key(message_index);
    if (key.empty()) return;
    const bool started = host_.play(mod_path(key));
    if (started) playing_ = true;
    page_owns_file_ = started;
    tail_end_ms_ = 0;
}

void MessageVoice::box_closed() {
    if (playing_) host_.stop();
    playing_ = false;
    page_owns_file_ = false;
    tail_end_ms_ = 0;
}

bool MessageVoice::page_held() {
    if (!playing_ || !page_owns_file_) return false;
    if (host_.audible()) {
        tail_end_ms_ = host_.now_ms() + host_.tail_ms();
        return true;
    }
    // The difference is read as signed, so the clock wrapping round releases the page instead of pinning it.
    if (tail_end_ms_ != 0 && static_cast<std::int32_t>(tail_end_ms_ - host_.now_ms()) > 0) return true;
    tail_end_ms_ = 0;
    return false;
}

void MessageVoice::frame_ended() {
    const bool paused = host_.paused && host_.paused();
    if (host_.hold) host_.hold(paused && playing_);
    if (paused) {
        drawn_ = false;
        return;
    }
    if (drawn_) {
        frames_undrawn_ = 0;
    } else if (playing_ && ++frames_undrawn_ > kUndrawnFrames) {
        box_closed();
    }
    drawn_ = false;
}

}  // namespace sh::audio
