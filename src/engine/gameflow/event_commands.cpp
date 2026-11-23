#include "engine/gameflow/event_commands.h"

namespace sh::gameflow::commands {

void fade(EventCommandHost& h, FadeCommand cmd, std::int32_t fade_out, FadeKind kind, std::int32_t rate, bool sub) {
    const FadeCommand active = cmd != FadeCommand::Auto ? cmd : static_cast<FadeCommand>(h.step(2));
    const bool screen = kind == FadeKind::Black || kind == FadeKind::White;
    switch (active) {
    case FadeCommand::Start:
        if (kind != FadeKind::Borders) {
            h.set_fade_rate(rate);
        }
        if (fade_out != 0) {
            if (screen) {
                h.start_fade(false, kind == FadeKind::White);
            } else {
                h.show_borders();
                if (kind == FadeKind::CutsceneBorders) {
                    h.mark_cutscene();
                }
            }
        } else if (screen) {
            h.start_fade(true, kind == FadeKind::White);
        } else {
            h.hide_borders();
        }
        if (cmd != FadeCommand::Start) {
            steps::advance(h, 2);
        }
        break;
    case FadeCommand::Wait:
        if (static_cast<std::int32_t>(kind) < static_cast<std::int32_t>(FadeKind::Borders)) {
            if ((fade_out == 0 && h.fade_idle()) || (fade_out == 1 && h.fade_done())) {
                steps::advance(h, sub ? 1 : 0);
            }
        } else if ((fade_out == 0 && h.borders_hidden()) || (fade_out == 1 && h.borders_shown())) {
            steps::advance(h, sub ? 1 : 0);
        }
        break;
    default:
        break;
    }
}

void background(EventCommandHost& h, BackgroundCommand cmd, int file, bool sub) {
    BackgroundCommand active = cmd;
    if (cmd == BackgroundCommand::Auto) {
        active = static_cast<BackgroundCommand>(h.step(2));
    } else if (cmd == BackgroundCommand::LoadEither) {
        active = h.step(2) == 0 ? BackgroundCommand::LoadSecondary : BackgroundCommand::AwaitLoad;
    }
    switch (active) {
    case BackgroundCommand::Load:
        h.load_background(file, false);
        if (cmd != BackgroundCommand::Load) {
            steps::advance(h, 2);
            if (h.files_loaded()) {
                steps::advance(h, sub ? 1 : 0);
            }
        }
        break;
    case BackgroundCommand::AwaitLoad:
        if (h.files_loaded()) {
            steps::advance(h, sub ? 1 : 0);
        }
        break;
    case BackgroundCommand::Draw:
        h.draw_background(false);
        break;
    case BackgroundCommand::StoreVram:
        h.store_vram();
        break;
    case BackgroundCommand::LoadSecondary:
        h.load_background(file, true);
        if (cmd == BackgroundCommand::LoadEither) {
            steps::set(h, 2, static_cast<std::int32_t>(BackgroundCommand::AwaitLoad));
        }
        break;
    case BackgroundCommand::DrawSecondary:
        h.draw_background(true);
        break;
    case BackgroundCommand::RestoreVram:
        h.restore_vram();
        break;
    default:
        break;
    }
}

void item(EventCommandHost& h, ItemCommand cmd, int item, int count, bool sub) {
    // The auto steps of the item command: load, wait, add.
    constexpr std::int32_t kLoad = 0, kWait = 1, kAdd = 2;

    if (cmd == ItemCommand::AutoAdd && h.step(2) == 0) {
        steps::set(h, 2, kAdd);
    }
    std::int32_t active = static_cast<std::int32_t>(cmd);
    if (cmd == ItemCommand::Nothing) {
        active = -1;
    } else if (cmd == ItemCommand::Add) {
        active = kAdd;
    } else if (static_cast<std::uint32_t>(cmd) >= static_cast<std::uint32_t>(ItemCommand::Nothing)) {
        active = h.step(2);
    }

    switch (active) {
    case kLoad:
        h.load_item_model(item);
        if (cmd == ItemCommand::Load) {
            // Clears the timer and the auto step of the sub-step without moving it.
            h.store_timer(0);
            h.store_step(2, 0);
        }
        steps::advance(h, 2);
        [[fallthrough]];
    case kWait:
        if (!h.files_loaded()) {
            break;
        }
        h.show_item_pickup(item);
        if (cmd == ItemCommand::AwaitLoad || cmd == ItemCommand::AutoNoAdd) {
            steps::advance(h, sub ? 1 : 0);
            break;
        }
        steps::advance(h, 2);
        [[fallthrough]];
    case kAdd:
        steps::set(h, 2, 0);
        if (cmd == ItemCommand::Add || cmd == ItemCommand::AutoAdd) {
            h.add_item(item, count);
        }
        break;
    default:
        break;
    }
}

}  // namespace sh::gameflow::commands

namespace sh::gameflow {

bool path_waypoint(std::int32_t mode, const Waypoint& player, std::int32_t x, std::int32_t z, Waypoint& out) {
    if (mode == 1) {
        out = Waypoint{x, player.y, z};
        return true;
    }
    if (mode == 0) {
        // 32-bit wrap as in the game.
        out = Waypoint{static_cast<std::int32_t>(static_cast<std::uint32_t>(player.x) + static_cast<std::uint32_t>(x)),
                       player.y,
                       static_cast<std::int32_t>(static_cast<std::uint32_t>(player.z) + static_cast<std::uint32_t>(z))};
        return true;
    }
    return false;
}

}  // namespace sh::gameflow
