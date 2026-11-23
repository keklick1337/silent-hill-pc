// SPDX-License-Identifier: GPL-3.0-or-later
// The movie game states and gamedata/menu/movies.json.

#include "engine/screens/movie_states.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

constexpr std::int32_t kStepEnter = 0;
constexpr std::int32_t kStepHold = 1;
constexpr std::int32_t kStepLeave = 2;

}  // namespace

std::int32_t MovieScreens::resolve(MovieHost& host, const MovieRef& ref) const {
    if (!ref.manifest.empty()) {
        const std::string name = host.manifest_string(ref.manifest);
        if (!name.empty()) {
            const std::int32_t id = host.find_movie(name);
            if (id >= 0) {
                return id;
            }
            host.warn("manifest \"" + ref.manifest + "\": no file \"" + name + "\" on the disc; keeping the original");
        }
    }
    const std::int32_t id = host.find_movie(ref.file);
    if (id < 0) {
        host.warn("movies.json: no file \"" + ref.file + "\" on the disc");
    }
    return id;
}

void MovieScreens::play(MovieHost& host, std::int32_t id, std::int32_t frames) const {
    host.wait_loads();
    // Whether the room falls silent for a movie is per scene: most movies keep the
    // world's ambience running under them.
    if (!spec_.silence_world_audio.empty()) {
        const std::string name = host.movie_name(id);
        if (std::find(spec_.silence_world_audio.begin(), spec_.silence_world_audio.end(), name) !=
            spec_.silence_world_audio.end()) {
            host.silence_world_audio();
        }
    }
    host.play_movie(id, frames);
    host.after_movie();
}

void MovieScreens::intro_fade_in(MovieHost& host) const {
    switch (host.step(0)) {
        case kStepEnter:
            host.wait_vblanks(spec_.fade_in_wait_vblanks);
            host.set_fade({FadeState::InStart, false});
            host.start_title_loads();
            host.set_step(0, host.step(0) + 1);
            break;
        case kStepHold:
            if (host.any_button_held() || host.frame_counter() > spec_.fade_in_hold_frames) {
                host.set_fade({FadeState::OutSteps, false});
                host.set_step(0, kStepLeave);
            }
            break;
        case kStepLeave:
            if (host.fade().state == FadeState::OutComplete) {
                host.wait_loads();
                host.set_next_state(spec_.fade_in_next);
            }
            break;
        default:
            break;
    }
    host.draw_card_message();
}

void MovieScreens::intro(MovieHost& host) const {
    const MovieRef& ref = host.extra_option(spec_.intro_extra_bit) ? spec_.intro_extra : spec_.intro;
    play(host, resolve(host, ref), 0);
    host.set_next_state(spec_.intro_next);
    host.set_fade_step(spec_.intro_fade_step);
}

void MovieScreens::intro_alternate(MovieHost& host) const {
    const auto offset = static_cast<std::int16_t>(
        spec_.alternate_offset_key.empty() ? 0 : host.manifest_int(spec_.alternate_offset_key));
    const std::int32_t id = resolve(host, spec_.intro);
    play(host, id, offset != 0 ? offset : spec_.alternate_offset);
    host.set_next_state(spec_.intro_next);
    host.set_fade_step(spec_.intro_fade_step);
}

void MovieScreens::opening(MovieHost& host) const {
    play(host, resolve(host, spec_.opening), 0);
    host.set_next_state(spec_.opening_next);
}

void MovieScreens::exit_movie(MovieHost& host) const { host.set_next_state(spec_.exit_next); }

void MovieScreens::debug_player(MovieHost& host) {
    if (host.clicked_cancel()) {
        host.set_next_state(spec_.debug_cancel_next);
    }
    if (host.pulsed_left()) {
        debug_index_--;
    }
    if (host.pulsed_right()) {
        debug_index_++;
    }
    host.set_debug_text_position(spec_.debug_text[0], spec_.debug_text[1]);
    if (host.clicked_enter()) {
        // Files of the disc's movie directory in table order, counted back from the last.
        play(host, host.find_movie(spec_.debug_last) - debug_index_, 0);
    }
}

namespace {

using json::Value;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string at(const Value& v, std::string_view what) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ": " + std::string(what);
}

const Value& member(const Value& parent, std::string_view key) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        fail(at(parent, "missing \"" + std::string(key) + "\""));
    }
    return *v;
}

void reject_unknown(const Value& object, std::initializer_list<std::string_view> known) {
    if (!object.is_object()) {
        fail(at(object, "an object is expected"));
    }
    for (const auto& [key, value] : object.members()) {
        bool found = key.rfind("_", 0) == 0 || key == "description";
        for (std::string_view k : known) {
            found = found || key == k;
        }
        if (!found) {
            fail(at(value, "unknown key \"" + key + "\""));
        }
    }
}

std::int32_t int_of(const Value& parent, std::string_view key, std::int32_t lo, std::int32_t hi) {
    const Value& v = member(parent, key);
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, "\"" + std::string(key) + "\" must be an integer in " + std::to_string(lo) + ".." +
                       std::to_string(hi)));
    }
    return static_cast<std::int32_t>(v.integer());
}

std::int32_t q12_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_number() || v.number() < 0.0 || v.number() > 64.0) {
        fail(at(v, "\"" + std::string(key) + "\" must be a number in 0..64"));
    }
    volatile float f = static_cast<float>(v.number()) * 4096.0f;
    return static_cast<std::int32_t>(f);
}

std::string string_of(const Value& parent, std::string_view key, bool may_be_empty = false) {
    const Value& v = member(parent, key);
    if (!v.is_string() || (!may_be_empty && v.string().empty())) {
        fail(at(v, "\"" + std::string(key) + "\" must be a non-empty string"));
    }
    return v.string();
}

MovieRef movie_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    reject_unknown(v, {"manifest", "file"});
    MovieRef ref;
    ref.manifest = string_of(v, "manifest", true);
    ref.file = string_of(v, "file");
    return ref;
}

}  // namespace

Result<MovieStates> parse_movie_states(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<MovieStates>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"intro_fade_in", "intro", "intro_alternate", "opening", "exit_movie", "debug_player",
                          "silence_world_audio"});
    MovieStates out;

    const Value& fade_in = member(root, "intro_fade_in");
    reject_unknown(fade_in, {"wait_vblanks", "hold_frames", "next_state"});
    out.fade_in_wait_vblanks = int_of(fade_in, "wait_vblanks", 0, 600);
    out.fade_in_hold_frames = int_of(fade_in, "hold_frames", 0, 1 << 20);
    out.fade_in_next = string_of(fade_in, "next_state");

    const Value& intro = member(root, "intro");
    reject_unknown(intro, {"movie", "extra_movie", "extra_option_bit", "fade_step_after", "next_state"});
    out.intro = movie_of(intro, "movie");
    out.intro_extra = movie_of(intro, "extra_movie");
    out.intro_extra_bit = int_of(intro, "extra_option_bit", 0, 31);
    out.intro_fade_step = q12_of(intro, "fade_step_after");
    out.intro_next = string_of(intro, "next_state");

    const Value& alt = member(root, "intro_alternate");
    reject_unknown(alt, {"frame_offset_manifest", "frame_offset"});
    out.alternate_offset_key = string_of(alt, "frame_offset_manifest", true);
    out.alternate_offset = int_of(alt, "frame_offset", 0, 32767);

    const Value& opening = member(root, "opening");
    reject_unknown(opening, {"movie", "next_state"});
    out.opening = movie_of(opening, "movie");
    out.opening_next = string_of(opening, "next_state");

    const Value& exit = member(root, "exit_movie");
    reject_unknown(exit, {"next_state"});
    out.exit_next = string_of(exit, "next_state");

    const Value& debug = member(root, "debug_player");
    reject_unknown(debug, {"last_file", "cancel_state", "text_position"});
    out.debug_last = string_of(debug, "last_file");
    out.debug_cancel_next = string_of(debug, "cancel_state");
    const Value& pos = member(debug, "text_position");
    if (!pos.is_array() || pos.array().size() != 2 || !pos.array()[0].is_integer() || !pos.array()[1].is_integer()) {
        fail(at(pos, "\"text_position\" is [x, y]"));
    }
    out.debug_text[0] = static_cast<std::int32_t>(pos.array()[0].integer());
    out.debug_text[1] = static_cast<std::int32_t>(pos.array()[1].integer());

    const Value& silence = member(root, "silence_world_audio");
    if (!silence.is_array()) {
        fail(at(silence, "\"silence_world_audio\" must be a list of movie files"));
    }
    for (const Value& s : silence.array()) {
        if (!s.is_string() || s.string().empty()) {
            fail(at(s, "a movie file name is expected"));
        }
        out.silence_world_audio.push_back(s.string());
    }
    return Result<MovieStates>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<MovieStates>::fail(e.what());
}

Result<MovieStates> load_movie_states(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                      std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "movies.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<MovieStates>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<MovieStates> parsed = parse_movie_states(text);
    if (!parsed) {
        return Result<MovieStates>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
