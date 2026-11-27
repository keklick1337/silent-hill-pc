// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Voices for the text boxes the game never voiced: a mod ships XA/msg_<KEY>.wav (or .opus), where KEY is the
// message's key with '.' and ':' written as '_' (MAP1_S00_23, COMMON_5, mymod_hello for a mod's "mymod:hello"),
// and the file plays when that page of the box comes up, after a short lead (the host's play()).
//
// A page without a file of its own leaves the file before it playing, so one recording can cover a message of
// several pages. The page that started a file is held on screen while the file is heard and for a short tail
// after it: the timer of such a page was written for a silent line and is usually shorter than the recording.
// Pages that only ride a recording keep their own pace. The box closing always stops the file.

#include <cstdint>
#include <functional>
#include <string>

namespace sh::audio {

class MessageVoice {
public:
    // What the voice needs of the game.
    struct Host {
        // The key of a message of the current map ("MAP1_S00.23"); empty when it has none.
        std::function<std::string(int message_index)> message_key;
        // Starts the mod's file of that name ("XA/msg_MAP1_S00_23.wav"); false when no mod has it or it does
        // not play. The sound may start a little later; the file counts as heard from this call on.
        std::function<bool(const std::string& mod_path)> play;
        std::function<void()> stop;
        // The file started by play() is still heard.
        std::function<bool()> audible;
        // The game's clock, and how long a page is held after its file went silent.
        std::function<std::uint32_t()> now_ms;
        std::function<std::uint32_t()> tail_ms;
        // The world is held (the pause, the inventory, a paper map): the box is not drawn but stays open. Optional.
        std::function<bool()> paused;
        // Holds the file's sound and its timing, or lets them go on. Optional.
        std::function<void(bool held)> hold;
    };

    explicit MessageVoice(Host host);

    // The message being drawn is one the game voices itself: such a message never gets a file.
    void set_game_voiced(bool voiced) { game_voiced_ = voiced; }
    // The box was drawn this frame.
    void box_drawn() { drawn_ = true; }
    // A message began or turned to a new page.
    void page_started(int message_index);
    void box_closed();
    // The page that started a file still has to stay: only the automatic advance asks, a player's press never
    // waits.
    bool page_held();
    // Once per game frame. Stops a file whose box is no longer drawn: some boxes close without box_closed()
    // (codes that close them by themselves, skipped cutscenes). While the world is held the file is held with it
    // and the frames without the box are not counted.
    void frame_ended();

    // The name of a message's file inside a mod.
    static std::string mod_path(const std::string& message_key);

private:
    Host host_;
    bool game_voiced_ = false;
    bool playing_ = false;
    bool drawn_ = false;
    int frames_undrawn_ = 0;
    // Only the page that started the file holds; a page riding it does not.
    bool page_owns_file_ = false;
    // When the tail after the file ends; moved on for as long as the file is heard. 0: no tail runs.
    std::uint32_t tail_end_ms_ = 0;
};

}  // namespace sh::audio
