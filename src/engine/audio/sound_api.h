/* C interface of the engine's sound system (engine.h) for the C code of the game. Plain C on purpose: the game's
 * translation units include it between their C ABI markers. Volumes are in the units the game computes them in. */
#ifndef SH_ENGINE_AUDIO_SOUND_API_H
#define SH_ENGINE_AUDIO_SOUND_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads a file of convert/ ("audio/vab/BASE.vab.json"); returns a malloc'd buffer (the caller frees) or NULL. */
typedef unsigned char* (*ShSoundReadFn)(const char* relative, size_t* size);
void ShSound_SetReader(ShSoundReadFn read);

/* The mix, for the output that pulls it: interleaved stereo 16-bit at 44.1 kHz. */
void ShSound_Render(int16_t* stereo, unsigned frames);

/* Effects. Bank slots 0..15; voices 0..23; volumes 0..0x3FFF before the bank and tone volumes. -1: nothing keyed. */
int ShSound_LoadBank(int slot, const char* name);
void ShSound_CloseBank(int slot);
int ShSound_KeyOnProgram(int slot, int program, int note, int fine, int left, int right);
int ShSound_KeyOnTone(int slot, int program, int tone, int note, int fine, int left, int right);
int ShSound_KeyOnVoice(int voice, int slot, int program, int tone, int note, int fine, int left, int right);
void ShSound_KeyOffProgram(int slot, int program, int note, int cut);
int ShSound_KeyOffVoice(int voice, int cut);
/* 0 off, 1 on, 2 off with the envelope still sounding, 3 on with the envelope silent. */
int ShSound_KeyStatus(int voice);
int ShSound_VoicePitch(int voice);
void ShSound_SetVoicePitch(int voice, int pitch);
void ShSound_SetVoiceVolume(int voice, int left, int right);
/* Voices the automatic release freed since the last call, as a bit mask. */
unsigned ShSound_TakeReleasedVoices(void);
void ShSound_SetMono(int mono);
/* Effects keyed while on are left out of the main volume (a menu's cues under the game's mute). */
void ShSound_SetMainExempt(int on);

/* Own files of the sound registry: a WAV on disk played for sound id `tag` (volumes as the key-on calls, pitch in
 * semitones). 1 when it plays; 0 when the file cannot be read. */
int ShSound_PlayFile(const char* path, int tag, int left, int right, float semitones, int loop);
void ShSound_StopFile(int tag);
void ShSound_SetFileVolume(int tag, int left, int right);
int ShSound_FadeFile(int tag, float level, float seconds, int stop_at_end);

/* Music. The music instruments are the bank in ShSound_SetMusicSlot (3 by default). Status: 0 none, 1 playing,
 * 2 stopped, 3 ended, 4 paused. */
void ShSound_SetMusicSlot(int slot);
int ShSound_OpenSong(const char* name); /* "SND/B" */
void ShSound_SongPlay(void);
void ShSound_SongStop(void);
void ShSound_SongClose(void);
void ShSound_SetSongVolume(int left, int right);
void ShSound_SetChannelVolume(int channel, int volume);
int ShSound_ChannelVolume(int channel);

/* Whole mix: 0..127. Gains 0..1 from the configuration. */
void ShSound_SetMainVolume(int left, int right);
void ShSound_SetReverbDepth(int left, int right);
void ShSound_SetLineGain(float gain);

/* Voice lines (the main line and a side line mixed over it). Track: "XA/05_02152"; the line starts at `sector` of the track and lasts `length_vblanks`. */
int ShSound_LineStart(const char* track, unsigned sector, unsigned length_vblanks);
/* A loose file: interleaved 16-bit frames (copied). side: mixed over the current line instead of replacing it. */
void ShSound_LineStartPcm(const int16_t* pcm, unsigned frames, int channels, unsigned rate, int side);
void ShSound_LineStop(int side);
int ShSound_LineActive(int side);
int ShSound_LineDrained(int side);
void ShSound_LinePause(int paused); /* the main line only */
/* The CD input volume, 0..0x7FFF, and the CD mix switch. */
void ShSound_SetLineVolume(int left, int right);
void ShSound_SetLineMix(int on);

#ifdef __cplusplus
}
#endif

#endif
