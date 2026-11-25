// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory screen's menu over the session: sh::inventory::update_menu
// decides; this host maps the menu state onto the menu record (inventory_menu()), the selected slot and the screen
// step the drawing, the mouse (src/input/inventory_pointer.h) and the item screen states read, and performs the
// effects the engine asks for. Before every effect the state is written back and read again after it, because the
// effects read and change it (scrolling, item use, the mouse). Inventory_Logic keeps the name the flow calls.

#include <cstdint>

#include "audio/game/game_sound_driver.h"
#include "engine/effects/map_lighting.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/game/inventory_host.h"
#include "engine/inventory/game/wide_screen.h"
#include "engine/inventory/inventory_menu.h"
#include "engine/inventory/item_screen_text.h"
#include "engine/inventory/item_view.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/runtime/game_hooks.h"
#include "engine/screens/paper_map_view.h"
#include "input/inventory_pointer.h"
#include "engine/app/loader/game_files.h"
#include "engine/loader/queue/disc_files.h"

extern "C" {
void Inventory_DirectionalInputSet(void);
void Inventory_ScrollUpdate(std::int32_t direction, std::int32_t slot);
void Inventory_ItemUse(std::int32_t slot);
}

namespace {

namespace inv = sh::inventory;
namespace link = sh::gameflow::game;
using sh::game::save_record;
using sh::game::session;

constexpr std::int32_t kStepLeaveToGame = 20;
constexpr std::int32_t kStepLeaveToOptions = 18;
constexpr std::int32_t kStepLeaveToMap = 19;
constexpr std::uint8_t kEnvironmentDark = 1u << 1;  // the environment preset is dark without a light
constexpr std::uint8_t kEnvironmentLit = 1u << 0;

// The menu sounds in MenuSound order: move, confirm, cancel, error.
constexpr std::uint16_t kMenuSounds[] = {1305, 1307, 1306, 1304};

// The menu state as the session holds it.
inv::MenuState read_state()
{
    const inv::InventoryMenuState& menu = inv::inventory_menu();
    inv::MenuState state;
    state.selection = static_cast<inv::Selection>(menu.selection);
    state.previous = static_cast<inv::Selection>(menu.outline_target);
    state.selection_age = menu.outline_age;
    state.command = menu.command;
    state.scrolling = menu.scrolling != 0;
    state.scrolled = menu.scrolled != 0;
    state.selected_slot = session().system.inventory_selected;
    state.step = session().game.game_steps[1];
    return state;
}

const sh::items::ItemDef* slot_item(int index) { return inv::game::item_def(save_record().items[index].item); }

class MenuHost final : public inv::MenuHost {
public:
    explicit MenuHost(inv::MenuState& state) : state_(state), seen_(state) {}

    // Writes what the engine changed since the state was last read; untouched values keep their exact bits (the
    // input flags hold raw button bits, for example).
    void flush()
    {
        inv::InventoryMenuState& menu = inv::inventory_menu();
        if (state_.selection != seen_.selection)
            menu.selection = static_cast<std::uint32_t>(state_.selection);
        if (state_.previous != seen_.previous)
            menu.outline_target = static_cast<std::int32_t>(state_.previous);
        if (state_.selection_age != seen_.selection_age)
            menu.outline_age = state_.selection_age;
        if (state_.command != seen_.command)
            menu.command = state_.command;
        if (state_.scrolling != seen_.scrolling)
            menu.scrolling = state_.scrolling;
        if (state_.scrolled != seen_.scrolled)
            menu.scrolled = state_.scrolled;
        if (state_.selected_slot != seen_.selected_slot)
            session().system.inventory_selected = static_cast<std::uint8_t>(state_.selected_slot);
        if (state_.step != seen_.step)
        {
            // Every step change of the menu restarts the step's own sequence.
            session().game.game_steps[1] = state_.step;
            session().game.game_steps[2] = 0;
        }
        seen_ = state_;
    }

    void reload() { state_ = seen_ = read_state(); }

    template <typename F>
    void effect(F&& run)
    {
        flush();
        run();
        reload();
    }

    inv::MenuInput read_input() override
    {
        inv::MenuInput in;
        effect([] {
            // Must run before the directions are read from the pad.
            sh::input::update_inventory_pointer();
            Inventory_DirectionalInputSet();
        });
        const std::uint32_t clicked = session().game.pads[0].clicked;
        const sh::game::PadBinds& binds = session().game.settings.binds;
        const inv::Directions& d = inv::item_screen().directions;
        in.up_clicked = d.up_clicked;
        in.up_pulsed = d.up_pulsed;
        in.down_clicked = d.down_clicked;
        in.down_pulsed = d.down_pulsed;
        in.left_clicked = d.left_clicked;
        in.left_held = d.left_held;
        in.right_clicked = d.right_clicked;
        in.right_held = d.right_held;
        in.right_pulsed = d.right_pulsed;
        in.enter = (clicked & binds.enter) != 0;
        in.cancel = (clicked & binds.cancel) != 0;
        in.item_button = (clicked & binds.item) != 0;
        return in;
    }

    void sound(inv::MenuSound sound, int pan) override
    {
        effect([&] {
            sh::audio::game_sound_driver().play_effect(kMenuSounds[static_cast<int>(sound)], static_cast<std::int8_t>(pan),
                                                       64);
        });
    }

    int slot_count() override { return save_record().slot_count; }

    inv::Slot slot(int index) override { return sh::game_state::inventory_slot(index); }

    bool weapon_in_hand() override { return session().system.combat.weapon_attack >= 0; }

    int weapon_slot() override { return session().system.combat.weapon_slot; }

    bool selection_refused(int index) override
    {
        const sh::items::ItemDef* item = slot_item(index);
        if (item == nullptr)
            return false;
        // The flashlight cannot be picked while it is unavailable.
        return !item->inventory.selectable ||
               (session().system.environment.flashlight_unavailable && item->inventory.switches == "flashlight");
    }

    bool can_see() override
    {
        const sh::game::EnvironmentMix& env = session().system.environment;
        return !(env.current.preset.flags & kEnvironmentDark) || env.flashlight_on ||
               (!(env.transition[0].preset.flags & kEnvironmentLit) && !(env.transition[1].preset.flags & kEnvironmentLit));
    }

    bool has_map() override { return sh::game_state::paper_map_held(save_record().paper_map); }

    void scroll(int direction, int index) override
    {
        effect([&] { Inventory_ScrollUpdate(direction, index); });
    }

    void leave(inv::MenuExit exit) override
    {
        effect([&] {
            switch (exit)
            {
                case inv::MenuExit::Game:
                {
                    const std::int32_t step = session().game.game_steps[2];
                    sh::game_state::screen_fade_start(true, false, false);
                    session().game.game_steps[1] = kStepLeaveToGame;
                    session().game.game_steps[2] = 0;
                    sh::game::player().update_weapon();
                    session().game.game_steps[2] = step;
                    break;
                }

                case inv::MenuExit::Options:
                    sh::game_state::screen_fade_start(true, false, false);
                    session().game.game_steps[1] = kStepLeaveToOptions;
                    session().game.game_steps[2] = 0;
                    link::load_options_screen();
                    break;

                case inv::MenuExit::Map:
                {
                    const int page = save_record().paper_map;
                    if (sh::screens::paper_map_marking_file(page) >= 0)
                        link::read_image(sh::screens::paper_map_marking_file(page), link::Picture::PaperMapMarkings);
                    link::seek_file(sh::screens::paper_map_image_file(page));
                    sh::game_state::screen_fade_start(true, false, false);
                    session().game.game_steps[1] = kStepLeaveToMap;
                    session().game.game_steps[2] = 0;
                    break;
                }
            }
        });
    }

    void show_map_unavailable(bool no_map) override
    {
        effect([&] { inv::draw_map_notice(no_map, inv::wide_screen().x(inv::Column::Left, 0)); });
    }

    void toggle(int index, bool on) override
    {
        effect([&] {
            const sh::items::ItemDef* item = slot_item(index);
            if (item == nullptr)
                return;
            if (item->inventory.switches == "flashlight")
            {
                sh::effects::map_lighting().set_flashlight(on);
            }
            else if (item->inventory.switches == "radio")
            {
                if (on)
                    save_record().item_toggles |= sh::game::save_bits::kRadioOn;
                else
                    save_record().item_toggles &= ~sh::game::save_bits::kRadioOn;
            }
        });
    }

    void examine(int index) override
    {
        effect([&] {
            const sh::items::ItemDef* item = slot_item(index);
            if (item == nullptr || item->inventory.examine_picture.empty())
                return;
            const sh::items::InventoryDef::ExamineFlag& mark = item->inventory.examine_flag;
            if (!mark.flag.empty())
            {
                const int when = mark.when.empty() ? -1 : sh::ids::flag_number(mark.when.c_str());
                const int flag = sh::ids::flag_number(mark.flag.c_str());
                if (flag < 0 || (!mark.when.empty() && when < 0))
                    SH_WARN("[INVENTORY] %s: unknown flag in examine_flag", item->id.c_str());
                else if (mark.when.empty() || save_record().event_flag(when))
                    save_record().set_event_flag(flag, true);
            }
            const int file = sh::loader::disc_files().find(item->inventory.examine_picture.c_str());
            if (file < 0)
            {
                SH_WARN("[INVENTORY] %s: unknown examine_picture \"%s\"", item->id.c_str(),
                        item->inventory.examine_picture.c_str());
                return;
            }
            link::read_image(file, link::Picture::ItemInspection);
        });
    }

    void use_item(int index) override
    {
        effect([&] { Inventory_ItemUse(index); });
    }

    bool item_use_allowed(int index) override { return sh::hooks::game_item_use(save_record().items[index].item); }

private:
    inv::MenuState& state_;
    inv::MenuState seen_;
};

}  // namespace

extern "C" void Inventory_Logic(void)
{
    inv::MenuState state = read_state();
    MenuHost host(state);
    inv::update_menu(state, host);
    host.flush();
}
