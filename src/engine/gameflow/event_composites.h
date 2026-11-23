#pragma once

// Composite helpers of the map event scripts: sequences built from the step
// counters and the commands of event_steps.h / event_commands.h. A script calls one every frame; it switches on the
// sub-step, runs commands, and ends by handing the state back to gameplay. They freeze the player for their span:
// a picture shown with a fade, a message over a picture, an item or a paper map picked up, a message with a sound or
// a voice, character animations that wait for their end and the event camera.

#include <cstdint>

#include "engine/gameflow/event_commands.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

// Opaque handle of a character; only the host knows what it is.
using CharaHandle = void*;

enum class CharaAnimCommand : std::int32_t { SetState = 0, AwaitEnd = 1, Lock = 2, Unlock = 3, Reset = 4 };
enum class PaperMapCommand : std::int32_t { Load = 0, Draw = 1, Unload = 2 };

// Where the picture the event stores (and later puts back) was taken from.
enum class VramArea : std::int32_t { MapSheet = 0, Pickup = 1 };

// Four numbers that steer the event camera: two accelerations and two maxima (zero in a request = host default).
struct CameraParams {
    std::int32_t accel_a = 0, accel_b = 0, max_a = 0, max_b = 0;
};

// A pickup the common-item helper knows (gamedata/config/pickups.json; the host resolves names to numbers).
struct CommonPickup {
    int item = -1;
    int message = -1;
    bool ammo = false;  // the count is the item's pickup count times the ammo multiplier
};

// The pages a paper map page marks as found along with itself.
struct PaperMapReveal {
    int page = 0;
    std::uint32_t pages = 0;  // bit mask of the pages
};

class CompositeHost : public EventCommandHost {
public:
    // Player and state.
    virtual void freeze_player() = 0;
    virtual void unfreeze_player() = 0;
    virtual void resume_gameplay() = 0;                 // the state goes back to gameplay
    // the "no NPC may move" system flag
    virtual bool world_frozen() const { return sh::game::session().sys_flag(sh::game::sys_flag::kSceneLock); }
    virtual bool hostile_npc_alive() const = 0;
    virtual void stop_frame_time() = 0;
    virtual bool confirm_pressed() = 0;                 // a click of the enter or cancel button this frame
    virtual void set_background_gamma(std::int32_t gamma) = 0;  // Q8
    virtual void play_sound(int sound, const Waypoint* at) = 0;
    virtual void set_flag(std::int16_t flag, bool on) = 0;

    // Characters.
    virtual bool is_player(CharaHandle chara) const = 0;
    virtual void anim_set_state(CharaHandle chara, std::int32_t state) = 0;
    virtual bool anim_ended(CharaHandle chara) = 0;
    virtual void anim_lock(CharaHandle chara) = 0;
    virtual void anim_unlock(CharaHandle chara) = 0;
    virtual void anim_reset(CharaHandle chara) = 0;

    // Messages with a voice.
    virtual MessageResult show_voiced_message(int message) = 0;  // marks the music state as voice dialog first
    virtual bool voice_redisplay_consume() = 0;          // the message page is a re-display of one that just ended
    virtual void voice_dropped(bool dropped, int command, int index, int message) = 0;
    virtual void play_voice(int command) = 0;

    // Items (the pickup prompt).
    virtual void pickup_begin() = 0;                     // keeps the player's pose across the freeze
    virtual void pickup_hold_world() = 0;                // the world stands while the model loads
    virtual bool pickup_animate(int item) = 0;           // true once the model turns on its own
    virtual void pickup_end() = 0;                       // gives the pose back
    virtual int ammo_pickup_count(int item) = 0;
    virtual int ammo_multiplier() = 0;                   // >= 1; the easy difficulty raises it to at least 2
    // Mod hooks (item.pickup / item.picked). The pickup is starting: false ends it at once, before anything was
    // frozen, shown or flagged, so the item stays where it lies.
    virtual bool pickup_starting(int item, int count, int flag) { (void)item, (void)count, (void)flag; return true; }
    // The count about to be added (a hook may have changed it when the pickup started).
    virtual void pickup_count(int item, int flag, int& count) { (void)item, (void)flag, (void)count; }
    virtual void pickup_added(int item, int count, int flag) { (void)item, (void)count, (void)flag; }

    // Paper maps.
    virtual void map_seek(int page) = 0;
    virtual void map_store_vram(VramArea area) = 0;
    virtual void map_restore_vram(VramArea area) = 0;
    virtual void map_read_page(int page, bool marking) = 0;  // queues the sheet (and the markings if asked)
    virtual void map_enter_screen(bool one_vblank) = 0;
    virtual void map_swap_buffers() = 0;
    virtual void map_wait_files() = 0;
    virtual void map_leave_screen() = 0;
    virtual void map_draw() = 0;
    virtual void map_mark_found(int page) = 0;
    virtual void map_mark_found_mask(std::uint32_t pages) = 0;

    // The event camera.
    virtual CameraParams camera_position_defaults() const = 0;
    virtual CameraParams camera_watch_defaults() const = 0;
    virtual void camera_move(const Waypoint& target, const CameraParams& params, bool warp) = 0;
    virtual void camera_watch(const Waypoint& target, const CameraParams& params, bool warp) = 0;
};

namespace composites {

// The same with a message over the picture.
void map_msg_with_bg(CompositeHost& h, int file, std::int32_t fade0, std::int32_t fade1, int message);
// Two messages; the picture is dimmed from the second on. `message0 == 0` replaces the first with a button wait.
void map_msg_with_dimmed_bg(CompositeHost& h, int file, std::int32_t fade0, std::int32_t fade1, int message0,
                            int message1);
void bg_load_fade_in(CompositeHost& h, int file, std::int32_t fade);
void bg_fade_in(CompositeHost& h, int file, std::int32_t fade0, std::int32_t fade1);
void bg_fade_out(CompositeHost& h, std::int32_t fade_from_black, std::int32_t fade_to_black);
void map_msg_with_sfx(CompositeHost& h, int message, int sound, const Waypoint* at);

// `sub` of the await-end command is true for the wait helpers below, as in the scripts.
void chara_anim(CompositeHost& h, CharaAnimCommand cmd, CharaHandle chara, std::int32_t state, bool sub);
void chara_anim_play_to_end(CompositeHost& h, CharaHandle chara, std::int32_t state);
void chara_anim_unlock_play_to_end(CompositeHost& h, CharaHandle chara);

// A voiced message page: when it ends, the next voice command of `commands` plays (commands outside 0x1000..0x16FF
// are dropped as silence). `index` counts the pages shown. `skip_redisplay` false: a re-displayed page plays its voice
// again too (the second copy of the helper some map scripts were written against).
void map_msg_with_audio(CompositeHost& h, int message, std::uint8_t& index, const std::uint16_t* commands,
                        bool skip_redisplay = true);

// The item prompt: freeze, load the model, turn it, ask; "yes" adds `count` of `item`, "no" clears the flag.
void item_take(CompositeHost& h, int item, int count, std::int16_t flag, int message);
// A pickup of a kind of the table: items and ammo with their own prompt message.
void common_item_take(CompositeHost& h, const CommonPickup* pickups, std::size_t count, std::uint32_t kind,
                      std::int16_t flag);
// A paper map picked up: the sheet is shown, "yes" marks the page and the pages it reveals.
void paper_map_take(CompositeHost& h, const PaperMapReveal* reveals, std::size_t count, int page, std::int16_t flag,
                    int message);
void paper_map_command(CompositeHost& h, PaperMapCommand cmd, int page);

// The event camera towards a point: `base` null = absolute, otherwise the offset from it; zero in the parameters
// means the camera's default.
void camera_position(CompositeHost& h, const Waypoint* base, const Waypoint& offset, const CameraParams& params,
                     bool warp);
void camera_look_at(CompositeHost& h, const Waypoint* base, const Waypoint& offset, const CameraParams& params,
                    bool warp);

}  // namespace composites
}  // namespace sh::gameflow
