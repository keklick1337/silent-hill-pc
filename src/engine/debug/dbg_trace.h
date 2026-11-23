/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Journal points: what the game loads, spawns and plays, for the log and
 * the console's status commands. Every function here only observes: it reads
 * game state and writes log lines, it never changes what the game does. */
#ifndef DBG_TRACE_H
#define DBG_TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* "CHARA/HERO.ILM" for a file number ("?" for none); buf of at least 32 bytes. */
const char* DbgTrace_FileName(int file, char* buf, int bufSize);

/* fs queue: a file read started; source is "cd" (sector read) or the loose
 * file path that replaced it. */
void DbgTrace_FileRead(int file, const char* source);

/* fs queue: a TIM went to VRAM. clutW 0 = no palette. Also kept for the
 * console `textures` list. */
void DbgTrace_TimLoaded(int file, int x, int y, int w, int h, int bpp,
                        int clutX, int clutY, int clutW, int clutH, int virtualSlot);

/* Movies (media/fmv/movie_player.cpp). */
void DbgTrace_FmvBegin(const char* name, int fileIdx, int maxFrames);
void DbgTrace_FmvFrame(void);
void DbgTrace_FmvEnd(int result);

/* Once per main-loop iteration (sh::debug::overlay_update): the watchers that log
 * spawns, removals, deaths, room changes, game/system state changes and event
 * flag changes by comparing with the last iteration. */
void DbgTrace_Frame(void);
/* Once per presented frame (sh::debug::overlay_draw): the journal's frame number and
 * the frame time `fps` reports. */
void DbgTrace_FramePresented(void);

/* For the console. */
const char* DbgTrace_CharaName(int charaId);
const char* DbgTrace_EventFlagName(int flag); /* NULL when the flag has no name */
const char* DbgTrace_GameStateName(int state);
const char* DbgTrace_SysStateName(int state);
int  DbgTrace_TimCount(void);
int  DbgTrace_TimGet(int idx, char* line, int lineSize);
int  DbgTrace_FmvStatus(char* line, int lineSize); /* 1 while a movie plays */
void DbgTrace_FrameStats(float* avgMs, float* minMs, float* maxMs, int* samples);

#ifdef __cplusplus
}
#endif

#endif /* DBG_TRACE_H */
