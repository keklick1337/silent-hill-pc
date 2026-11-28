// SPDX-License-Identifier: GPL-3.0-or-later
//
// The arithmetic of the touch controls (touch_controls.cpp): the floating stick and the pad bytes it gives, which
// controls show on a frame, the pinch of the paper map, the drag that scrolls a menu (with its glide after the
// finger lifts) and what counts as a tap. No SDL, no renderer, no game.
#pragma once

#include <array>
#include <cstdint>

#include "input/touch/touch_layout.h"

namespace sh::input::touch {

// ---- the stick ----

struct StickTuning {
    float dead = 0.18f;      // per axis, a fraction of the radius: a resting thumb does not move Harry
    float run_at = 0.75f;    // pushed this far (a fraction of the radius) into the forward half, the stick also runs
    float run_keep = 0.62f;  // a run keeps going down to this push, so a thumb near the edge does not flicker
    bool run = true;         // `touch_stick_run`
    bool running = false;    // the stick ran on the frame before
};

struct StickOutput {
    float x = 0.0f, y = 0.0f;  // -1..1, +y down (towards the player)
    float magnitude = 0.0f;    // 0..1
    bool run = false;
    std::uint8_t axis_x = 128, axis_y = 128;  // the pad's analog bytes (128 = centre)
};

// One axis as the pad's analog byte. Inside `dead` it is the centre; outside, the push starts just past the game's
// own threshold (64 from the centre either way), so the slightest push already walks, and the full push is the edge.
std::uint8_t axis_byte(float value, float dead);

// The finger `dx`, `dy` pixels from the stick's origin with a radius of `radius` pixels.
StickOutput stick_output(float dx, float dy, float radius, const StickTuning& tuning);

// A floating stick's origin follows a finger dragged further than `follow` radii away, so a thumb that slid off
// keeps steering. Returns the new origin.
struct Point {
    float x = 0.0f, y = 0.0f;
};
Point follow_origin(Point origin, Point finger, float radius, float follow);

// ---- the d-pad ----

// The arrows of the d-pad (`touch_movement = dpad`), held as the pad's digital directions.
inline constexpr std::uint8_t kDpadUp = 1, kDpadRight = 2, kDpadDown = 4, kDpadLeft = 8;

struct DpadTuning {
    float dead = 0.22f;        // a fraction of the radius around the centre that presses nothing
    float dead_keep = 0.16f;   // a pressed d-pad lets go only nearer the centre than this, so it does not flicker
    float hysteresis = 0.12f;  // radians a sector reaches past its edge while it is pressed (about 7 degrees)
};

// The arrows a finger `dx`, `dy` pixels from the cross's centre presses (+y down), with a radius of `radius` pixels:
// none in the dead centre, else the one of 8 sectors of 45 degrees the finger is in: a straight one presses its
// arrow, a diagonal one the two arrows beside it. `previous` is what the finger pressed on the frame before: its
// sector holds a little past its edges. Beyond the radius the direction still counts, so a finger that slid off the
// cross keeps steering.
std::uint8_t dpad_bits(float dx, float dy, float radius, std::uint8_t previous, const DpadTuning& tuning = {});

// A button that toggles on each press and stays (the d-pad's run): a press is a finger landing on it, and further
// fingers landing while one already holds it change nothing.
class Toggle {
public:
    void press() {
        if (held_++ == 0) on_ = !on_;
    }
    void release() {
        if (held_ > 0) --held_;
    }
    void release_all() { held_ = 0; }
    void reset() { on_ = false, held_ = 0; }
    bool on() const { return on_; }
    bool held() const { return held_ > 0; }

private:
    bool on_ = false;
    int held_ = 0;
};

// ---- which controls show ----

// What the screen is on a frame, as the touch controls need it.
struct Facts {
    bool gameplay = false;       // free movement, nothing over it (no menu, console, cutscene)
    bool armed = false;          // a weapon is in hand: aim shows
    bool aiming = false;         // the weapon is raised (or the aim control is latched)
    bool action_target = false;  // something in reach answers the action button
    bool action_always = false;  // `touch_action_button` 1
    bool dpad = false;           // `touch_movement = dpad`: the d-pad and the run toggle instead of the stick
    bool look_drag = false;      // an alternate camera, turned by a drag: no View (camera) button
    std::uint32_t prompted = 0;  // bits of Control the game asks for (engine/input/touch_prompts.h)
};

// The controls of the frame and how strongly each one shows (0..1). Controls come back a moment after gameplay
// resumes and the action button lingers a moment after its target left reach, so neither flickers when the state
// changes for a frame or two.
class Visibility {
public:
    static constexpr float kShowDelay = 0.12f;     // seconds of gameplay before the controls come back
    static constexpr float kFade = 0.15f;          // seconds of a fade in or out
    static constexpr float kActionLinger = 0.35f;  // seconds the action button stays after its target

    void update(const Facts& facts, float seconds);
    // Every control off at once (a screen opened over the game).
    void hide();

    std::uint32_t visible() const { return visible_; }  // bits of Control that take fingers
    float alpha(Control c) const { return alpha_[static_cast<std::size_t>(c)]; }
    bool shown() const { return shown_; }

private:
    float gameplay_for_ = 0.0f;
    float since_target_ = 1e9f;
    bool shown_ = false;
    std::uint32_t visible_ = 0;
    std::array<float, kControlCount> alpha_{};
};

// ---- mashing ----

// Presses one after another, each held for `hold` pad updates with `gap` free updates between them, so every one
// reaches the game's 30 Hz tick as a press of its own however fast they come in.
class PressTrain {
public:
    static constexpr int kHold = 3;
    static constexpr int kGap = 3;
    static constexpr int kMaxQueued = 8;  // a burst beyond this is dropped, not played out long after

    void add();
    // One pad update: true while a press is held.
    bool step();
    void clear() { queued_ = 0, frame_ = 0; }
    int queued() const { return queued_; }

private:
    int queued_ = 0;
    int frame_ = 0;  // 0: idle; 1..kHold held; kHold+1..kHold+kGap the gap
};

// A thumb resting on the stick holds it: the game counts a mash only on new pushes, as a pad's stick wiggled.
// A flick is a push past `push` after the thumb came back under `rest`, or a turn of more than `turn` radians from
// the direction of the last flick while pushed.
class Flicks {
public:
    static constexpr float kPush = 0.5f;
    static constexpr float kRest = 0.3f;
    static constexpr float kTurn = 1.3f;  // about 75 degrees

    // The stick at a pad update (x, y as stick_output gives them); true on a flick.
    bool update(float x, float y);
    void reset() { armed_ = true, have_dir_ = false; }

private:
    bool armed_ = true;  // the thumb came back near the centre since the last flick
    bool have_dir_ = false;
    float dir_ = 0.0f;  // radians of the last flick
};

// ---- gestures ----

// A tap: short, and the finger stayed put.
inline constexpr float kTapSeconds = 0.30f;
bool is_tap(float seconds, float moved, float slop);
// The pointer's finger in a menu taps (picks what is under it) when it stayed within `slop` pixels, however long it
// rested: a slow tap must not be lost. A drag picks nothing, nor does a finger that landed to stop a gliding list.
bool pointer_tap(float moved, float slop, bool stop_only);

// A pinch of two fingers: +1 once they spread to `ratio` times their first distance, -1 once they close to 1/ratio,
// then nothing more until a new pinch starts.
class Pinch {
public:
    void start(float distance);
    int update(float distance);
    bool active() const { return start_ > 0.0f; }
    void stop() { start_ = 0.0f; }

    static constexpr float kRatio = 1.25f;

private:
    float start_ = 0.0f;
    bool fired_ = false;
};

// A drag that scrolls a list by notches of the wheel: a notch every `step` pixels, and after the finger lifts the
// list glides on with the finger's speed, slowing down (friction per second).
class Scroller {
public:
    void press();
    // The finger moved `delta` pixels (+ down/right) in `seconds` (0: no sample of its speed, see sample()).
    // Returns the notches to scroll, + = the wheel up (the list moves the way the finger does, as a phone's lists
    // do).
    int drag(float delta, float seconds, float step);
    // The finger's speed alone: `delta` pixels in `seconds` (once a frame, when the moves come several a frame).
    void sample(float delta, float seconds);
    // The finger lifted.
    void release();
    // A frame without a finger: the glide's notches.
    int glide(float seconds, float step);
    bool gliding() const { return velocity_ != 0.0f; }
    void stop() { velocity_ = 0.0f, carry_ = 0.0f; }

    static constexpr float kFriction = 4.0f;       // 1/s
    static constexpr float kMinVelocity = 60.0f;   // px/s below which the glide stops
    static constexpr float kMaxVelocity = 6000.0f; // px/s

private:
    int take(float distance, float step);

    float carry_ = 0.0f;     // pixels not yet a notch
    float velocity_ = 0.0f;  // px/s of the glide
    float sample_ = 0.0f;    // the finger's speed, smoothed
    bool held_ = false;
};

// The paper map's pan follows the finger: its speed (pixels a second, a fraction of the screen height a second
// in `per_height`) as the stick's byte offset from the centre, past the game's threshold when it moves at all.
std::uint8_t pan_axis(float speed_per_height, float gain);

}  // namespace sh::input::touch
