// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game started from a macOS application bundle (Silent Hill PC.app, the macOS release). A bundle cannot be
// written to: the game's root (config.cfg, gamedata/ with the import, .savegames/, logs/, mods/) becomes
// ~/Library/Application Support/Silent Hill PC/, and the read-only files of Contents/Resources are copied there
// when the bundle is new to it. Binaries stay in Contents/MacOS (PcPaths_BinDir).
#pragma once

namespace sh::app {

// Moves the root when the executable is in Contents/MacOS of a bundle, and on Android to the app's storage, where the
// activity has unpacked the base data (stdout and stderr then go to the system log); nothing elsewhere. Call first in
// main, before any path is asked for. False when the root could not be made (said on stderr).
bool use_bundle_root();

}  // namespace sh::app
