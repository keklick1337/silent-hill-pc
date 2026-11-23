// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/item_screen_flow.h"

namespace sh::inventory {

namespace {

constexpr std::int32_t step_of(FlowStep step) { return static_cast<std::int32_t>(step); }
constexpr std::int32_t step_of(ScreenStep step) { return static_cast<std::int32_t>(step); }

// The game counts play time in Q12 seconds and adds an overflow every 290 hours. On PC the
// 290-hour constant does not fit its 32-bit type and becomes 2^31, so the wrap happens there;
// kept as the PC game behaves.
constexpr std::uint32_t kPlayTimeWrap = 0x80000000u;
constexpr std::uint32_t kPlayTimeSaturated = kPlayTimeWrap - 1;
constexpr std::uint8_t kPlayTimeOverflowsMax = 3;
constexpr std::uint32_t kPlayTimeCap = 130u * 60u * 60u * 4096u;  // 130 hours after the last overflow

void open(ScreenHost& host)
{
    if (host.results_pending()) {
        host.set_step(step_of(FlowStep::ResultsOpen));
        return;
    }
    if (host.restart_requested()) {
        host.restart_game();
        return;
    }
    host.grant_unlocked_items();
    host.open_screen();
    switch (host.entered_from()) {
        case EnteredFrom::StatusScreen:
            host.start_menu(true, Selection::Items);
            break;
        case EnteredFrom::Options:
            host.start_menu(true, Selection::Option);
            break;
        case EnteredFrom::Map:
            host.start_menu(true, Selection::Map);
            break;
        case EnteredFrom::Other:
            host.start_menu(false, Selection::Items);
            break;
    }
    host.prepare_items();
    host.set_step(step_of(ScreenStep::Browse));
}

bool is_browsing(std::int32_t step)
{
    switch (step) {
        case step_of(ScreenStep::Browse):
        case step_of(ScreenStep::Equip):
        case step_of(ScreenStep::Unequip):
        case step_of(ScreenStep::ReloadEquipped):
        case step_of(ScreenStep::ReloadOther):
        case step_of(ScreenStep::UseHealth):
        case step_of(ScreenStep::UseOnEvent):
        case step_of(ScreenStep::CantUseHere):
        case step_of(ScreenStep::Examine):
        case step_of(ScreenStep::TooDark):
            return true;
        default:
            return false;
    }
}

void browse(ScreenHost& host)
{
    if (host.menu_ready()) {
        const std::int32_t sub_step = host.sub_step();
        host.run_menu();
        host.set_sub_step(sub_step);
    }
    host.draw_commands();

    const std::int32_t step = host.step();
    if (step == step_of(ScreenStep::CantUseHere) || step == step_of(ScreenStep::TooDark)) {
        host.set_step(step_of(ScreenStep::Browse));
        host.show_notice(step == step_of(ScreenStep::CantUseHere) ? Notice::CantUseHere : Notice::TooDark);
    }
}

// Returns false when the frame ends here (the game state changed).
bool update_step(ScreenHost& host)
{
    const std::int32_t step = host.step();
    if (step == step_of(FlowStep::Open)) {
        open(host);
        return false;
    }
    if (is_browsing(step)) {
        browse(host);
        return true;
    }
    switch (static_cast<FlowStep>(step)) {
        case FlowStep::LeaveWithWeapon: {
            const std::int32_t sub_step = host.sub_step();
            host.fade_out();
            host.set_step(step_of(FlowStep::Closing));
            host.update_weapon();
            host.set_sub_step(sub_step);
            return true;
        }

        case FlowStep::ToOptions:
            if (host.fade_finished()) {
                host.go_to_options();
                return false;
            }
            return true;

        case FlowStep::ToMap:
            if (host.fade_finished()) {
                host.go_to_map();
                return false;
            }
            return true;

        case FlowStep::Closing:
            if (host.fade_finished()) {
                host.return_to_game();
                return false;
            }
            return true;

        case FlowStep::ResultsOpen:
            host.open_results();
            host.set_step(step_of(FlowStep::ResultsWait));
            return true;

        case FlowStep::ResultsWait:
            if (host.results_continue_pressed())
                host.set_step(step_of(FlowStep::ResultsChoice));
            return true;

        case FlowStep::ResultsChoice:
            if (host.results_side_pressed()) {
                host.set_results_choice(host.results_choice() == 0 ? 1 : 0);
                host.sound(MenuSound::Move, 0);
            }
            if (host.results_enter_pressed()) {
                host.fade_out();
                const std::int32_t sub_step = host.sub_step();
                host.sound(MenuSound::Confirm, 0);
                if (host.results_choice() == 0) {
                    host.load_save_screen();
                    host.set_step(step_of(FlowStep::ResultsToSave));
                } else {
                    host.set_step(step_of(FlowStep::ResultsRestart));
                }
                host.set_sub_step(sub_step);
            }
            return true;

        case FlowStep::ResultsToSave:
            if (host.fade_finished()) {
                host.go_to_save_screen();
                return false;
            }
            return true;

        case FlowStep::ResultsRestart:
            if (host.fade_finished() && host.restart_data_loaded()) {
                host.restart_game();
                return false;
            }
            return true;

        default:
            return true;
    }
}

}  // namespace

void update_item_screen(ScreenHost& host)
{
    host.begin_frame();
    if (host.step() < step_of(FlowStep::ResultsOpen))
        host.advance_play_time();

    if (!update_step(host))
        return;

    host.draw_screen();
    if (host.step() < step_of(FlowStep::ResultsOpen))
        host.draw_scroll_arrows();
    if (host.step() >= step_of(FlowStep::ResultsChoice))
        host.draw_results();
}

void advance_play_time(std::uint32_t& timer, std::uint8_t& overflows, std::int32_t delta)
{
    timer += static_cast<std::uint32_t>(delta);
    if (timer >= kPlayTimeWrap) {
        if (overflows < kPlayTimeOverflowsMax) {
            overflows++;
            timer -= kPlayTimeWrap;
        } else {
            timer = kPlayTimeSaturated;
        }
    }
    if (overflows == kPlayTimeOverflowsMax) {
        if (timer < 1)
            timer = 1;
        else if (timer > kPlayTimeCap)
            timer = kPlayTimeCap;
    }
}

}  // namespace sh::inventory
