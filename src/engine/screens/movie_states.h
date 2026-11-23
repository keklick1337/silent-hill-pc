// SPDX-License-Identifier: GPL-3.0-or-later
// The movie game states: the intro's fade-in over the save-card message, the intro
// movie (or its alternate cut), the opening movie of a new game, the return to the game
// after an event movie and the unused debug movie player; and the playback call every
// movie goes through. Movies are named by disc file ("XA/C1_20670"); the manifest
// (gamedata/game.json) can name others. Timings, next states and the movies that stop
// the world's sound come from gamedata/menu/movies.json; playback, loads, fades and
// input are the game's (MovieHost).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/screens/staff_roll.h"
#include "formats/common/status.h"

namespace sh::screens {

// A movie: the manifest key that may name it, else the disc file.
struct MovieRef {
    std::string manifest;
    std::string file;
};

struct MovieStates {
    // Fade-in over the card message before the intro.
    std::int32_t fade_in_wait_vblanks = 0;  // before the fade starts
    std::int32_t fade_in_hold_frames = 0;
    std::string fade_in_next;

    MovieRef intro;
    MovieRef intro_extra;      // played instead while the extra option bit is set
    std::int32_t intro_extra_bit = 0;
    std::int32_t intro_fade_step = 4096;  // Q12, set after the intro for the next screen
    std::string intro_next;

    // The alternate intro: the same file from a frame offset (a manifest key may give it).
    std::string alternate_offset_key;
    std::int32_t alternate_offset = 0;

    MovieRef opening;
    std::string opening_next;

    std::string exit_next;

    std::string debug_last;  // the debug player counts back from this file
    std::string debug_cancel_next;
    std::int32_t debug_text[2] = {0, 0};

    std::vector<std::string> silence_world_audio;  // movies the world's sound stops for
};

class MovieHost {
public:
    virtual ~MovieHost() = default;

    virtual std::int32_t step(int level) = 0;
    virtual void set_step(int level, std::int32_t value) = 0;
    virtual Fade fade() = 0;
    virtual void set_fade(Fade fade) = 0;
    virtual void set_fade_step(std::int32_t step_q12) = 0;
    virtual void wait_vblanks(std::int32_t count) = 0;
    virtual void set_next_state(const std::string& name) = 0;

    virtual bool any_button_held() = 0;
    virtual bool clicked_enter() = 0;
    virtual bool clicked_cancel() = 0;
    virtual bool pulsed_left() = 0;
    virtual bool pulsed_right() = 0;
    virtual std::int32_t frame_counter() = 0;
    virtual bool extra_option(std::int32_t bit) = 0;

    virtual void start_title_loads() = 0;
    virtual void wait_loads() = 0;
    virtual void draw_card_message() = 0;
    virtual void set_debug_text_position(std::int32_t x, std::int32_t y) = 0;

    // The manifest (gamedata/game.json): "" / 0 when the key is absent.
    virtual std::string manifest_string(const std::string& key) = 0;
    virtual std::int32_t manifest_int(const std::string& key) = 0;
    // Disc files: the id of "DIR/NAME", -1 when the disc has none; the name of an id.
    virtual std::int32_t find_movie(const std::string& name) = 0;
    virtual std::string movie_name(std::int32_t id) = 0;
    virtual void warn(const std::string& message) = 0;

    // Playback blocks until the movie ends; 0 frames plays the whole file.
    virtual void silence_world_audio() = 0;
    virtual void play_movie(std::int32_t id, std::int32_t frames) = 0;
    virtual void after_movie() = 0;
};

class MovieScreens {
public:
    explicit MovieScreens(const MovieStates& spec) : spec_(spec) {}

    void intro_fade_in(MovieHost& host) const;
    void intro(MovieHost& host) const;
    void intro_alternate(MovieHost& host) const;
    void opening(MovieHost& host) const;
    void exit_movie(MovieHost& host) const;
    void debug_player(MovieHost& host);

    // Every movie: loads finish first; some movies stop the world's sound.
    void play(MovieHost& host, std::int32_t id, std::int32_t frames) const;

    std::int32_t resolve(MovieHost& host, const MovieRef& ref) const;
    std::int32_t debug_index() const { return debug_index_; }

private:
    const MovieStates& spec_;
    std::int32_t debug_index_ = 0;
};

Result<MovieStates> parse_movie_states(std::string_view json_text);
Result<MovieStates> load_movie_states(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                      std::string* source = nullptr);

}  // namespace sh::screens
