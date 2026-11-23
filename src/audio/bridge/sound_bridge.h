/* The game's side of the engine's sound system (engine/audio/sound_api.h): where the converted sound data comes
 * from, which converted bank or song a disc file of the game's tables is, and the output device. The sector functions
 * are plain C. */
#ifndef SH_AUDIO_BRIDGE_SOUND_BRIDGE_H
#define SH_AUDIO_BRIDGE_SOUND_BRIDGE_H

#ifdef __cplusplus
#include "engine/audio/output/output_device.h"

namespace sh::audio {

// The output's settings from a config.cfg (audio_backend, audio_mode, audio_rate, audio_bit_perfect); the
// defaults when the file cannot be read.
output::OutputSettings load_output_settings(const char* config_path);

// Installs the reader of convert/ (mods first) and opens the game's sound output. False when no sink plays: the
// game then runs silent.
bool start_sound_output(const output::OutputSettings& settings);

}  // namespace sh::audio
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The bank or song file that starts at disc sector `lba` (the game's audio tables name files by sector), loaded
 * into the engine. 1 on success. */
int ShSoundBridge_LoadBankAt(int slot, int lba);
int ShSoundBridge_OpenSongAt(int lba);

/* The game path of the voice track at `lba` ("XA/05_02152"); 1 on success. */
int ShSoundBridge_TrackAt(int lba, char* out, unsigned out_size);

#ifdef __cplusplus
}
#endif

#endif
