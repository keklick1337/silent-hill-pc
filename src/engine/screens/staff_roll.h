// SPDX-License-Identifier: GPL-3.0-or-later
// The staff roll: the scrolling credits after an ending (a flat roll with a drop shadow
// on its own game state) and the roll on a tilted plane of the UFO ending, which ends
// on the logo spinning up, coming closer and flying away. The module owns the timing
// (the roll is fitted to the length of the ending's music), the layout and the screen's
// step logic; everything the game owns (fades, sound, the display, the clock variable,
// the line list) is reached through StaffRollHost.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/screens/roll_text.h"

namespace sh::screens {

// Screen fade as the game encodes it.
enum class FadeState : std::uint8_t {
    Reset = 0,
    None = 1,
    OutStart = 2,
    OutSteps = 3,
    ResetTimestep = 4,
    OutComplete = 5,
    InStart = 6,
    InSteps = 7,
    Other = 255,  // a value outside the encoding
};

struct Fade {
    FadeState state = FadeState::None;
    bool white = false;
};

// Timing of one ending's roll: the clock value the music starts at and the roll length
// in clock ticks (the music's length).
struct EndingTiming {
    std::int32_t start = 0;
    std::int32_t length = 0;
};

// The logo animation at the end of the perspective roll, in roll ticks after the logo
// reached its stop line.
struct LogoAnimation {
    std::int32_t tilt_angle = 0;      // Q.12 turn: the plane's tilt the logo spins out of
    std::int32_t spin_ticks = 120;    // spins upright
    std::int32_t approach_ticks = 60; // then comes closer
    float approach_from = 240.0f;     // plane distance before and after the approach
    float approach_to = 210.0f;
    std::int32_t hold_until = 360;    // then stays until this tick
    std::int32_t retreat_ticks = 120; // then flies away
    float retreat_to = 3840.0f;
};

struct StaffRollStyle {
    // Flat roll: two shadow passes (one pixel above and below), then the text.
    std::uint32_t shadow_rgb = 0;
    std::uint32_t shadow_blend = 1;
    std::uint32_t text_rgb = 0;
    // Perspective roll: the flat text color the screen sets each frame (kept for the
    // game's texture state; the plane text itself is drawn white).
    std::uint32_t perspective_flat_rgb = 0;
    RollPlane plane;          // the tilted plane the lines scroll on
    LogoAnimation logo;       // the logo's planes are this plane at other distances and tilts
};

// The endings in the game's order (the index the ending screens pass to the roll).
inline constexpr std::size_t kEndingCount = 6;
inline constexpr std::string_view kEndingNames[kEndingCount] = {"unused", "good_plus", "good",
                                                                "bad_plus", "bad", "ufo"};

// One ending's roll: the sound name of its music (an XA line of sounds.json) and its timing.
struct EndingRoll {
    std::string music;
    EndingTiming timing;
};

struct StaffRollData {
    std::array<EndingRoll, kEndingCount> endings;
    RollFont font;
    StaffRollStyle style;
    std::vector<std::string> lines;  // encoded, one per roll row
    std::string logo;                // encoded
};

class StaffRollHost {
public:
    virtual ~StaffRollHost() = default;

    // The line list and the row count the roll's timing divides the music length by.
    virtual std::int32_t row_count() = 0;
    virtual const std::uint8_t* row(std::int32_t index) = 0;
    virtual const std::uint8_t* logo() = 0;

    // The credits clock (vertical blanks since the roll's music started).
    virtual std::int32_t clock() = 0;
    virtual void set_clock(std::int32_t value) = 0;
    virtual std::int32_t vblanks() = 0;

    virtual EndingTiming ending(std::int32_t index) = 0;
    virtual void preload_music(std::int32_t ending) = 0;
    virtual void play_music(std::int32_t ending) = 0;
    // 0: stopped, 1: just started, 2+: loading or playing (the game's stream check).
    virtual std::int32_t music_state() = 0;
    virtual void stop_bgm() = 0;
    virtual void stop_sfx() = 0;
    virtual void stop_voice() = 0;

    virtual bool skip_pressed() = 0;

    // The flat text's blend mode (0..3), which the game's texture page mirrors, and
    // the moment the perspective text takes its texture placement from the flat text.
    virtual void set_flat_blend_mode(std::uint32_t mode) = 0;
    virtual void plane_changed() = 0;

    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;

    // Game state steps (0..2) and transitions.
    virtual std::int32_t step(int level) = 0;
    virtual void set_step(int level, std::int32_t value) = 0;
    virtual void reset_step_timer() = 0;
    virtual bool came_from_gameplay() = 0;
    virtual void enter_roll_state() = 0;
    virtual void return_to_previous_state() = 0;
    virtual std::int32_t file_queue_length() = 0;

    virtual Fade fade() = 0;
    virtual void set_fade(Fade fade) = 0;
    virtual void set_fade_step(std::int32_t step_q12) = 0;
    virtual void set_background(std::uint32_t rgb) = 0;

    // Display: keep the game screen aside and clear the frame (white or black) for the
    // roll; the roll's display mode; give the game screen back.
    virtual void save_screen() = 0;
    virtual void clear_for_roll(bool white) = 0;
    virtual void set_roll_display(bool on) = 0;
    virtual void set_frame_interval(std::int32_t vblanks) = 0;
    virtual void restore_screen() = 0;
};

class StaffRoll {
public:
    StaffRoll(RollFont& font, const StaffRollStyle& style)
        : style_(style), flat_(font), perspective_(font) {}

    // --- flat roll (after an ending) ---
    // Fits the roll to ending `index` and preloads its music.
    void start_flat(std::int32_t index, StaffRollHost& host);
    // Plays the music once loaded; true when it started (the clock is set to its start).
    bool music_started(StaffRollHost& host);
    // Fades out to the roll's game state; true once switched.
    bool finish(StaffRollHost& host);
    // One tick of the roll's game state.
    void update(StaffRollHost& host, RollSink& sink, RollSink& bar_sink);

    // --- perspective roll (UFO ending) ---
    // Fits the roll to ending `index` and puts the text on its resting plane.
    void start_perspective(std::int32_t index, StaffRollHost& host);
    // Draws one tick; true when the logo animation finished.
    bool update_perspective(StaffRollHost& host, RollSink& sink);

    const RollPlane& plane() const { return perspective_.plane(); }

private:
    bool enter(StaffRollHost& host);
    bool roll(StaffRollHost& host, RollSink& sink, RollSink& bar_sink);
    bool leave(StaffRollHost& host);
    bool draw_flat(StaffRollHost& host, RollSink& sink);
    void set_flat_blend(StaffRollHost& host, bool semi_transparent, std::uint32_t mode);
    void set_logo_plane(StaffRollHost& host, std::int32_t z_q12, std::int32_t row_y, std::int32_t row_z);

    const StaffRollStyle& style_;
    FlatRollText flat_;
    PerspectiveRollText perspective_;

    std::int32_t ending_ = 0;      // the ending whose timing the roll uses
    std::int32_t music_step_ = 0;  // 0 idle, 1 play next, 2 wait for the stream
    std::int32_t row_step_ = 0;    // pixels between rows
    std::int32_t inv_row_step_ = 0;  // 0x10000 / row_step
    std::int32_t roll_end_ = 0;    // clock at which the logo line passes the stop line
    std::int32_t wait_ = 0;        // ticks of the current screen step
    std::int32_t outro_ = 0;
};

}  // namespace sh::screens
