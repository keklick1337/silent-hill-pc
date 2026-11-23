// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// One frame of the game's main loop: the fixed skeleton
// (input, the soft-reset checks, the game state update, frame pacing, the buffer swap, the
// end-of-frame draw of the world list and the UI list) and the named host hooks that run at its
// fixed points. The engine owns the order and every end-of-frame draw decision; the host runs
// each named step over the game's records and the renderer.
//
// Hook lists and the draw rules are gamedata/config/main_frame.json; a mod replaces the file whole.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/gameflow/clear_color.h"
#include "formats/common/status.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

// The points of the frame the hooks run at, in frame order.
enum class FrameStage : int {
    Startup,       // once, before the first frame
    Top,           // right after the input poll
    Input,         // the pad pipeline and the overlays that read it
    AfterUpdate,   // after the game state update
    Late,          // fades, audio, the file queue (the files mark follows it)
    Sync,          // before frame pacing
    AfterSwap,     // after the buffer swap, before the end-of-frame framing
    AfterPresent,  // after the scene is ended
    Count,
};

// The frame profiler's stamps, in the game's numbering.
enum class FrameMarkPoint : int { Top = 0, UpdateStart = 1, UpdateEnd = 2, FilesEnd = 3, SwapEnd = 4 };

struct OpaqueDepthRoom {
    std::string map;
    std::vector<std::int32_t> rooms;
};

struct MainFrameRules {
    std::vector<std::string> hooks[static_cast<int>(FrameStage::Count)];
    // Rooms whose world draw tests opaque depth (their ordering table overlaps furniture).
    std::vector<OpaqueDepthRoom> opaque_depth_rooms;
    // Maps whose cutscenes clear to black over a fog-coloured renderer fog.
    std::vector<std::string> black_void_cutscene_maps;
    // States whose UI pass is a 4:3 menu once the world is off screen and the fade is not opaque.
    std::vector<std::int32_t> ui_menu_game_states;
    std::vector<std::int32_t> ui_menu_sys_states;

    static MainFrameRules builtin();
    static Result<MainFrameRules> parse(std::string_view json);

    bool opaque_depth(std::string_view map, std::int32_t room) const;
    bool black_void(std::string_view map) const;
};

// The game state the end-of-frame decisions read, taken once after the framing gate.
struct FrameSignals {
    std::int32_t game_state = 0;
    std::int32_t sys_state = 0;
    bool cutscene_flag = false;        // the system's cutscene flag
    bool border_active = false;        // letterbox bars are up or moving
    bool menu_flag = false;            // the system's menu flag (set the frame a menu button is pressed)
    std::int32_t pickup_item = 0;      // the item-pickup model flag, raw
    bool puzzle_depth = false;         // a full-screen puzzle wants per-pixel depth
    bool world_drawn = false;          // the world was submitted this frame
    bool present_last_frame = false;   // a frozen frame is presented behind a message
    bool fade_opaque = false;          // the screen fade is opaque (holds the framing)
    bool map_screen = false;           // the paper-map overlay is displayed
    bool fog_enabled = false;
    bool has_save = false;
    std::string map;                   // "" when there is no save
    std::int32_t room = 0;
};

class MainFrameHost {
public:
    virtual ~MainFrameHost() = default;

    // The id of a named hook, or -1 when the host has none by that name.
    virtual int hook_id(std::string_view name) = 0;
    virtual void run_hook(int id) = 0;

    // The coming frame presents the last update again and updates nothing (a frame before the next vertical blank).
    virtual bool render_only_due() { return false; }
    // Start of such a frame: input is sampled so a press is not lost before the next update.
    virtual void render_only_begin() {}
    // Before such a frame is presented: the last update's draw lists wait to be drawn again, the scene is the blend
    // of the last two updates.
    virtual void render_only_present() {}
    // The update that just ran is one of several making up a frame longer than usual: it is not presented.
    virtual bool update_only_due() { return false; }
    // End of such an update: what it drew is dropped.
    virtual void update_only_end() {}
    // Before an updated frame is presented.
    virtual void updated_present() {}

    virtual void tick() = 0;
    virtual void mark(FrameMarkPoint point) = 0;
    virtual void poll_input() = 0;
    virtual bool warm_reset_due() = 0;
    virtual void warm_boot() = 0;
    // Picks the draw buffer, points the packet arena at it and clears both ordering tables.
    virtual void begin_buffers() = 0;
    // Clears the per-frame flags the update re-arms (music status, item pickup, puzzle depth, world drawn).
    virtual void clear_frame_flags() = 0;
    virtual void replay_tick() = 0;
    virtual void update_game_state() = 0;

    virtual bool demo_active() { return sh::game::session().sys_flag(sh::game::sys_flag::kDemo); }
    virtual void pace_demo_frame() = 0;
    virtual void clock_demo_frame() = 0;
    virtual void pace_frame() = 0;
    virtual void clock_credit_vblanks() = 0;
    virtual void profile_sample() = 0;
    virtual void clock_line_frame() = 0;
    virtual void clock_commit() = 0;
    virtual void swap_buffers() = 0;

    // Wide or 4:3 for this frame, written to the renderer; true while a 2D background is held.
    virtual bool framing_gate() = 0;
    virtual FrameSignals signals() = 0;
    virtual void publish_cutscene(bool live) = 0;
    virtual void publish_item_take(std::int32_t pickup_item) = 0;
    virtual std::int32_t world_anchor() = 0;
    virtual void set_projection_offset(std::int32_t x, std::int32_t y) = 0;
    virtual void set_dither_suppressed(bool on) = 0;
    virtual void apply_clear_color(const ClearColorOut& out) = 0;
    virtual void sort_clear() = 0;
    virtual void prepare_world_list() = 0;
    virtual void item_depth_begin() = 0;
    virtual void draw_world_list(bool opaque_depth) = 0;
    virtual void item_depth_end() = 0;
    virtual void draw_ui_list(bool menu_framing) = 0;
    virtual void end_scene() = 0;
};

class MainFrame {
public:
    MainFrame(MainFrameHost& host, MainFrameRules rules);

    // Hook names the host does not know (they are skipped).
    const std::vector<std::string>& unknown_hooks() const { return unknown_; }
    const MainFrameRules& rules() const { return rules_; }

    void startup();
    // One frame; false when a soft reset cut it short.
    bool run();

private:
    void stage(FrameStage s);
    void run_render_only();
    void present(bool again);

    MainFrameHost& host_;
    MainFrameRules rules_;
    std::vector<int> ids_[static_cast<int>(FrameStage::Count)];
    std::vector<std::string> unknown_;
};

// The Numpad-period developer key: logs the player's position on the press edge and, with the free
// camera on, toggles the fog; the fog is never off without the free camera.
struct PositionKeyIn {
    bool enabled = false;    // developer controls allowed, keyboard up, in game
    bool key_down = false;
    bool free_camera = false;
    bool fog_disabled = false;
};

struct PositionKeyOut {
    bool log_position = false;
    bool fog_toggled = false;     // echo the new fog state
    bool fog_disabled = false;
    bool force_fog_off = false;   // clear the world's fog flag this frame
};

class PositionKey {
public:
    PositionKeyOut update(const PositionKeyIn& in);

private:
    bool prev_ = false;
};

}  // namespace sh::gameflow
