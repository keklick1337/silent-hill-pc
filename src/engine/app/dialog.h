// SPDX-License-Identifier: GPL-3.0-or-later
// Error and information dialogs. Every dialog text also goes to stderr with a "[DIALOG]" prefix,
// so test logs show it; with console mode on (--dialogs-to-console or SH_DIALOGS_TO_CONSOLE=1)
// the modal window is not shown at all, so headless runs (Xvfb, replay harness) never wait on it.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ShDialog_SetConsole(int on);
// error != 0: error icon; title/text: UTF-8.
void ShDialog_Show(int error, const char* title, const char* text);

#ifdef __cplusplus
}
#endif
