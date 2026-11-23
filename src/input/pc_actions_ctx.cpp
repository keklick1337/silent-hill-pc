/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The game-state half of the input action bridge: which
 * screen is up and the game's controller configuration. Read-only, through
 * the game state interface (engine/game_state). See pc_actions.h.
 */
#include "engine/game_state/game_state.h"
#include "input/pc_actions.h"
#include "engine/config/pc_config.h"
#include "ui/pause/pause_menu.h"

namespace gs = sh::game_state;

/* sh::input::Context */
enum { CTX_MENU = 0, CTX_WORLD = 1, CTX_GAMEPLAY = 2, CTX_INVENTORY = 3, CTX_MAP = 4, CTX_PAUSED = 5 };

int PcActions_CtxQuery(unsigned short outCfg[13])
{
    std::uint16_t binds[gs::kPadBindCount];
    if (gs::pad_binds(binds))
    {
        int i;

        for (i = 0; i < 13; i++)
            outCfg[i] = binds[i];
    }
    else
    {
        int i;
        for (i = 0; i < 13; i++)
            outCfg[i] = 0;
    }

    switch (gs::game_state())
    {
        case gs::GameState::InventoryScreen: return CTX_INVENTORY;
        case gs::GameState::PaperMapScreen:  return CTX_MAP;
        case gs::GameState::InGame:
        case gs::GameState::MapEvent:
            break;
        default:
            return CTX_MENU;
    }
    switch (gs::sys_state())
    {
        case gs::SysState::Gameplay:   return CTX_GAMEPLAY;
        case gs::SysState::StatusMenu: return CTX_INVENTORY;
        case gs::SysState::MapScreen:  return CTX_MAP;
        /* The PC pause menu takes Cancel; the original PAUSED screen only the
         * pause button. */
        case gs::SysState::GamePaused: return sh::ui::pause_menu().replaces_pause() ? CTX_MENU : CTX_PAUSED;
        case gs::SysState::LoadOverlay:
        case gs::SysState::LoadRoom:
        case gs::SysState::EventCallback:
        case gs::SysState::EventSetFlag:
        case gs::SysState::EventPlaySound:
            return CTX_WORLD;
        default:
            return CTX_MENU;
    }
}
