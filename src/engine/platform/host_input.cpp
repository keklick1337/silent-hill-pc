// Host input: the debug input hooks and the start of the input devices (the stock binds, then the pads).
#include "engine/platform/host_state.h"

#include "input/pad.h"

GameDebugKeysHandlerFunc g_dbg_gameDebugKeys = nullptr;
GameDebugMouseHandlerFunc g_dbg_gameDebugMouse = nullptr;
int g_dbg_polygonSelected = 0;

void sh_host_default_input()
{
    sh::input::set_default_bindings();
    sh::input::init_pads();
}
