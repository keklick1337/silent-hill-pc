// Map-start audio: the sound-command tables of the background
// music and the ambient bank, and the two step machines that bring a map's music and ambience up
// while the file queue drains. The tables are gamedata/config/boot_audio.json; the game answers the
// questions the machines ask through the host interfaces.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::loader {

class BootAudio {
public:
    // Parses the JSON document; every table must be a non-empty array of 0..65535 integers.
    static Result<BootAudio> parse(std::string_view json_text);

    // The sound command that loads a music track / selects its channels / loads an ambient bank;
    // nullopt when the index is outside the table.
    std::optional<std::uint16_t> bgm_load(std::int32_t track) const { return at(bgm_load_, track); }
    std::optional<std::uint16_t> bgm_channels(std::int32_t track) const { return at(bgm_channels_, track); }
    std::optional<std::uint16_t> ambient_load(std::int32_t bank) const { return at(ambient_load_, bank); }

private:
    static std::optional<std::uint16_t> at(const std::vector<std::uint16_t>& t, std::int32_t i) {
        if (i < 0 || static_cast<std::size_t>(i) >= t.size()) {
            return std::nullopt;
        }
        return t[static_cast<std::size_t>(i)];
    }

    std::vector<std::uint16_t> bgm_load_;
    std::vector<std::uint16_t> bgm_channels_;
    std::vector<std::uint16_t> ambient_load_;
};

// What a step machine reports: the caller keeps calling while the result is not Done. Wait means
// nothing was done this call (audio streaming or file queue busy).
enum class BootStep : std::uint8_t { Wait, Run, Done };

class MusicInitHost {
public:
    virtual ~MusicInitHost() = default;
    virtual bool audio_or_queue_busy() = 0;
    // The map's own music is the "keep playing" marker: restore the loop that was cut.
    virtual void restore_kept_track() = 0;
    // The map's track is a real one that is not the loaded track.
    virtual bool track_differs() = 0;
    virtual void fade_out_and_mute() = 0;
    virtual bool sound_idle() = 0;
    virtual void load_map_track() = 0;
};

// One call of the music step machine; `step` is the caller's persistent step counter.
BootStep music_init_step(std::int32_t& step, MusicInitHost& host);

class AmbientInitHost {
public:
    virtual ~AmbientInitHost() = default;
    virtual bool audio_or_queue_busy() = 0;
    // Map-specific override of the ambient bank, then whether the current bank differs from it.
    virtual bool target_differs() = 0;
    virtual void fade_out_ambient() = 0;
    virtual void load_target_ambient() = 0;
};

BootStep ambient_init_step(std::int32_t& step, AmbientInitHost& host);

}  // namespace sh::loader
