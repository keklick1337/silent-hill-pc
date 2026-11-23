#ifndef SH_ENGINE_PLATFORM_HOST_API_H
#define SH_ENGINE_PLATFORM_HOST_API_H

#include "render/gpu/render_state.h"

typedef void(*GameDebugKeysHandlerFunc)(int nKey, char down);
typedef void(*GameDebugMouseHandlerFunc)(int x, int y);

//------------------------------------------------------------------------

#if defined(_LANGUAGE_C_PLUS_PLUS)||defined(__cplusplus)||defined(c_plusplus)
extern "C" {
#endif

/* Debug inputs */
extern GameDebugKeysHandlerFunc		g_dbg_gameDebugKeys;
extern GameDebugMouseHandlerFunc	g_dbg_gameDebugMouse;

#if defined(_LANGUAGE_C_PLUS_PLUS)||defined(__cplusplus)||defined(c_plusplus)
}
#endif

#endif
