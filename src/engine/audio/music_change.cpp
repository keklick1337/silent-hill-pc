#include "engine/audio/music_change.h"

namespace sh::audio {

namespace {

enum Step : int { kCheck = 0, kFade = 1, kWaitSilent = 2, kDone = 3 };

}  // namespace

void music_crossfade_step(int track, MusicChangeHost& host) {
    if (host.busy()) {
        return;
    }
    switch (host.step()) {
    case kCheck:
        if (!host.track_differs(track)) {
            host.set_step(kDone);
            break;
        }
        host.request_mute();
        host.set_step(kFade);
        break;
    case kFade:
        host.request_mute();
        host.fade_out_fast();
        host.set_step(kWaitSilent);
        break;
    case kWaitSilent:
        host.request_mute();
        if (host.music_silent()) {
            host.load_track(track);
            host.set_step(kDone);
        }
        break;
    case kDone:
        host.finish();
        break;
    default:
        break;
    }
}

}  // namespace sh::audio
