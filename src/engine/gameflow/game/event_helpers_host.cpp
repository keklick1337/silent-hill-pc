// The map scripts' helpers on the engine: step counters, waits and
// flag branches, fades and cutscene borders, the event background, the picked-up item model, path waypoints,
// tweens, and the composite helpers (picture with a fade or messages, message with a sound or a voice, item and
// paper map pickups, character animations that wait for their end, the event camera). The entry points keep the
// names the map scripts and the Lua API call. The common pickups and the pages a paper map reveals come from
// gamedata/config/pickups.json and paper_map_reveals.json.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/camera/game_camera.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/composite_data.h"
#include "engine/gameflow/event_composites.h"
#include "engine/gameflow/event_tweens.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/game/step_hosts.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/screens/paper_map_view.h"
#include "engine/mods/data_reload.h"

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;
using sh::game::PlayerVar;

using StepHost = link::SessionSteps<>;
using CommandHost = link::SessionCommands<gf::EventCommandHost>;

// A story event's own item command (Event_InvItemCmd): the item comes without a pickup prompt, so mods hear of it as
// item.given; the pickup prompt adds through the composites and reports item.pickup / item.picked instead.
class GivingHost final : public CommandHost {
public:
    void add_item(int item, int count) override {
        const int given = link::hook_item_given(item, count, "event");
        if (given > 0)
            CommandHost::add_item(item, given);
    }
};

// The prompt messages of the shared message table by name (pickups.json): their numbers in the table.
int shared_message(const std::string& name) {
    static const struct {
        const char* name;
        int index;
    } kNames[] = {{"Yes", 0}, {"No", 1}, {"SaveGame", 2}, {"NoMap", 3}, {"TooDarkForMap", 4},
                  {"FirstAidSelect", 5}, {"HealthDrinkSelect", 6}, {"AmpouleSelect", 7},
                  {"HandgunAmmoSelect", 8}, {"RifleAmmoSelect", 9}, {"ShotgunAmmoSelect", 10},
                  {"DoorJammed", 11}, {"DoorLocked", 12}, {"DoorUnlocked", 13}};
    for (const auto& n : kNames) {
        if (name == n.name) {
            return n.index;
        }
    }
    SH_ERR("[GAMEFLOW] pickups.json: unknown message \"%s\"", name.c_str());
    return -1;
}

template <typename T>
T load_or_fail(const char* name, const std::string& gamedata, const std::string& mods) {
    std::vector<std::string> warnings;
    std::string source;
    const sh::Result<std::string> text = gf::read_config_file(gamedata, mods, name, &source, warnings);
    for (const std::string& w : warnings) {
        SH_WARN("[GAMEFLOW] %s", w.c_str());
    }
    if (!text) {
        SH_ERR("[GAMEFLOW] config/%s: %s", name, text.error().c_str());
        return T{};
    }
    sh::Result<T> parsed = T::parse(text.value());
    if (!parsed) {
        SH_ERR("[GAMEFLOW] config/%s: %s", name, parsed.error().c_str());
        return T{};
    }
    SH_LOG("[GAMEFLOW] config/%s loaded%s%s", name, source.empty() ? "" : " from mod ", source.c_str());
    return parsed.value();
}

struct CompositeData {
    std::vector<gf::CommonPickup> pickups;  // indexed by the common pickup kind of the events
    std::vector<gf::PaperMapReveal> reveals;
};

const CompositeData& data() {
    static CompositeData loaded;
    static std::uint32_t loaded_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(loaded_epoch)) {
        loaded = [] {
            CompositeData d;
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            const std::string gamedata = link::gamedata_path();
            const gf::PickupTable table = load_or_fail<gf::PickupTable>("pickups.json", gamedata, mods);
            for (const gf::PickupEntry& e : table.entries()) {
                const int kind = link::common_pickup_number(e.item.c_str());
                if (kind < 0) {
                    SH_ERR("[GAMEFLOW] pickups.json: \"%s\" is not a common pickup", e.item.c_str());
                    continue;
                }
                if (static_cast<std::size_t>(kind) >= d.pickups.size()) {
                    d.pickups.resize(static_cast<std::size_t>(kind) + 1);
                }
                gf::CommonPickup& p = d.pickups[static_cast<std::size_t>(kind)];
                p.item = link::item_number(e.item.c_str());
                p.message = shared_message(e.message);
                p.ammo = e.ammo;
            }
            const gf::PaperMapReveals reveals = load_or_fail<gf::PaperMapReveals>("paper_map_reveals.json", gamedata, mods);
            for (const gf::RevealEntry& e : reveals.entries()) {
                gf::PaperMapReveal r;
                r.page = link::paper_map_number(e.page.c_str());
                for (const std::string& name : e.revealed) {
                    const int page = link::paper_map_number(name.c_str());
                    if (page >= 0 && page < 32) {
                        r.pages |= 1u << page;
                    }
                }
                if (r.page >= 0) {
                    d.reveals.push_back(r);
                }
            }
            return d;
        }();
    }
    return loaded;
}

// The pose a pickup prompt keeps across the player freeze (one prompt is open at a time).
struct KeptPose {
    std::int64_t state = 0, lower = 0, upper = 0;
    std::int64_t step[2] = {0, 0}, control[2] = {0, 0};
};
KeptPose g_kept_pose;

// Harry's states a kept pose gives back: no state, no upper body state, lower body standing up to running left.
constexpr std::int64_t kStateNone = 0;
constexpr std::int64_t kUpperNone = 0;
constexpr std::int64_t kLowerRunLeft = 8;

class CompositeHost final : public link::SessionCommands<gf::CompositeHost> {
public:
    void freeze_player() override { link::overlay_freeze_player(); }
    void unfreeze_player() override { link::overlay_unfreeze_player(); }
    void resume_gameplay() override { sh::game::session().set_sys_state_next(sh::game::sys_state::kGameplay); }
    bool hostile_npc_alive() const override { return link::hostile_npc_alive(); }
    void stop_frame_time() override { link::set_delta_time(0); }
    bool confirm_pressed() override {
        const auto& binds = sh::game::session().game.settings.binds;
        return (sh::game::session().game.pads[0].clicked & (binds.enter | binds.cancel)) != 0;
    }
    void set_background_gamma(std::int32_t gamma) override { link::set_background_gamma(gamma); }
    void play_sound(int sound, const gf::Waypoint* at) override { link::play_sound_at(sound, at); }
    void set_flag(std::int16_t flag, bool on) override { sh::game_state::set_event_flag(flag, on); }

    bool is_player(gf::CharaHandle chara) const override { return chara == link::player_record(); }
    void anim_set_state(gf::CharaHandle chara, std::int32_t state) override {
        link::overlay_anim(link::AnimOp::SetState, chara, is_player(chara), state);
    }
    bool anim_ended(gf::CharaHandle chara) override {
        return link::overlay_anim(link::AnimOp::PlaybackState, chara, is_player(chara), 0) == link::kAnimPlaybackEnd;
    }
    void anim_lock(gf::CharaHandle chara) override { link::overlay_anim(link::AnimOp::Lock, chara, is_player(chara), 0); }
    void anim_unlock(gf::CharaHandle chara) override {
        link::overlay_anim(link::AnimOp::Unlock, chara, is_player(chara), 0);
    }
    void anim_reset(gf::CharaHandle chara) override { link::overlay_anim(link::AnimOp::Reset, chara, is_player(chara), 0); }

    gf::MessageResult show_voiced_message(int message) override {
        sh::game::session().system.process.music_status |= link::kMusicVoiceDialog;
        link::set_message_game_voiced(true);
        const int state = link::map_message_draw(message);
        link::set_message_game_voiced(false);
        return static_cast<gf::MessageResult>(state);
    }
    bool voice_redisplay_consume() override {
        gf::MapEventState& s = gf::map_event_state();
        const bool was = s.redisplay_setup != 0;
        s.redisplay_setup = 0;
        return was;
    }
    void voice_dropped(bool dropped, int command, int index, int message) override {
        gf::map_event_state().voice_dropped = dropped ? 1 : 0;
        static int logged = 0;
        if (dropped && logged < 20) {
            SH_DBG("[MSGVOICE] dropped out-of-range voice cmd 0x%04X at audioIdx=%d (msg %d) - table overrun?", command,
                   index, message);
            logged++;
        }
    }
    void play_voice(int command) override { sh::game_state::sound_command(command); }

    bool pickup_starting(int item, int count, int flag) override { return link::hook_item_pickup(item, count, flag); }
    void pickup_count(int item, int flag, int& count) override { count = link::hook_item_pickup_count(item, flag, count); }
    void pickup_added(int item, int count, int flag) override { link::hook_item_picked(item, count, flag); }
    void pickup_begin() override {
        // The prompt freezes the world on a capture of the pose Harry had when it opened; the freeze zeroes his
        // sub-states, which showed as a hop on the first live frame. Keep them to give them back afterwards.
        const sh::game::Player& p = sh::game::player();
        g_kept_pose.state = p.get(PlayerVar::State);
        g_kept_pose.lower = p.get(PlayerVar::LowerBody);
        g_kept_pose.upper = p.get(PlayerVar::UpperBody);
        g_kept_pose.step[0] = p.get(PlayerVar::BodyStateStep);
        g_kept_pose.step[1] = p.get(PlayerVar::UpperStateStep);
        g_kept_pose.control[0] = p.get(PlayerVar::BodyControlState);
        g_kept_pose.control[1] = p.get(PlayerVar::UpperControlState);
    }
    void pickup_hold_world() override {
        // The world stands from the first frame, not once the model has loaded: otherwise the camera eases toward its
        // target during the load and the freeze captures it mid-way.
        sh::game::session().system.process.music_status |= sh::game::music_status::kPause;
        link::hold_last_frame();
        link::set_pickup_item_active(true);
    }
    bool pickup_animate(int item) override { return link::animate_pickup_item(item); }
    void pickup_end() override {
        // Plain standing or walking only; anything else keeps the reset the unfreeze did.
        if (g_kept_pose.state == kStateNone && g_kept_pose.upper == kUpperNone && g_kept_pose.lower <= kLowerRunLeft) {
            sh::game::Player& p = sh::game::player();
            p.set(PlayerVar::LowerBody, g_kept_pose.lower);
            p.set(PlayerVar::BodyStateStep, g_kept_pose.step[0]);
            p.set(PlayerVar::UpperStateStep, g_kept_pose.step[1]);
            p.set(PlayerVar::BodyControlState, g_kept_pose.control[0]);
            p.set(PlayerVar::UpperControlState, g_kept_pose.control[1]);
        }
    }
    int ammo_pickup_count(int item) override { return link::ammo_pickup_count(item); }
    int ammo_multiplier() override {
        constexpr int kEasy = -1;
        int mult = sh::game::session().game.settings.bullet_multiplier + 1;
        if (sh::game_state::save_difficulty() == kEasy && mult < 2) {
            mult = 2;
        }
        return mult;
    }

    void map_seek(int page) override { link::seek_file(sh::screens::paper_map_image_file(page)); }
    void map_store_vram(gf::VramArea area) override { link::keep_paper_map_area(area == gf::VramArea::Pickup); }
    void map_restore_vram(gf::VramArea area) override { link::restore_paper_map_area(area == gf::VramArea::Pickup); }
    void map_read_page(int page, bool marking) override {
        link::read_image(sh::screens::paper_map_image_file(page), link::Picture::PaperMap);
        if (marking) {
            link::read_image(sh::screens::paper_map_marking_file(page), link::Picture::PaperMapMarkings);
        }
    }
    void map_enter_screen(bool one_vblank) override {
        link::set_map_screen_active(true);
        if (one_vblank) {
            link::set_interval_vblanks(1);
        }
        link::screen_init(true);
    }
    void map_swap_buffers() override { link::swap_display_buffers(); }
    void map_wait_files() override { link::wait_file_queue(); }
    void map_leave_screen() override {
        link::set_map_screen_active(false);
        link::screen_init(false);
    }
    void map_draw() override { link::draw_paper_map_sheet(); }
    void map_mark_found(int page) override { link::save_mark_paper_map(page); }
    void map_mark_found_mask(std::uint32_t pages) override { link::save_mark_paper_maps(pages); }

    gf::CameraParams camera_position_defaults() const override {
        const auto& c = sh::camera::game_camera().user_camera;
        return {c.accel_xz, c.accel_y, c.max_speed_xz, c.max_speed_y};
    }
    gf::CameraParams camera_watch_defaults() const override {
        const auto& c = sh::camera::game_camera().default_watch;
        return {c.accel_x, c.accel_y, c.max_speed_x, c.max_speed_y};
    }
    void camera_move(const gf::Waypoint& target, const gf::CameraParams& p, bool warp) override {
        link::camera_move(target, p, warp);
    }
    void camera_watch(const gf::Waypoint& target, const gf::CameraParams& p, bool warp) override {
        link::camera_watch(target, p, warp);
    }
};

gf::Waypoint waypoint_at(const std::int32_t* v) { return gf::Waypoint{v[0], v[1], v[2]}; }

// Tweens run in the engine; the PS1 shares one timer array, the engine's second copy of the helpers (sh.event.ramp_*)
// had its own and the map scripts written against it keep that.
gf::EventTweens& tweens() {
    static gf::EventTweens t;
    return t;
}
gf::EventTweens& ramps() {
    static gf::EventTweens t;
    return t;
}

}  // namespace

extern "C" {

void Event_WaitPlayerStop(void) {
    StepHost h;
    gf::steps::wait_player_stop(h);
}

void Event_WaitTimer(std::int32_t delay, unsigned int incSubStep) {
    StepHost h;
    gf::steps::wait_timer(h, delay, incSubStep != 0);
}

void Event_SysStateBranchOnFlag(std::int32_t eventFlagIdx, std::int32_t stepTrue, std::int32_t stepFalse,
                                unsigned int setSubStep) {
    StepHost h;
    gf::steps::branch_on_flag(h, static_cast<std::int16_t>(eventFlagIdx), stepTrue, stepFalse, setSubStep != 0);
}

void Event_DisplayMapMsg(unsigned int hasSelection, std::int32_t mapMsgIdx, std::int32_t step0, std::int32_t step1,
                         std::int32_t step2, unsigned int incSubStep) {
    StepHost h;
    gf::steps::message(h, hasSelection != 0, mapMsgIdx, step0, step1, step2, incSubStep != 0);
}

void Event_ScreenFadeCmd(std::int32_t cmd, unsigned int fadeOut, std::int32_t fadeType, std::int32_t fadeTimestep,
                         unsigned int incSubStep) {
    CommandHost h;
    gf::commands::fade(h, static_cast<gf::FadeCommand>(cmd), static_cast<std::int32_t>(fadeOut),
                       static_cast<gf::FadeKind>(fadeType), fadeTimestep, incSubStep != 0);
}

void Event_BgTextureCmd(std::int32_t cmd, std::int32_t textureFileIdx, unsigned int incSubStep) {
    CommandHost h;
    gf::commands::background(h, static_cast<gf::BackgroundCommand>(cmd), textureFileIdx, incSubStep != 0);
}

void Event_InvItemCmd(std::int32_t cmd, std::int32_t itemId, std::int32_t itemCount, unsigned int incSubStep) {
    GivingHost h;
    gf::commands::item(h, static_cast<gf::ItemCommand>(cmd), itemId, itemCount, incSubStep != 0);
}

void Event_PathWaypointSet(unsigned int isAbs, std::int32_t charaSlotIdx, std::int32_t waypointIdx,
                           std::int16_t headingAngle, std::int32_t posX, std::int32_t posZ) {
    const sh::game::Player& player = sh::game::player();
    const gf::Waypoint at{static_cast<std::int32_t>(player.get(PlayerVar::PositionX)),
                          static_cast<std::int32_t>(player.get(PlayerVar::PositionY)),
                          static_cast<std::int32_t>(player.get(PlayerVar::PositionZ))};
    gf::Waypoint w;
    if (gf::path_waypoint(static_cast<std::int32_t>(isAbs), at, posX, posZ, w)) {
        gf::MapEventState& s = gf::map_event_state();
        std::int32_t* out = s.waypoints[charaSlotIdx][waypointIdx];
        out[0] = w.x;
        out[1] = w.y;
        out[2] = w.z;
        s.waypoint_headings[charaSlotIdx] = headingAngle;
    }
}

void Event_PathWaypointExecutePlayer(std::int32_t animId, std::int32_t waypointCount, unsigned int incSubStep) {
    gf::MapEventState& s = gf::map_event_state();
    if (link::overlay_player_path(animId, &s.waypoints[0][0][0], s.waypoint_headings[0], waypointCount)) {
        StepHost h;
        gf::steps::advance(h, incSubStep ? 1 : 0);
    }
}

void Event_PathWaypointExecuteChara(void* chara, std::int32_t animId, std::int32_t waypointCount,
                                    unsigned int incSubStep) {
    gf::MapEventState& s = gf::map_event_state();
    if (link::overlay_chara_path(chara, animId, &s.waypoints[1][0][0], s.waypoint_headings[1], waypointCount)) {
        StepHost h;
        gf::steps::advance(h, incSubStep ? 1 : 0);
    }
}

void Event_TweenReset(std::int32_t timerIdx) {
    tweens().reset(timerIdx);
}

std::int32_t Event_TweenLinear(std::int32_t target, std::int32_t duration, std::int32_t timerIdx) {
    return tweens().linear(target, duration, timerIdx, sh::game_state::delta_time());
}

void Event_RampReset(std::int32_t timerIdx) {
    ramps().reset(timerIdx);
}

std::int32_t Event_RampAdvance(std::int32_t target, std::int32_t duration, std::int32_t timerIdx) {
    return ramps().linear(target, duration, timerIdx, sh::game_state::delta_time());
}

void Event_CharaAnimCmdExecute(std::int32_t cmd, void* chara, std::int32_t animState, unsigned int incSubStep) {
    CompositeHost h;
    gf::composites::chara_anim(h, static_cast<gf::CharaAnimCommand>(cmd), chara, animState, incSubStep != 0);
}

void Event_PaperMapCmd(std::int32_t cmd, std::int32_t paperMapIdx) {
    CompositeHost h;
    gf::composites::paper_map_command(h, static_cast<gf::PaperMapCommand>(cmd), paperMapIdx);
}

void Event_DisplayMapMsgWithAudio(std::int32_t mapMsgIdx, std::uint8_t* audioIdx, const std::uint16_t* audioCmds) {
    CompositeHost h;
    gf::composites::map_msg_with_audio(h, mapMsgIdx, *audioIdx, audioCmds);
}

// The same with a re-displayed page voiced again: the second copy of the helper, which some map scripts call.
void Map_MessageWithAudio(std::int32_t mapMsgIdx, std::uint8_t* audioIdx, const std::uint16_t* audioCmds) {
    CompositeHost h;
    gf::composites::map_msg_with_audio(h, mapMsgIdx, *audioIdx, audioCmds, false);
}

void Event_CameraPositionSet(const std::int32_t* pos, std::int32_t offsetOrPosX, std::int32_t offsetOrPosY,
                             std::int32_t offsetOrPosZ, std::int32_t accelXz, std::int32_t accelY,
                             std::int32_t speedXzMax, std::int32_t speedYMax, unsigned int warp) {
    CompositeHost h;
    gf::Waypoint base;
    if (pos != nullptr) {
        base = waypoint_at(pos);
    }
    gf::composites::camera_position(h, pos != nullptr ? &base : nullptr,
                                    gf::Waypoint{offsetOrPosX, offsetOrPosY, offsetOrPosZ},
                                    gf::CameraParams{accelXz, accelY, speedXzMax, speedYMax}, warp != 0);
}

void Event_CameraLookAtSet(const std::int32_t* lookAt, std::int32_t offsetOrPosX, std::int32_t offsetOrPosY,
                           std::int32_t offsetOrPosZ, std::int32_t angularAccelX, std::int32_t angularAccelY,
                           std::int32_t angularSpeedXMax, std::int32_t angularSpeedYMax, unsigned int warp) {
    CompositeHost h;
    gf::Waypoint base;
    if (lookAt != nullptr) {
        base = waypoint_at(lookAt);
    }
    gf::composites::camera_look_at(h, lookAt != nullptr ? &base : nullptr,
                                   gf::Waypoint{offsetOrPosX, offsetOrPosY, offsetOrPosZ},
                                   gf::CameraParams{angularAccelX, angularAccelY, angularSpeedXMax, angularSpeedYMax},
                                   warp != 0);
}

void Event_CharaAnimPlayToEnd(void* chara, std::int32_t animState) {
    CompositeHost h;
    gf::composites::chara_anim_play_to_end(h, chara, animState);
}

void Event_CharaAnimUnlockPlayToEnd(void* chara) {
    CompositeHost h;
    gf::composites::chara_anim_unlock_play_to_end(h, chara);
}

void Event_BgTextureLoadFadeIn(std::int32_t textureFileIdx, std::int32_t fadeTimestep) {
    CompositeHost h;
    gf::composites::bg_load_fade_in(h, textureFileIdx, fadeTimestep);
}

void Event_BgTextureFadeIn(std::int32_t textureFileIdx, std::int32_t fadeTimestep0, std::int32_t fadeTimestep1) {
    CompositeHost h;
    gf::composites::bg_fade_in(h, textureFileIdx, fadeTimestep0, fadeTimestep1);
}

void Event_BgTextureFadeOut(std::int32_t fadeTimestepFromBlack, std::int32_t fadeTimestepToBlack) {
    CompositeHost h;
    gf::composites::bg_fade_out(h, fadeTimestepFromBlack, fadeTimestepToBlack);
}

void Event_DisplayMapMsgWithSfx(std::int32_t mapMsgIdx, std::int32_t sfxId, const std::int32_t* sfxPos) {
    CompositeHost h;
    gf::Waypoint at;
    if (sfxPos != nullptr) {
        at = waypoint_at(sfxPos);
    }
    gf::composites::map_msg_with_sfx(h, mapMsgIdx, sfxId, sfxPos != nullptr ? &at : nullptr);
}

void Event_DisplayMapMsgWithBg(std::int32_t textureFileIdx, std::int32_t fadeTimestep0, std::int32_t fadeTimestep1,
                               std::int32_t mapMsgIdx) {
    CompositeHost h;
    gf::composites::map_msg_with_bg(h, textureFileIdx, fadeTimestep0, fadeTimestep1, mapMsgIdx);
}

void Event_DisplayMapMsgWithDimmedBg(std::int32_t textureFileIdx, std::int32_t fadeTimestep0,
                                     std::int32_t fadeTimestep1, std::int32_t mapMsgIdx0, std::int32_t mapMsgIdx1) {
    CompositeHost h;
    gf::composites::map_msg_with_dimmed_bg(h, textureFileIdx, fadeTimestep0, fadeTimestep1, mapMsgIdx0, mapMsgIdx1);
}

void Event_ItemTake(std::int32_t itemId, std::int32_t itemCount, std::int32_t eventFlagIdx, std::int32_t mapMsgIdx) {
    CompositeHost h;
    gf::composites::item_take(h, itemId, itemCount, eventFlagIdx, mapMsgIdx);
}

void Event_CommonItemTake(std::uint32_t pickupType, std::int32_t eventFlagIdx) {
    CompositeHost h;
    const CompositeData& d = data();
    gf::composites::common_item_take(h, d.pickups.data(), d.pickups.size(), pickupType, eventFlagIdx);
}

// The prompt message of the common pickup that gives `item` (pickups.json), -1 for none.
std::int32_t Event_ItemPickupMessage(std::int32_t item) {
    for (const gf::CommonPickup& p : data().pickups) {
        if (p.item == item) {
            return p.message;
        }
    }
    return -1;
}

// The prompt message of a common pickup kind (pickups.json), -1 for none.
std::int32_t Event_CommonPickupMessage(std::uint32_t pickupType) {
    const CompositeData& d = data();
    return pickupType < d.pickups.size() ? d.pickups[pickupType].message : -1;
}

void Event_PaperMapTake(std::int32_t paperMapFlagIdx, std::int32_t eventFlagIdx, std::int32_t mapMsgIdx) {
    CompositeHost h;
    const CompositeData& d = data();
    gf::composites::paper_map_take(h, d.reveals.data(), d.reveals.size(), paperMapFlagIdx, eventFlagIdx, mapMsgIdx);
}

}  // extern "C"
