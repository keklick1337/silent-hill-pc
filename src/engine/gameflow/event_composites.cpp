#include "engine/gameflow/event_composites.h"

#include <algorithm>

namespace sh::gameflow::composites {

namespace {

constexpr std::int32_t kNoChoice = -1;           // the step a message leaves for "no"
constexpr std::int32_t kBackgroundGamma = 48;    // Q8 6/32: the picture behind the second message
constexpr std::int32_t kPaperMapGamma = 88;      // Q8 11/32
constexpr int kVoiceFirst = 0x1000;              // voice commands of the sound driver
constexpr int kVoiceEnd = 0x1700;

void fade(CompositeHost& h, FadeCommand cmd, bool out, std::int32_t rate, bool sub) {
    commands::fade(h, cmd, out ? 1 : 0, FadeKind::Black, rate, sub);
}

void picture(CompositeHost& h, BackgroundCommand cmd, int file, bool sub) {
    commands::background(h, cmd, file, sub);
}

void say(CompositeHost& h, int message) {
    steps::message(h, false, message, 0, 0, 0, true);
}

void close_picture(CompositeHost& h, std::int32_t fade0) {
    fade(h, FadeCommand::Start, false, fade0, false);
    h.unfreeze_player();
    h.resume_gameplay();
}

// Time stands still while a hostile character is alive, unless the world is frozen anyway.
void hold_time_for_npcs(CompositeHost& h) {
    if (!h.world_frozen() && h.hostile_npc_alive()) {
        h.stop_frame_time();
    }
}

}  // namespace

void bg_load_fade_in(CompositeHost& h, int file, std::int32_t fade_rate) {
    switch (h.step(1)) {
    case 0:
        fade(h, FadeCommand::Start, true, fade_rate, false);
        steps::advance(h, 1);
        [[fallthrough]];
    case 1:
        picture(h, BackgroundCommand::Auto, file, true);
        break;
    default:
        fade(h, FadeCommand::Wait, true, 0, false);
        break;
    }
}

void bg_fade_in(CompositeHost& h, int file, std::int32_t fade0, std::int32_t fade1) {
    switch (h.step(1)) {
    case 0:
        fade(h, FadeCommand::Start, true, fade0, false);
        steps::advance(h, 1);
        [[fallthrough]];
    case 1:
        picture(h, BackgroundCommand::Auto, file, true);
        break;
    case 2:
        fade(h, FadeCommand::Wait, true, 0, true);
        break;
    default:
        picture(h, BackgroundCommand::Draw, 0, false);
        fade(h, FadeCommand::Auto, false, fade1, false);
        break;
    }
}

void bg_fade_out(CompositeHost& h, std::int32_t fade_from_black, std::int32_t fade_to_black) {
    if (h.step(1) == 0) {
        picture(h, BackgroundCommand::Draw, 0, false);
        fade(h, FadeCommand::Auto, true, fade_to_black, true);
        return;
    }
    fade(h, FadeCommand::Start, false, fade_from_black, false);
    steps::advance(h, 0);
}

void map_msg_with_sfx(CompositeHost& h, int message, int sound, const Waypoint* at) {
    hold_time_for_npcs(h);
    switch (h.step(1)) {
    case 0:
        h.freeze_player();
        h.play_sound(sound, at);
        steps::advance(h, 1);
        [[fallthrough]];
    case 1:
        steps::wait_timer(h, 819, true);  // Q12 0.2 s
        break;
    case 2:
        steps::message(h, false, message, 0, 0, 0, true);
        break;
    default:
        h.unfreeze_player();
        h.resume_gameplay();
        break;
    }
}

void map_msg_with_bg(CompositeHost& h, int file, std::int32_t fade0, std::int32_t fade1, int message) {
    switch (h.step(1)) {
    case 0:
        h.freeze_player();
        fade(h, FadeCommand::Start, true, fade0, false);
        steps::advance(h, 1);
        [[fallthrough]];
    case 1:
        picture(h, BackgroundCommand::Auto, file, true);
        break;
    case 2:
        fade(h, FadeCommand::Wait, true, 0, true);
        break;
    case 3:
        picture(h, BackgroundCommand::Draw, 0, false);
        fade(h, FadeCommand::Auto, false, fade1, true);
        break;
    case 4:
        picture(h, BackgroundCommand::Draw, 0, false);
        say(h, message);
        break;
    case 5:
        picture(h, BackgroundCommand::Draw, 0, false);
        fade(h, FadeCommand::Auto, true, fade1, true);
        break;
    default:
        close_picture(h, fade0);
        break;
    }
}

void map_msg_with_dimmed_bg(CompositeHost& h, int file, std::int32_t fade0, std::int32_t fade1, int message0,
                            int message1) {
    switch (h.step(1)) {
    case 0:
        h.freeze_player();
        fade(h, FadeCommand::Start, true, fade0, false);
        steps::advance(h, 1);
        [[fallthrough]];
    case 1:
        picture(h, BackgroundCommand::Auto, file, true);
        break;
    case 2:
        fade(h, FadeCommand::Wait, true, 0, true);
        break;
    case 3:
        picture(h, BackgroundCommand::Draw, 0, false);
        fade(h, FadeCommand::Auto, false, fade1, true);
        break;
    case 4:
        picture(h, BackgroundCommand::Draw, 0, false);
        if (message0 != 0) {
            say(h, message0);
            break;
        }
        if (h.confirm_pressed()) {
            steps::advance(h, 1);
        }
        break;
    case 5:
        h.set_background_gamma(kBackgroundGamma);
        picture(h, BackgroundCommand::Draw, 0, false);
        say(h, message1);
        break;
    case 6:
        h.set_background_gamma(kBackgroundGamma);
        picture(h, BackgroundCommand::Draw, 0, false);
        fade(h, FadeCommand::Auto, true, fade1, true);
        break;
    default:
        close_picture(h, fade0);
        break;
    }
}

void chara_anim(CompositeHost& h, CharaAnimCommand cmd, CharaHandle chara, std::int32_t state, bool sub) {
    switch (cmd) {
    case CharaAnimCommand::SetState:
        h.anim_set_state(chara, state);
        break;
    case CharaAnimCommand::AwaitEnd:
        if (h.anim_ended(chara)) {
            steps::advance(h, sub ? 1 : 0);
        }
        break;
    case CharaAnimCommand::Lock:
        h.anim_lock(chara);
        break;
    case CharaAnimCommand::Unlock:
        h.anim_unlock(chara);
        break;
    case CharaAnimCommand::Reset:
        // The player is unlocked before the reset; a character is just reset.
        if (h.is_player(chara)) {
            h.anim_unlock(chara);
        }
        h.anim_reset(chara);
        break;
    }
}

void chara_anim_play_to_end(CompositeHost& h, CharaHandle chara, std::int32_t state) {
    switch (h.step(1)) {
    case 0:
        chara_anim(h, CharaAnimCommand::SetState, chara, state, false);
        steps::advance(h, 1);
        break;
    case 1:
        chara_anim(h, CharaAnimCommand::AwaitEnd, chara, 0, true);
        break;
    default:
        steps::advance(h, 0);
        break;
    }
}

void chara_anim_unlock_play_to_end(CompositeHost& h, CharaHandle chara) {
    switch (h.step(1)) {
    case 0:
        chara_anim(h, CharaAnimCommand::Unlock, chara, 0, false);
        steps::advance(h, 1);
        break;
    case 1:
        chara_anim(h, CharaAnimCommand::AwaitEnd, chara, 0, true);
        break;
    default:
        steps::advance(h, 0);
        break;
    }
}

void map_msg_with_audio(CompositeHost& h, int message, std::uint8_t& index, const std::uint16_t* commands,
                        bool skip_redisplay) {
    const MessageResult state = h.show_voiced_message(message);
    if (state == MessageResult::Choice0) {
        steps::advance(h, 0);
        return;
    }
    if (state != MessageResult::Finished) {
        return;
    }
    // A page that is only a re-display of the one that just ended plays nothing and leaves the index alone, so the
    // voices stay in step with the pages.
    if (skip_redisplay && h.voice_redisplay_consume()) {
        return;
    }
    const int command = commands[index];
    if (command >= kVoiceFirst && command < kVoiceEnd) {
        h.voice_dropped(false, command, index, message);
        h.play_voice(command);
    } else {
        // A message that consumes more pages than authored runs the index past its table; silence beats noise.
        h.voice_dropped(true, command, index, message);
    }
    ++index;
}

void item_take(CompositeHost& h, int item, int count, std::int16_t flag, int message) {
    hold_time_for_npcs(h);

    switch (h.step(1)) {
    case 0:
        if (!h.pickup_starting(item, count, flag)) {
            h.resume_gameplay();
            break;
        }
        h.pickup_begin();
        h.freeze_player();
        commands::item(h, ItemCommand::Load, item, 0, false);
        steps::advance(h, 1);
        [[fallthrough]];
    case 1:
        h.pickup_hold_world();
        commands::item(h, ItemCommand::AwaitLoad, item, 0, true);
        break;
    case 2:
        if (h.pickup_animate(item)) {
            steps::message(h, true, message, 3, kNoChoice, 0, true);
        }
        // The flag is set while the prompt is up and cleared again on "no".
        h.set_flag(flag, true);
        break;
    case 3:
        h.pickup_count(item, flag, count);
        commands::item(h, ItemCommand::Add, item, count, false);
        h.pickup_added(item, count, flag);
        steps::advance(h, 1);
        [[fallthrough]];
    default:
        if (h.step(1) == kNoChoice) {
            h.set_flag(flag, false);
        }
        h.unfreeze_player();
        h.pickup_end();
        h.resume_gameplay();
        break;
    }
}

void common_item_take(CompositeHost& h, const CommonPickup* pickups, std::size_t count, std::uint32_t kind,
                      std::int16_t flag) {
    if (kind >= count || pickups[kind].item < 0) {
        return;
    }
    const CommonPickup& p = pickups[kind];
    int amount = 1;
    if (p.ammo) {
        amount = h.ammo_multiplier() * h.ammo_pickup_count(p.item);
    }
    item_take(h, p.item, amount, flag, p.message);
}

void paper_map_take(CompositeHost& h, const PaperMapReveal* reveals, std::size_t count, int page, std::int16_t flag,
                    int message) {
    h.stop_frame_time();
    switch (h.step(1)) {
    case 0:
        h.freeze_player();
        h.map_seek(page);
        steps::advance(h, 1);
        [[fallthrough]];
    case 1:
        fade(h, FadeCommand::Auto, true, 0, true);
        break;
    case 2:
        h.map_store_vram(VramArea::Pickup);
        h.map_read_page(page, false);
        h.map_enter_screen(true);
        h.map_swap_buffers();
        fade(h, FadeCommand::Start, false, 0, false);
        h.map_wait_files();
        steps::advance(h, 1);
        [[fallthrough]];
    case 3:
        h.set_background_gamma(kPaperMapGamma);
        h.map_draw();
        steps::message(h, true, message, 4, 5, 0, true);
        break;
    case 4:
        h.map_mark_found(page);
        for (std::size_t i = 0; i < count; ++i) {
            if (reveals[i].page == page) {
                h.map_mark_found_mask(reveals[i].pages);
            }
        }
        h.set_flag(flag, true);
        steps::advance(h, 1);
        [[fallthrough]];
    case 5:
        h.set_background_gamma(kPaperMapGamma);
        h.map_draw();
        fade(h, FadeCommand::Auto, true, 0, true);
        break;
    default:
        h.map_restore_vram(VramArea::Pickup);
        h.map_leave_screen();
        fade(h, FadeCommand::Start, false, 0, false);
        h.unfreeze_player();
        h.resume_gameplay();
        break;
    }
}

void paper_map_command(CompositeHost& h, PaperMapCommand cmd, int page) {
    switch (cmd) {
    case PaperMapCommand::Load:
        h.map_store_vram(VramArea::MapSheet);
        h.map_read_page(page, true);
        h.map_enter_screen(false);
        h.map_swap_buffers();
        h.map_wait_files();
        break;
    case PaperMapCommand::Draw:
        h.map_draw();
        break;
    case PaperMapCommand::Unload:
        h.map_restore_vram(VramArea::MapSheet);
        h.map_leave_screen();
        break;
    }
}

namespace {

CameraParams with_defaults(const CameraParams& request, const CameraParams& defaults) {
    CameraParams p;
    p.accel_a = request.accel_a == 0 ? defaults.accel_a : request.accel_a;
    p.accel_b = request.accel_b == 0 ? defaults.accel_b : request.accel_b;
    p.max_a = request.max_a == 0 ? defaults.max_a : request.max_a;
    p.max_b = request.max_b == 0 ? defaults.max_b : request.max_b;
    return p;
}

Waypoint target_of(const Waypoint* base, const Waypoint& offset) {
    if (base == nullptr) {
        return offset;
    }
    return Waypoint{base->x + offset.x, base->y + offset.y, base->z + offset.z};
}

}  // namespace

void camera_position(CompositeHost& h, const Waypoint* base, const Waypoint& offset, const CameraParams& params,
                     bool warp) {
    h.camera_move(target_of(base, offset), with_defaults(params, h.camera_position_defaults()), warp);
}

void camera_look_at(CompositeHost& h, const Waypoint* base, const Waypoint& offset, const CameraParams& params,
                    bool warp) {
    h.camera_watch(target_of(base, offset), with_defaults(params, h.camera_watch_defaults()), warp);
}

}  // namespace sh::gameflow::composites
