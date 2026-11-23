// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The running game's sound driver: sh::audio::SoundDriver over the tables of
// gamedata/config/sound_tables.json and the rules of gamedata/config/sound_driver.json, connected to the engine's
// mixer, the disc drive model and the voice-line player.

#pragma once

#include <cstdint>

#include "engine/audio/sound_driver.h"
#include "engine/audio/sound_rules.h"

namespace sh::audio {

SoundDriver& game_sound_driver();

// The model of the console's disc drive the driver reads through; the voice lines time their start by its head.
class DiscDrive;
DiscDrive& game_disc_drive();

// gamedata/config/sound_driver.json, read once; all zero (and an error in the log) when it does not load.
const SoundRules& game_sound_rules();
// Its sound names as the game's sound numbers.
const ResolvedSoundRules& game_sound_rules_resolved();

// The bank slot effect `id` keys: `slot`, or the guest bank of a character spawned off its maps
// (guest_sound_banks.cpp).
int guest_effect_slot(std::uint16_t id, int slot);

}  // namespace sh::audio

extern "C" {
// Loads the sound banks a character spawned by the engine needs on this map (1: all its sounds play).
int Pc_SoundBanks_EnsureFor(int charaId);
// Releases the guest banks: on a map load and on a new ambient bank.
void Pc_SoundBanks_Reset(void);
}
