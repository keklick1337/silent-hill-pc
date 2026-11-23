// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/item_carousel.h"

#include <algorithm>
#include <cstdlib>

#include "engine/world/fixed_trig.h"
#include "engine/inventory/inventory_menu.h"
#include "engine/inventory/item_screen_flow.h"

namespace sh::inventory {

namespace {

using sh::world::fixed_sin;

constexpr std::int32_t step_of(ScreenStep step) { return static_cast<std::int32_t>(step); }

constexpr std::int32_t kOne = 4096;          // Q12 1.0
constexpr std::int32_t kActionFrames = 9;    // an action's animation ends on this frame
constexpr std::int32_t kScrollFrames = 8;
constexpr std::int32_t kGrowPerFrame = 512;  // scale step of a growing or shrinking model
constexpr std::int32_t kSlotTurn = 1024;     // a quarter turn per slot of distance, before / 3
constexpr std::int32_t kRowY = 64;           // Q8 0.25
constexpr std::int32_t kRowZ = -1024;        // Q8 -4.0
constexpr std::int32_t kHandY = -1408;       // Q8 -5.5
constexpr std::int32_t kOffscreenX = 10240;  // Q8 40.0: models more than three slots away
constexpr std::int32_t kSelectedSpin = -16;
constexpr std::int32_t kHandSpin = 32;
constexpr std::int32_t kGlareShown = 32;
constexpr std::int32_t kGlareMax = 64;

// Signed distance (in slots) from `centre` to `slot` the short way round the list. The settled
// layout does not special-case the centre slot; the scrolling one does (it matters only when
// half the slot count is 0).
std::int32_t ring_offset(std::int32_t slot, std::int32_t centre, std::int32_t count, bool centre_is_zero)
{
    if (std::abs(slot - centre) < (count >> 1))
        return slot - centre;
    if (centre_is_zero && slot == centre)
        return 0;
    return centre < slot ? (slot - centre) - count : count - (centre - slot);
}

// A model `angle` along the arc in front of the camera: sideways by a third of the angle,
// back by the sine of `depth_angle`.
std::int32_t arc_x(std::int32_t angle) { return fixed_sin(angle / 3) / 3; }
std::int32_t arc_z(std::int32_t depth_angle) { return std::abs(fixed_sin(depth_angle)) + kRowZ; }

void browse(CarouselState& state, CarouselHost& host)
{
    for (int i = 0; i < kCarouselModels; i++)
        host.scale_model(i, kOne);

    const std::int32_t selected = host.selected_slot();
    const std::int32_t count = host.slot_count();
    if (selected == state.shown_slot) {
        for (int i = 0; i < kCarouselModels; i++) {
            const std::int32_t slot = host.display_slot(i);
            if (slot == -1)
                continue;
            if (slot == selected) {
                host.scale_model(i, kOne);
                host.place_model(i, 0, kRowY, kRowZ);
                continue;
            }
            const std::int32_t angle = ring_offset(slot, selected, count, false) * kSlotTurn;
            host.place_model(i, arc_x(angle), kRowY, arc_z(angle >> 2));
            host.turn_model(i, -angle / 3);
        }
        return;
    }

    // Scrolling: the timer runs negative towards the right.
    state.timer++;
    if (selected == state.shown_slot + 1 || state.shown_slot - selected == count - 1)
        state.timer = -state.timer;
    if (std::abs(state.timer) == kScrollFrames) {
        state.timer = 0;
        state.shown_slot = selected;
    }

    for (int i = 0; i < kCarouselModels; i++) {
        const std::int32_t slot = host.display_slot(i);
        if (slot == -1)
            continue;
        const std::int32_t offset = ring_offset(slot, state.shown_slot, count, true);
        const std::int32_t angle = offset * kSlotTurn;
        std::int32_t x = 0;
        std::int32_t z = kRowZ;
        if (state.timer != 0) {
            const std::int32_t swing = state.timer << 7;
            x = arc_x(angle + swing);
            z = arc_z((angle + swing) >> 2);
            host.turn_model(i, -((angle / 3) + (swing / 3)));
        } else if (slot != state.shown_slot) {
            x = arc_x(angle);
            z = arc_z(angle / 4);
            host.turn_model(i, -angle / 3);
        }
        // Models more than three slots away wait off screen.
        if (offset < -3 || offset > 3)
            x = kOffscreenX;
        host.place_model(i, x, kRowY, z);
    }
    state.timer = std::abs(state.timer);
}

void shrink_slot_models(CarouselHost& host, std::int32_t slot, std::int32_t timer)
{
    for (int i = 0; i < kCarouselModels; i++) {
        if (host.display_slot(i) == slot)
            host.scale_model(i, kOne - timer * kGrowPerFrame);
    }
}

void back_to_browse(CarouselState& state, CarouselHost& host)
{
    state.timer = 0;
    host.select_item_row();
    host.set_step(step_of(ScreenStep::Browse), false);
}

// The end of a reload or a use: the slot that ran out shrinks away and is emptied, or the
// animation just runs out.
void finish_or_shrink(CarouselState& state, CarouselHost& host, std::int32_t emptied)
{
    state.timer++;
    if (state.timer == kActionFrames) {
        host.empty_slot(emptied);
        back_to_browse(state, host);
        host.refresh_commands();
    } else {
        shrink_slot_models(host, emptied, state.timer);
    }
}

void wait_out(CarouselState& state, CarouselHost& host)
{
    state.timer++;
    if (state.timer == kActionFrames)
        back_to_browse(state, host);
}

void equip(CarouselState& state, CarouselHost& host)
{
    if (state.timer == 0)
        host.equip_start();
    state.timer++;
    if (state.timer == kActionFrames) {
        back_to_browse(state, host);
        host.equip_finish();
    } else {
        host.scale_model(kEquippedModel, state.timer * kGrowPerFrame);
    }
    host.place_model(kEquippedModel, 0, kHandY, 0);
}

void unequip(CarouselState& state, CarouselHost& host)
{
    state.timer++;
    if (state.timer == kActionFrames) {
        back_to_browse(state, host);
        host.unequip_finish();
    } else {
        host.scale_model(kEquippedModel, kOne - state.timer * kGrowPerFrame);
    }
    host.place_model(kEquippedModel, 0, kHandY, 0);
}

// After a reload: an emptied ammo slot (`first`) or the slot after `next_to` (where the ammo of
// the gun usually sits) shrinks away.
void after_reload(CarouselState& state, CarouselHost& host, bool first_emptied, std::int32_t next_to)
{
    const std::int32_t selected = host.selected_slot();
    host.select_item_row();
    if (first_emptied) {
        finish_or_shrink(state, host, selected);
    } else if (host.slot(next_to + 1).count == 0 && state.ammo_slot != -1) {
        finish_or_shrink(state, host, next_to + 1);
    } else {
        wait_out(state, host);
    }
}

void reload_equipped(CarouselState& state, CarouselHost& host)
{
    if (state.timer == 0)
        state.ammo_slot = host.reload_equipped();
    const std::int32_t selected = host.selected_slot();
    const bool emptied = host.slot(selected).count == 0 && host.is_ammo(selected);
    after_reload(state, host, emptied, host.weapon_slot());
}

void reload_other(CarouselState& state, CarouselHost& host)
{
    // The emptied ammo slot only counts on the frame of the reload itself.
    std::int32_t emptied_ammo = -1;
    if (state.timer == 0) {
        state.ammo_slot = -1;
        const ReloadResult result = host.reload_selected();
        state.ammo_slot = result.ammo_slot;
        emptied_ammo = result.emptied_slot;
    }
    const std::int32_t selected = host.selected_slot();
    const bool emptied = (host.slot(selected).count == 0 && host.is_ammo(selected)) ||
                         (emptied_ammo >= 0 && host.slot(emptied_ammo).count == 0);
    after_reload(state, host, emptied, selected);
}

void use_health(CarouselState& state, CarouselHost& host)
{
    if (state.timer == 0)
        host.use_health();
    const std::int32_t selected = host.selected_slot();
    if (host.slot(selected).count == 0)
        finish_or_shrink(state, host, selected);
    else
        wait_out(state, host);
}

void use_on_event(CarouselState& state, CarouselHost& host)
{
    const std::int32_t selected = host.selected_slot();
    if (state.timer == 0 && host.used_up_on_use(selected))
        host.set_slot_count(selected, 0);
    state.timer++;
    if (host.slot(selected).count != 0)
        return;
    if (state.timer == kActionFrames) {
        host.empty_slot(selected);
        state.timer = 0;
        host.select_item_row();
        host.set_step(static_cast<std::int32_t>(FlowStep::LeaveWithWeapon), false);
    } else {
        shrink_slot_models(host, selected, state.timer);
    }
}

void examine(CarouselState& state, CarouselHost& host, std::int32_t step)
{
    if (step == step_of(ScreenStep::Examine)) {
        state.glare_time = std::min(state.glare_time + 1, kGlareShown);
        state.timer = std::min(state.timer + 1, kGlareShown);
        if (state.timer == kGlareShown && host.files_loaded())
            host.set_step(step_of(ScreenStep::ExamineShown), true);
    } else if (step == step_of(ScreenStep::ExamineShown)) {
        state.glare_time = std::min(state.glare_time + 1, kGlareMax);
        if (host.confirm_clicked() && state.glare_time == kGlareMax) {
            state.timer = 0;
            host.reset_selection_age();
            host.set_step(step_of(ScreenStep::ExamineClosing), true);
            state.glare_time = 0;
        }
    } else {
        state.glare_time += 2;
        state.timer = std::min(state.timer + 1, kActionFrames);
        if (state.glare_time > kGlareShown) {
            state.timer = 0;
            host.set_step(step_of(ScreenStep::Browse), true);
            host.select_item_row();
            state.glare_time = 0;
            host.reset_selection_age();
        }
    }
}

}  // namespace

int scroll_in_slot(std::array<std::int32_t, kCarouselModels>& displays, bool right, std::int32_t entering,
                   std::int32_t slot_count)
{
    // Truncating %, as the game's: with fewer than seven slots the sum can stay negative.
    const std::int32_t leaving = (entering + (right ? -kCarouselModels : kCarouselModels) + slot_count) % slot_count;
    int display = 0;
    for (int i = 0; i < kCarouselModels; i++) {
        if (displays[static_cast<std::size_t>(i)] == leaving) {
            display = i;
            break;
        }
    }
    displays[static_cast<std::size_t>(display)] = entering;
    return display;
}

const std::array<ResultsPose, kResultsModels>& results_poses()
{
    static constexpr std::array<ResultsPose, kResultsModels> kPoses = {{
        {0, -832, 5120},
        {-776, -384, 5120},
        {776, -384, 5120},
        {-776, 512, 5120},
        {776, 512, 5120},
        {0, 960, 5120},
    }};
    return kPoses;
}

void update_carousel(CarouselState& state, CarouselHost& host)
{
    const std::int32_t step = host.step();
    switch (static_cast<ScreenStep>(step)) {
        case ScreenStep::Browse: browse(state, host); break;
        case ScreenStep::Equip: equip(state, host); break;
        case ScreenStep::Unequip: unequip(state, host); break;
        case ScreenStep::ReloadEquipped: reload_equipped(state, host); break;
        case ScreenStep::ReloadOther: reload_other(state, host); break;
        case ScreenStep::UseHealth: use_health(state, host); break;
        case ScreenStep::UseOnEvent: use_on_event(state, host); break;
        case ScreenStep::Examine:
        case ScreenStep::ExamineShown:
        case ScreenStep::ExamineClosing: examine(state, host, step); break;
        default: break;
    }

    const std::int32_t selected = host.selected_slot();
    for (int i = 0; i < kCarouselModels; i++) {
        if (host.display_slot(i) == selected)
            host.spin_model(i, kSelectedSpin);
    }
    host.spin_model(kEquippedModel, kHandSpin);
}

}  // namespace sh::inventory
