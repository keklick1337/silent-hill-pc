#pragma once

// Changing the music track from an event: the map's mix is asked to mute, the
// playing song fades out fast, and once the driver's music has gone quiet the new track loads. One step per frame
// of the event that asks for it; the event's own step machine carries the progress.

namespace sh::audio {

class MusicChangeHost {
public:
    virtual ~MusicChangeHost() = default;
    // A voice line streams or the file queue is still loading: wait.
    virtual bool busy() = 0;
    // The caller's step (level 1) and its change; `finish` moves the caller's state on (level 0).
    virtual int step() = 0;
    virtual void set_step(int step) = 0;
    virtual void finish() = 0;
    // Whether `track` is a real track different from the one loaded.
    virtual bool track_differs(int track) = 0;
    // Holds the map's mix muted this frame.
    virtual void request_mute() = 0;
    // Starts the fast fade-out of the playing song.
    virtual void fade_out_fast() = 0;
    // The driver's music has no channel sounding.
    virtual bool music_silent() = 0;
    // Loads `track` and makes it the current one.
    virtual void load_track(int track) = 0;
};

// One frame of the change to `track`. A track equal to the playing one (or none) finishes at once.
void music_crossfade_step(int track, MusicChangeHost& host);

}  // namespace sh::audio
