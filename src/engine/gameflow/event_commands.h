#pragma once

// Command helpers of the map event scripts: screen fades and cutscene borders,
// the event background picture, the picked-up item model and the path waypoints. A script calls a helper every frame
// with a command; "start" commands act at once, "wait" commands advance the script's step when the work is done, and
// "auto" commands take the command from the auto step, so one call per frame walks through start, load and wait.
// The command numbers are the scripts' (overlays and events.lua).

#include <cstdint>

#include "engine/gameflow/event_steps.h"

namespace sh::gameflow {

enum class FadeCommand : std::int32_t { Start = 0, Wait = 1, Auto = 2 };
enum class FadeKind : std::int32_t { Black = 0, White = 1, Borders = 2, CutsceneBorders = 3 };

enum class BackgroundCommand : std::int32_t {
    Load = 0,           // queue the picture
    AwaitLoad = 1,      // advance once it is in
    Draw = 2,
    StoreVram = 3,      // keep the VRAM the picture overwrites
    LoadSecondary = 4,  // queue the second picture
    DrawSecondary = 5,
    RestoreVram = 6,
    Auto = 7,           // the command is the auto step
    LoadEither = 8,     // the second picture while the auto step is 0, then wait for it
};

enum class ItemCommand : std::uint32_t {
    Load = 0,           // queue the item model
    AwaitLoad = 1,      // advance once it is in (the pickup display is set up)
    Nothing = 2,
    Add = 3,            // add the item to the inventory
    AutoNoAdd = 4,      // the command is the auto step; no inventory add
    Auto = 5,
    AutoAdd = 6,        // the command is the auto step, then the item is added
};

class EventCommandHost : public StepHost {
public:
    // Fades.
    virtual void set_fade_rate(std::int32_t rate) = 0;  // Q12 per second
    virtual void start_fade(bool fade_in, bool white) = 0;  // keeps the progress of a running fade
    virtual void show_borders() = 0;
    virtual void hide_borders() = 0;
    virtual void mark_cutscene() = 0;
    virtual bool fade_idle() const = 0;        // no fade on screen
    virtual bool fade_done() const = 0;        // a fade out has finished
    virtual bool borders_hidden() const = 0;
    virtual bool borders_shown() const = 0;

    // Background pictures.
    virtual void load_background(int file, bool secondary) = 0;
    virtual bool files_loaded() = 0;
    virtual void draw_background(bool secondary) = 0;
    virtual void store_vram() = 0;
    virtual void restore_vram() = 0;

    // Items.
    virtual void load_item_model(int item) = 0;
    virtual void show_item_pickup(int item) = 0;
    virtual void add_item(int item, int count) = 0;
};

namespace commands {

// `fade_out` is the script's raw value: a start command tests it for non-zero, a wait command for exactly 0 or 1.
void fade(EventCommandHost& h, FadeCommand cmd, std::int32_t fade_out, FadeKind kind, std::int32_t rate, bool sub);
void background(EventCommandHost& h, BackgroundCommand cmd, int file, bool sub);
void item(EventCommandHost& h, ItemCommand cmd, int item, int count, bool sub);

}  // namespace commands

// A path waypoint of an event: absolute (mode 1), or relative to the player on the ground plane (mode 0); the height
// is the player's. Other modes set nothing (false).
struct Waypoint {
    std::int32_t x = 0, y = 0, z = 0;
};

bool path_waypoint(std::int32_t mode, const Waypoint& player, std::int32_t x, std::int32_t z, Waypoint& out);

}  // namespace sh::gameflow
